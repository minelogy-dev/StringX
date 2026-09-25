// SPDX-License-Identifier: MIT
#ifndef SX
#define SX

#include "sx_core.h"

/*
 * ===================================================================
 * Behavior boundaries — sx_split (advanced layer)
 * ===================================================================
 * Splits h by the NON-OVERLAPPING occurrences of the separator
 * string `sep` (strlen semantics: an embedded '\0' ends the
 * separator), scanned left to right.
 *
 * Return shape (the classic "char**" convention, as sx_view_t*):
 *   arr[0] .. arr[count-1]   the tokens IN ORDER — empty fields are
 *                            kept: "a,,b" -> "a", "", "b"; leading
 *                            or trailing separators yield empty edge
 *                            tokens; splitting an empty h yields the
 *                            single token "".  Exactly
 *                            count == 1 + number of separator
 *                            occurrences.
 *   arr[count]               the END GUARD: one element whose
 *                            .ref == NULL — iterate while
 *                            arr[i].ref != NULL.
 *
 * The tokens are VIEWS INTO h's buffer: nothing is copied and the
 * original string is never modified — do not edit h while the
 * views are in use.  The caller owns the array and releases it
 * with free(); the views stay valid as long as h lives and its
 * length does not change.
 *
 * Returns NULL for NULL h or NULL/EMPTY sep, or on allocation
 * failure.
 */

/* Split h on the non-overlapping occurrences of the C-string
 * separator sep.  NO COPY, NO MODIFICATION of the original string:
 * the returned views alias h's content; the array is
 * NULL-token-terminated (last element .ref == NULL) and must be
 * freed by the caller. */
sx_view_t *sx_split(const sx_t *h, const char *sep);

/*
 * ===================================================================
 * Advanced layer — regular expressions (sx_reg_*, PCRE2 wrapper)
 * ===================================================================
 * A thin wrapper around PCRE2's 8-bit library.  PCRE2 is BSD-3-Clause
 * licensed; see THIRD_PARTY_NOTICES.md.  Usage: compile a pattern
 * (a C string) with sx_reg_compile() once, then run it against any
 * number of sx buffers with the three families below, then release
 * it with sx_reg_free().  Everything else is PCRE2's defaults:
 * 8-bit, non-UTF mode — \d \w \s \b and friends match ASCII only.
 * Subjects are matched by BYTE LENGTH, so embedded '\0' and every
 * byte value are ordinary data (binary-safe); the pattern itself is
 * a C string and cannot contain a literal NUL.  A compiled regex is
 * immutable and may be shared across threads (each call uses its
 * own match data).  The library links pcre2-8 itself: consumers
 * only need -lstringx.
 *
 * No-match / invalid input / allocation failure NEVER crashes; the
 * per-family error shape is a not-found sentinel: int -> 0,
 * sx_view_t -> {0} (v.ref == NULL), sx_view_t* / sx_t* -> NULL.
 */

/*
 * -------------------------------------------------------------------
 * Block 0 (common) — compile / release
 * -------------------------------------------------------------------
 * sx_reg_compile(pattern): returns an opaque compiled regex, or NULL
 * for a NULL / syntactically invalid pattern or on allocation
 * failure (the pattern string itself is not retained — the caller
 * may free it immediately).  The returned handle is freed with
 * sx_reg_free().
 * sx_reg_free(r): releases the handle; NULL is allowed (no-op).
 */
typedef struct sx_reg_t sx_reg_t;
sx_reg_t *sx_reg_compile(const char *pattern);
void sx_reg_free(sx_reg_t *r);

/*
 * -------------------------------------------------------------------
 * Block 1 — match family
 * -------------------------------------------------------------------
 * sx_reg_is_match:  0/1 — the cheapest form: a single PCRE2 match
 *                   call, no allocation.  1 iff some match exists
 *                   anywhere in h; a zero-length match (e.g. "a*"
 *                   against "bbb") counts as a match.
 * sx_reg_match:     the LEFTMOST match as a view INTO h's buffer.
 *                   v.ref == NULL -> no match (or NULL args).  A
 *                   zero-length match is still a match: v.ref !=
 *                   NULL with v.len == 0.  Leftmost-first PCRE2
 *                   semantics: "b|bc" against "abc" matches "b" at
 *                   offset 1.  One match call, no allocation beyond
 *                   PCRE2's own match data.
 * sx_reg_match_all: every match, one left-to-right pass, NON-
 *                   OVERLAPPING: after each match the next search
 *                   starts at the match's end, or one byte further
 *                   right for a zero-length match ("aa|aaa" against
 *                   "aaaa" -> two matches, offsets 0 and 2).
 *                   Returns a malloc'ed sx_view_t array with the
 *                   sx_split shape: arr[0 .. count-1] are the
 *                   matches and arr[count].ref == NULL is the end
 *                   guard; NULL when there is no match at all or on
 *                   allocation failure.  Caller frees the array
 *                   with free(); the views stay valid while h lives.
 */
/* 0 if no match exists in h, 1 otherwise.  NULL r or h -> 0. */
int sx_reg_is_match(const sx_reg_t *r, const sx_t *h);
sx_view_t sx_reg_match(const sx_reg_t *r, const sx_t *h);
sx_view_t *sx_reg_match_all(const sx_reg_t *r, const sx_t *h);

/*
 * -------------------------------------------------------------------
 * Block 2 — replace family
 * -------------------------------------------------------------------
 * Both sub a C-string template `repl` into the match(es) and return
 * a NEW sx_t (freed by the caller with sx_free()); h is never
 * modified.  The template uses PCRE2's substitution syntax: $0 / $n
 * expand to the corresponding capture of the match (group 0 is the
 * whole match); a group that took no part — or does not exist —
 * expands to the empty string; everything else is literal text
 * (the wrapper sets PCRE2's unset/unknown-to-empty options, so a
 * template can never be rejected for missing captures).
 * Binary-safe both ways: embedded NULs in h are ordinary matching
 * data and survive into the result (checked by byte length, not by
 * C-string).  If there is no match at all, the result is an
 * unchanged copy of h.
 * sx_reg_replace:        every match is substituted.
 * sx_reg_replace_first:  only the leftmost match is substituted.
 * Both return NULL for NULL r / h / repl, when PCRE2 rejects the
 * template, or on allocation failure.
 */
sx_t *sx_reg_replace(const sx_reg_t *r, const sx_t *h, const char *repl);
sx_t *sx_reg_replace_first(const sx_reg_t *r, const sx_t *h, const char *repl);

/*
 * -------------------------------------------------------------------
 * Block 3 — split family
 * -------------------------------------------------------------------
 * Splits h by the NON-OVERLAPPING matches of the regex, scanned left
 * to right, with EXACTLY the sx_split return shape and token rules:
 * the tokens are the gaps between matches (count == matches + 1),
 * empty tokens are kept everywhere (leading, trailing, interior),
 * splitting an empty h with no match yields the single token "".
 * The returned array is malloc'ed and end-guarded (arr[count].ref
 * == NULL); the tokens are views INTO h.  NULL for NULL r / h or on
 * allocation failure.  Caller frees the array with free().
 */
sx_view_t *sx_reg_split(const sx_reg_t *r, const sx_t *h);

#endif