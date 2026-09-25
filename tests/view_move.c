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

#define TEST_VIEW(v, o, l)                                                     \
  TEST((v).offset == (o) && (v).len == (l))
/*
 * ===================================================================
 * Behavior boundaries — view movement family (sx_view_advance /
 *                         sx_view_retreat / sx_view_shift /
 *                         sx_view_range)
 * ===================================================================
 * All four re-derive the (offset, len) window over a buffer; none of
 * them ever touches the buffer's bytes.
 *
 * In scope (contract): with H = backing buffer content length and an
 * input view (off0, len0):
 *   · sx_view_advance(v, n)  move the START right by n.  While the
 *     shifted window ends strictly inside the buffer
 *     (off0 + len0 + n < H) the END is kept fixed and len shrinks by
 *     exactly n; otherwise the window slides to (off0+n, H) — the
 *     end clamps to the buffer end — and if off0 + n >= H it becomes
 *     the empty view (H, 0).  Advancing past the view's own end is
 *     therefore well-defined and clamps to the buffer end, never
 *     past it.
 *   · sx_view_retreat(v, n)  move the START left by n, the END stays
 *     fixed, len grows by n; saturates at offset 0 (len0 + off0
 *     bytes), never under the buffer start.
 *   · sx_view_shift(v, off)  signed move; len is PRESERVED (unlike
 *     advance/retreat).  Positive: end clamped so the window never
 *     runs past H (offset = H - len).  Negative: start saturates
 *     at 0.
 *   · sx_view_range(v, off, len)  re-anchor ABSOLUTE over the whole
 *     buffer, ignoring the input view: offset = off,
 *     len' = min(len, H - off) — the window is clamped to the buffer
 *     end; precondition off <= H.
 *
 * Out of contract (not exercised):
 *   · off0 + len0 > H input views; off > H for sx_view_range
 *     (H - off underflows); off + len overflowing size_t.
 */

int main(void) {
  sx_t *s = sx_new_from_cstr("abcdef"); /* H = 6 */
  sx_view_t full = sx_view(s);

  /* ---- sx_view_advance ---- */
  TEST_VIEW(sx_view_advance(full, 0), 0, 6);   /* no-op */
  TEST_VIEW(sx_view_advance(full, 2), 2, 4);   /* keep end, shrink len */
  TEST_VIEW(sx_view_advance(full, 6), 6, 0);   /* exhausted buffer     */
  TEST_VIEW(sx_view_advance(full, 100), 6, 0); /* saturate at (H, 0)   */

  /* advancing past a sub-view's own end clamps to the buffer end */
  sx_view_t bcd = sx_view_advance(full, 1);
  bcd.len = 3; /* (1,3) "bcd" */
  TEST_VIEW(sx_view_advance(bcd, 2), 3, 3);   /* end clamps to H        */
  TEST_VIEW(sx_view_advance(bcd, 3), 4, 2);   /* exactly at the end     */
  TEST_VIEW(sx_view_advance(bcd, 100), 6, 0); /* past the end: (H, 0)   */

  /* ---- sx_view_retreat ---- */
  sx_view_t cdef = sx_view_advance(full, 2); /* (2,4) */
  TEST_VIEW(sx_view_retreat(cdef, 0), 2, 4);   /* no-op                 */
  TEST_VIEW(sx_view_retreat(cdef, 2), 0, 6);   /* end stays fixed       */
  TEST_VIEW(sx_view_retreat(cdef, 100), 0, 6); /* saturate at offset 0  */
  sx_view_t mid = sx_view_advance(full, 2);
  mid.len = 2; /* (2,2) "cd" */
  TEST_VIEW(sx_view_retreat(mid, 1), 1, 3); /* extend left, end fixed  */
  TEST_VIEW(sx_view_retreat(full, 5), 0, 6);  /* full view: no-op       */

  /* ---- sx_view_shift (len preserved) ---- */
  sx_view_t v3 = sx_view_advance(full, 1);
  v3.len = 3; /* (1,3) "bcd" */
  TEST_VIEW(sx_view_shift(v3, 0), 1, 3);
  TEST_VIEW(sx_view_shift(v3, 2), 3, 3);   /* len kept                */
  TEST_VIEW(sx_view_shift(v3, 100), 3, 3); /* end clamped to H        */
  sx_view_t v4 = sx_view_advance(full, 3); /* (3,3) "def" */
  TEST_VIEW(sx_view_shift(v4, -2), 1, 3);   /* start left, len kept    */
  TEST_VIEW(sx_view_shift(v4, -100), 0, 3); /* saturate at 0           */
  TEST_VIEW(sx_view_shift(full, 2), 0, 6);  /* clamped: no room to go  */
  TEST_VIEW(sx_view_shift(full, -1), 0, 6); /* already at the start    */

  /* ---- sx_view_range (absolute, input view ignored) ---- */
  TEST_VIEW(sx_view_range(full, 2, 2), 2, 2);   /* inside               */
  TEST_VIEW(sx_view_range(full, 4, 100), 4, 2); /* len clamped to end   */
  TEST_VIEW(sx_view_range(full, 6, 1), 6, 0);   /* len 0 at the end     */
  TEST_VIEW(sx_view_range(full, 0, 0), 0, 0);   /* empty view           */
  /* any input view is irrelevant: anchored anew over the buffer */
  TEST_VIEW(sx_view_range(cdef, 0, 3), 0, 3);

  /* buffer bytes untouched by the whole family */
  TEST(sx_len(s) == 6 && sx_cmp_cstr(s, "abcdef") == 0);
  sx_free(s);

  /* empty buffer is a valid target (H == 0) */
  s = sx_new();
  TEST_VIEW(sx_view_advance(sx_view(s), 0), 0, 0);
  TEST_VIEW(sx_view_advance(sx_view(s), 5), 0, 0);
  TEST_VIEW(sx_view_shift(sx_view(s), -7), 0, 0);
  TEST_VIEW(sx_view_range(sx_view(s), 0, 1), 0, 0);
  sx_free(s);

  PASS();
}