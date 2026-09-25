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
 * Behavior boundaries — sx_substr
 * ===================================================================
 * Copy-out a byte range [idx, idx+n) of h into a NEW, caller-owned
 * buffer: byte-exact (binary safe, embedded '\0' included) and fully
 * independent of h.
 *
 * In scope (contract):
 *   · precondition idx + n <= len(h); n == 0 yields an empty buffer.
 *   · the result is a fresh buffer: buf[len] == '\0', and mutations
 *     on either side never affect the other.
 *   · unlike every other sx_t* producible here, the result is NOT
 *     owned by the library: it must be released with sx_free() (the
 *     leak sanitizer is the witness).
 *   · NULL is returned only on allocation failure.
 *
 * Out of contract (not exercised):
 *   · h == NULL or freed; idx + n > len(h) (out-of-bounds read);
 *     holding the result past a mutation of h (it is an independent
 *     copy by contract, so no aliasing exists either way).
 */

int main(void) {
  sx_t *s = sx_new_from_cstr("abcdef");

  /* middle range */
  sx_t *sub = sx_substr(s, 2, 3);
  TEST(sub != NULL);
  TEST(sx_len(sub) == 3 && sx_cmp_cstr(sub, "cde") == 0);
  TEST(sx_buf_mut(sub)[3] == '\0');
  sx_free(sub);

  /* full copy and empty copy */
  sub = sx_substr(s, 0, 6);
  TEST(sx_len(sub) == 6 && sx_cmp_cstr(sub, "abcdef") == 0);
  sx_free(sub);

  sub = sx_substr(s, 3, 0);
  TEST(sx_len(sub) == 0 && sx_buf_mut(sub)[0] == '\0');
  sx_free(sub);

  /* independence in both directions */
  sub = sx_substr(s, 0, 3); /* "abc" */
  sx_buf_mut(s)[0] = 'X';
  TEST(sx_buf_mut(sub)[0] == 'a'); /* src write invisible in result */
  sx_buf_mut(sub)[1] = 'Y';
  TEST(sx_buf_mut(s)[1] == 'b'); /* result write invisible in src */
  sx_free(sub);
  sx_free(s);

  /* binary-safe copy: embedded NUL round-trips */
  s = sx_new_from_cstr("x");
  s = sx_write_cstr(s, 2, "z");
  s = sx_write_cstr(s, 4, "z");
  sx_buf_mut(s)[1] = '\0';
  sx_buf_mut(s)[3] = '\0'; /* "x\0z\0z" (len 5) */
  sub = sx_substr(s, 1, 3);
  TEST(sx_len(sub) == 3);
  TEST(sx_buf_mut(sub)[0] == '\0' && sx_buf_mut(sub)[1] == 'z' &&
       sx_buf_mut(sub)[2] == '\0');
  TEST(sx_buf_mut(sub)[3] == '\0');
  sx_free(sub);
  sx_free(s);

  PASS();
}