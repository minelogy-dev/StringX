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
 * Behavior boundaries — sx_write* family
 * ===================================================================
 * Overwrite a byte range starting at `idx`, then extend the length so
 * that buf[idx..idx+n) holds the new bytes:
 *     len' = max(len, idx + n)          (never shrinks)
 * and buf[len'] == '\0' afterwards. Writing at idx >= len appends or
 * pads; the padding bytes in (len, idx) are UNSPECIFIED.
 *
 * In scope (contract):
 *   · idx == len    behaves as append.
 *   · idx < len     overwrites in place; len unchanged if the written
 *                   range stays inside the content.
 *   · idx > len     extends len to idx+n; bytes in [old len, idx) are
 *                   indeterminate (never asserted on).
 *   · sx_write(self, idx, owned) frees `owned` after copy
 *     (ownership transfer).
 *   · sx_write_view(self, idx, v) is binary safe and may alias self
 *     (memmove path), INCLUDING when the buffer must grow.
 *   · *_cstr uses strlen (embedded '\0' truncates); NULL argument
 *     returns NULL without modifying self.
 *
 * Out of contract (not exercised):
 *   · self == NULL or freed; idx + n overflowing size_t; reading the
 *     padding gap after an out-of-bounds write; sx_write(self, idx,
 *     self) via the owned variant.
 */

int main(void) {
  sx_t *s;

  /* in-place overwrite: shorter, equal, longer than the old range */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_write_cstr(s, 2, "XY") == s); /* "abXYef": len stays 6 */
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abXYef") == 0);
  sx_free(s);

  s = sx_new_from_cstr("abcdef");
  TEST(sx_write_cstr(s, 2, "XYZ") == s); /* "abXYZf" */
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abXYZf") == 0);
  sx_free(s);

  s = sx_new_from_cstr("abcdef");
  TEST(sx_write_cstr(s, 2, "PQRSTUV") == s); /* extends: len 2+7=9 */
  TEST(sx_len(s) == 9 && sx_cmp_cstr(s, "abPQRSTUV") == 0); /* strict */
  TEST(sx_buf_mut(s)[9] == '\0');
  sx_free(s);

  /* idx == len behaves as append */
  s = sx_new_from_cstr("abc");
  TEST(sx_write_cstr(s, 3, "def") == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0);
  sx_free(s);

  /* idx > len: length extension with unspecified padding */
  s = sx_new_from_cstr("abcd");
  TEST(sx_write_cstr(s, 10, "XY") == s);
  TEST(sx_len(s) == 12);           /* extended to idx+n */
  TEST(sx_buf_mut(s)[0] == 'a');   /* old content survives */
  TEST(sx_buf_mut(s)[12] == '\0'); /* terminator moved out */
  s = sx_write_cstr(s, 4, "efghij"); /* define the gap bytes */
  TEST(sx_cmp_cstr(s, "abcdefghijXY") == 0); /* now fully known */
  sx_free(s);

  /* idx == 0 on an empty buffer */
  s = sx_new();
  TEST(sx_write_cstr(s, 0, "hi") == s);
  TEST(sx_len(s) == 2 && sx_cmp_cstr(s, "hi") == 0);
  sx_free(s);

  /* write (owned): content copied, owner freed */
  s = sx_new_from_cstr("abcd");
  sx_t *o = sx_new_from_cstr("XY");
  TEST(sx_write(s, 1, o) == s);
  TEST(sx_len(s) == 4 && sx_cmp_cstr(s, "aXYd") == 0);
  sx_free(s);

  /* NULL handling: self untouched */
  s = sx_new_from_cstr("abcd");
  TEST(sx_write_cstr(s, 2, NULL) == NULL);
  TEST(sx_write(s, 2, NULL) == NULL);
  TEST(sx_len(s) == 4 && sx_cmp_cstr(s, "abcd") == 0);
  sx_free(s);

  /* write_view aliasing self: memmove order, even across growth */
  s = sx_new_from_cstr("abcdef");
  TEST(sx_write_view(s, 0, sx_view_advance(sx_view(s), 2)) == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "cdefef") == 0);
  sx_free(s);

  /* growth + aliasing: pre-grow must keep the source pointer valid */
  s = sx_new_from_cstr("abc");
  TEST(sx_write_view(s, 3, sx_view(s)) == s); /* "abcabc" */
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcabc") == 0);
  sx_free(s);

  /* write_view embedded NUL: binary safe */
  sx_t *src = sx_new_from_cstr("a");
  src = sx_write_cstr(src, 2, "c");
  sx_buf_mut(src)[1] = '\0'; /* "a\0c" */
  s = sx_new_from_cstr("xxxxxx");
  TEST(sx_write_view(s, 1, sx_view(src)) == s);
  TEST(sx_len(s) == 6);
  TEST(sx_buf_mut(s)[0] == 'x' && sx_buf_mut(s)[1] == 'a' &&
       sx_buf_mut(s)[2] == '\0' && sx_buf_mut(s)[3] == 'c');
  sx_free(s);
  sx_free(src);

  PASS();
}