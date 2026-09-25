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
 * Behavior boundaries — sx_find* family (leftmost search)
 * ===================================================================
 * Byte-exact (memcmp semantics) searches; an embedded '\0' in either
 * operand is an ordinary content byte (binary safe).
 *
 * In scope (contract):
 *   · returns the byte OFFSET of the leftmost match window that lies
 *     ENTIRELY inside the searched region, or (size_t)-1 when there
 *     is none.  A window [p, p+m) must satisfy p >= region start and
 *     p + m <= region end.
 *   · whole-string search: region = [0, len(h)) — sx_find,
 *     sx_find_view, sx_find_cstr, sx_find_char (char is the m == 1
 *     case).
 *   · sx_find_view_b(h, needle, boundary): region = [boundary,
 *     len(h)).  boundary >= len(h), or a region too small for one
 *     window, or len(needle) == 0, are "not found" (-1).
 *   · sx_find_cstr / *_cstr needle semantics: strlen (embedded '\0'
 *     ENDS the needle).
 *   · all NULL arguments (haystack or needle) return (size_t)-1.
 *   · the needle is searched by content: a view needle of len n
 *     reads n bytes from v.ref->ptr + v.offset.
 *
 * Out of contract (not exercised):
 *   · h or needle freed/overlapping during the call; views whose
 *     bounds exceed their backing buffer; boundary + needle.len
 *     overflowing.
 */

int main(void) {
  sx_t *h = sx_new_from_cstr("abcabc");

  /* whole-string, all four + variants */
  TEST(sx_find_cstr(h, "bc") == 1);
  TEST(sx_find_cstr(h, "abc") == 0);
  TEST(sx_find_cstr(h, "cab") == 2);
  TEST(sx_find_cstr(h, "z") == (size_t)-1);
  TEST(sx_find_char(h, 'a') == 0);
  TEST(sx_find_char(h, 'c') == 2); /* leftmost, not 5 */
  TEST(sx_find_char(h, 'z') == (size_t)-1);
  sx_t *tail = sx_new_from_cstr("bcda");
  TEST(sx_find_char(tail, 'a') == 3); /* single hit at the tail */
  sx_free(tail);

  sx_t *nd = sx_new_from_cstr("ca");
  TEST(sx_find(h, nd) == 2);
  TEST(sx_find_view(h, sx_view(nd)) == 2);

  /* needle longer than haystack */
  TEST(sx_find_cstr(h, "abcabcx") == (size_t)-1);

  /* empty needle: not found */
  TEST(sx_find_cstr(h, "") == (size_t)-1);
  sx_t *empty = sx_new();
  TEST(sx_find(h, empty) == (size_t)-1);
  sx_free(empty);

  /* NULL handling */
  TEST(sx_find(h, NULL) == (size_t)-1);
  TEST(sx_find(NULL, nd) == (size_t)-1);
  TEST(sx_find(NULL, NULL) == (size_t)-1);
  TEST(sx_find_cstr(h, NULL) == (size_t)-1);
  TEST(sx_find_view(NULL, sx_view(nd)) == (size_t)-1);
  TEST(sx_find_char(NULL, 'a') == (size_t)-1);
  sx_free(nd);
  sx_free(h);

  /* sx_find_view_b: region is [boundary, len(h)) */
  h = sx_new_from_cstr("ababa");
  nd = sx_new_from_cstr("ba");
  TEST(sx_find_view_b(h, sx_view(nd), 0) == 1);  /* whole string        */
  TEST(sx_find_view_b(h, sx_view(nd), 1) == 1);  /* "baba": still [1,5) */
  TEST(sx_find_view_b(h, sx_view(nd), 2) == 3);  /* "aba": 3 not 0      */
  TEST(sx_find_view_b(h, sx_view(nd), 3) == 3);  /* "ba" at region head */
  TEST(sx_find_view_b(h, sx_view(nd), 4) == (size_t)-1); /* "a": too small */
  TEST(sx_find_view_b(h, sx_view(nd), 5) == (size_t)-1); /* empty region  */
  TEST(sx_find_view_b(h, sx_view(nd), 100) == (size_t)-1);
  /* a window straddling the boundary does not count */
  sx_free(nd); /* drop "ba" before re-pointing */
  nd = sx_new_from_cstr("ab");
  TEST(sx_find_view_b(h, sx_view(nd), 1) == 2); /* [1,5): "ab" at 2    */
  TEST(sx_find_view_b(h, sx_view(nd), 2) == 2); /* [2,5): at 2          */
  sx_free(nd);
  /* boundary == len(h): empty region */
  TEST(sx_find_view_b(h, sx_view(h), 5) == (size_t)-1);
  sx_free(h);

  /* embedded NUL is an ordinary byte in both operands */
  h = sx_new_from_cstr("xa");
  h = sx_write_cstr(h, 3, "c"); /* content "xa?c", bytes 0..3 */
  sx_buf_mut(h)[2] = '\0';      /* "x a \0 c" — chunk [1,4) = "a\0c" */
  sx_t *nn = sx_new_from_cstr("a");
  nn = sx_write_cstr(nn, 2, "c");
  sx_buf_mut(nn)[1] = '\0'; /* needle "a\0c", len 3, content 0..2 */
  TEST(sx_len(h) == 4 && sx_len(nn) == 3);
  TEST(sx_find_view(h, sx_view(nn)) == 1); /* the NUL byte matches too */
  /* cstr semantics: strlen stops the needle at the first NUL */
  TEST(sx_find_cstr(h, "a\0c") == 1); /* needle effectively "a"       */
  sx_free(nn);
  sx_free(h);

  PASS();
}