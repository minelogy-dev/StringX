// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <sx.h>
#include <sds.h>
#include <bstrlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "benchmark check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define SAMPLES 11
enum { COPY_CSTR, COPY_BYTES, APPEND, COMPARE, FIND, FIND_CHAR, SUBSTR, VIEW, SPLIT_OWNED, SPLIT_VIEW };
enum { HIT_FRONT = 1, HIT_END = 2, REPEATED = 4, DIFFERENT = 8, RESERVED = 16 };
typedef struct { const char *name; int op; size_t n, part; unsigned flags; } Case;
static const Case cases[] = {
  {"copy_cstr_16", COPY_CSTR, 16, 0, 0},
  {"copy_cstr_64", COPY_CSTR, 64, 0, 0},
  {"copy_cstr_1024", COPY_CSTR, 1024, 0, 0},
  {"copy_cstr_65536", COPY_CSTR, 65536, 0, 0},
  {"copy_bytes_64", COPY_BYTES, 64, 0, 0},
  {"copy_bytes_65536", COPY_BYTES, 65536, 0, 0},
  {"append_1x4096_auto", APPEND, 4096, 1, 0},
  {"append_1x4096_reserved", APPEND, 4096, 1, RESERVED},
  {"append_16x256_auto", APPEND, 4096, 16, 0},
  {"append_16x256_reserved", APPEND, 4096, 16, RESERVED},
  {"append_1024x1024_auto", APPEND, 1048576, 1024, 0},
  {"append_1024x1024_reserved", APPEND, 1048576, 1024, RESERVED},
  {"compare_equal_64", COMPARE, 64, 0, 0},
  {"compare_equal_65536", COMPARE, 65536, 0, 0},
  {"compare_last_diff_65536", COMPARE, 65536, 0, DIFFERENT},
  {"find_char_end_65536", FIND_CHAR, 65536, 1, HIT_END},
  {"find_char_miss_65536", FIND_CHAR, 65536, 1, 0},
  {"find8_front_64", FIND, 64, 8, HIT_FRONT},
  {"find8_end_1024", FIND, 1024, 8, HIT_END},
  {"find8_end_65536", FIND, 65536, 8, HIT_END},
  {"find8_miss_65536", FIND, 65536, 8, 0},
  {"find128_repeated_miss_65536", FIND, 65536, 128, REPEATED},
  {"substr_copy_64", SUBSTR, 65536, 64, 0},
  {"substr_copy_4096", SUBSTR, 65536, 4096, 0},
  {"substr_view_64", VIEW, 65536, 64, 0},
  {"split_owned_comma_4096", SPLIT_OWNED, 4096, 1, 0},
  {"split_owned_pair_4096", SPLIT_OWNED, 4096, 2, 0},
  {"split_views_comma_4096", SPLIT_VIEW, 4096, 1, 0}
};

typedef struct {
  const Case *c;
  char *data, *other, *needle;
  const char *sep;
  sx_t *sx, *sx_other, *sx_needle;
  sds sd, sd_other;
  bstring bs, bs_other, bs_needle, bs_sep;
  uint64_t expected;
} Fixture;
static int validating;
static volatile uint64_t sink;

static inline void escape(const void *p) {
  __asm__ __volatile__("" : : "g"(p) : "memory");
}
static uint64_t digest(const void *ptr, size_t n) {
  const unsigned char *p = ptr;
  escape(ptr);
  return (uint64_t)n + (n ? (uint64_t)p[0] + p[n - 1] : 0);
}
static uint64_t checked_content(Fixture *f, const void *p, size_t n, int append) {
  if (validating) {
    size_t wanted = f->c->op == SUBSTR || f->c->op == VIEW ? f->c->part : f->c->n;
    CHECK(n == wanted);
    if (append) {
      for (size_t at = 0; at < n; at += f->c->part)
        CHECK(memcmp((const char *)p + at, f->data, f->c->part) == 0);
    } else {
      size_t off = f->c->op == SUBSTR || f->c->op == VIEW ? f->c->n / 3 : 0;
      CHECK(memcmp(p, f->data + off, n) == 0);
    }
  }
  return digest(p, n);
}
static uint64_t order(int result) { return result < 0 ? 0 : result > 0 ? 2 : 1; }
static uint64_t reference_split(Fixture *f) {
  size_t start = 0, at = 0;
  uint64_t sum = 0;
  while (at + f->c->part <= f->c->n) {
    if (memcmp(f->data + at, f->sep, f->c->part) != 0) { ++at; continue; }
    sum += 1 + digest(f->data + start, at - start);
    at += f->c->part;
    start = at;
  }
  return sum + 1 + digest(f->data + start, f->c->n - start);
}
static void prepare(Fixture *f, const Case *c) {
  memset(f, 0, sizeof(*f));
  f->c = c;
  f->data = malloc(c->n + 1);
  f->other = malloc(c->n + 1);
  f->needle = malloc((c->part ? c->part : 1) + 1);
  CHECK(f->data && f->other && f->needle);
  uint32_t state = 0x12ab34cd;
  for (size_t i = 0; i < c->n; ++i) {
    state = state * 1664525u + 1013904223u;
    f->data[i] = (char)('a' + (state % 25));
  }
  f->data[c->n] = 0;
  if (c->op == COPY_BYTES) {
    for (size_t i = 3; i < c->n; i += 17) f->data[i] = 0;
  }
  size_t m = c->part ? c->part : 1;
  memset(f->needle, 'Z', m);
  f->needle[m] = 0;
  if (c->op == FIND && (c->flags & REPEATED)) {
    memset(f->data, 'a', c->n);
    memset(f->needle, 'a', m);
    f->needle[m - 1] = 'b';
  } else if (c->op == FIND || c->op == FIND_CHAR) {
    if (c->flags & HIT_FRONT) memcpy(f->data, f->needle, m);
    if (c->flags & HIT_END) memcpy(f->data + c->n - m, f->needle, m);
  }
  f->sep = c->part == 2 ? "::" : ",";
  if (c->op == SPLIT_OWNED || c->op == SPLIT_VIEW) {
    memcpy(f->data, f->sep, c->part);
    for (size_t at = 64 - c->part; at + c->part <= c->n; at += 64)
      memcpy(f->data + at, f->sep, c->part);
  }
  memcpy(f->other, f->data, c->n + 1);
  if (c->flags & DIFFERENT)
    f->other[c->n - 1] = f->data[c->n - 1] == 'a' ? 'b' : 'a';

  f->sx = sx_new_with_cap(c->n + 1);
  CHECK(f->sx && sx_truncate(f->sx, c->n));
  memcpy(sx_buf_mut(f->sx), f->data, c->n);
  f->sx_other = sx_new_with_cap(c->n + 1);
  CHECK(f->sx_other && sx_truncate(f->sx_other, c->n));
  memcpy(sx_buf_mut(f->sx_other), f->other, c->n);
  f->sx_needle = sx_new_from_cstr(f->needle);
  f->sd = sdsnewlen(f->data, c->n);
  f->sd_other = sdsnewlen(f->other, c->n);
  f->bs = blk2bstr(f->data, (int)c->n);
  f->bs_other = blk2bstr(f->other, (int)c->n);
  f->bs_needle = bfromcstr(f->needle);
  f->bs_sep = bfromcstr(f->sep);
  CHECK(f->sx_needle && f->sd && f->sd_other && f->bs && f->bs_other && f->bs_needle && f->bs_sep);

  switch (c->op) {
  case APPEND:
    f->expected = c->n + (unsigned char)f->data[0] + (unsigned char)f->data[c->part - 1];
    break;
  case COMPARE: f->expected = order(memcmp(f->data, f->other, c->n)); break;
  case FIND: {
    const char *p = memmem(f->data, c->n, f->needle, c->part);
    f->expected = p ? (uint64_t)(p - f->data) + 1 : 0;
    break;
  }
  case FIND_CHAR: {
    const char *p = memchr(f->data, 'Z', c->n);
    f->expected = p ? (uint64_t)(p - f->data) + 1 : 0;
    break;
  }
  case SUBSTR: case VIEW:
    f->expected = digest(f->data + c->n / 3, c->part);
    break;
  case SPLIT_OWNED: case SPLIT_VIEW: f->expected = reference_split(f); break;
  default: f->expected = digest(f->data, c->n); break;
  }
}
static void cleanup(Fixture *f) {
  sx_free(f->sx); sx_free(f->sx_other); sx_free(f->sx_needle);
  sdsfree(f->sd); sdsfree(f->sd_other);
  CHECK(bdestroy(f->bs) == BSTR_OK);
  CHECK(bdestroy(f->bs_other) == BSTR_OK);
  CHECK(bdestroy(f->bs_needle) == BSTR_OK);
  CHECK(bdestroy(f->bs_sep) == BSTR_OK);
  free(f->data); free(f->other); free(f->needle);
}

static uint64_t work_sx(Fixture *f, size_t iterations) {
  const Case *c = f->c;
  uint64_t sum = 0;
  for (size_t it = 0; it < iterations; ++it) {
    escape(f->sx);
    sx_t *s;
    switch (c->op) {
    case COPY_CSTR: case COPY_BYTES:
      s = c->op == COPY_CSTR ? sx_new_from_cstr(f->data) : sx_new_from_view(sx_view(f->sx));
      CHECK(s);
      sum += checked_content(f, sx_buf_mut(s), (size_t)sx_len(s), 0);
      sx_free(s); break;
    case APPEND: {
      s = sx_new(); CHECK(s);
      if (c->flags & RESERVED) CHECK(sx_reserve(s, c->n));
      sx_view_t chunk = sx_view_range(sx_view(f->sx), 0, c->part);
      for (size_t at = 0; at < c->n; at += c->part) CHECK(sx_append_view(s, chunk));
      sum += checked_content(f, sx_buf_mut(s), (size_t)sx_len(s), 1);
      sx_free(s); break;
    }
    case COMPARE: sum += order(sx_cmp(f->sx, f->sx_other)); break;
    case FIND: sum += sx_find(f->sx, f->sx_needle) + (size_t)1; break;
    case FIND_CHAR: sum += sx_find_char(f->sx, 'Z') + (size_t)1; break;
    case SUBSTR:
      s = sx_substr(f->sx, c->n / 3, c->part); CHECK(s);
      sum += checked_content(f, sx_buf_mut(s), (size_t)sx_len(s), 0);
      sx_free(s); break;
    case VIEW: {
      sx_view_t v = sx_view_range(sx_view(f->sx), c->n / 3, c->part);
      sum += checked_content(f, sx_buf_view_mut(v), v.len, 0); break;
    }
    case SPLIT_OWNED: case SPLIT_VIEW: {
      sx_view_t *tokens = sx_split(f->sx, f->sep); CHECK(tokens);
      for (size_t j = 0; tokens[j].ref; ++j) {
        if (c->op == SPLIT_OWNED) {
          s = sx_new_from_view(tokens[j]); CHECK(s);
          sum += 1 + digest(sx_buf_mut(s), (size_t)sx_len(s));
          sx_free(s);
        } else sum += 1 + digest(sx_buf_view_mut(tokens[j]), tokens[j].len);
      }
      free(tokens); break;
    }
    default: CHECK(0);
    }
  }
  return sum;
}
static uint64_t work_sds(Fixture *f, size_t iterations) {
  const Case *c = f->c;
  uint64_t sum = 0;
  for (size_t it = 0; it < iterations; ++it) {
    escape(f->sd);
    sds s;
    switch (c->op) {
    case COPY_CSTR: case COPY_BYTES:
      s = c->op == COPY_CSTR ? sdsnew(f->data) : sdsnewlen(f->data, c->n);
      CHECK(s); sum += checked_content(f, s, sdslen(s), 0); sdsfree(s); break;
    case APPEND:
      s = sdsempty(); CHECK(s);
      if (c->flags & RESERVED) { s = sdsMakeRoomFor(s, c->n); CHECK(s); }
      for (size_t at = 0; at < c->n; at += c->part) { s = sdscatlen(s, f->data, c->part); CHECK(s); }
      sum += checked_content(f, s, sdslen(s), 1); sdsfree(s); break;
    case COMPARE: sum += order(sdscmp(f->sd, f->sd_other)); break;
    case FIND: {
      const char *p = memmem(f->sd, sdslen(f->sd), f->needle, c->part);
      sum += p ? (uint64_t)(p - f->sd) + 1 : 0; break;
    }
    case FIND_CHAR: {
      const char *p = memchr(f->sd, 'Z', sdslen(f->sd));
      sum += p ? (uint64_t)(p - f->sd) + 1 : 0; break;
    }
    case SUBSTR:
      s = sdsnewlen(f->sd + c->n / 3, c->part); CHECK(s);
      sum += checked_content(f, s, sdslen(s), 0); sdsfree(s); break;
    case VIEW:
      sum += checked_content(f, f->sd + c->n / 3, c->part, 0); break;
    case SPLIT_OWNED: {
      int count = 0;
      sds *tokens = sdssplitlen(f->sd, (ssize_t)sdslen(f->sd), f->sep, (int)c->part, &count);
      CHECK(tokens);
      for (int j = 0; j < count; ++j) sum += 1 + digest(tokens[j], sdslen(tokens[j]));
      sdsfreesplitres(tokens, count); break;
    }
    default: CHECK(0);
    }
  }
  return sum;
}
static uint64_t work_bstr(Fixture *f, size_t iterations) {
  const Case *c = f->c;
  uint64_t sum = 0;
  for (size_t it = 0; it < iterations; ++it) {
    escape(f->bs);
    bstring s;
    switch (c->op) {
    case COPY_CSTR: case COPY_BYTES:
      s = c->op == COPY_CSTR ? bfromcstr(f->data) : blk2bstr(f->data, (int)c->n);
      CHECK(s); sum += checked_content(f, s->data, (size_t)s->slen, 0);
      CHECK(bdestroy(s) == BSTR_OK); break;
    case APPEND:
      s = bfromcstr(""); CHECK(s);
      if (c->flags & RESERVED) CHECK(balloc(s, (int)c->n + 1) == BSTR_OK);
      for (size_t at = 0; at < c->n; at += c->part) CHECK(bcatblk(s, f->data, (int)c->part) == BSTR_OK);
      sum += checked_content(f, s->data, (size_t)s->slen, 1);
      CHECK(bdestroy(s) == BSTR_OK); break;
    case COMPARE: sum += order(bstrcmp(f->bs, f->bs_other)); break;
    case FIND: { int p = binstr(f->bs, 0, f->bs_needle); sum += p >= 0 ? (uint64_t)p + 1 : 0; break; }
    case FIND_CHAR: { int p = bstrchr(f->bs, 'Z'); sum += p >= 0 ? (uint64_t)p + 1 : 0; break; }
    case SUBSTR:
      s = bmidstr(f->bs, (int)(c->n / 3), (int)c->part); CHECK(s);
      sum += checked_content(f, s->data, (size_t)s->slen, 0);
      CHECK(bdestroy(s) == BSTR_OK); break;
    case VIEW: {
      struct tagbstring view;
      bmid2tbstr(view, f->bs, (int)(c->n / 3), (int)c->part);
      sum += checked_content(f, view.data, (size_t)view.slen, 0); break;
    }
    case SPLIT_OWNED: {
      struct bstrList *tokens = bsplitstr(f->bs, f->bs_sep); CHECK(tokens);
      for (int j = 0; j < tokens->qty; ++j)
        sum += 1 + digest(tokens->entry[j]->data, (size_t)tokens->entry[j]->slen);
      CHECK(bstrListDestroy(tokens) == BSTR_OK); break;
    }
    default: CHECK(0);
    }
  }
  return sum;
}

typedef uint64_t (*Worker)(Fixture *, size_t);
static Worker workers[] = {work_sx, work_sds, work_bstr};
static const char *library_name(const Case *c, int lib) {
  if (lib == 0) return c->op == SPLIT_OWNED ? "StringX+materialize" : "StringX";
  if (lib == 1) return c->op == FIND || c->op == FIND_CHAR ? "SDS+glibc" :
                       c->op == VIEW ? "SDS+raw-view" : "SDS";
  return c->op == VIEW ? "bstrlib+borrowed-view" : "bstrlib";
}
#ifndef BENCH_ALLOC
static uint64_t now_ns(void) {
  struct timespec t; CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
  return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
#endif
static uint64_t checked_run(Worker fn, Fixture *f, size_t it) {
  uint64_t value = fn(f, it);
  CHECK(value == f->expected * (uint64_t)it);
  sink ^= value;
  return value;
}
#ifndef BENCH_ALLOC
static uint64_t measure(Worker fn, Fixture *f, size_t it) {
  uint64_t begin = now_ns();
  checked_run(fn, f, it);
  return now_ns() - begin;
}
static size_t calibrate(Worker fn, Fixture *f) {
  size_t it = 1;
  for (;;) {
    uint64_t a = measure(fn, f, it), b = measure(fn, f, it);
    if ((a < b ? a : b) >= 20000000ULL || it >= (1u << 24)) return it;
    it *= 2;
  }
}

#endif
#ifdef BENCH_ALLOC
static int counting;
static uint64_t mallocs, callocs, reallocs, frees, requested;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n) {
  if (counting) { ++mallocs; requested += n; }
  return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t s) {
  if (counting) { ++callocs; requested += n * s; }
  return __real_calloc(n, s);
}
void *__wrap_realloc(void *p, size_t n) {
  if (counting) { ++reallocs; requested += n; }
  return __real_realloc(p, n);
}
void __wrap_free(void *p) {
  if (counting && p) ++frees;
  __real_free(p);
}
#endif

int main(int argc, char **argv) {
  int check_only = argc == 2 && strcmp(argv[1], "--check") == 0;
  #ifndef BENCH_ALLOC
  uint32_t rng = 0x7132a8ef;
  #endif
#ifdef BENCH_ALLOC
  if (!check_only) puts("case,library,bytes,part,malloc_calls,calloc_calls,realloc_calls,free_calls,requested_bytes");
#else
  if (!check_only) puts("case,library,bytes,part,iterations,sample,elapsed_ns,ns_per_op,mib_per_s");
#endif
  size_t combinations = 0;
  for (size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ++ci) {
    Fixture f; prepare(&f, &cases[ci]);
    int libs = f.c->op == SPLIT_VIEW ? 1 : 3;
    validating = 1;
    for (int lib = 0; lib < libs; ++lib) {
      checked_run(workers[lib], &f, 1);
      ++combinations;
    }
    validating = 0;
    if (!check_only) {
#ifdef BENCH_ALLOC
      for (int lib = 0; lib < libs; ++lib) {
        mallocs = callocs = reallocs = frees = requested = 0;
        counting = 1;
        checked_run(workers[lib], &f, 1);
        counting = 0;
        printf("%s,%s,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
               f.c->name, library_name(f.c, lib), f.c->n, f.c->part,
               mallocs, callocs, reallocs, frees, requested);
      }
#else
      size_t iterations[3];
      for (int lib = 0; lib < libs; ++lib) iterations[lib] = calibrate(workers[lib], &f);
      for (int sample = 0; sample < SAMPLES; ++sample) {
        int sequence[] = {0, 1, 2};
        for (int i = libs - 1; i > 0; --i) {
          rng = rng * 1664525u + 1013904223u;
          int j = (int)(rng % (uint32_t)(i + 1));
          int tmp = sequence[i]; sequence[i] = sequence[j]; sequence[j] = tmp;
        }
        for (int j = 0; j < libs; ++j) {
          int lib = sequence[j];
          uint64_t elapsed = measure(workers[lib], &f, iterations[lib]);
          double ns = (double)elapsed / (double)iterations[lib];
          size_t bytes = f.c->op == SUBSTR || f.c->op == VIEW ? f.c->part : f.c->n;
          double rate = (double)bytes * 1000000000.0 / ns / 1048576.0;
          printf("%s,%s,%zu,%zu,%zu,%d,%" PRIu64 ",%.6f,%.6f\n",
                 f.c->name, library_name(f.c, lib), f.c->n, f.c->part,
                 iterations[lib], sample, elapsed, ns, rate);
        }
      }
#endif
    }
    cleanup(&f);
  }
  fprintf(stderr, "Validated %zu case/library combinations; sink=%" PRIu64 "\n", combinations, sink);
  return 0;
}
