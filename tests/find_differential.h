#ifndef SX_FIND_DIFFERENTIAL_TEST
#define SX_FIND_DIFFERENTIAL_TEST
#include "sx_core.h"
#include <build.h>
#include <stdint.h>
#include <string.h>

/* Independent window-by-window oracle. Test binary content, nonzero
   view offsets and boundaries at either side of legal match windows.
   Both native and forced-portable builds execute the same cases. */
static size_t reference(const unsigned char *h, size_t n,
                        const unsigned char *p, size_t m, size_t start) {
  if (!m || start >= n || m > n - start)
    return SIZE_MAX;
  for (size_t i = start; i <= n - m; ++i) {
    size_t j = 0;
    while (j < m && h[i + j] == p[j])
      ++j;
    if (j == m)
      return i;
  }
  return SIZE_MAX;
}

static uint32_t state = 0x72d9a351;
static uint32_t next(void) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

static void check(const unsigned char *h, size_t n,
                  const unsigned char *p, size_t m) {
  /* Minimal capacities put the content terminator immediately before
     the ASan redzone. The view's content also ends at its terminator. */
  sx_t *text = sx_new_with_cap(n + 1);
  sx_t *pattern = sx_new_with_cap(m + 4);
  TEST(text && pattern);
  TEST(sx_truncate(text, n) == text);
  TEST(sx_truncate(pattern, m + 3) == pattern);
  memcpy(sx_buf_mut(text), h, n);
  memset(sx_buf_mut(pattern), 0xa5, 3);
  memcpy(sx_buf_mut(pattern) + 3, p, m);
  sx_view_t v = sx_view(pattern);
  v.offset = 3;
  v.len = m;

  size_t expected = reference(h, n, p, m, 0);
  TEST(sx_find_view(text, v) == expected);
  sx_t *owned_pattern = sx_new_from_view(v);
  TEST(owned_pattern);
  TEST(sx_find(text, owned_pattern) == expected);
  if (!memchr(p, 0, m))
    TEST(sx_find_cstr(text, sx_buf_mut(owned_pattern)) == expected);

  size_t bounds[] = {0, 1, n / 2, n, n + 1, SIZE_MAX,
                     n >= m ? n - m : 0, next() % (n + 1)};
  for (size_t i = 0; i < sizeof(bounds) / sizeof(bounds[0]); ++i)
    TEST(sx_find_view_b(text, v, bounds[i]) ==
         reference(h, n, p, m, bounds[i]));
  if (expected != SIZE_MAX) {
    TEST(sx_find_view_b(text, v, expected) == expected);
    TEST(sx_find_view_b(text, v, expected + 1) ==
         reference(h, n, p, m, expected + 1));
  }
  sx_free(owned_pattern);
  sx_free(pattern);
  sx_free(text);
}

int main(void) {
  unsigned char h[2048], p[320];
  const size_t lengths[] = {0, 1, 2, 3, 7, 8, 16, 31, 32, 33,
                            63, 64, 127, 128, 129, 255, 256, 257, 320};
  for (size_t k = 0; k < sizeof(lengths) / sizeof(lengths[0]); ++k) {
    size_t m = lengths[k];
    for (size_t n = 0; n <= m + 2; ++n) {
      memset(h, 'a', n);
      memset(p, 'a', m);
      check(h, n, p, m);
      if (m) {
        p[m - 1] = 'b';
        check(h, n, p, m);
      }
    }
    for (unsigned mode = 0; mode < 6; ++mode) {
      memset(h, 'a', sizeof(h));
      memset(p, 'a', m);
      if (m)
        p[m - 1] = 'b';
      if (mode == 1 && m) /* tail hit */
        memcpy(h + sizeof(h) - m, p, m);
      if (mode == 2 && m) /* middle hit */
        memcpy(h + sizeof(h) / 2, p, m);
      if (mode == 3) /* both endpoint bytes present; near matches */
        for (size_t i = 0; i < sizeof(h); ++i)
          h[i] = (i % (m + 1) == 0) ? 'b' : 'a';
      if (mode == 4) /* binary content, high bytes and NULs */
        for (size_t i = 0; i < m; ++i)
          p[i] = (unsigned char)(i * 113);
      if (mode == 5 && m) {
        memset(p, 'a', m);
        p[m / 2] = 'c'; /* equal endpoints do not imply a match */
        h[sizeof(h) / 2] = 'c';
      }
      check(h, sizeof(h), p, m);
    }
  }
  for (unsigned it = 0; it < 3000; ++it) {
    size_t n = next() % (sizeof(h) + 1);
    size_t m = next() % (sizeof(p) + 1);
    unsigned alphabet = it % 3 == 0 ? 256 : it % 3 == 1 ? 3 : 1;
    for (size_t i = 0; i < n; ++i)
      h[i] = (unsigned char)(next() % alphabet);
    for (size_t i = 0; i < m; ++i)
      p[i] = (unsigned char)(next() % alphabet);
    if (m && m <= n && it % 2 == 0)
      memcpy(h + next() % (n - m + 1), p, m);
    check(h, n, p, m);
  }
  PASS();
}
#endif
