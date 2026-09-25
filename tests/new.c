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
 * Behavior boundaries — sx_new* family (creation & destruction)
 * ===================================================================
 * Every buffer is born empty (len 0) with ptr[0] == '\0', and stays
 * single-NUL-terminated: ptr[len] == '\0' at all times.
 *
 * In scope (contract):
 *   · sx_new()              empty buffer; cap >= 1 (usable for any
 *                           later append/truncate path).
 *   · sx_new_from_cstr(s)   copy strlen(s) bytes (embedded '\0'
 *                           truncates; binary content needs views);
 *                           NULL s returns NULL.
 *   · sx_new_from_view(v)   copy v.len bytes from v.ref->ptr+v.offset,
 *                           byte-exact (binary safe, independent copy).
 *   · sx_new_with_cap(n)    empty buffer; cap == n so it can hold at
 *                           most n-1 content bytes (len <= cap-1).
 *   · sx_dup(src)           deep, independent copy of src (content and
 *                           capacity are not shared); NULL src -> NULL.
 *   · sx_free(h)            release the buffer (checked by the leak
 *                           sanitizer: every buffer must be freed or
 *                           its ownership transferred).
 * Returned sx_t* values are NULL only on NULL input / allocation
 * failure, and then nothing is allocated.
 *
 * Out of contract (not exercised):
 *   · h == NULL or double-free; keeping sx_buf_mut pointers across
 *     calls that may reallocate.
 */

int main(void) {
  /* sx_new: empty and NUL-terminated */
  sx_t *s = sx_new();
  TEST(sx_len(s) == 0 && sx_cmp_cstr(s, "") == 0);
  TEST(sx_buf_mut(s)[0] == '\0');
  TEST(sx_cap(s) >= 1);
  sx_free(s);

  /* sx_new_from_cstr */
  s = sx_new_from_cstr("hello");
  TEST(sx_len(s) == 5);
  TEST(sx_cmp_cstr(s, "hello") == 0);
  TEST(sx_buf_mut(s)[sx_len(s)] == '\0');
  sx_free(s);

  s = sx_new_from_cstr("");
  TEST(sx_len(s) == 0 && sx_cmp_cstr(s, "") == 0);
  TEST(sx_buf_mut(s)[0] == '\0');
  sx_free(s);

  TEST(sx_new_from_cstr(NULL) == NULL);

  /* sx_new_from_view: byte-exact copy, embedded NUL included */
  sx_t *src = sx_new_from_cstr("abc");
  sx_view_t v = sx_view(src);
  TEST(v.offset == 0 && v.len == 3);
  sx_t *cp = sx_new_from_view(v);
  TEST(sx_len(cp) == 3);
  TEST(memcmp(sx_buf_mut(cp), "abc", 3) == 0);
  sx_buf_mut(src)[0] = 'X'; /* no aliasing: copy stays intact */
  TEST(sx_buf_mut(cp)[0] == 'a');
  sx_free(cp);

  /* NUL-content copy */
  cp = sx_new_from_cstr("a");
  cp = sx_write_cstr(cp, 2, "c");
  sx_buf_mut(cp)[1] = '\0'; /* raw: "a\0c" */
  sx_t *nul = sx_new_from_view(sx_view(cp));
  TEST(sx_len(nul) == 3);
  TEST(sx_buf_mut(nul)[0] == 'a' && sx_buf_mut(nul)[1] == '\0' &&
       sx_buf_mut(nul)[2] == 'c');
  sx_free(nul);
  sx_free(cp);
  sx_free(src);

  /* sx_new_with_cap: n bytes total, so cap == n, max n-1 content */
  s = sx_new_with_cap(0);
  TEST(sx_len(s) == 0 && sx_cap(s) == 1);
  TEST(sx_cmp_cstr(s, "") == 0);
  TEST(sx_buf_mut(s)[0] == '\0');
  sx_free(s);

  s = sx_new_with_cap(64);
  TEST(sx_cap(s) == 64);
  s = sx_write_cstr(s, 0, "abcdef");
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0); /* strict */
  TEST(sx_cap(s) == 64);
  sx_free(s);

  /* sx_dup: deep copy in both directions */
  s = sx_new_from_cstr("dup me");
  sx_t *d = sx_dup(s);
  TEST(sx_len(d) == 6);
  TEST(sx_cmp_cstr(d, "dup me") == 0);
  sx_buf_mut(s)[0] = 'X';
  TEST(sx_buf_mut(d)[0] == 'd'); /* src write not visible in dup */
  sx_buf_mut(d)[1] = 'Y';
  TEST(sx_buf_mut(s)[1] == 'u'); /* dup write not visible in src */
  sx_free(d);
  sx_free(s);
  TEST(sx_dup(NULL) == NULL);

  /* binary-safe dup */
  src = sx_new_from_cstr("x");
  src = sx_write_cstr(src, 2, "z");
  sx_buf_mut(src)[1] = '\0'; /* "x\0z" */
  d = sx_dup(src);
  TEST(sx_len(d) == 3);
  TEST(memcmp(sx_buf_mut(d), sx_buf_mut(src), 3) == 0);
  sx_free(d);
  sx_free(src);

  PASS();
}