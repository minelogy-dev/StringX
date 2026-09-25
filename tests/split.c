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
#include "sx.h"
#include <build.h>
#include <string.h>

/*
 * ===================================================================
 * Behavior boundaries — sx_split (advanced layer)
 * ===================================================================
 * Splits the buffer by the non-overlapping occurrences of a
 * separator C-string, scanned left to right, into an array of
 * views shaped like a classic "char**":
 *
 *   · sx_split(h, sep): len(sep) >= 1; embedded '\0' in sep is
 *     impossible (strlen semantics); sep is NUL-checked, the
 *     returned array has EXACTLY 1 + (separator occurrences)
 *     entries, empty fields included ("a,,b" -> "a", "", "b";
 *     leading/trailing separators produce empty edge tokens;
 *     an empty h splits to the single token "").
 *   · the array is malloc'ed; END GUARD: the element right after
 *     the last token has .ref == NULL — iterate while
 *     arr[i].ref != NULL; the caller frees the array with free()
 *     (leak sanitizer is the witness).
 *   · tokens are VIEWS INTO h: NOTHING IS COPIED and h's content
 *     is never modified (no realloc either: the buffer pointer is
 *     bitwise unchanged); tokens are invalidated by later edits of h.
 *   · NULL h, NULL sep, EMPTY sep, or allocation failure -> NULL.
 *
 * Out of contract (not exercised):
 *   · editing h while tokens are in use; freeing anything but the
 *     array itself; keeping tokens past h's death.
 */

static int count_tokens(sx_view_t *arr) {
  int n = 0;
  while (arr[n].ref != NULL)
    n++;
  return n;
}

static void check_token(sx_view_t *arr, size_t i, size_t off, size_t len,
                        const char *expect) {
  TEST(arr[i].ref != NULL);
  TEST(arr[i].offset == off && arr[i].len == len);
  TEST(memcmp(sx_buf_view_mut(arr[i]), expect, len) == 0);
}

int main(void) {
  sx_t *h;
  sx_view_t *arr;

  /* basic: three tokens, exact offsets, buffer untouched */
  h = sx_new_from_cstr("ab,cd,ef");
  char *bp = sx_buf_mut(h);
  arr = sx_split(h, ",");
  TEST(arr != NULL);
  TEST(count_tokens(arr) == 3);
  check_token(arr, 0, 0, 2, "ab");
  check_token(arr, 1, 3, 2, "cd");
  check_token(arr, 2, 6, 2, "ef");
  TEST(arr[3].ref == NULL); /* the end guard */
  TEST(sx_buf_view_mut(arr[1]) == bp + 3); /* views alias h, no copy */
  TEST(sx_buf_mut(h) == bp);               /* no realloc happened    */
  TEST(sx_len(h) == 8 && sx_cmp_cstr(h, "ab,cd,ef") == 0); /* no edit */
  free(arr);
  sx_free(h);

  /* no separator: the whole string is the single token */
  h = sx_new_from_cstr("abcdef");
  arr = sx_split(h, ";");
  TEST(count_tokens(arr) == 1);
  check_token(arr, 0, 0, 6, "abcdef");
  TEST(arr[1].ref == NULL);
  free(arr);
  sx_free(h);

  /* empty fields are kept, at every edge and inside */
  h = sx_new_from_cstr("a,,b");
  arr = sx_split(h, ",");
  TEST(count_tokens(arr) == 3);
  check_token(arr, 0, 0, 1, "a");
  check_token(arr, 1, 2, 0, "");
  check_token(arr, 2, 3, 1, "b");
  free(arr);
  sx_free(h);

  h = sx_new_from_cstr(",ab");
  arr = sx_split(h, ",");
  TEST(count_tokens(arr) == 2);
  check_token(arr, 0, 0, 0, "");
  check_token(arr, 1, 1, 2, "ab");
  free(arr);
  sx_free(h);

  h = sx_new_from_cstr("ab,");
  arr = sx_split(h, ",");
  TEST(count_tokens(arr) == 2);
  check_token(arr, 0, 0, 2, "ab");
  check_token(arr, 1, 3, 0, "");
  free(arr);
  sx_free(h);

  /* multi-byte separator, non-overlapping, left to right */
  h = sx_new_from_cstr("a--b---c");
  arr = sx_split(h, "--");
  TEST(count_tokens(arr) == 3);
  check_token(arr, 0, 0, 1, "a");
  check_token(arr, 1, 3, 1, "b"); /* the "---" yields a "-"-lead token */
  check_token(arr, 2, 6, 2, "-c");
  free(arr);
  sx_free(h);

  /* overlapping candidate matches: "aa" cannot re-use a matched byte */
  h = sx_new_from_cstr("aaaa");
  arr = sx_split(h, "aa");
  TEST(count_tokens(arr) == 3);
  check_token(arr, 0, 0, 0, "");
  check_token(arr, 1, 2, 0, "");
  check_token(arr, 2, 4, 0, "");
  free(arr);
  sx_free(h);

  /* the whole string being the separator splits into two empties */
  h = sx_new_from_cstr("ab");
  arr = sx_split(h, "ab");
  TEST(count_tokens(arr) == 2);
  check_token(arr, 0, 0, 0, "");
  check_token(arr, 1, 2, 0, "");
  free(arr);
  sx_free(h);

  /* empty buffer: the single empty token */
  h = sx_new();
  arr = sx_split(h, ",");
  TEST(count_tokens(arr) == 1);
  check_token(arr, 0, 0, 0, "");
  TEST(arr[1].ref == NULL);
  free(arr);
  sx_free(h);

  /* NULL / empty separator and NULL h: refused with NULL */
  h = sx_new_from_cstr("ab,cd");
  TEST(sx_split(h, "") == NULL);
  TEST(sx_split(h, NULL) == NULL);
  TEST(sx_split(NULL, ",") == NULL);
  TEST(sx_split(NULL, NULL) == NULL);
  TEST(sx_len(h) == 5 && sx_cmp_cstr(h, "ab,cd") == 0);
  sx_free(h);

  /* count rule: exactly 1 + separator occurrences; a trailing
     separator leaves an empty tail token */
  h = sx_new_from_cstr("x,;y,;z,;");
  arr = sx_split(h, ",;");
  TEST(count_tokens(arr) == 4); /* separators at 1, 4 and 7 */
  check_token(arr, 0, 0, 1, "x");
  check_token(arr, 1, 3, 1, "y");
  check_token(arr, 2, 6, 1, "z");
  check_token(arr, 3, 9, 0, ""); /* after the last ",;" comes nothing */
  free(arr);
  sx_free(h);

  PASS();
}