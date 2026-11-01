// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <sx_core.h>
#include <bstrlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "search diagnostic failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define NOINLINE __attribute__((noinline))
typedef struct {
  const char *name;
  size_t n, m;
  int kind;
} Case;
static const Case cases[] = {
  {"absent_first_8_64", 64, 8, 0},
  {"absent_first_8_1024", 1024, 8, 0},
  {"absent_first_8_65536", 65536, 8, 0},
  {"repeated_128_65536", 65536, 128, 1},
  {"common_first_8_miss_65536", 65536, 8, 2},
  {"common_first_8_end_65536", 65536, 8, 3},
  {"periodic_8_miss_65536", 65536, 8, 4},
  {"front_hit_8_64", 64, 8, 5}
};
typedef struct {
  const Case *c;
  unsigned char *t, *p;
  size_t pi[256];
  sx_t *text, *needle;
  bstring bt, bp;
  size_t expected;
} Input;
static volatile uint64_t sink;
static inline void escape(const void *p) {
  __asm__ __volatile__("" : : "g"(p) : "memory");
}
static void make_pi(const unsigned char *p, size_t m, size_t *pi) {
  pi[0] = 0;
  for (size_t k = 0, i = 1; i < m; ++i) {
    while (k && p[i] != p[k]) k = pi[k - 1];
    if (p[i] == p[k]) ++k;
    pi[i] = k;
  }
}
static void prepare(Input *f, const Case *c) {
  memset(f, 0, sizeof(*f));
  f->c = c;
  f->t = malloc(c->n + 1);
  f->p = malloc(c->m + 1);
  CHECK(f->t && f->p);
  uint32_t state = 0x12ab34cd;
  for (size_t i = 0; i < c->n; ++i) {
    state = state * 1664525u + 1013904223u;
    f->t[i] = (unsigned char)('a' + state % 25);
  }
  memset(f->p, 'Z', c->m);
  if (c->kind == 1) {
    memset(f->t, 'a', c->n);
    memset(f->p, 'a', c->m);
    f->p[c->m - 1] = 'b';
  } else if (c->kind == 2 || c->kind == 3) {
    memcpy(f->p, "abcdefgh", 8);
    if (c->kind == 3) memcpy(f->t + c->n - c->m, f->p, c->m);
  } else if (c->kind == 4) {
    for (size_t i = 0; i < c->n; ++i) f->t[i] = i & 1 ? 'b' : 'a';
    memset(f->p, 'a', c->m);
    f->p[c->m - 1] = 'b';
  } else if (c->kind == 5) memcpy(f->t, f->p, c->m);
  f->t[c->n] = f->p[c->m] = 0;
  f->text = sx_new_from_cstr((const char *)f->t);
  f->needle = sx_new_from_cstr((const char *)f->p);
  f->bt = blk2bstr(f->t, (int)c->n);
  f->bp = blk2bstr(f->p, (int)c->m);
  CHECK(f->text && f->needle && f->bt && f->bp);
  make_pi(f->p, c->m, f->pi);
  const unsigned char *found = memmem(f->t, c->n, f->p, c->m);
  f->expected = found ? (size_t)(found - f->t) : SIZE_MAX;
  CHECK(f->expected == (c->kind == 3 ? c->n - c->m : c->kind == 5 ? 0 : SIZE_MAX));
}
static void cleanup(Input *f) {
  sx_free(f->text); sx_free(f->needle);
  CHECK(bdestroy(f->bt) == BSTR_OK); CHECK(bdestroy(f->bp) == BSTR_OK);
  free(f->t); free(f->p);
}

#ifdef DIAG_WRAP_ALLOC
static int use_arena, arena_busy;
static alignas(max_align_t) unsigned char arena[4096];
void *__real_malloc(size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n) {
  if (!use_arena) return __real_malloc(n);
  CHECK(!arena_busy && n <= sizeof(arena));
  arena_busy = 1;
  return arena;
}
void __wrap_free(void *p) {
  if (p == arena) { CHECK(arena_busy); arena_busy = 0; return; }
  __real_free(p);
}
static void arena_mode(int enabled) { CHECK(!arena_busy); use_arena = enabled; }
#else
static void arena_mode(int enabled) { CHECK(!enabled); }
#endif

static NOINLINE size_t kernel_sx(Input *f) { return sx_find(f->text, f->needle); }
static NOINLINE size_t kernel_bstr(Input *f) {
  int result = binstr(f->bt, 0, f->bp);
  return result < 0 ? SIZE_MAX : (size_t)result;
}
static NOINLINE size_t kernel_memmem(Input *f) {
  const unsigned char *p = memmem(f->t, f->c->n, f->p, f->c->m);
  return p ? (size_t)(p - f->t) : SIZE_MAX;
}
/* Diagnostic-only specialization of the existing forward-FIRST algorithm.
   Bounds are valid by construction; it has no reverse/ALL-mode handling. */
static NOINLINE size_t kernel_kmp_first(Input *f) {
  size_t n = f->c->n, m = f->c->m;
  size_t *pi = malloc(m * sizeof(*pi));
  CHECK(pi);
  make_pi(f->p, m, pi);
  size_t q = 0;
  for (size_t i = 0; i < n; ++i) {
    while (q && f->t[i] != f->p[q]) q = pi[q - 1];
    if (f->t[i] == f->p[q]) ++q;
    if (q == m) { free(pi); return i + 1 - m; }
  }
  free(pi);
  return SIZE_MAX;
}
static NOINLINE size_t kernel_kmp_scan(Input *f) {
  size_t q = 0, n = f->c->n, m = f->c->m;
  for (size_t i = 0; i < n; ++i) {
    while (q && f->t[i] != f->p[q]) q = f->pi[q - 1];
    if (f->t[i] == f->p[q]) ++q;
    if (q == m) return i + 1 - m;
  }
  return SIZE_MAX;
}
static size_t prefix_scratch[256];
static NOINLINE size_t kernel_prefix(Input *f) {
  make_pi(f->p, f->c->m, prefix_scratch);
  escape(prefix_scratch);
  return prefix_scratch[f->c->m - 1];
}
static NOINLINE size_t kernel_alloc(Input *f) {
  size_t bytes = f->c->m * sizeof(size_t);
  void *p = malloc(bytes); CHECK(p);
  escape(p);
  free(p);
  return bytes;
}
/* Byte probes are controls, not complete substring-search replacements. */
static NOINLINE size_t kernel_scalar_probe(Input *f) {
  unsigned char c = f->p[0];
  for (size_t i = 0; i < f->c->n; ++i) if (f->t[i] == c) return i;
  return SIZE_MAX;
}
static NOINLINE size_t kernel_first_probe(Input *f) {
  const unsigned char *p = memchr(f->t, f->p[0], f->c->n);
  return p ? (size_t)(p - f->t) : SIZE_MAX;
}
static NOINLINE size_t kernel_last_probe(Input *f) {
  const unsigned char *p = memchr(f->t, f->p[f->c->m - 1], f->c->n);
  return p ? (size_t)(p - f->t) : SIZE_MAX;
}
typedef size_t (*Kernel)(Input *);
typedef struct { const char *name; Kernel fn; int expected_kind, arena; } Variant;
static const Variant variants[] = {
  {"sx_heap", kernel_sx, 0, 0},
#ifdef DIAG_WRAP_ALLOC
  {"sx_arena", kernel_sx, 0, 1},
#endif
  {"kmp_first_only_heap", kernel_kmp_first, 0, 0},
  {"kmp_scan_precomputed", kernel_kmp_scan, 0, 0},
  {"prefix_build_only", kernel_prefix, 1, 0},
  {"malloc_free_only", kernel_alloc, 2, 0},
  {"bstrlib", kernel_bstr, 0, 0},
  {"glibc_memmem", kernel_memmem, 0, 0},
  {"scalar_first_byte_probe", kernel_scalar_probe, 3, 0},
  {"glibc_first_byte_probe", kernel_first_probe, 3, 0},
  {"glibc_last_byte_probe", kernel_last_probe, 4, 0}
};
static uint64_t now_ns(void) {
  struct timespec t; CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
  return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
static uint64_t batch(const Variant *v, Input *f, size_t iterations) {
  uint64_t value = 0;
  for (size_t i = 0; i < iterations; ++i) {
    escape(f);
    value += (uint64_t)v->fn(f);
  }
  return value;
}
static uint64_t timed(const Variant *v, Input *f, size_t iterations, size_t expected) {
  uint64_t start = now_ns(), value = batch(v, f, iterations);
  uint64_t elapsed = now_ns() - start;
  CHECK(value == (uint64_t)expected * (uint64_t)iterations);
  sink ^= value;
  return elapsed;
}

extern uint64_t diag_bstr_comparisons, diag_bstr_resets;
int diag_binstr_counted(const_bstring, int, const_bstring);
static void count_logical(Input *f) {
  size_t pi[256] = {0}, q = 0;
  uint64_t prep = 0, prep_fallback = 0, text_checks = 0, fallback = 0, visited = 0;
  for (size_t i = 1, k = 0; i < f->c->m; ++i) {
    while (k) {
      ++prep;
      if (f->p[i] == f->p[k]) break;
      ++prep_fallback; k = pi[k - 1];
    }
    ++prep;
    if (f->p[i] == f->p[k]) ++k;
    pi[i] = k;
  }
  size_t result = SIZE_MAX;
  for (size_t i = 0; i < f->c->n; ++i) {
    ++visited;
    while (q) {
      ++text_checks;
      if (f->t[i] == f->p[q]) break;
      ++fallback; q = pi[q - 1];
    }
    ++text_checks;
    if (f->t[i] == f->p[q]) ++q;
    if (q == f->c->m) { result = i + 1 - q; break; }
  }
  CHECK(result == f->expected);
  diag_bstr_comparisons = diag_bstr_resets = 0;
  int br = diag_binstr_counted(f->bt, 0, f->bp);
  CHECK((br < 0 ? SIZE_MAX : (size_t)br) == result);
  printf("COUNTS_JSON={\"case\":\"%s\",\"text_bytes_visited\":%" PRIu64
         ",\"kmp_text_comparisons\":%" PRIu64 ",\"kmp_text_fallbacks\":%" PRIu64
         ",\"prefix_comparisons\":%" PRIu64 ",\"prefix_fallbacks\":%" PRIu64
         ",\"bstr_text_comparisons\":%" PRIu64 ",\"bstr_start_resets\":%" PRIu64 "}\n",
         f->c->name, visited, text_checks, fallback, prep, prep_fallback,
         diag_bstr_comparisons, diag_bstr_resets);
}
int main(int argc, char **argv) {
  int check_only = argc > 1 && strcmp(argv[1], "--check") == 0;
  int unwrapped_only = argc > 1 && strcmp(argv[1], "--unwrapped-only") == 0;
  uint32_t rng = 0x731a9;
  CHECK(check_only || argc == 3);
  FILE *csv = check_only ? NULL : fopen(argv[2], "w");
  CHECK(check_only || csv);
  if (csv) fputs("case,variant,iterations,sample,elapsed_ns,ns_per_op\n", csv);
  for (size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ++ci) {
    Input f; prepare(&f, &cases[ci]); count_logical(&f);
    size_t expected[16], iterations[16], count = sizeof(variants) / sizeof(variants[0]);
    CHECK(count <= 16);
    if (unwrapped_only) count = 1;
    for (size_t j = 0; j < count; ++j) {
      const Variant *v = &variants[j];
      expected[j] = v->expected_kind == 1 ? f.pi[f.c->m - 1] :
                    v->expected_kind == 2 ? f.c->m * sizeof(size_t) :
                    v->expected_kind == 3 ? kernel_first_probe(&f) :
                    v->expected_kind == 4 ? kernel_last_probe(&f) : f.expected;
      arena_mode(v->arena);
      CHECK(v->fn(&f) == expected[j]);
      iterations[j] = 1;
      if (!check_only) for (;;) {
        uint64_t a = timed(v, &f, iterations[j], expected[j]);
        uint64_t b = timed(v, &f, iterations[j], expected[j]);
        if ((a < b ? a : b) >= 20000000ULL || iterations[j] >= (1u << 24)) break;
        iterations[j] *= 2;
      }
      arena_mode(0);
    }
    if (!check_only) for (int sample = 0; sample < 9; ++sample) {
      size_t sequence[16]; for (size_t j = 0; j < count; ++j) sequence[j] = j;
      for (size_t j = count - 1; j > 0; --j) {
        rng = rng * 1664525u + 1013904223u;
        size_t k = rng % (j + 1), temp = sequence[j]; sequence[j] = sequence[k]; sequence[k] = temp;
      }
      for (size_t k = 0; k < count; ++k) {
        size_t j = sequence[k]; const Variant *v = &variants[j];
        arena_mode(v->arena);
        uint64_t elapsed = timed(v, &f, iterations[j], expected[j]);
        arena_mode(0);
        fprintf(csv, "%s,%s,%zu,%d,%" PRIu64 ",%.6f\n", f.c->name,
                unwrapped_only ? "sx_unwrapped" : v->name, iterations[j],
                sample, elapsed, (double)elapsed / (double)iterations[j]);
      }
    }
    cleanup(&f);
  }
  if (csv) CHECK(fclose(csv) == 0);
  fprintf(stderr, "search diagnostics validated; sink=%" PRIu64 "\n", sink);
  return 0;
}
