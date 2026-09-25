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
#define TEST_CMP(a, b, val)                                                    \
  do {                                                                         \
    sx_t *s1 = sx_new_from_cstr(a);                                            \
    sx_t *s2 = sx_new_from_cstr(b);                                            \
    TEST(sx_cmp(s1, s2) val);                                               \
    sx_free(s1);                                                               \
    sx_free(s2);                                                               \
  } while (0)

/*
 * ===================================================================
 * Behavior boundaries — sx_cmp* family
 * ===================================================================
 * Lexicographic comparison over the CONTENT bytes only (never the
 * terminator): byte-wise memcmp order, so ASCII and UTF-8 strings
 * compare by code unit ("10" < "2", "Z" < "中", "é" > "é").
 *
 * In scope (contract):
 *   · sx_cmp(h, other) / sx_cmp_view(h, v) / sx_cmp_cstr(h, s):
 *     returns < 0 if h sorts before the other, 0 if equal, > 0 if h
 *     sorts after.
 *   · a shorter string is the smaller prefix: "abc" < "abcd".
 *   · embedded '\0' is an ordinary content byte (binary safe);
 *     *_cstr compares against strlen(s) bytes.
 *   · NULL arguments are outside the ordering: any NULL yields 0
 *     (treated as equal) — never relied upon as meaningful.
 *
 * Out of contract (not exercised):
 *   · comparing a buffer while it is being modified; views whose
 *     bounds exceed their backing buffer.
 */

int main(void) {
  TEST_CMP("abc", "abc", ==0);
  TEST_CMP("abc", "abcd", <0);
  TEST_CMP("abcd", "abc", >0);
  TEST_CMP("10", "10", ==0);
  TEST_CMP("10", "2", <0);
  TEST_CMP("2", "10", >0);
  TEST_CMP("a", "á", <0);
  TEST_CMP("á", "b", >0);
  TEST_CMP("中", "文", <0);
  TEST_CMP("中", "中国", <0);
  TEST_CMP("中国", "中文", <0);
  TEST_CMP("Z", "中", <0);
  TEST_CMP("😀", "😁", <0);
  TEST_CMP("é", "e\u0301", >0);
  PASS();
}