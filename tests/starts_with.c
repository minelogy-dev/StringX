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
 * Behavior boundaries — sx_starts_with* family
 * ===================================================================
 * Prefix predicate: 1 iff the first len(prefix) content bytes of h
 * equal the prefix byte-for-byte.
 *
 * In scope (contract):
 *   · the empty prefix always matches (even on an empty buffer).
 *   · a prefix longer than h never matches.
 *   · byte-exact semantics: an embedded '\0' in h or the prefix is an
 *     ordinary byte; *_cstr uses strlen (its NUL cannot be part of a
 *     prefix).
 *   · *_view reads prefix.len bytes from prefix.ref->ptr +
 *     prefix.offset; the owned variant reads the needle's content.
 *   · NULL h or NULL prefix -> 0.
 *
 * Out of contract (not exercised):
 *   · views whose bounds exceed their backing buffer.
 */

int main(void) {
  sx_t *h = sx_new_from_cstr("abcdef");

  /* cstr */
  TEST(sx_starts_with_cstr(h, "abc") == 1);
  TEST(sx_starts_with_cstr(h, "abcdef") == 1); /* prefix == whole h */
  TEST(sx_starts_with_cstr(h, "abx") == 0);
  TEST(sx_starts_with_cstr(h, "abcdefg") == 0); /* longer than h    */
  TEST(sx_starts_with_cstr(h, "") == 1);        /* empty prefix     */

  /* owned */
  sx_t *p = sx_new_from_cstr("abc");
  TEST(sx_starts_with(h, p) == 1);
  sx_free(p);
  p = sx_new_from_cstr("abd");
  TEST(sx_starts_with(h, p) == 0);
  sx_free(p);

  /* view: prefix taken from the middle of another buffer */
  sx_t *src = sx_new_from_cstr("xab-y");
  sx_view_t pv = sx_view_advance(sx_view(src), 1);
  pv.len = 2; /* "ab" */
  TEST(sx_starts_with_view(h, pv) == 1);
  pv.offset = 0;
  pv.len = 2; /* "xa" */
  TEST(sx_starts_with_view(h, pv) == 0);
  sx_free(src);

  /* empty buffer */
  sx_t *e = sx_new();
  TEST(sx_starts_with_cstr(e, "") == 1);
  TEST(sx_starts_with_cstr(e, "a") == 0);
  sx_free(e);

  /* empty view prefix matches */
  {
    sx_view_t ev = sx_view(h);
    ev.len = 0;
    TEST(sx_starts_with_view(h, ev) == 1);
  }
  sx_free(h);

  /* NULL arguments */
  h = sx_new_from_cstr("abc");
  TEST(sx_starts_with(h, NULL) == 0);
  TEST(sx_starts_with(NULL, h) == 0);
  TEST(sx_starts_with(NULL, NULL) == 0);
  TEST(sx_starts_with_cstr(h, NULL) == 0);
  TEST(sx_starts_with_cstr(NULL, "a") == 0);
  TEST(sx_starts_with_view(NULL, sx_view(h)) == 0);

  /* binary-safe prefix: an embedded NUL is a content byte */
  sx_buf_mut(h)[1] = '\0'; /* h = "a\0c" */
  sx_t *pfx = sx_new_from_cstr("a");
  pfx = sx_write_cstr(pfx, 2, "c");
  sx_buf_mut(pfx)[1] = '\0'; /* prefix "a\0c" */
  TEST(sx_starts_with_view(h, sx_view(pfx)) == 1);
  sx_free(pfx);
  sx_free(h);

  PASS();
}