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
 * Behavior boundaries — sx_append* family
 * ===================================================================
 * Append bytes at the end of a buffer. All three forms append at
 * index len(self), return `self` on success, and leave self's length
 * exactly len+appended bytes with buf[len] == '\0' afterwards.
 *
 * In scope (contract):
 *   · sx_append(self, owned)   append owned's CONTENT, then free
 *                              `owned` (ownership transfer: the
 *                              caller must not use `owned` after the
 *                              call). NULL owned -> NULL, self
 *                              untouched.
 *   · sx_append_cstr(self, s)  strlen semantics: an embedded '\0'
 *                              truncates the appended bytes. NULL s
 *                              -> NULL, self untouched.
 *   · sx_append_view(self, v)  byte-exact: v.len bytes from
 *                              v.ref->ptr + v.offset, embedded '\0'
 *                              included. v may alias self (memmove
 *                              path), e.g. double a buffer in place.
 *   · Growing may reallocate the backing array: pointers obtained
 *     from sx_buf_mut are invalidated; the sx_t handle is not.
 *
 * Out of contract (not exercised):
 *   · self == NULL or freed; sx_append(self, self) via the owned
 *     variant (overlapping copy through the non-view path); views
 *     whose bounds exceed their backing buffer.
 */

int main(void) {
  sx_t *s;

  /* append_cstr: basic, empty, repeated */
  s = sx_new_from_cstr("abc");
  TEST(sx_append_cstr(s, "def") == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0);
  TEST(sx_append_cstr(s, "") == s); /* empty append: no-op */
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0);
  TEST(sx_append_cstr(s, "gh") == s);
  TEST(sx_cmp_cstr(s, "abcdefgh") == 0);
  sx_free(s);

  /* append_cstr NULL: NULL result, self untouched */
  s = sx_new_from_cstr("abc");
  TEST(sx_append_cstr(s, NULL) == NULL);
  TEST(sx_len(s) == 3 && sx_cmp_cstr(s, "abc") == 0);
  sx_free(s);

  /* append (owned): ownership transfer */
  s = sx_new_from_cstr("abc");
  sx_t *o = sx_new_from_cstr("def");
  TEST(sx_append(s, o) == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0);
  /* o is gone: no double free, leak sanitizer is the witness */
  sx_free(s);

  /* append NULL owned: NULL result, self untouched */
  s = sx_new_from_cstr("abc");
  TEST(sx_append(s, NULL) == NULL);
  TEST(sx_len(s) == 3 && sx_cmp_cstr(s, "abc") == 0);
  sx_free(s);

  /* append_view: middle slice, byte-exact */
  sx_t *src = sx_new_from_cstr("uz");
  s = sx_new_from_cstr("abc");
  sx_view_t mid = sx_view_advance(sx_view(src), 1); /* "z" */
  TEST(sx_append_view(s, mid) == s);
  TEST(sx_cmp_cstr(s, "abcz") == 0);
  sx_free(s);
  sx_free(src);

  /* append_view aliasing self: in-place doubling uses memmove */
  s = sx_new_from_cstr("abc");
  TEST(sx_append_view(s, sx_view(s)) == s);
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcabc") == 0);
  sx_free(s);

  /* append_view with embedded NUL: binary safe */
  src = sx_new_from_cstr("a");
  src = sx_write_cstr(src, 2, "c");
  sx_buf_mut(src)[1] = '\0'; /* "a\0c" */
  s = sx_new_from_cstr("xy");
  TEST(sx_append_view(s, sx_view(src)) == s);
  TEST(sx_len(s) == 5);
  TEST(memcmp(sx_buf_mut(s), "xy", 2) == 0);
  TEST(sx_buf_mut(s)[2] == 'a' && sx_buf_mut(s)[3] == '\0' &&
       sx_buf_mut(s)[4] == 'c');
  TEST(sx_buf_mut(s)[5] == '\0');
  sx_free(s);
  sx_free(src);

  /* growth across the initial capacity, append chain */
  s = sx_new_with_cap(2);
  for (int i = 0; i < 3; i++)
    TEST(sx_append_cstr(s, "abcd") == s);
  TEST(sx_len(s) == 12 && sx_cmp_cstr(s, "abcdabcdabcd") == 0);
  TEST(sx_buf_mut(s)[12] == '\0');
  sx_free(s);

  PASS();
}