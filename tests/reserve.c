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
 * Behavior boundaries — sx_reserve family
 * ===================================================================
 * Capacity growth only: sx_reserve(self, n) guarantees that the
 * buffer can hold n CONTENT bytes plus the terminator — cap >= n + 1 —
 * so that the next n bytes of append/write/insert cannot reallocate.
 *
 * In scope (contract):
 *   · returns `self` on success, NULL on allocation failure (for
 *     which no test is expressible without fault injection).
 *   · a request at or below the current cap is a no-op: cap is
 *     never shrunk, the handle stays the same.
 *   · content is preserved byte-for-byte (realloc), including the
 *     terminator invariant buf[len] == '\0'.
 *   · capacity only grows monotonically; after the call all pointers
 *     from sx_buf_mut must be re-fetched (realloc may move).
 *
 * Out of contract (not exercised):
 *   · self == NULL or freed; n == SIZE_MAX (overflow rejection).
 */

int main(void) {
  sx_t *s = sx_new_from_cstr("abc");
  int cap0 = sx_cap(s);

  /* small / no-op requests */
  TEST(sx_reserve(s, 0) == s);
  TEST(sx_cap(s) == cap0);
  TEST(sx_reserve(s, cap0 - 1) == s); /* already larger: no-op */
  TEST(sx_cap(s) == cap0);
  TEST(sx_len(s) == 3 && sx_cmp_cstr(s, "abc") == 0);
  sx_free(s);

  /* large request: cap >= n+1; content intact */
  s = sx_new_from_cstr("abc");
  TEST(sx_reserve(s, 1000) == s);
  TEST(sx_cap(s) >= 1001);
  TEST(sx_len(s) == 3 && sx_cmp_cstr(s, "abc") == 0);
  sx_free(s);

  /* reserve followed by full-capacity append: no growth, no loss.
     reserve(n) guarantees room for n CONTENT bytes total, so on a
     len-3 buffer the append budget is 997 bytes. */
  s = sx_new_from_cstr("abc");
  TEST(sx_reserve(s, 1000) == s);
  int c1 = sx_cap(s);
  for (int i = 0; i < 997; i++)
    TEST(sx_append_cstr(s, "x") == s);
  TEST(sx_len(s) == 1000);
  TEST(sx_cap(s) == c1); /* never grew: the reservation held */
  /* strict content, not length alone: "abc" + 997 'x' */
  TEST(memcmp(sx_buf_mut(s), "abc", 3) == 0);
  for (int i = 3; i < 1000; i++)
    TEST(sx_buf_mut(s)[i] == 'x');
  TEST(sx_buf_mut(s)[1000] == '\0');
  sx_free(s);

  /* reserve does not shrink when a later larger cap is released by
     truncate + append (capacity is monotonic) */
  s = sx_new_from_cstr("abc");
  TEST(sx_reserve(s, 500) == s);
  int c2 = sx_cap(s);
  TEST(sx_truncate(s, 0) == s);
  TEST(sx_cap(s) == c2); /* truncate never releases capacity */
  sx_free(s);

  PASS();
}