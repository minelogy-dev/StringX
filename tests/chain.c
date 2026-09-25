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
 * Behavior boundaries — chained, zero-leak string processing
 * ===================================================================
 * The library's personality: every edit returns `self`, never a new
 * handle, so edits COMPOSE into long chains over one stable buffer;
 * ownership follows one transfer rule (an sx_t* is owned by exactly
 * one place at a time), so a chain — however long, however many
 * buffers it welds — releases exactly once at its end.  The leak
 * sanitizer of the ASan build is the zero-leak witness: any chain
 * below that leaked or double-freed would abort the run.
 *
 * In scope (contract):
 *   · handle identity: the sx_t* it started with IS the sx_t* it
 *     ends with; growth reallocates the backing array (so
 *     sx_buf_mut pointers go stale), never the handle.
 *   · ownership transfer: sx_append/-write/-insert(owned) CONSUME
 *     their sx argument (freed inside); the constructors, sx_dup
 *     and sx_substr return caller-owned buffers; welding consumes
 *     its source — the number of sx_free calls in a chain equals
 *     the number of buffers CREATED at the chain's start, not the
 *     number of edits.
 *   · views are (ref, offset, len) snapshots: later edits never
 *     adjust a captured view — re-derive after a length change.
 *   · a NULL argument short-circuits the chain: returns NULL and
 *     leaves the buffer intact.
 *
 * Out of contract (not exercised):
 *   · double-free or use-after-free of a consumed buffer; using a
 *     view captured before an edit that changed the length; passing
 *     a buffer to a consuming call twice.
 */

int main(void) {
  /* ---- single-expression chains: returned self keeps identity ---- */
  sx_t *s = sx_new_from_cstr("x");
  TEST(sx_append_cstr(sx_append_cstr(s, "y"), "z") == s);
  TEST(sx_cmp_cstr(s, "xyz") == 0);
  sx_free(s);

  /* ---- owned-welding chain in one expression: every source buffer
     is consumed exactly once, only the target survives ---- */
  sx_t *p1 = sx_new_from_cstr("ab");
  sx_t *p2 = sx_new_from_cstr("cd");
  sx_t *p3 = sx_new_from_cstr("ef");
  sx_t *m = sx_new();
  TEST(sx_append(sx_append(sx_append(m, p1), p2), p3) == m);
  TEST(sx_cmp_cstr(m, "abcdef") == 0);
  sx_free(m); /* p1..p3 were welded: exactly this one free remains */

  /* ---- write/insert owned variants inside a chain ---- */
  sx_t *w = sx_new_from_cstr("----");
  sx_t *o1 = sx_new_from_cstr("XY");
  sx_t *o2 = sx_new_from_cstr("!?");
  TEST(sx_write(w, 0, o1) == w);
  TEST(sx_insert(w, 2, o2) == w);
  TEST(sx_cmp_cstr(w, "XY!?--") == 0);
  sx_free(w);

  /* ---- substr results are caller-owned: handing them to a
     consuming call transfers the ownership, nothing leaks ---- */
  sx_t *src = sx_new_from_cstr("0123456789");
  sx_t *acc = sx_new();
  TEST(sx_append(acc, sx_substr(src, 2, 3)) == acc);
  TEST(sx_append(acc, sx_substr(src, 7, 2)) == acc);
  TEST(sx_cmp_cstr(acc, "23478") == 0);
  sx_free(acc);
  sx_free(src);

  /* ---- two independent buffers, then one welded into the other:
     editing one never disturbs the other ---- */
  sx_t *x = sx_new_from_cstr("alpha");
  sx_t *y = sx_new_from_cstr("alpha");
  TEST(sx_append_cstr(x, "1") == x);
  TEST(sx_append_cstr(y, "2") == y);
  TEST(sx_cmp_cstr(x, "alpha1") == 0 && sx_cmp_cstr(y, "alpha2") == 0);
  TEST(sx_append(x, y) == x); /* consume y's owner */
  TEST(sx_cmp_cstr(x, "alpha1alpha2") == 0);
  sx_free(x); /* y was consumed: nothing else to free */

  /* ---- handle stability across 64 reallocations: the chain grows
     the backing array every few appends, the handle never moves ---- */
  sx_t *big = sx_new_with_cap(2);
  const sx_t *h0 = big;
  for (int i = 0; i < 64; i++)
    TEST((big = sx_append_cstr(big, "0123456789")) == h0);
  sx_t *exp = sx_new();
  for (int i = 0; i < 64; i++)
    TEST(sx_append_cstr(exp, "0123456789") == exp);
  TEST(sx_len(big) == 640 && sx_cmp(big, exp) == 0); /* strict */
  TEST(sx_buf_mut(big)[640] == '\0');
  sx_free(exp);
  sx_free(big);

  /* ---- NULL short-circuits the chain without damage ---- */
  sx_t *n = sx_new_from_cstr("abc");
  TEST(sx_append_cstr(n, NULL) == NULL);
  TEST(sx_append(n, NULL) == NULL);
  TEST(sx_len(n) == 3 && sx_cmp_cstr(n, "abc") == 0);
  sx_free(n);

  /* ---- captured views are snapshots: edits never adjust them,
     re-derive after a length change ---- */
  sx_t *g = sx_new_from_cstr("abcd");
  sx_view_t v_old = sx_view(g);
  TEST(sx_len_view(v_old) == 4);
  TEST(sx_append_cstr(g, "ef") == g);
  TEST(sx_len(g) == 6);
  TEST(sx_len_view(v_old) == 4); /* unchanged: a snapshot */
  sx_view_t v_new = sx_view(g);
  TEST(sx_len_view(v_new) == 6 && sx_cmp_view(g, v_new) == 0);
  sx_free(g);

  /* ---- a no-op chain on an empty buffer ---- */
  sx_t *e = sx_new();
  TEST(sx_append_cstr(e, "") == e);
  TEST(sx_clear(e) == e);
  TEST(sx_truncate(e, 0) == e);
  TEST(sx_len(e) == 0 && sx_cmp_cstr(e, "") == 0);
  sx_free(e);

  /* ---- showcase: one continuous text-processing pipeline over a
     single source buffer — trim, tokenize by comma (skip empty
     tokens), weld the tokens into a second buffer, all by views
     and one consuming step layout, zero intermediate copies ---- */
  src = sx_new_from_cstr("  alpha,beta,,gamma \n");
  acc = sx_new();
  sx_t *cma = sx_new_from_cstr(",");
  sx_view_t v0 = sx_view_trim(sx_view(src)); /* "alpha,beta,,gamma" */
  int lo = (int)v0.offset;
  int hi = (int)(v0.offset + v0.len);
  int start = lo;
  int first = 1;
  for (;;) {
    size_t c = sx_find_view_b(src, sx_view(cma), (size_t)start);
    int end = (c == (size_t)-1 || (int)c > hi) ? hi : (int)c;
    if (end > start) { /* non-empty token */
      if (!first)
        TEST(sx_append_cstr(acc, "|") == acc);
      first = 0;
      sx_view_t t = sx_view(src);
      t.offset = (size_t)start;
      t.len = (size_t)(end - start);
      TEST(sx_append_view(acc, t) == acc);
    }
    if (end == hi)
      break;
    start = end + 1;
  }
  TEST(first == 0);
  TEST(sx_len(acc) == 16 && sx_cmp_cstr(acc, "alpha|beta|gamma") == 0);
  TEST(sx_buf_mut(acc)[16] == '\0');
  TEST(sx_cmp_cstr(src, "  alpha,beta,,gamma \n") == 0);
  sx_free(cma);
  sx_free(acc);
  sx_free(src);

  PASS();
}