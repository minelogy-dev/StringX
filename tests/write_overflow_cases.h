#ifndef SX_WRITE_OVERFLOW_CASES
#define SX_WRITE_OVERFLOW_CASES
#include "sx_core.h"
#include <stdint.h>
#include <string.h>
#ifdef SX_WRITE_OVERFLOW_STANDALONE
#include <stdio.h>
#include <stdlib.h>
#define TEST(x) do { if (!(x)) { \
  fprintf(stderr, "failed %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)
#define PASS() puts("PASSED: write overflow")
#else
#include <build.h>
#endif

static void unchanged(sx_t *s, char *buffer, int cap) {
  TEST(sx_buf_mut(s) == buffer);
  TEST(sx_cap(s) == cap && sx_len(s) == 3);
  TEST(memcmp(sx_buf_mut(s), "abc", 4) == 0);
}

int main(void) {
  sx_t *s = sx_new_from_cstr("abc");
  sx_t *source = sx_new_with_cap(41);
  TEST(s && source && sx_truncate(source, 40) == source);
  for (size_t i = 0; i < 40; ++i)
    sx_buf_mut(source)[i] = i % 7 == 0 ? 0 : (char)(0x80 + i);
  sx_view_t view = sx_view(source);
  view.offset = 4;
  view.len = 32;
  char text[33];
  memset(text, 'x', 32);
  text[32] = 0;
  char *buffer = sx_buf_mut(s);
  int cap = sx_cap(s);

  /* Baseline repro: SIZE_MAX + 1 wraps to zero, bypasses reserve,
     then copies one byte just before the target allocation. */
  TEST(sx_write_cstr(s, SIZE_MAX, "X") == NULL);
  unchanged(s, buffer, cap);

  /* Cover sums wrapping to 0..31, including wrapped values large
     enough to trigger the view pre-reserve on the old implementation.
     back==32 yields SIZE_MAX, which cannot include a terminator. */
  for (size_t back = 0; back <= 32; ++back) {
    size_t idx = SIZE_MAX - back;
    TEST(sx_write_cstr(s, idx, text) == NULL);
    unchanged(s, buffer, cap);
    TEST(sx_write_view(s, idx, view) == NULL);
    unchanged(s, buffer, cap);
    sx_t *owned = sx_new_from_cstr(text);
    TEST(owned);
    TEST(sx_write(s, idx, owned) == NULL); /* consumed even on failure */
    unchanged(s, buffer, cap);
    TEST(sx_len(source) == 40 && sx_buf_mut(source)[4] == (char)0x84);
  }

  sx_view_t alias = sx_view(s);
  for (size_t back = 0; back <= 3; ++back) {
    TEST(sx_write_view(s, SIZE_MAX - back, alias) == NULL);
    unchanged(s, buffer, cap);
    TEST(memcmp(sx_buf_view_mut(alias), "abc", 3) == 0);
  }
  view.len = 0;
  TEST(sx_write_view(s, SIZE_MAX, view) == NULL);
  unchanged(s, buffer, cap);
  TEST(sx_write_view(s, 3, view) == s);
  unchanged(s, buffer, cap);

  sx_t *binary = sx_new_from_cstr("XqY");
  TEST(binary);
  sx_buf_mut(binary)[1] = 0;
  TEST(sx_write_view(s, 1, sx_view(binary)) == s);
  const char expected[] = {'a', 'X', 0, 'Y'};
  TEST(sx_len(s) == 4 && memcmp(sx_buf_mut(s), expected, 4) == 0);
  TEST(sx_buf_mut(s)[4] == 0);

  /* Valid aliased writes must still reacquire the source after growth. */
  alias = sx_view(s);
  TEST(sx_write_view(s, 4, alias) == s);
  TEST(sx_len(s) == 8);
  TEST(memcmp(sx_buf_mut(s), expected, 4) == 0);
  TEST(memcmp(sx_buf_mut(s) + 4, expected, 4) == 0);
  TEST(sx_buf_mut(s)[8] == 0);
  TEST(sx_write(s, 0, sx_new_from_cstr("ok")) == s);
  TEST(memcmp(sx_buf_mut(s), "ok", 2) == 0 && sx_len(s) == 8);
  sx_free(binary);
  sx_free(source);
  sx_free(s);
  PASS();
  return 0;
}
#endif
