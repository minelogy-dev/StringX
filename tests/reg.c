// Include include
// Include src
// Include /usr/lib/forge/include
// Source src/sx_raw.c
// Source src/sx_core.c
// Source src/sx_adv.c
// Link pcre2-8
// Option -std=c23
// Option -O1
// Option -g
// Option -fsanitize=address,undefined
// Option -fno-omit-frame-pointer
// Library /usr/local/lib
/* SPDX-License-Identifier: MIT */
#include "sx.h"
#include <build.h>
#include <string.h>

/*
 * ===================================================================
 * Behavior boundaries — sx_reg_* (advanced layer, PCRE2 wrapper)
 * ===================================================================
 * Lifecycle: sx_reg_compile() once, then any number of calls across
 * the match / replace / split families, then sx_reg_free().
 *
 *   · compile: NULL / syntactically invalid pattern -> NULL.
 *   · is_match: 0/1, a zero-length match counts as a match.
 *   · match: the LEFTMOST match as a view INTO h; no match (or NULL
 *     args) -> v.ref == NULL; a zero-length match still has
 *     v.ref != NULL.
 *   · match_all: full non-overlapping left-to-right pass — after a
 *     hit the scan resumes at the match end, or one byte further
 *     right for a zero-length match; malloc'ed array, END GUARD
 *     (last .ref == NULL); no match (or NULL args) -> NULL.
 *   · replace / replace_first: new sx_t (caller frees), h untouched;
 *     $0/$n backreferences; unset OR non-existent groups -> empty;
 *     no match -> unchanged copy; NULL args -> NULL.
 *   · split: exactly the sx_split shape — tokens are the gaps
 *     between matches, empty tokens kept everywhere, count ==
 *     matches + 1, end guard, views alias h; NULL args -> NULL.
 *
 * Subjects are bytes: matching is binary-safe (embedded NUL is
 * ordinary data), 8-bit non-UTF — \d \w match ASCII only.
 *
 * Out of contract (not exercised): freeing anything but the
 * returned arrays/strings; keeping views past h's death; editing h
 * while views are in use; reusing a freed regex.
 */

static int count_views(sx_view_t *arr) {
  int n = 0;
  while (arr[n].ref != NULL)
    n++;
  return n;
}

static void check_view(sx_view_t v, size_t off, size_t len,
                       const char *expect) {
  TEST(v.ref != NULL);
  TEST(v.offset == off && v.len == len);
  TEST(memcmp(sx_buf_view_mut(v), expect, len) == 0);
}

/* Build an sx_t from raw bytes (embedded NULs allowed). */
static sx_t *bin(const char *bytes, size_t n) {
  sx_t *h = sx_new_with_cap(n + 1);
  if (!h)
    return NULL;
  memcpy(sx_buf_mut(h), bytes, n);
  sx_truncate(h, n);
  return h;
}

int main(void) {
  sx_t *h;
  sx_reg_t *r;
  sx_view_t *arr, v;
  sx_t *out;
  char *bp;

  /* ---------------- compile / free ---------------- */
  TEST(sx_reg_compile(NULL) == NULL);
  TEST(sx_reg_compile("(") == NULL);  /* unbalanced paren */
  TEST(sx_reg_compile("[") == NULL);  /* unterminated class */
  TEST(sx_reg_compile("\\") == NULL); /* dangling escape */
  sx_reg_free(NULL);                  /* must be a no-op */

  r = sx_reg_compile("abc");
  TEST(r != NULL);
  sx_reg_free(r); /* early release of a valid regex */

  r = sx_reg_compile("abc"); /* reused by every family below */

  /* ---------------- Block 1: is_match ---------------- */
  h = sx_new_from_cstr("xxabcxx");
  TEST(sx_reg_is_match(r, h) == 1);
  sx_free(h);
  h = sx_new_from_cstr("ab");
  TEST(sx_reg_is_match(r, h) == 0);
  sx_free(h);
  h = sx_new_from_cstr("xyz");
  TEST(sx_reg_is_match(r, h) == 0);
  sx_free(h);
  h = sx_new();
  TEST(sx_reg_is_match(r, h) == 0);
  sx_free(h);
  TEST(sx_reg_is_match(NULL, h) == 0);
  TEST(sx_reg_is_match(r, NULL) == 0);

  /* a zero-length match counts as a match */
  sx_reg_free(r);
  r = sx_reg_compile("a*");
  h = sx_new();
  TEST(sx_reg_is_match(r, h) == 1);
  sx_free(h);
  sx_reg_free(r);
  r = sx_reg_compile("abc");

  /* ---------------- Block 1: match (first) ---------------- */
  /* leftmost-first semantics: "b" wins over "bc" at offset 1 */
  h = sx_new_from_cstr("abc");
  sx_reg_free(r);
  r = sx_reg_compile("b|bc");
  v = sx_reg_match(r, h);
  check_view(v, 1, 1, "b");
  TEST(sx_buf_view_mut(v) == sx_buf_mut(h) + 1); /* aliases h, no copy */
  sx_free(h);

  h = sx_new_from_cstr("abc");
  sx_reg_free(r);
  r = sx_reg_compile("bc");
  v = sx_reg_match(r, h);
  check_view(v, 1, 2, "bc");
  TEST(sx_buf_mut(h)[0] == 'a' && sx_buf_mut(h)[3] == '\0'); /* no edit */

  /* anchors: ^ binds at offset 0, $ at len(h) */
  sx_reg_free(r);
  r = sx_reg_compile("^ab");
  TEST(sx_reg_match(r, h).ref != NULL && sx_reg_match(r, h).offset == 0);
  sx_free(h);
  h = sx_new_from_cstr("xabcd");
  TEST(sx_reg_match(r, h).ref == NULL); /* ^ misses */
  sx_free(h);
  sx_reg_free(r);
  r = sx_reg_compile("cd$");
  h = sx_new_from_cstr("abcd");
  v = sx_reg_match(r, h);
  check_view(v, 2, 2, "cd");
  sx_free(h);
  h = sx_new_from_cstr("abcde");
  TEST(sx_reg_match(r, h).ref == NULL); /* $ misses */
  sx_free(h);
  h = sx_new();
  TEST(sx_reg_match(r, h).ref == NULL); /* "cd$" cannot match an empty subject */
  sx_free(h);
  sx_reg_free(r);
  r = sx_reg_compile("$");
  h = sx_new();
  v = sx_reg_match(r, h);
  TEST(v.ref != NULL && v.offset == 0 && v.len == 0); /* "$" on "" matches empty */
  sx_free(h);

  /* binary-safe subjects: '.' matches an embedded NUL, not '\n' */
  sx_reg_free(r);
  r = sx_reg_compile(".");
  h = bin("\0", 1);
  v = sx_reg_match(r, h);
  check_view(v, 0, 1, "\0");
  sx_free(h);
  h = sx_new_from_cstr("\n");
  TEST(sx_reg_match(r, h).ref == NULL);
  sx_free(h);
  sx_reg_free(r);
  r = sx_reg_compile("A.B");
  h = bin("A\0B", 3);
  v = sx_reg_match(r, h);
  check_view(v, 0, 3, "A\0B");
  sx_free(h);

  /* zero-length match is a real match */
  sx_reg_free(r);
  r = sx_reg_compile("a*");
  h = sx_new_from_cstr("bbb");
  v = sx_reg_match(r, h);
  TEST(v.ref != NULL && v.offset == 0 && v.len == 0);
  sx_free(h);

  /* no match / NULL args -> the null view */
  sx_reg_free(r);
  r = sx_reg_compile("z");
  h = sx_new_from_cstr("abc");
  TEST(sx_reg_match(r, h).ref == NULL);
  TEST(sx_reg_match(NULL, h).ref == NULL);
  TEST(sx_reg_match(r, NULL).ref == NULL);
  sx_free(h);

  /* 8-bit non-UTF: \d is ASCII only, non-ASCII bytes never match it */
  sx_reg_free(r);
  r = sx_reg_compile("\\d");
  h = sx_new_from_cstr("5");
  v = sx_reg_match(r, h);
  check_view(v, 0, 1, "5");
  sx_free(h);
  h = bin("\xC2\xA5", 2); /* the bytes of U+00A5 */
  TEST(sx_reg_match(r, h).ref == NULL);
  sx_free(h);
  sx_reg_free(r);
  r = sx_reg_compile("abc");

  /* ---------------- Block 1: match_all ---------------- */
  /* non-overlapping: "aa" wins leftmost, "aaa" never gets a chance */
  h = sx_new_from_cstr("aaaa");
  sx_reg_free(r);
  r = sx_reg_compile("aa|aaa");
  arr = sx_reg_match_all(r, h);
  TEST(arr != NULL);
  TEST(count_views(arr) == 2);
  check_view(arr[0], 0, 2, "aa");
  check_view(arr[1], 2, 2, "aa");
  TEST(arr[2].ref == NULL); /* the end guard */
  free(arr);
  sx_free(h);

  h = sx_new_from_cstr("abcb");
  sx_reg_free(r);
  r = sx_reg_compile("b");
  arr = sx_reg_match_all(r, h);
  TEST(arr != NULL && count_views(arr) == 2);
  check_view(arr[0], 1, 1, "b");
  check_view(arr[1], 3, 1, "b");
  TEST(arr[2].ref == NULL);
  free(arr);
  sx_free(h);

  /* zero-length matches: every position is reported, then the scan
     resumes one byte later ("bbb" -> offsets 0, 1, 2 and 3) */
  sx_reg_free(r);
  r = sx_reg_compile("a*");
  h = sx_new_from_cstr("bbb");
  arr = sx_reg_match_all(r, h);
  TEST(arr != NULL && count_views(arr) == 4);
  for (size_t i = 0; i < 4; i++)
    TEST(arr[i].ref != NULL && arr[i].offset == i && arr[i].len == 0);
  TEST(arr[4].ref == NULL);
  free(arr);
  sx_free(h);

  /* a right anchor matches once, at the very end */
  sx_reg_free(r);
  r = sx_reg_compile("$");
  h = sx_new_from_cstr("abc");
  arr = sx_reg_match_all(r, h);
  TEST(arr != NULL && count_views(arr) == 1);
  check_view(arr[0], 3, 0, "");
  free(arr);
  sx_free(h);

  /* no match -> NULL; NULL args -> NULL */
  sx_reg_free(r);
  r = sx_reg_compile("z");
  h = sx_new_from_cstr("abc");
  TEST(sx_reg_match_all(r, h) == NULL);
  TEST(sx_reg_match_all(NULL, h) == NULL);
  TEST(sx_reg_match_all(r, NULL) == NULL);
  sx_free(h);

  /* ---------------- Block 2: replace / replace_first ---------------- */
  h = sx_new_from_cstr("foo bar");
  sx_reg_free(r);
  r = sx_reg_compile("o");
  out = sx_reg_replace(r, h, "0");
  TEST(out != NULL);
  TEST(sx_len(out) == 7 && sx_cmp_cstr(out, "f00 bar") == 0);
  sx_free(out);
  out = sx_reg_replace_first(r, h, "0");
  TEST(out != NULL);
  TEST(sx_cmp_cstr(out, "f0o bar") == 0);
  sx_free(out);
  TEST(sx_len(h) == 7 && sx_cmp_cstr(h, "foo bar") == 0); /* untouched */
  sx_free(h);

  /* $n backreferences, word swaps */
  h = sx_new_from_cstr("hello world");
  sx_reg_free(r);
  r = sx_reg_compile("(\\w+) (\\w+)");
  out = sx_reg_replace(r, h, "$2 $1");
  TEST(out != NULL);
  TEST(sx_cmp_cstr(out, "world hello") == 0);
  sx_free(out);
  sx_free(h);

  /* non-existent group ($3) and unset group ($1) both expand to "" */
  h = sx_new_from_cstr("ab");
  sx_reg_free(r);
  r = sx_reg_compile("(a)(b)");
  out = sx_reg_replace(r, h, "$1$3");
  TEST(out != NULL && sx_cmp_cstr(out, "a") == 0);
  sx_free(out);
  sx_free(h);
  h = sx_new_from_cstr("a");
  sx_reg_free(r);
  r = sx_reg_compile("a(b)?");
  out = sx_reg_replace(r, h, "[$1]");
  TEST(out != NULL && sx_cmp_cstr(out, "[]") == 0);
  sx_free(out);
  sx_free(h);

  /* whole-subject match with an empty template -> empty result */
  h = sx_new_from_cstr("abc");
  sx_reg_free(r);
  r = sx_reg_compile("abc");
  out = sx_reg_replace(r, h, "");
  TEST(out != NULL && sx_len(out) == 0);
  sx_free(out);
  sx_free(h);

  /* empty matches are substituted too ("bbb" -> "-b-b-b-") */
  h = sx_new_from_cstr("bbb");
  sx_reg_free(r);
  r = sx_reg_compile("a*");
  out = sx_reg_replace(r, h, "-");
  TEST(out != NULL);
  TEST(sx_cmp_cstr(out, "-b-b-b-") == 0);
  sx_free(out);
  sx_free(h);

  /* binary-safe both ways: embedded NUL survives the round trip */
  h = bin("a\0b", 3);
  sx_reg_free(r);
  r = sx_reg_compile("b");
  out = sx_reg_replace(r, h, "B");
  TEST(out != NULL && sx_len(out) == 3);
  TEST(memcmp(sx_buf_mut(out), "a\0B", 3) == 0);
  sx_free(out);
  sx_reg_free(r);
  r = sx_reg_compile(".");
  out = sx_reg_replace(r, h, "X");
  TEST(out != NULL && sx_len(out) == 3);
  TEST(memcmp(sx_buf_mut(out), "XXX", 3) == 0);
  sx_free(out);
  sx_free(h);

  /* no match -> an unchanged copy, never NULL for valid inputs */
  h = sx_new_from_cstr("abc");
  sx_reg_free(r);
  r = sx_reg_compile("z");
  out = sx_reg_replace(r, h, "Q");
  TEST(out != NULL && sx_cmp_cstr(out, "abc") == 0);
  sx_free(out);
  sx_free(h);

  /* NULL args -> NULL */
  h = sx_new_from_cstr("abc");
  TEST(sx_reg_replace(r, h, NULL) == NULL);
  TEST(sx_reg_replace(NULL, h, "x") == NULL);
  TEST(sx_reg_replace(r, NULL, "x") == NULL);
  TEST(sx_reg_replace_first(r, h, NULL) == NULL);
  sx_free(h);

  /* replace vs replace_first with $0 */
  h = sx_new_from_cstr("xax");
  sx_reg_free(r);
  r = sx_reg_compile("x");
  out = sx_reg_replace(r, h, "<$0>");
  TEST(out != NULL && sx_cmp_cstr(out, "<x>a<x>") == 0);
  sx_free(out);
  out = sx_reg_replace_first(r, h, "<$0>");
  TEST(out != NULL && sx_cmp_cstr(out, "<x>ax") == 0);
  sx_free(out);
  sx_free(h);

  /* ---------------- Block 3: split ---------------- */
  h = sx_new_from_cstr("a1b22c");
  sx_reg_free(r);
  r = sx_reg_compile("[0-9]+");
  arr = sx_reg_split(r, h);
  TEST(arr != NULL);
  TEST(count_views(arr) == 3); /* one token more than matches */
  check_view(arr[0], 0, 1, "a");
  check_view(arr[1], 2, 1, "b"); /* "22" is ONE match */
  check_view(arr[2], 5, 1, "c");
  TEST(arr[3].ref == NULL);
  free(arr);
  sx_free(h);

  /* a plain-separator regex agrees with sx_split field by field */
  h = sx_new_from_cstr("ab,cd,ef");
  sx_reg_free(r);
  r = sx_reg_compile(",");
  arr = sx_reg_split(r, h);
  sx_view_t *ref = sx_split(h, ",");
  TEST(arr != NULL && ref != NULL);
  TEST(count_views(arr) == count_views(ref));
  for (int i = 0; arr[i].ref; i++) {
    TEST(arr[i].offset == ref[i].offset && arr[i].len == ref[i].len);
    TEST(memcmp(sx_buf_view_mut(arr[i]), sx_buf_view_mut(ref[i]),
                ref[i].len) == 0);
  }
  free(arr);
  free(ref);
  sx_free(h);

  /* leading and trailing matches yield empty edge tokens */
  h = sx_new_from_cstr(",ab");
  sx_reg_free(r);
  r = sx_reg_compile(",");
  arr = sx_reg_split(r, h);
  TEST(count_views(arr) == 2);
  check_view(arr[0], 0, 0, "");
  check_view(arr[1], 1, 2, "ab");
  free(arr);
  sx_free(h);

  h = sx_new_from_cstr("ab,");
  arr = sx_reg_split(r, h);
  TEST(count_views(arr) == 2);
  check_view(arr[0], 0, 2, "ab");
  check_view(arr[1], 3, 0, "");
  free(arr);
  sx_free(h);

  /* the whole string being one match splits into two empties */
  h = sx_new_from_cstr("ab");
  sx_reg_free(r);
  r = sx_reg_compile("ab");
  arr = sx_reg_split(r, h);
  TEST(count_views(arr) == 2);
  check_view(arr[0], 0, 0, "");
  check_view(arr[1], 2, 0, "");
  free(arr);
  sx_free(h);

  /* empty subject, no match: the single empty token */
  h = sx_new();
  arr = sx_reg_split(r, h);
  TEST(count_views(arr) == 1);
  check_view(arr[0], 0, 0, "");
  TEST(arr[1].ref == NULL);
  free(arr);
  sx_free(h);

  /* zero-length matches split at every position: "", "b", "b", "b", "" */
  h = sx_new_from_cstr("bbb");
  sx_reg_free(r);
  r = sx_reg_compile("a*");
  arr = sx_reg_split(r, h);
  TEST(count_views(arr) == 5);
  check_view(arr[0], 0, 0, "");
  check_view(arr[1], 0, 1, "b");
  check_view(arr[2], 1, 1, "b");
  check_view(arr[3], 2, 1, "b");
  check_view(arr[4], 3, 0, "");
  TEST(arr[5].ref == NULL);
  free(arr);
  sx_free(h);

  /* token count == match_all count + 1, and the tokens are the gaps
     around each match ("abcb" / "bc": one match [1,3), gaps "a" and "b") */
  h = sx_new_from_cstr("abcb");
  sx_reg_free(r);
  r = sx_reg_compile("bc");
  arr = sx_reg_split(r, h);
  sx_view_t *hits = sx_reg_match_all(r, h);
  TEST(arr != NULL && hits != NULL);
  TEST(count_views(arr) == count_views(hits) + 1);
  TEST(hits[0].offset == 1);
  TEST(arr[0].offset == 0 && arr[1].offset == 3);
  check_view(arr[0], 0, 1, "a");
  check_view(arr[1], 3, 1, "b");
  free(arr);
  free(hits);
  sx_free(h);

  /* NULL args -> NULL */
  h = sx_new_from_cstr("abc");
  TEST(sx_reg_split(NULL, h) == NULL);
  TEST(sx_reg_split(r, NULL) == NULL);
  sx_free(h);

  /* views alias h's buffer (no copy), for every family */
  h = sx_new_from_cstr("abc");
  sx_reg_free(r);
  r = sx_reg_compile("b");
  bp = sx_buf_mut(h);
  v = sx_reg_match(r, h);
  TEST(sx_buf_view_mut(v) == bp + 1);
  arr = sx_reg_match_all(r, h);
  TEST(sx_buf_view_mut(arr[0]) == bp + 1);
  free(arr);
  arr = sx_reg_split(r, h);
  TEST(sx_buf_view_mut(arr[1]) == bp + 2);
  free(arr);

  sx_reg_free(r);
  sx_free(h);

  PASS();
}