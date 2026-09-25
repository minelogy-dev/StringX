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
 * Behavior boundaries — sx_view_trim* family
 * ===================================================================
 * Trim the ASCII whitespace set {space, '\t', '\v', '\f', '\r',
 * '\n'} from the edges of a view.  A view that whitespace exactly
 * trims to the empty view (offset = H, len = 0) where H is the
 * buffer's content length.
 *
 * In scope (contract):
 *   · sx_view_trim_front(v)  drop leading whitespace: offset moves
 *                            right and len shrinks by the same
 *                            amount, so the window end stays fixed
 *                            and (offset + len) <= H always.
 *   · sx_view_trim_back(v)   drop trailing whitespace: len shrinks
 *                            leftward from the window's LAST content
 *                            byte (golden rule: the view covers
 *                            [offset, offset+len) — bytes outside it
 *                            are never inspected, and the last
 *                            inspected index is offset+len-1).
 *   · sx_view_trim(v)        trim_front then trim_back, equivalent
 *                            to trimming both edges of one window.
 *   · no whitespace: all three return the view unchanged.
 *   · no buffer bytes are ever modified.
 *
 * Out of contract (not exercised):
 *   · views with offset+len > H already at entry (the family keeps
 *     (offset, len) canonical, it does not repair non-canonical
 *     inputs); v.ref == NULL.
 */

int main(void) {
  /* "  abc  " : H = 7 */
  sx_t *s = sx_new_from_cstr("  abc  ");
  sx_view_t full = sx_view(s); /* (0,7) */

  /* combined trim strips both edges */
  TEST_VIEW(sx_view_trim(full), 2, 3);
  TEST_VIEW(sx_view_trim_front(full), 2, 5);
  TEST_VIEW(sx_view_trim_back(full), 0, 5);

  /* non-whitespace window: no-ops */
  sx_view_t mid = sx_view_advance(full, 2);
  mid.len = 3; /* "abc" */
  TEST_VIEW(sx_view_trim(mid), 2, 3);
  TEST_VIEW(sx_view_trim_front(mid), 2, 3);
  TEST_VIEW(sx_view_trim_back(mid), 2, 3);

  /* sub-view with whitespace on the edge: careful with the end
     clamp — the last checked byte is offset+len-1, in-bounds */
  sx_view_t head = sx_view_advance(full, 1); /* " abc  " (1,6) */
  TEST_VIEW(sx_view_trim_front(head), 2, 5); /* one leading space */
  TEST_VIEW(sx_view_trim(head), 2, 3);

  /* all-whitespace view: trims to the empty view (H, 0) */
  sx_view_t alws = sx_view_advance(full, 0);
  alws.len = 2; /* "  " (0,2) — H is still 7, end not at H */
  TEST_VIEW(sx_view_trim(alws), 2, 0);
  TEST_VIEW(sx_view_trim_front(alws), 2, 0);
  TEST_VIEW(sx_view_trim_back(alws), 0, 0);

  /* whitespace-only view NESTED in a whitespace run: the front scan
     is bounded by the view's own len, never by the buffer end —
     trimming must stop at (2, 0), not consume the spaces after the
     view and wrap len around (regression) */
  sx_t *sp = sx_new_from_cstr("    x"); /* H = 5 */
  sx_view_t spv = sx_view(sp);
  spv.len = 2; /* (0,2); bytes 2..3 are whitespace too, outside the view */
  TEST_VIEW(sx_view_trim_front(spv), 2, 0);
  TEST_VIEW(sx_view_trim(spv), 2, 0);
  sx_free(sp);

  /* trailing whitespace only */
  sx_view_t trail = sx_view_advance(full, 2);
  trail.len = 5; /* "abc  " (2,5) -> "abc" */
  TEST_VIEW(sx_view_trim_back(trail), 2, 3);

  /* interior whitespace is untouched */
  sx_view_t inner = sx_view_advance(full, 3); /* "bc  " (3,4) */
  TEST_VIEW(sx_view_trim(inner), 3, 2);       /* no leading space:    */
  /* front stops at 'b'; back eats the two trailing spaces */
  TEST_VIEW(sx_view_trim_front(inner), 3, 4); /* no leading space */

  /* tab / CR / LF are whitespace too */
  sx_t *t = sx_new_from_cstr("\txy\n");
  sx_view_t tv = sx_view(t);
  TEST_VIEW(sx_view_trim(tv), 1, 2);
  TEST_VIEW(sx_view_trim_front(tv), 1, 3);
  TEST_VIEW(sx_view_trim_back(tv), 0, 3);

  /* buffer bytes untouched by the family */
  TEST(sx_len(s) == 7 && sx_cmp_cstr(s, "  abc  ") == 0);
  TEST(sx_len(t) == 4 && sx_cmp_cstr(t, "\txy\n") == 0);
  sx_free(s);
  sx_free(t);

  PASS();
}