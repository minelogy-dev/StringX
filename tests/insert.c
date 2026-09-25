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
#include <string.h>

/*
 * ===================================================================
 * Behavior boundaries — sx_insert* family
 * ===================================================================
 * Insert bytes at index `idx`, shifting the tail right; the result is
 * buf[0..idx) ++ bytes ++ buf[idx..len) and len' = len + n.
 *
 * In scope (contract):
 *   · 0 <= idx <= len: at idx == len insertion is append-like
 *     ("abcd" + "EF" at 4 -> "abcdEF").
 *   · sx_insert(self, idx, owned) frees `owned` after the copy
 *     (ownership transfer); `owned` must not be `self`.
 *   · sx_insert_view(self, idx, v) is binary safe and v may alias
 *     self (two-phase memmove copy), even when the buffer grows.
 *   · *_cstr uses strlen (embedded '\0' truncates); NULL argument
 *     returns NULL without modifying self.
 *   · self is returned on success; after every call
 *     buf[len'] == '\0'.
 *
 * Out of contract (not exercised):
 *   · self == NULL or freed; idx > len (tail-shift underflows);
 *     sx_insert(self, idx, self) via the owned variant (frees the
 *     buffer being modified); views whose bounds exceed their
 *     backing buffer.
 */

int main(void) {
  sx_t *s;

  /* middle, front, end */
  s = sx_new_from_cstr("abcd");
  TEST(sx_insert_cstr(s, 2, "XY") == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abXYcd") == 0);
  sx_free(s);

  s = sx_new_from_cstr("abcd");
  TEST(sx_insert_cstr(s, 0, "XY") == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "XYabcd") == 0);
  sx_free(s);

  s = sx_new_from_cstr("abcd");
  TEST(sx_insert_cstr(s, 4, "EF") == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdEF") == 0);
  sx_free(s);

  /* empty buffer */
  s = sx_new();
  TEST(sx_insert_cstr(s, 0, "A") == s);
  TEST(sx_len(s) == 1 && sx_cmp_cstr(s, "A") == 0);
  sx_free(s);

  /* insert (owned): content copied, owner freed */
  s = sx_new_from_cstr("cd");
  sx_t *o = sx_new_from_cstr("ab");
  TEST(sx_insert(s, 0, o) == s);
  TEST(sx_len(s) == 4 && sx_cmp_cstr(s, "abcd") == 0);
  sx_free(s);

  /* NULL handling: self untouched */
  s = sx_new_from_cstr("abcd");
  TEST(sx_insert_cstr(s, 2, NULL) == NULL);
  TEST(sx_insert(s, 2, NULL) == NULL);
  TEST(sx_len(s) == 4 && sx_cmp_cstr(s, "abcd") == 0);
  sx_free(s);

  /* insert_view from another buffer */
  sx_t *src = sx_new_from_cstr("-XY-");
  s = sx_new_from_cstr("abcd");
  sx_view_t fv = sx_view_advance(sx_view(src), 1);
  fv.len = 2; /* "XY" */
  TEST(sx_insert_view(s, 2, fv) == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abXYcd") == 0);
  sx_free(s);
  sx_free(src);

  /* insert_view aliasing self, no growth: two-phase copy is safe */
  s = sx_new_from_cstr("abcd");
  sx_view_t v = sx_view_advance(sx_view(s), 1);
  v.len = 2; /* "bc" */
  TEST(sx_insert_view(s, 1, v) == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcbcd") == 0);
  sx_free(s);

  /* insert_view aliasing self, WITH growth: the inner reserve must
     not invalidate the source pointer (currently FAILS: the source
     pointer is computed before insert()'s realloc — see sx_core.c,
     sx_insert_view / insert).  "abcdcdef". */
  s = sx_new_from_cstr("abcdef"); /* cap 7 */
  v = sx_view_advance(sx_view(s), 2);
  v.len = 2; /* "cd" */
  TEST(sx_insert_view(s, 2, v) == s);
  TEST(sx_len(s) == 8 && sx_cmp_cstr(s, "abcdcdef") == 0);
  sx_free(s);

  /* binary-safe insert (embedded NUL) */
  src = sx_new_from_cstr("a");
  src = sx_write_cstr(src, 2, "c");
  sx_buf_mut(src)[1] = '\0'; /* "a\0c" */
  s = sx_new_from_cstr("xy");
  TEST(sx_insert_view(s, 1, sx_view(src)) == s);
  TEST(sx_len(s) == 5);
  TEST(sx_buf_mut(s)[0] == 'x' && sx_buf_mut(s)[1] == 'a' &&
       sx_buf_mut(s)[2] == '\0' && sx_buf_mut(s)[3] == 'c' &&
       sx_buf_mut(s)[4] == 'y');
  TEST(sx_buf_mut(s)[5] == '\0');
  sx_free(s);
  sx_free(src);

  PASS();
}