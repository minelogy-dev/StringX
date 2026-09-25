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
 * Behavior boundaries — shrink family (sx_truncate / sx_erase /
 *                         sx_clear)
 * ===================================================================
 * Operations that remove content (and sx_truncate's growing side).
 * After every call buf[len'] == '\0'.
 *
 * In scope (contract):
 *   · sx_truncate(self, n)   set len = n: shorter -> prefix kept, tail
 *                            dropped; unchanged n -> no-op; LONGER n
 *                            grows the buffer: len = n, bytes in
 *                            [old len, n) are UNSPECIFIED, buf[n]
 *                            == '\0'.
 *   · sx_erase(self, idx, n) remove the n bytes at idx, shifting the
 *                            tail left (memmove); len' = len - n.
 *                            If idx + n > len the call is a no-op
 *                            (returns self unchanged). n == 0 is a
 *                            no-op. Erasing exactly to the end works
 *                            (idx + n <= len).
 *   · sx_clear(self)         len = 0, buf[0] = '\0'; the capacity is
 *                            kept, so the buffer stays reusable.
 *   · all return `self`.
 *
 * Out of contract (not exercised):
 *   · self == NULL or freed; erase idx > len with n such that
 *     idx + n wraps; reading the unspecified gap after a growing
 *     truncate.
 */

int main(void) {
  sx_t *s;

  /* truncate: shrink to a shorter length */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_truncate(s, 3) == s);
  TEST(sx_len(s) == 3 && sx_cmp_cstr(s, "abc") == 0);
  TEST(sx_buf_mut(s)[3] == '\0');
  sx_free(s);

  /* truncate to zero */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_truncate(s, 0) == s);
  TEST(sx_len(s) == 0 && sx_buf_mut(s)[0] == '\0');
  sx_free(s);

  /* truncate to the same length: no-op */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_truncate(s, 6) == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0);
  sx_free(s);

  /* truncate growth: len extends, prefix preserved, NUL at the end */
  s = sx_new_from_cstr("abc");
  TEST(sx_truncate(s, 10) == s);
  TEST(sx_len(s) == 10);
  TEST(memcmp(sx_buf_mut(s), "abc", 3) == 0);
  TEST(sx_buf_mut(s)[10] == '\0');
  /* gap bytes (3..9) are unspecified: fill them, then compare the
     whole content strictly instead of judging by length alone */
  s = sx_write_cstr(s, 3, "defghij"); /* now content fully known */
  TEST(sx_cmp_cstr(s, "abcdefghij") == 0);
  TEST(sx_len(s) == 10); /* the write did not shorten the buffer */
  sx_free(s);

  /* erase: middle, front, exact-end */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_erase(s, 2, 2) == s);
  TEST(sx_len(s) == 4 && sx_cmp_cstr(s, "abef") == 0);
  sx_free(s);

  s = sx_new_from_cstr("abcdef");
  TEST(sx_erase(s, 0, 2) == s);
  TEST(sx_len(s) == 4 && sx_cmp_cstr(s, "cdef") == 0);
  sx_free(s);

  s = sx_new_from_cstr("abcdef");
  TEST(sx_erase(s, 2, 4) == s); /* idx+n == len: allowed */
  TEST(sx_len(s) == 2 && sx_cmp_cstr(s, "ab") == 0);
  sx_free(s);

  /* erase beyond the end and n == 0: no-ops */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_erase(s, 4, 10) == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0);
  sx_free(s);

  s = sx_new_from_cstr("abcdef");
  TEST(sx_erase(s, 0, 0) == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0);
  sx_free(s);

  /* erase then append: buffer stays healthy (NUL moved correctly) */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_erase(s, 1, 3) == s); /* "aef" */
  TEST(sx_append_cstr(s, "g") == s);
  TEST(sx_len(s) == 4 && sx_cmp_cstr(s, "aefg") == 0);
  sx_free(s);

  /* clear: empty again, buffer reusable */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_clear(s) == s);
  TEST(sx_len(s) == 0 && sx_buf_mut(s)[0] == '\0');
  TEST(sx_append_cstr(s, "new") == s);
  TEST(sx_len(s) == 3 && sx_cmp_cstr(s, "new") == 0);
  sx_free(s);

  PASS();
}