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
 * Behavior boundaries — sx_ends_with* family
 * ===================================================================
 * Suffix predicate: 1 iff the last len(suffix) content bytes of h
 * equal the suffix byte-for-byte.
 *
 * In scope (contract):
 *   · the empty suffix always matches (even on an empty buffer).
 *   · a suffix longer than h never matches.
 *   · byte-exact semantics: an embedded '\0' in h or the suffix is an
 *     ordinary byte; *_cstr uses strlen (its NUL cannot be part of a
 *     suffix).
 *   · *_view reads suffix.len bytes from suffix.ref->ptr +
 *     suffix.offset; the owned variant reads the needle's content.
 *   · NULL h or NULL suffix -> 0.
 *
 * Out of contract (not exercised):
 *   · views whose bounds exceed their backing buffer.
 */

int main(void) {
  sx_t *h = sx_new_from_cstr("abcdef");

  /* cstr */
  TEST(sx_ends_with_cstr(h, "def") == 1);
  TEST(sx_ends_with_cstr(h, "abcdef") == 1); /* suffix == whole h */
  TEST(sx_ends_with_cstr(h, "dex") == 0);
  TEST(sx_ends_with_cstr(h, "0abcdef") == 0); /* longer than h    */
  TEST(sx_ends_with_cstr(h, "") == 1);        /* empty suffix     */

  /* owned */
  sx_t *sfx = sx_new_from_cstr("def");
  TEST(sx_ends_with(h, sfx) == 1);
  sx_free(sfx);
  sfx = sx_new_from_cstr("dEf");
  TEST(sx_ends_with(h, sfx) == 0);
  sx_free(sfx);

  /* view: suffix taken from the middle of another buffer */
  sx_t *src = sx_new_from_cstr("ef-gh");
  sx_view_t sv = sx_view_advance(sx_view(src), 0);
  sv.len = 2; /* "ef": a genuine suffix */
  TEST(sx_ends_with_view(h, sv) == 1);
  sv = sx_view_advance(sx_view(src), 0);
  sv.len = 3; /* "ef-": not a suffix */
  TEST(sx_ends_with_view(h, sv) == 0);
  sv = sx_view_advance(sx_view(src), 1);
  sv.len = 3; /* "f-g": not a suffix either */
  TEST(sx_ends_with_view(h, sv) == 0);
  sx_free(src);

  /* suffix ending at the tail of a longer h ('f' at position 5) */
  TEST(sx_ends_with_cstr(h, "f") == 1);

  /* empty buffer */
  sx_t *e = sx_new();
  TEST(sx_ends_with_cstr(e, "") == 1);
  TEST(sx_ends_with_cstr(e, "a") == 0);
  sx_free(e);

  /* empty view suffix matches */
  {
    sx_view_t ev = sx_view(h);
    ev.len = 0;
    TEST(sx_ends_with_view(h, ev) == 1);
  }
  sx_free(h);

  /* NULL arguments */
  h = sx_new_from_cstr("abc");
  TEST(sx_ends_with(h, NULL) == 0);
  TEST(sx_ends_with(NULL, h) == 0);
  TEST(sx_ends_with(NULL, NULL) == 0);
  TEST(sx_ends_with_cstr(h, NULL) == 0);
  TEST(sx_ends_with_cstr(NULL, "a") == 0);
  TEST(sx_ends_with_view(NULL, sx_view(h)) == 0);

  /* binary-safe suffix: an embedded NUL is a content byte */
  sx_buf_mut(h)[1] = '\0'; /* h = "a\0c" */
  sx_t *sf = sx_new_from_cstr("a");
  sf = sx_write_cstr(sf, 2, "c");
  sx_buf_mut(sf)[1] = '\0'; /* suffix "a\0c" */
  TEST(sx_ends_with_view(h, sx_view(sf)) == 1);
  sx_free(sf);
  sx_free(h);

  PASS();
}