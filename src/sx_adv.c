// SPDX-License-Identifier: MIT
#include "sx.h"
#include "_sx.h"
#include "export.h"
#define PCRE2_CODE_UNIT_WIDTH 8 /* the 8-bit pcre2-8 API */
#include <pcre2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Self-contained byte-exact separator test (the core's KMP search
   stays private to sx_core.c; the O(n*m) scan is fine for the
   advanced layer and keeps this TU independent).  `i` is the
   candidate offset; the caller guarantees i + m <= H. */
static int is_sep(const char *p, const char *sep, size_t m, size_t i) {
  size_t k;
  for (k = 0; k < m; k++)
    if (p[i + k] != sep[k])
      return 0;
  return 1;
}

/* Two deterministic passes over the same scan: count first so the
   result array is allocated EXACTLY (no realloc growth), then fill.
   Separators never overlap: after a hit the scan resumes m bytes
   further right. */
SYMBOL_PUBLIC sx_view_t *sx_split(const sx_t *h, const char *sep) {
  if (!h || !sep)
    return NULL;
  size_t m = strlen(sep);
  if (m == 0)
    return NULL; /* an empty separator is refused, not "every byte" */
  const size_t H = h->ref->len;
  const char *p = h->ref->ptr;

  /* pass 1: tokens = separators + 1 */
  size_t cnt = 1;
  for (size_t at = 0; at + m <= H;) {
    if (is_sep(p, sep, m, at)) {
      cnt++;
      at += m;
    } else {
      at++;
    }
  }

  sx_view_t *arr = malloc((cnt + 1) * sizeof(*arr));
  if (!arr)
    return NULL;

  /* pass 2: fill the token views (same scan, same hit positions) */
  size_t i = 0, start = 0;
  for (size_t at = 0; at + m <= H;) {
    if (!is_sep(p, sep, m, at)) {
      at++;
      continue;
    }
    arr[i++] = (sx_view_t){.ref = h->ref, .offset = start, .len = at - start};
    at += m;
    start = at;
  }
  arr[i++] = (sx_view_t){.ref = h->ref, .offset = start, .len = H - start};
  arr[i] = (sx_view_t){0}; /* end guard: v.ref == NULL stops iteration */
  return arr;
}

/* =====================================================================
   Regular expressions — thin PCRE2 (pcre2-8) wrapper.  The compiled
   pattern is immutable after sx_reg_compile(); every call below uses
   its own match data, so one regex may serve several threads.

   Scan rule shared by match-all and split: matches are non-overlapping
   and taken left to right; after a hit the next search starts at the
   match's end, or ONE BYTE further right when the match is empty, so
   a pattern that matches empty (e.g. "a*") cannot stall the scan.
   ===================================================================== */

typedef struct sx_reg_t {
  pcre2_code *code;
} sx_reg_t;

SYMBOL_PUBLIC sx_reg_t *sx_reg_compile(const char *pattern) {
  if (!pattern)
    return NULL;
  int errcode;
  PCRE2_SIZE erroff;
  pcre2_code *code =
      pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, 0, &errcode,
                    &erroff, NULL);
  if (!code)
    return NULL; /* syntactically invalid pattern */
  sx_reg_t *r = malloc(sizeof(*r));
  if (!r) {
    pcre2_code_free(code);
    return NULL;
  }
  r->code = code;
  return r;
}

SYMBOL_PUBLIC void sx_reg_free(sx_reg_t *r) {
  if (!r)
    return;
  pcre2_code_free(r->code);
  free(r);
}

/* One pcre2_match attempt on buff from offset `at`.  Returns 1 on a
   match and writes the whole match's byte range [start, end); 0 when
   there is no further match (PCRE2's end-of-subject / beyond-end
   errors land here too). */
static int reg_step(const pcre2_code *code, const _sx *buff, size_t at,
                    pcre2_match_data *md, size_t *start, size_t *end) {
  int rc = pcre2_match(code, (PCRE2_SPTR)buff->ptr, buff->len, at, 0, md, NULL);
  if (rc < 0)
    return 0;
  PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
  *start = (size_t)ov[0];
  *end = (size_t)ov[1];
  return 1;
}

/* Scan offset after the match [start, end): the match's end, or one
   byte right of an empty match (pcre2_match itself rejects a start
   offset beyond the subject, so the loop terminates). */
static size_t reg_next_at(size_t start, size_t end) {
  return end > start ? end : start + 1;
}

SYMBOL_PUBLIC int sx_reg_is_match(const sx_reg_t *r, const sx_t *h) {
  if (!r || !h)
    return 0;
  pcre2_match_data *md = pcre2_match_data_create_from_pattern(r->code, NULL);
  if (!md)
    return 0;
  int rc = pcre2_match(r->code, (PCRE2_SPTR)h->ref->ptr, h->ref->len, 0, 0, md,
                       NULL);
  pcre2_match_data_free(md);
  return rc >= 0;
}

SYMBOL_PUBLIC sx_view_t sx_reg_match(const sx_reg_t *r, const sx_t *h) {
  sx_view_t v = {0};
  if (!r || !h)
    return v;
  pcre2_match_data *md = pcre2_match_data_create_from_pattern(r->code, NULL);
  if (!md)
    return v;
  size_t start, end;
  if (reg_step(r->code, h->ref, 0, md, &start, &end))
    v = (sx_view_t){.ref = h->ref, .offset = start, .len = end - start};
  pcre2_match_data_free(md);
  return v;
}

SYMBOL_PUBLIC sx_view_t *sx_reg_match_all(const sx_reg_t *r, const sx_t *h) {
  if (!r || !h)
    return NULL;
  pcre2_match_data *md = pcre2_match_data_create_from_pattern(r->code, NULL);
  if (!md)
    return NULL;
  const _sx *buff = h->ref;

  /* pass 1: count the matches (deterministic scan; pass 2 hits the
     exact same positions) */
  size_t start, end, at = 0, cnt = 0;
  while (reg_step(r->code, buff, at, md, &start, &end)) {
    cnt++;
    at = reg_next_at(start, end);
  }
  if (cnt == 0) {
    pcre2_match_data_free(md);
    return NULL;
  }

  sx_view_t *arr = malloc((cnt + 1) * sizeof(*arr));
  if (!arr) {
    pcre2_match_data_free(md);
    return NULL;
  }

  /* pass 2: fill the match views */
  size_t i = 0;
  at = 0;
  while (reg_step(r->code, buff, at, md, &start, &end)) {
    arr[i++] = (sx_view_t){.ref = h->ref, .offset = start, .len = end - start};
    at = reg_next_at(start, end);
  }
  arr[i] = (sx_view_t){0}; /* end guard */
  pcre2_match_data_free(md);
  return arr;
}

/* Shared body of sx_reg_replace / sx_reg_replace_first: run
   pcre2_substitute twice — first with a zero-size buffer to learn
   the exact output size (PCRE2_SUBSTITUTE_OVERFLOW_LENGTH), then
   into a fitting buffer, which is adopted into a fresh sx_t
   (byte-exact, NUL-safe).  Both calls expand unset and non-existent
   groups to the empty string (the two flags below; PCRE2's defaults
   would error instead).  `options` differs only by
   PCRE2_SUBSTITUTE_GLOBAL.  No match at all is not an error: the
   result is an unchanged copy of h. */
static sx_t *substitute(const sx_reg_t *r, const sx_t *h, const char *repl,
                        uint32_t options) {
  options |= PCRE2_SUBSTITUTE_UNSET_EMPTY | PCRE2_SUBSTITUTE_UNKNOWN_UNSET;
  PCRE2_SIZE needed = 0;
  int rc = pcre2_substitute(r->code, (PCRE2_SPTR)h->ref->ptr, h->ref->len, 0,
                            options | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, NULL,
                            NULL, (PCRE2_SPTR)repl, PCRE2_ZERO_TERMINATED, NULL,
                            &needed);
  if (rc != PCRE2_ERROR_NOMEMORY)
    return NULL; /* PCRE2 rejected the pattern/replacement pair */
  char *buf = malloc(needed ? needed : 1); /* `needed` includes the NUL */
  if (!buf)
    return NULL;
  PCRE2_SIZE used = needed;
  rc = pcre2_substitute(r->code, (PCRE2_SPTR)h->ref->ptr, h->ref->len, 0,
                        options, NULL, NULL, (PCRE2_SPTR)repl,
                        PCRE2_ZERO_TERMINATED, (PCRE2_UCHAR *)buf, &used);
  if (rc < 0) {
    free(buf);
    return NULL;
  }
  sx_t *s = sx_new_with_cap(used + 1); /* content + terminator */
  if (!s) {
    free(buf);
    return NULL;
  }
  memcpy(s->ref->ptr, buf, used);
  s->ref->len = used;
  s->ref->ptr[used] = '\0';
  free(buf);
  return s;
}

SYMBOL_PUBLIC sx_t *sx_reg_replace(const sx_reg_t *r, const sx_t *h,
                                   const char *repl) {
  if (!r || !h || !repl)
    return NULL;
  return substitute(r, h, repl, PCRE2_SUBSTITUTE_GLOBAL);
}

SYMBOL_PUBLIC sx_t *sx_reg_replace_first(const sx_reg_t *r, const sx_t *h,
                                         const char *repl) {
  if (!r || !h || !repl)
    return NULL;
  return substitute(r, h, repl, 0);
}

SYMBOL_PUBLIC sx_view_t *sx_reg_split(const sx_reg_t *r, const sx_t *h) {
  if (!r || !h)
    return NULL;
  pcre2_match_data *md = pcre2_match_data_create_from_pattern(r->code, NULL);
  if (!md)
    return NULL;
  const _sx *buff = h->ref;

  /* pass 1: token count = matches + 1 (same scan as match_all) */
  size_t start, end, at = 0, m = 0;
  while (reg_step(r->code, buff, at, md, &start, &end)) {
    m++;
    at = reg_next_at(start, end);
  }

  sx_view_t *arr = malloc((m + 2) * sizeof(*arr)); /* tokens + guard */
  if (!arr) {
    pcre2_match_data_free(md);
    return NULL;
  }

  /* pass 2: fill the token views — the gaps between matches */
  size_t i = 0, seg = 0;
  at = 0;
  while (reg_step(r->code, buff, at, md, &start, &end)) {
    arr[i++] = (sx_view_t){.ref = h->ref, .offset = seg, .len = start - seg};
    seg = end;
    at = reg_next_at(start, end);
  }
  arr[i++] = (sx_view_t){.ref = h->ref, .offset = seg, .len = buff->len - seg};
  arr[i] = (sx_view_t){0}; /* end guard: v.ref == NULL stops iteration */
  pcre2_match_data_free(md);
  return arr;
}