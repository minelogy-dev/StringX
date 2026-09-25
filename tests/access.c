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
// Library /usr/local/lib
#include "sx_core.h"
#include <build.h>

/*
 * ===================================================================
 * Behavior boundaries — accessor family (sx_len / sx_cap / sx_buf /
 *                         sx_view)
 * ===================================================================
 * Read-only plumbing: sizes, raw pointer, and the whole-buffer view.
 * None of these functions ever mutate the buffer.
 *
 * In scope (contract):
 *   · sx_len(h)      number of CONTENT bytes; counts embedded '\0'
 *                    (binary safe; does not stop at NUL).
 *   · sx_len_view(v) the view's stored len, unchanged by the call.
 *   · sx_cap(h)      total bytes allocated for the content array;
 *                    always >= len+1 (single-NUL invariant).
 *   · sx_buf_mut(h)  pointer to the content array; buf[len] == '\0';
 *                    the caller may rewrite bytes inside [0, len)
 *                    without changing len, and must not extend past
 *                    len without a length-changing call.
 *   · sx_buf_view_mut(v) buf_mut(v.ref) + v.offset: mutable access to
 *                        the slice behind a view.
 *   · sx_view(h)     {ref, offset: 0, len: len(h)} — the canonical
 *                    whole-string view.
 *
 * Out of contract (not exercised):
 *   · h == NULL; writing at index >= len through *_mut without a
 *     matching length change; views pointing past the buffer end.
 */

int main(void) {
  sx_t *s = sx_new_from_cstr("abc");

  /* sx_len counts content bytes */
  TEST(sx_len(s) == 3 && sx_cmp_cstr(s, "abc") == 0);
  sx_t *e = sx_new();
  TEST(sx_len(e) == 0);
  sx_free(e);
  /* embedded NUL is content, not a terminator */
  e = sx_new_from_cstr("a");
  e = sx_write_cstr(e, 2, "c");
  sx_buf_mut(e)[1] = '\0'; /* "a\0c" */
  TEST(sx_len(e) == 3);
  TEST(sx_buf_mut(e)[0] == 'a' && sx_buf_mut(e)[1] == '\0' &&
       sx_buf_mut(e)[2] == 'c'); /* content, byte-exact */
  /* NUL bytes in the buffer do not stop sx_len */
  TEST(sx_buf_mut(e)[sx_len(e)] == '\0'); /* terminator is the 4th byte */
  sx_free(e);

  /* sx_view: whole-string view */
  sx_view_t v = sx_view(s);
  TEST(v.offset == 0 && v.len == 3);

  /* sx_cap: room for content + terminator */
  TEST(sx_cap(s) >= sx_len(s) + 1);
  s = sx_write_cstr(s, 3, "defg"); /* len 7 now; buffer grew */
  TEST(sx_cap(s) >= 8);
  TEST(sx_buf_mut(s)[sx_len(s)] == '\0');
  sx_free(s);

  /* sx_buf_mut: in-place byte rewrite, len untouched */
  s = sx_new_from_cstr("abc");
  char *b = sx_buf_mut(s);
  b[0] = 'x';
  TEST(sx_len(s) == 3);
  TEST(sx_cmp_cstr(s, "xbc") == 0);
  sx_free(s);

  /* sx_len_view and sx_buf_view_mut on a derived view */
  s = sx_new_from_cstr("abcdef");
  sx_view_t sub = sx_view_advance(sx_view(s), 2); /* "cdef" */
  TEST(sx_len_view(sub) == 4);
  char *sb = sx_buf_view_mut(sub);
  TEST(sb == sx_buf_mut(s) + 2); /* slice starts inside the same array */
  sb[0] = 'X';                   /* and writes reach the buffer */
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abXdef") == 0); /* strict */
  sx_free(s);

  /* views never mutate the buffer */
  s = sx_new_from_cstr("abc");
  sx_view(s);
  TEST(sx_len(s) == 3 && sx_cmp_cstr(s, "abc") == 0);
  sx_free(s);

  PASS();
}