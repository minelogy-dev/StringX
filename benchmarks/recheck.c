// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "recheck.h"
#include <cpuid.h>
#include <emmintrin.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
typedef struct {
  const char *name;
  int kind;
  size_t n, m, period, phase;
  uint32_t seed;
  int hit;
} Case;
#include "recheck_cases.h"

uint64_t recheck_call_counts[4];
static volatile uint64_t sink;
static recheck_fn const workers[] = {
  recheck_sx, recheck_sds, recheck_bstr, recheck_raw
};
static const char *const names[] = {
  "StringX", "SDS+glibc", "bstrlib", "raw_glibc_same_buffer"
};

static uint32_t next(uint32_t *s) {
  *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
  return *s;
}
static uint64_t now_ns(void) {
  struct timespec t;
  CHECK(clock_gettime(CLOCK_MONOTONIC_RAW, &t) == 0);
  return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
static uint64_t digest(const unsigned char *p, size_t n) {
  uint64_t h = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= UINT64_C(1099511628211); }
  return h;
}
/* Independent byte-window oracle, outside every measured interval. */
static uint64_t reference(const unsigned char *h, size_t n,
                          const unsigned char *p, size_t m) {
  CHECK(m && m <= n);
  for (size_t at = 0; at <= n - m; ++at) {
    size_t j = 0;
    while (j < m && h[at + j] == p[j]) ++j;
    if (j == m) return (uint64_t)at + 1;
  }
  return 0;
}
static uint64_t prepare(recheck_input *f, const Case *c) {
  memset(f, 0, sizeof(*f));
  unsigned char *h = malloc(c->n);
  f->pattern = malloc(c->m + 1);
  CHECK(h && f->pattern && c->m && c->m <= c->n);
  f->n = c->n; f->m = c->m;
  uint32_t state = 0x12ab34cd;
  for (size_t i = 0; i < c->n; ++i) {
    state = state * UINT32_C(1664525) + UINT32_C(1013904223);
    h[i] = (unsigned char)('a' + state % 25);
  }
  memset(f->pattern, 'Z', c->m);
  if (c->kind == 1) {
    for (size_t i = 0; i < c->m; ++i) f->pattern[i] = (unsigned char)('a' + i % 25);
  } else if (c->kind == 2 || c->kind == 3 || c->kind == 6 || c->kind == 7) {
    memset(h, 'a', c->n);
    memset(f->pattern, 'a', c->m);
    f->pattern[c->m - 1] = 'b';
    if (c->kind == 3) {
      CHECK(c->period);
      for (size_t i = 0; i < c->n; ++i)
        h[i] = (i + c->phase) % c->period == c->period - 1 ? 'b' : 'a';
    }
    if (c->kind == 6) {
      if (c->phase == 0) h[c->n - 1] = 'b'; /* real tail hit */
      if (c->phase == 1) { h[c->n - 1] = 'b'; h[c->n - 2] = 'c'; }
      if (c->phase == 2) { h[c->m - 1] = 'b'; h[c->m - 2] = 'c'; }
    }
    if (c->kind == 7) {
      CHECK(c->period && c->m >= 3);
      state = c->seed;
      f->pattern[c->m / 2] = 'c'; /* absent interior byte, independent of b frequency */
      for (size_t i = 0; i < c->n; ++i) h[i] = next(&state) % c->period == 0 ? 'b' : 'a';
    }
  } else if (c->kind == 4 || c->kind == 5) {
    unsigned alphabet = c->kind == 5 ? 256 : 25;
    unsigned base = c->kind == 5 ? 0 : 'a';
    state = c->seed;
    for (size_t i = 0; i < c->n; ++i) h[i] = (unsigned char)(base + next(&state) % alphabet);
    state = c->seed ^ UINT32_C(0x9e3779b9);
    for (size_t i = 0; i < c->m; ++i) f->pattern[i] = (unsigned char)(base + next(&state) % alphabet);
  }
  f->pattern[c->m] = 0;
  size_t hit_at = 0;
  if (c->hit) {
    hit_at = c->hit == 1 ? 0 : c->hit == 2 ? (c->n - c->m) / 2 : c->n - c->m;
    memcpy(h + hit_at, f->pattern, c->m);
  }
  uint64_t expected = reference(h, c->n, f->pattern, c->m);
  if (c->hit) CHECK(expected == hit_at + 1);
  f->sx = sx_new_with_cap(c->n + 1);
  f->sx_pattern = sx_new_with_cap(c->m + 1);
  CHECK(f->sx && f->sx_pattern);
  CHECK(sx_truncate(f->sx, c->n) && sx_truncate(f->sx_pattern, c->m));
  memcpy(sx_buf_mut(f->sx), h, c->n);
  memcpy(sx_buf_mut(f->sx_pattern), f->pattern, c->m);
  f->sd = sdsnewlen(h, c->n);
  f->bs = blk2bstr(h, (int)c->n);
  f->bs_pattern = blk2bstr(f->pattern, (int)c->m);
  CHECK(f->sd && f->bs && f->bs_pattern);
  CHECK(sx_len(f->sx) == (int)c->n && sx_len(f->sx_pattern) == (int)c->m);
  CHECK(sdslen(f->sd) == c->n && f->bs->slen == (int)c->n && f->bs_pattern->slen == (int)c->m);
  f->text_bytes[0] = (const unsigned char *)sx_buf_mut(f->sx);
  f->text_bytes[1] = (const unsigned char *)f->sd;
  f->text_bytes[2] = f->bs->data;
  f->text_bytes[3] = (const unsigned char *)f->sd;
  f->pattern_bytes[0] = (const unsigned char *)sx_buf_mut(f->sx_pattern);
  f->pattern_bytes[1] = f->pattern;
  f->pattern_bytes[2] = f->bs_pattern->data;
  f->pattern_bytes[3] = f->pattern;
  for (int lib = 0; lib < 4; ++lib) {
    CHECK(memcmp(f->text_bytes[lib], h, c->n) == 0);
    CHECK(memcmp(f->pattern_bytes[lib], f->pattern, c->m) == 0);
  }
  free(h);
  return expected;
}
static void cleanup(recheck_input *f) {
  sx_free(f->sx); sx_free(f->sx_pattern); sdsfree(f->sd);
  CHECK(bdestroy(f->bs) == BSTR_OK && bdestroy(f->bs_pattern) == BSTR_OK);
  free(f->pattern);
}
static void flush_range(const unsigned char *p, size_t n) {
  if (!n) return;
  _mm_clflush(p);
  uintptr_t at = ((uintptr_t)p + 64) & ~(uintptr_t)63;
  for (; at < (uintptr_t)p + n; at += 64) _mm_clflush((const void *)at);
}
static void flush_input(recheck_input *f, int lib) {
  flush_range(f->text_bytes[lib], f->n);
  flush_range(f->pattern_bytes[lib], f->m);
  _mm_mfence(); _mm_lfence();
}
/* Separate adapters, no LTO, and a volatile function pointer force a
   real call for each iteration. The release disassembly is retained. */
__attribute__((noinline)) uint64_t
recheck_hot(recheck_fn fn, recheck_input *f, size_t iterations) {
  recheck_fn volatile call = fn;
  uint64_t sum = 0;
  for (size_t i = 0; i < iterations; ++i) sum += call(f);
  return sum;
}
__attribute__((noinline)) static uint64_t noop(recheck_input *f) {
  __asm__ __volatile__("" : : "g"(f) : "memory");
  return 0;
}
static uint64_t hot_time(recheck_fn fn, recheck_input *f, size_t it, uint64_t expected) {
  uint64_t begin = now_ns();
  uint64_t sum = recheck_hot(fn, f, it);
  _mm_lfence();
  uint64_t elapsed = now_ns() - begin;
  CHECK(sum == expected * it);
  sink ^= sum;
  return elapsed;
}
static size_t calibrate(recheck_fn fn, recheck_input *f, uint64_t expected) {
  size_t it = 1;
  for (;;) {
    uint64_t t = hot_time(fn, f, it, expected);
    if (t >= UINT64_C(10000000) || it >= (1u << 22)) return it;
    it *= 2;
  }
}
static uint64_t cold_once(recheck_fn fn, recheck_input *f, int lib, uint64_t expected) {
  flush_input(f, lib);
  recheck_fn volatile call = fn;
  uint64_t begin = now_ns();
  uint64_t result = call(f);
  _mm_lfence();
  uint64_t elapsed = now_ns() - begin;
  CHECK(result == expected);
  sink ^= result;
  return elapsed;
}
static void shuffle(int *a, int n, uint32_t *rng) {
  for (int i = n - 1; i > 0; --i) {
    int j = (int)(next(rng) % (unsigned)(i + 1)), t = a[i];
    a[i] = a[j]; a[j] = t;
  }
}
int main(int argc, char **argv) {
  unsigned eax, ebx, ecx, edx;
  CHECK(__get_cpuid(1, &eax, &ebx, &ecx, &edx));
  CHECK((edx & (1u << 19)) && (((ebx >> 8) & 255) * 8 == 64));
  CHECK(argc >= 2);
  int verify = strcmp(argv[1], "--verify") == 0;
  int describe = strcmp(argv[1], "--describe") == 0;
  CHECK(verify || describe || strcmp(argv[1], "--measure") == 0);
  uint32_t rng = UINT32_C(0x7132a8ef) + (argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) * 17 : 0);
  enum { N = sizeof(cases) / sizeof(cases[0]), SAMPLES = 7, COLD_OPS = 8 };
  int order[N];
  for (int i = 0; i < N; ++i) order[i] = i;
  if (!verify && !describe) {
    shuffle(order, N, &rng);
    puts("case,library,mode,n,m,expected_pos,sample,iterations,elapsed_ns,ns_per_op");
    size_t it = calibrate(noop, NULL, 0);
    double noop_ns = (double)hot_time(noop, NULL, it, 0) / (double)it;
    uint64_t clock_sum = 0;
    for (int i = 0; i < 1000; ++i) {
      uint64_t begin = now_ns(); _mm_lfence(); clock_sum += now_ns() - begin;
    }
    fprintf(stderr, "RECHECK_CONTROL_JSON={\"noop_hot_ns\":%.3f,\"clock_pair_mean_ns\":%.3f}\n",
            noop_ns, (double)clock_sum / 1000);
  }
  uint64_t signature = 0;
  for (int ci = 0; ci < N; ++ci) {
    const Case *c = &cases[order[ci]];
    recheck_input f;
    uint64_t expected = prepare(&f, c);
    signature ^= digest(f.text_bytes[0], f.n) ^ digest(f.pattern, f.m)
                 ^ digest((const unsigned char *)c->name, strlen(c->name));
    if (describe) {
      size_t first = 0, last = 0;
      for (size_t i = 0; i < f.n; ++i) {
        first += f.text_bytes[0][i] == f.pattern[0];
        last += f.text_bytes[0][i] == f.pattern[f.m - 1];
      }
      printf("{\"case\":\"%s\",\"kind\":%d,\"n\":%zu,\"m\":%zu,\"period\":%zu,\"phase\":%zu,"
             "\"seed\":%" PRIu32 ",\"expected_pos\":%lld,\"first_byte_count\":%zu,\"last_byte_count\":%zu,"
             "\"text_hash\":\"%016" PRIx64 "\",\"needle_hash\":\"%016" PRIx64 "\"}\n",
             c->name,c->kind,c->n,c->m,c->period,c->phase,c->seed,(long long)expected-1,
             first,last,digest(f.text_bytes[0],f.n),digest(f.pattern,f.m));
    } else if (verify) {
      memset(recheck_call_counts, 0, sizeof(recheck_call_counts));
      for (int lib = 0; lib < 4; ++lib) {
        CHECK(workers[lib](&f) == expected);
        CHECK(recheck_hot(workers[lib], &f, 7) == expected * 7);
        (void)cold_once(workers[lib], &f, lib, expected);
#ifdef RECHECK_VERIFY_CALLS
        CHECK(recheck_call_counts[lib] == 9);
#endif
      }
    } else {
      size_t iterations[4];
      for (int lib = 0; lib < 4; ++lib) {
        CHECK(workers[lib](&f) == expected);
        iterations[lib] = calibrate(workers[lib], &f, expected);
      }
      for (int sample = 0; sample < SAMPLES; ++sample) {
        int libs[] = {0,1,2,3}; shuffle(libs, 4, &rng);
        for (int li = 0; li < 4; ++li) {
          int lib = libs[li], first_mode = (int)(next(&rng) & 1);
          for (int which = 0; which < 2; ++which) {
            int cold = first_mode ^ which;
            uint64_t elapsed = 0;
            size_t it = cold ? COLD_OPS : iterations[lib];
            if (cold) {
              for (int op = 0; op < COLD_OPS; ++op)
                elapsed += cold_once(workers[lib], &f, lib, expected);
            } else {
              for (int warm = 0; warm < 3; ++warm) CHECK(workers[lib](&f) == expected);
              elapsed = hot_time(workers[lib], &f, it, expected);
            }
            printf("%s,%s,%s,%zu,%zu,%lld,%d,%zu,%" PRIu64 ",%.6f\n",
                   c->name,names[lib],cold?"flushed":"hot",f.n,f.m,(long long)expected-1,
                   sample,it,elapsed,(double)elapsed/(double)it);
          }
        }
      }
    }
    cleanup(&f);
  }
  fprintf(stderr, "RECHECK_MANIFEST_SIGNATURE=%016" PRIx64 "\n", signature);
  fprintf(stderr, "RECHECK_VALIDATED cases=%d adapters=4 mode=%s sink=%" PRIu64 "\n",
          N, verify?"verify":describe?"describe":"measure", sink);
  return 0;
}
