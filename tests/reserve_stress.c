// Include include
// Include src
// Include /usr/lib/forge/include
// Source src/sx_raw.c
// Source src/sx_core.c
// Source src/sx_adv.c
// Option -std=c23
// Option -O1
// Option -g
// Option -fsanitize=address,undefined
// Option -fno-omit-frame-pointer
// Option -DSX_FORCE_EXACT_RESERVE
// Library /usr/local/lib
#include "sx_core.h"
#include <build.h>
#include <string.h>

/*
 * ===================================================================
 * Behavior boundaries — exact-reserve allocation stress
 * ===================================================================
 * BUILD CONFIG: the `// Option -DSX_FORCE_EXACT_RESERVE' line above
 * remaps sx__reserve (src/sx_raw.c) from geometric growth to exact
 * sizing (cap == need + 1).  The allocation then ends exactly at the
 * terminator, so any access beyond len hits the ASan redzone
 * immediately — no slack capacity to hide an off-by-one or a stale
 * len behind.  This file is that mode's dedicated test config.
 *
 * The workload is deliberately allocation-heavy:
 *   - every live buffer starts from the minimal capacity (cap 1),
 *     so appends pay the worst case: a realloc per byte;
 *   - insert / erase / write (in-bounds and extending) / truncate /
 *     aliased-view edits rotate through three live buffers, and each
 *     edit is verified against a byte-exact model;
 *   - periodic dup / substr / free cycles and the aliased
 *     insert_view-growth regression close the loop.
 *
 * In scope (contract):
 *   · EVERY edit is asserted by strict content comparison
 *     (sx_cmp_cstr against the model) — never by length alone;
 *     len appears only alongside full-content equality.
 *   · whenever an edit changes the capacity, exact mode must yield
 *     cap == len + 1 (tight); cap >= len + 1 and buf[len] == '\0'
 *     (single-NUL invariant) hold always.
 *   · shrinking edits (erase / truncate down / clear) must never
 *     reallocate.
 *
 * Out of contract (not exercised):
 *   · models beyond 512 bytes (the +0x10000 growth band never
 *     applies in exact mode); multi-threaded access.
 * Determinism: a fixed LCG drives every edit, so any failure
 * reproduces byte-for-byte.
 */

/* ---- deterministic PRNG ---- */
static unsigned long long rng = 0x9e3779b97f4a7c15ULL;
static unsigned long long prng(void) {
  rng ^= rng << 7;
  rng ^= rng >> 9;
  return rng;
}
static size_t rnd(size_t hi) { return (size_t)(prng() % hi); }

/* ---- byte-exact model of one buffer ---- */
typedef struct {
  char buf[512];
  size_t len;
} model_t;

static void m_append(model_t *m, const char *tok, size_t n) {
  memcpy(m->buf + m->len, tok, n);
  m->len += n;
  m->buf[m->len] = '\0'; /* keep the model NUL-terminated: stale
                            bytes from a truncate may sit beyond len */
}
static void m_insert(model_t *m, size_t idx, const char *tok, size_t n) {
  memmove(m->buf + idx + n, m->buf + idx, m->len - idx);
  memcpy(m->buf + idx, tok, n);
  m->len += n;
  m->buf[m->len] = '\0';
}
static void m_erase(model_t *m, size_t idx, size_t n) {
  memmove(m->buf + idx, m->buf + idx + n, m->len - idx - n);
  m->len -= n;
  m->buf[m->len] = '\0';
}
static void m_write(model_t *m, size_t idx, const char *tok, size_t n) {
  memcpy(m->buf + idx, tok, n);
  if (idx + n > m->len)
    m->len = idx + n;
  m->buf[m->len] = '\0';
}

/* content check: strict cmp only (token alphabet avoids '\0', so
   cmp_cstr's strlen always agrees with the model's len) */
static void check(sx_t *cur, const model_t *m, int cap0, int len0) {
  TEST(sx_cmp_cstr(cur, m->buf) == 0);
  TEST(sx_buf_mut(cur)[sx_len(cur)] == '\0'); /* terminator invariant */
  TEST(sx_cap(cur) >= sx_len(cur) + 1);
  if (sx_cap(cur) != cap0) { /* this edit reallocated */
#if defined(SX_FORCE_EXACT_RESERVE)
    TEST(sx_cap(cur) == sx_len(cur) + 1); /* exact mode: tight */
#else
    TEST(sx_cap(cur) > cap0);
#endif
  }
  if (sx_len(cur) < len0) /* shrinking edits never allocate */
    TEST(sx_cap(cur) == cap0);
}

int main(void) {
  /* three live buffers from the minimal capacity */
  sx_t *a = sx_new_with_cap(1);
  sx_t *b = sx_new_with_cap(1);
  sx_t *c = sx_new_with_cap(1);
  model_t ma = {{0}, 0}, mb = {{0}, 0}, mc = {{0}, 0};

  for (int it = 0; it < 1500; it++) {
    sx_t *cur;
    model_t *m;
    switch (it % 3) { /* rotate the hot buffer */
    case 0: cur = a; m = &ma; break;
    case 1: cur = b; m = &mb; break;
    default: cur = c; m = &mc; break;
    }

    char tok[8];
    size_t n = 1 + rnd(7);
    for (size_t i = 0; i < n; i++)
      tok[i] = (char)('a' + rnd(23));
    tok[n] = '\0'; /* cstr variants read via strlen */

    const int cap0 = sx_cap(cur);
    const int len0 = sx_len(cur);

    switch (rnd(6)) {
    case 0: /* append: worst-case growth from minimal cap */
      m_append(m, tok, n);
      TEST(sx_append_cstr(cur, tok) == cur);
      break;
    case 1: /* insert at 0 <= idx <= len */
      {
        size_t idx = rnd(m->len + 1);
        m_insert(m, idx, tok, n);
        TEST(sx_insert_cstr(cur, idx, tok) == cur);
      }
      break;
    case 2: /* erase: 1 <= k <= len - idx, idx < len */
      if (m->len > 0) {
        size_t idx = rnd(m->len);
        size_t k = 1 + rnd(m->len - idx);
        m_erase(m, idx, k);
        TEST(sx_erase(cur, idx, k) == cur);
      }
      break;
    case 3: /* write at idx < len; may extend when idx+n > len */
      {
        size_t idx = rnd(m->len ? m->len : 1);
        m_write(m, idx, tok, n);
        TEST(sx_write_cstr(cur, idx, tok) == cur);
      }
      break;
    case 4: /* truncate to 0 <= t <= len */
      {
        size_t t = rnd(m->len + 1);
        m->len = t;
        m->buf[t] = '\0'; /* stale bytes may sit beyond t */
        TEST(sx_truncate(cur, t) == cur);
      }
      break;
    case 5: /* aliased edit: append a slice of the buffer to itself */
      if (m->len > 0) {
        size_t off = rnd(m->len);
        size_t k = 1 + rnd(m->len - off);
        m_append(m, m->buf + off, k);
        sx_view_t sv = sx_view(cur);
        sv.offset = off;
        sv.len = k;
        TEST(sx_append_view(cur, sv) == cur);
      }
      break;
    }

    check(cur, m, cap0, len0);

    /* periodic clone / copy-out / free cycles */
    if (it % 257 == 0) {
      sx_t *d = sx_dup(cur);
      TEST(sx_cmp_cstr(d, m->buf) == 0);
      sx_free(d);
    }
    if (it % 503 == 0 && m->len > 2) {
      size_t off = rnd(m->len - 2);
      size_t ln = 1 + rnd(m->len - off);
      sx_t *sub = sx_substr(cur, off, ln);
      TEST(sx_len(sub) == (int)ln);
      TEST(memcmp(sx_buf_mut(sub), m->buf + off, ln) == 0);
      sx_free(sub);
    }
  }
  sx_free(a);
  sx_free(b);

  /* aliased-view regression: the insert pre-reserve must cover the
     FINAL length, or the source pointer dangles across the realloc */
  a = sx_new_from_cstr("abcdef");
  sx_view_t v = sx_view_advance(sx_view(a), 2);
  v.len = 2; /* "cd" */
  TEST(sx_insert_view(a, 2, v) == a);
  TEST(sx_cmp_cstr(a, "abcdcdef") == 0);
  sx_free(a);

  a = sx_new_from_cstr("abc");
  TEST(sx_append_view(a, sx_view(a)) == a);
  TEST(sx_cmp_cstr(a, "abcabc") == 0);
  sx_free(a);

  a = sx_new_from_cstr("abcdef");
  TEST(sx_write_view(a, 0, sx_view_advance(sx_view(a), 2)) == a);
  TEST(sx_cmp_cstr(a, "cdefef") == 0);
  sx_free(a);

  /* embedded NUL round-trips byte-exact through dup and substr */
  b = sx_new_from_cstr("x");
  b = sx_write_cstr(b, 2, "z");
  sx_buf_mut(b)[1] = '\0'; /* "x\0z" */
  sx_t *bd = sx_dup(b);
  TEST(sx_len(bd) == 3);
  TEST(sx_buf_mut(bd)[0] == 'x' && sx_buf_mut(bd)[1] == '\0' &&
       sx_buf_mut(bd)[2] == 'z');
  sx_t *bs = sx_substr(b, 0, 3);
  TEST(sx_len(bs) == 3);
  TEST(sx_buf_mut(bs)[0] == 'x' && sx_buf_mut(bs)[1] == '\0' &&
       sx_buf_mut(bs)[2] == 'z');
  sx_free(bs);
  sx_free(bd);
  sx_free(b);
  sx_free(c);

  PASS();
  return 0;
}