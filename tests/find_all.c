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
 * Behavior boundaries — sx_find_all* family (all occurrences)
 * ===================================================================
 * Leftmost-first collection of every non-overlapping-in-position
 * match: OVERLAPPING matches are all reported (e.g. "aa" inside
 * "aaaa" matches 3 times, at 0, 1 and 2).
 *
 * In scope (contract):
 *   · returns a malloc'ed size_t array, arr[0] = COUNT and
 *     arr[1 .. count] = ascending match offsets, counting only
 *     matches that lie entirely inside the whole-string region
 *     [0, len(h)).
 *   · returns NULL when there is no match — INCLUDING the empty
 *     needle and NULL arguments (allocation failure also yields
 *     NULL, indistinguishable by contract).
 *   · the caller owns the result and releases it with free().
 *   · *_cstr needle semantics = strlen; *_view / owned = byte-exact,
 *     binary safe; *_char = m == 1 case.
 *
 * Out of contract (not exercised):
 *   · modifying h or the needle while the result is in use;
 *     interpreting arr beyond arr[0] + 1 entries.
 */

int main(void) {
  sx_t *h = sx_new_from_cstr("ababab");
  sx_t *nd = sx_new_from_cstr("ab");

  /* contiguous matches */
  size_t *r = sx_find_all(h, nd);
  TEST(r != NULL);
  TEST(r[0] == 3 && r[1] == 0 && r[2] == 2 && r[3] == 4);
  free(r);

  /* overlapping matches are included */
  sx_free(nd); /* drop the "ab" needle before re-pointing */
  nd = sx_new_from_cstr("aa");
  sx_t *aaa = sx_new_from_cstr("aaaa");
  r = sx_find_all(aaa, nd);
  TEST(r != NULL);
  TEST(r[0] == 3 && r[1] == 0 && r[2] == 1 && r[3] == 2);
  free(r);
  sx_free(aaa);
  sx_free(nd);
  sx_free(h);

  /* cstr and char variants */
  h = sx_new_from_cstr("abab");
  r = sx_find_all_cstr(h, "b");
  TEST(r[0] == 2 && r[1] == 1 && r[2] == 3);
  free(r);
  r = sx_find_all_char(h, 'a');
  TEST(r[0] == 2 && r[1] == 0 && r[2] == 2);
  free(r);
  nd = sx_new_from_cstr("ab");
  r = sx_find_all_view(h, sx_view(nd));
  TEST(r[0] == 2 && r[1] == 0 && r[2] == 2);
  free(r);
  sx_free(nd);
  sx_free(h);

  /* miss / empty needle / empty haystack -> NULL */
  h = sx_new_from_cstr("abc");
  nd = sx_new_from_cstr("z");
  TEST(sx_find_all(h, nd) == NULL);
  sx_free(nd);
  nd = sx_new_from_cstr("");
  TEST(sx_find_all(h, nd) == NULL);
  sx_free(nd);
  sx_t *empty = sx_new();
  TEST(sx_find_all(empty, empty) == NULL);
  sx_free(empty);
  sx_free(h);

  /* a single trailing character is reported */
  h = sx_new_from_cstr("a");
  r = sx_find_all_char(h, 'a');
  TEST(r != NULL && r[0] == 1 && r[1] == 0);
  free(r);
  sx_free(h);

  /* NULL arguments -> NULL */
  h = sx_new_from_cstr("abc");
  TEST(sx_find_all(h, NULL) == NULL);
  TEST(sx_find_all(NULL, h) == NULL);
  TEST(sx_find_all_view(NULL, sx_view(h)) == NULL);
  TEST(sx_find_all_cstr(h, NULL) == NULL);
  TEST(sx_find_all_char(NULL, 'a') == NULL);
  sx_free(h);

  PASS();
}