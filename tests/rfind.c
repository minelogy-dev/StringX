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
 * Behavior boundaries — sx_rfind* family (rightmost search)
 * ===================================================================
 * Mirror of the sx_find* family, scanning RIGHT to LEFT: returns the
 * byte offset of the rightmost match window, or (size_t)-1.  Same
 * byte-exact / binary-safe / strlen-needle / NULL rules as sx_find*.
 *
 * In scope (contract):
 *   · whole-string region: [0, len(h)) — sx_rfind, sx_rfind_view,
 *     sx_rfind_cstr, sx_rfind_char.
 *   · sx_rfind_view_b(h, needle, boundary): region = [0, min(boundary,
 *     len(h))).  A match counts only if its window ENDS inside the
 *     region (p + m <= boundary); boundary 0, an empty needle, or a
 *     region too small for one window are all -1.
 *   · all NULL arguments return (size_t)-1.
 *
 * Out of contract (not exercised):
 *   · h or needle freed during the call; views whose bounds exceed
 *     their backing buffer.
 */

int main(void) {
  sx_t *h = sx_new_from_cstr("ababa");
  sx_t *nd = sx_new_from_cstr("ba");

  /* whole-string rightmost positions */
  TEST(sx_rfind(h, nd) == 3); /* "ba" at 1 and 3; rightmost is 3 */
  TEST(sx_rfind_view(h, sx_view(nd)) == 3);
  TEST(sx_rfind_cstr(h, "ab") == 2);
  TEST(sx_rfind_cstr(h, "a") == 4);
  TEST(sx_rfind_char(h, 'b') == 3);
  TEST(sx_rfind_char(h, 'a') == 4);
  TEST(sx_rfind_char(h, 'z') == (size_t)-1);
  TEST(sx_rfind_cstr(h, "zx") == (size_t)-1);

  /* single occurrence / at the head */
  sx_t *g = sx_new_from_cstr("ab");
  TEST(sx_rfind_cstr(g, "ab") == 0);
  sx_free(g);

  /* needle longer than haystack */
  TEST(sx_rfind_cstr(h, "ababab") == (size_t)-1);

  /* empty needle */
  TEST(sx_rfind_cstr(h, "") == (size_t)-1);
  sx_t *empty = sx_new();
  TEST(sx_rfind(h, empty) == (size_t)-1);
  sx_free(empty);

  /* NULL handling */
  TEST(sx_rfind(h, NULL) == (size_t)-1);
  TEST(sx_rfind(NULL, nd) == (size_t)-1);
  TEST(sx_rfind(NULL, NULL) == (size_t)-1);
  TEST(sx_rfind_cstr(h, NULL) == (size_t)-1);
  TEST(sx_rfind_view(NULL, sx_view(nd)) == (size_t)-1);
  TEST(sx_rfind_char(NULL, 'a') == (size_t)-1);
  sx_free(nd);

  /* sx_rfind_view_b: region [0, min(boundary, H)) */
  nd = sx_new_from_cstr("ba");
  TEST(sx_rfind_view_b(h, sx_view(nd), 3) == 1);  /* [0,3): "aba"        */
  TEST(sx_rfind_view_b(h, sx_view(nd), 2) == (size_t)-1); /* [0,2): "ab"  */
  TEST(sx_rfind_view_b(h, sx_view(nd), 0) == (size_t)-1); /* empty region */
  TEST(sx_rfind_view_b(h, sx_view(nd), 100) == 3);       /* clamps to H  */
  /* a match whose window sticks out past the boundary does not count */
  sx_free(nd); /* drop "ba" before re-pointing */
  nd = sx_new_from_cstr("aba");
  TEST(sx_rfind_view_b(h, sx_view(nd), 3) == 0); /* "aba" at 0, ends 3   */
  TEST(sx_rfind_view_b(h, sx_view(nd), 2) == (size_t)-1); /* ends 3 > 2  */
  sx_free(nd);
  /* boundary >= len(h): behaves as unbounded rightmost search */
  TEST(sx_rfind_view_b(h, sx_view(h), 100) == 0);
  sx_free(h);

  /* embedded NUL: ordinary byte on both sides */
  h = sx_new_from_cstr("xa");
  h = sx_write_cstr(h, 2, "a");
  h = sx_write_cstr(h, 3, "c");
  sx_buf_mut(h)[1] = '\0'; /* "x\0ac", len 4 */
  sx_t *nn = sx_new_from_cstr("x");
  nn = sx_write_cstr(nn, 2, "a");
  nn = sx_write_cstr(nn, 3, "c");
  sx_buf_mut(nn)[1] = '\0'; /* "x\0ac"; the needle is its tail [1,4) */
  sx_view_t nv = sx_view_advance(sx_view(nn), 1); /* "\0ac" */
  TEST(sx_rfind_view(h, nv) == 1);
  sx_free(nn);
  sx_free(h);

  PASS();
}