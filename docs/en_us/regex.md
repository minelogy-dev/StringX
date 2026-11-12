English · [中文](../zh_cn/regex.md)

[Docs home](README.md) › Regular expressions

# Regular expressions

## Overview

`<sx.h>` adds an advanced layer: a thin, fixed-configuration wrapper around
the **PCRE2 8-bit library** (`pcre2-8`, version 10.46). It provides a
compile/free pair and three families — match, replace and split — and adds no
policy: anything not stated here is PCRE2's default.

The lifecycle:

1. compile a pattern into an opaque `sx_reg_t *` handle;
2. run any number of calls against any number of `sx_t` subjects;
3. release the handle once with `sx_reg_free`.

A compiled handle is immutable and shareable across threads; each call uses
its own match data. Subjects are matched by **byte length**, so an embedded NUL
is ordinary data (binary safe); patterns and replacement templates are C
strings and cannot contain a literal NUL.

The layer is part of `libstringx`, which links `pcre2-8`: consumers link
`-lstringx` and pass no PCRE2 flags. PCRE2 is BSD-3-Clause; see
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). For `$n` substitution
see [Substitution templates](#substitution-templates); for task recipes see the
[user guide](guide.md).

The families at a glance:

| Family | Functions | Result |
| --- | --- | --- |
| Compile / release | `sx_reg_compile`, `sx_reg_free` | handle |
| Match | `sx_reg_is_match`, `sx_reg_match`, `sx_reg_match_all` | 0/1, one view, or an array |
| Replace | `sx_reg_replace`, `sx_reg_replace_first` | new `sx_t` |
| Split | `sx_reg_split` | array of views |

## Lifecycle

Compile the pattern once before the loop that uses it. The pattern string is
not retained, so free it after compilation; release the handle with
`sx_reg_free` when done.

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

static void show(const char *label, sx_view_t v) {
  printf("%s: ", label);
  if (v.ref == NULL) {
    printf("<none>\n");
    return;
  }
  fwrite(sx_buf_view_mut(v), 1, v.len, stdout);
  putchar('\n');
}

int main(void) {
  sx_t *h = sx_new_from_cstr("bob@example.com and alice@example.org");
  sx_reg_t *re = sx_reg_compile("(\\w+)@(\\w+\\.\\w+)");
  if (!h || !re)
    return 1;

  printf("is_match: %d\n", sx_reg_is_match(re, h));
  show("first", sx_reg_match(re, h));

  sx_view_t *all = sx_reg_match_all(re, h);
  for (int i = 0; all && all[i].ref != NULL; i++)
    printf("match %d: %zu bytes at offset %zu\n", i, all[i].len,
           all[i].offset);
  free(all);

  sx_t *out = sx_reg_replace(re, h, "[$2: $1]");
  if (out) {
    printf("replaced: ");
    fwrite(sx_buf_mut(out), 1, (size_t)sx_len(out), stdout);
    putchar('\n');
    sx_free(out);
  }

  sx_view_t *tok = sx_reg_split(re, h);
  for (int i = 0; tok && tok[i].ref != NULL; i++)
    printf("token %d: %zu bytes\n", i, tok[i].len);
  free(tok);

  sx_reg_free(re);
  sx_free(h);
  return 0;
}
```

Three properties keep it safe in long-lived code:

- The handle is independent of subjects: one compiled pattern serves any
  number of buffers.
- Results that alias a subject are views, valid only while that subject lives
  and keeps its length.
- Arrays from `sx_reg_match_all` and `sx_reg_split` are caller-owned: release
  them with `free()`, not `sx_free`.

### sx_reg_compile

```c
sx_reg_t *sx_reg_compile(const char *pattern);
```

Compiles a pattern with PCRE2 and returns an opaque handle.

| Parameter | Meaning |
| --- | --- |
| `pattern` | NUL-terminated PCRE2 pattern; not retained by the handle |

Returns a new handle, or `NULL` for a `NULL` pattern, invalid syntax or
allocation failure. The wrapper exposes no PCRE2 error code or offset, so these
cases are indistinguishable: validate patterns during development and treat
`NULL` as unusable.

- The pattern string can be freed or modified immediately after the call.
- No compile options are passed. Features selected inside the pattern, such as
  inline `(?i)`, still apply.
- Compilation is the expensive step, so compile once and reuse the handle
  ([performance](#performance)).

```c
sx_reg_t *re = sx_reg_compile("(\\w+)@(\\w+)"); /* NULL on bad input */
/* re == NULL means: NULL pattern, invalid syntax, or out of memory */
/* ... run match / replace / split calls ... */
sx_reg_free(re); /* release the handle after its last use */
```

### sx_reg_free

```c
void sx_reg_free(sx_reg_t *r);
```

Releases a compiled handle. `NULL` is accepted and is a no-op.

- Free a handle exactly once, after its last use.
- Earlier results do not depend on the handle: match views alias the subject
  and arrays are caller-owned.
- Reusing a freed handle is out of contract.

```c
sx_reg_free(re); /* release the handle after its last use */
```

## Match family

All three calls scan content bytes from the left with PCRE2's
**leftmost-first** semantics: the earliest start wins; same-start alternatives
follow PCRE2's order. A zero-length match is a real match. Views alias the
subject and `h` is never modified.

### sx_reg_is_match

```c
int sx_reg_is_match(const sx_reg_t *r, const sx_t *h);
```

Answers "does the pattern match anywhere in `h`?".

| Parameter | Meaning |
| --- | --- |
| `r` | compiled pattern |
| `h` | subject buffer |

Returns `1` if a match exists anywhere in `h`, otherwise `0` — including for
`NULL` `r`/`h` or internal allocation failure. A zero-length match counts, so
`a*` matches an empty subject.

- The cheapest existence test: one PCRE2 match call and no result allocation;
  each call still creates and frees a small PCRE2 match-data block.
- The subject is searched by byte length; anchors refer to byte offsets.

```c
sx_reg_t *re = sx_reg_compile("a*");
sx_t *empty = sx_new();
int yes = sx_reg_is_match(re, empty); /* 1: a zero-length match counts */
sx_free(empty);
sx_reg_free(re);
```

### sx_reg_match

```c
sx_view_t sx_reg_match(const sx_reg_t *r, const sx_t *h);
```

Returns the leftmost match as a view into `h`.

| Parameter | Meaning |
| --- | --- |
| `r` | compiled pattern |
| `h` | subject buffer |

On success the result is a view into `h`: `.ref`, `.offset` and `.len` select
the matched bytes. No match, a `NULL` argument or allocation failure returns the
null view `{ref = NULL, offset = 0, len = 0}`; a zero-length match still has
`.ref != NULL` and `.len == 0`.

- Leftmost, not longest: alternatives follow PCRE2's order, so `b|bc` against
  `abc` matches `b` at offset 1.
- One PCRE2 match call; the view is not a copy.
- Anchors follow PCRE2's defaults: `^` matches at the subject start
  (offset 0) and `$` at the subject end; by default `$` also matches before
  a final newline.
- Using the view after `h` is freed or its length changes is out of
  contract.

```c
sx_reg_t *re = sx_reg_compile("bc");
sx_t *h = sx_new_from_cstr("abc");
sx_view_t v = sx_reg_match(re, h); /* v.offset == 1, v.len == 2 ("bc") */
sx_free(h);
sx_reg_free(re);
```

### sx_reg_match_all

```c
sx_view_t *sx_reg_match_all(const sx_reg_t *r, const sx_t *h);
```

Collects every match in one left-to-right pass.

| Parameter | Meaning |
| --- | --- |
| `r` | compiled pattern |
| `h` | subject buffer |

Returns a `malloc`'ed array with the `sx_split` shape: `arr[0 .. count-1]`
are matches and `arr[count].ref == NULL` is the end guard. Release it with
`free()`; views alias `h` while `h` lives and keeps its length. No match, a
`NULL` argument or allocation failure returns `NULL` — indistinguishable by
this API.

- Matches do not overlap: after each match the scan resumes at its end, or one
  byte right for a zero-length match ([the scan rule](#the-scan-rule)).
- Counting and filling are two passes over the same deterministic scan, so the
  array is allocated once at final size.
- `sx_find_all*` reports overlapping positions instead; regex matches cannot
  overlap.

```c
sx_reg_t *re = sx_reg_compile("a*");
sx_t *h = sx_new_from_cstr("bbb");
sx_view_t *arr = sx_reg_match_all(re, h);
int n = 0;
while (arr[n].ref != NULL)
  n++; /* n == 4: zero-length matches at offsets 0, 1, 2 and 3 */
free(arr);
sx_free(h);
sx_reg_free(re);
```

## The scan rule

`sx_reg_match_all` and `sx_reg_split` share one scan:

- matches are taken left to right and never overlap;
- after a match the next search starts at the match's end;
- after a zero-length match the next search starts **one byte to the right**
  of the match start, so an empty-matching pattern cannot stall the scan;
- a match may end exactly at `len(h)` (for example `$`); the scan then
  stops because PCRE2 rejects a start offset beyond the subject.

| Pattern | Subject | Reported matches (offset, length) |
| --- | --- | --- |
| `aa\|aaa` | `aaaa` | (0,2), (2,2) — `aaa` never gets a chance |
| `a*` | `bbb` | (0,0), (1,0), (2,0), (3,0) |
| `$` | `abc` | (3,0) |
| `bc` | `abcb` | (1,2) |

The same rule fixes the `sx_reg_split` token count: always
`number_of_matches + 1`.

## Replace family

Both replace calls substitute a C-string template into matches and return a
**new** caller-owned `sx_t`; `h` is never modified. Free the result with
`sx_free` like any other buffer.

### sx_reg_replace

```c
sx_t *sx_reg_replace(const sx_reg_t *r, const sx_t *h, const char *repl);
```

Replaces every match in `h` with `repl`.

| Parameter | Meaning |
| --- | --- |
| `r` | compiled pattern |
| `h` | subject buffer; never modified |
| `repl` | NUL-terminated substitution template; not retained |

Returns a new buffer, or `NULL` for a `NULL` argument, a template PCRE2
rejects, or allocation failure. With valid arguments and no match, the result
is an **unchanged copy** of `h`, never `NULL`.

- The template expands `$0` / `$n`; see
  [Substitution templates](#substitution-templates).
- Zero-length matches are substituted too: `a*` against `bbb` with the
  template `-` yields `-b-b-b-`.
- An empty template deletes every match; a whole-subject match therefore
  yields an empty buffer.
- Byte-exact: embedded NULs in `h` are ordinary matching data and are copied
  into the result.

```c
sx_reg_t *re = sx_reg_compile("(\\w+) (\\w+)");
sx_t *h = sx_new_from_cstr("hello world");
sx_t *swapped = sx_reg_replace(re, h, "$2 $1"); /* "world hello" */
sx_free(swapped);
sx_free(h);
sx_reg_free(re);
```

### sx_reg_replace_first

```c
sx_t *sx_reg_replace_first(const sx_reg_t *r, const sx_t *h,
                           const char *repl);
```

Same contract as `sx_reg_replace`, but only the **leftmost** match is
substituted; all other bytes are copied unchanged.

```c
sx_reg_t *re = sx_reg_compile("x");
sx_t *h = sx_new_from_cstr("xax");
sx_t *out = sx_reg_replace_first(re, h, "<$0>"); /* "<x>ax" */
sx_free(out);
sx_free(h);
sx_reg_free(re);
```

## Substitution templates

Both replace calls use PCRE2's substitution syntax for `repl`, with two
wrapper-fixed options that make missing captures harmless.

| Template element | Expands to |
| --- | --- |
| `$0` | the whole match |
| `$n` (`n >= 1`) | capture group `n`, or the empty string if the group took no part or does not exist |
| any other text | copied; PCRE2's substitution syntax still defines how to write `$` and `\` literally |

| Pattern | Subject | Template | Result |
| --- | --- | --- | --- |
| `(\w+) (\w+)` | `hello world` | `$2 $1` | `world hello` |
| `(a)(b)` | `ab` | `$1$3` | `a` |
| `a(b)?` | `a` | `[$1]` | `[]` |
| `abc` | `abc` | (empty template) | (empty buffer) |

- The wrapper sets `PCRE2_SUBSTITUTE_UNSET_EMPTY` and
  `PCRE2_SUBSTITUTE_UNKNOWN_UNSET`: a group that did not participate and a
  non-existent group number both expand to empty, so a template is never
  rejected merely for a missing capture.
- `$n` refers to each match's own capture groups; group 0 is the whole
  match.
- `repl` is a C string, so a literal NUL cannot appear in it; matched NUL
  bytes still reach the result through `$0` / `$n`.
- The full syntax is PCRE2's; see the
  [PCRE2 documentation](https://www.pcre.org/).

## Split family

### sx_reg_split

```c
sx_view_t *sx_reg_split(const sx_reg_t *r, const sx_t *h);
```

Splits `h` into the gaps between non-overlapping matches.

| Parameter | Meaning |
| --- | --- |
| `r` | compiled pattern |
| `h` | subject buffer |

Returns a `malloc`'ed array with the `sx_split` shape: tokens in order at
`arr[0 .. count-1]`, end guard `arr[count].ref == NULL`, and
`count == number_of_matches + 1`. Empty tokens are kept at every edge and in
the interior; free the array with `free()`; tokens are views into `h`. `NULL`
on a `NULL` argument or allocation failure. No match is not an error: the whole
subject is the single token.

- Zero-length matches split at every position: `a*` against `bbb` gives `""`,
  `"b"`, `"b"`, `"b"`, `""` — five tokens.
- A whole-subject match yields two empty tokens.
- Leading and trailing matches yield empty edge tokens; adjacent matches yield
  interior empty tokens.
- Do not edit `h` while the tokens are in use (views are snapshots; see the
  [user guide](guide.md)).

```c
sx_reg_t *re = sx_reg_compile("[0-9]+");
sx_t *h = sx_new_from_cstr("a1b22c");
sx_view_t *tok = sx_reg_split(re, h);
/* 3 tokens: "a" (0,1), "b" (2,1), "c" (5,1) — "22" is one match */
free(tok);
sx_free(h);
sx_reg_free(re);
```

## Binary safety

| Direction | Rule |
| --- | --- |
| Subject `h` | matched by byte length; embedded NUL and every byte value are ordinary data |
| Pattern | a C string: NUL-terminated, so no literal NUL inside the pattern |
| Template `repl` | a C string: no literal NUL; captures can inject NUL bytes into the result |

- PCRE2 runs in **8-bit non-UTF mode**: `\d`, `\w`, `\s`, `\b` and friends
  match ASCII only; bytes forming a non-ASCII character (e.g. UTF-8 `C2 A5`)
  never match `\d`.
- In 8-bit non-UTF mode, `.` matches any byte except newline, including an
  embedded NUL: the pattern `A.B` matches the three bytes `A\0B`.
- Replace is binary-safe in both directions: `a\0b` with the pattern `b` and
  template `B` produces `a\0B`; `.` with template `X` over the same subject
  produces `XXX`.
- Because the pattern is a C string, a NUL byte in the subject can only be
  described with a PCRE2 escape; see the
  [PCRE2 documentation](https://www.pcre.org/).

## Error sentinels

No documented call crashes on invalid input, a missing match or allocation
failure; each family reports a fixed sentinel.

| Call | No match | `NULL` argument / invalid input | Allocation failure |
| --- | --- | --- | --- |
| `sx_reg_compile` | — | `NULL` (also for a syntax error) | `NULL` |
| `sx_reg_free` | — | `NULL` is a no-op | — |
| `sx_reg_is_match` | `0` | `0` | `0` |
| `sx_reg_match` | null view `{ref = NULL}` | null view | null view |
| `sx_reg_match_all` | `NULL` | `NULL` | `NULL` |
| `sx_reg_replace` | unchanged copy | `NULL` | `NULL` |
| `sx_reg_replace_first` | unchanged copy | `NULL` | `NULL` |
| `sx_reg_split` | one-token array (the whole subject) | `NULL` | `NULL` |

- For `sx_reg_match_all` and `sx_reg_split`, no match and allocation failure
  both yield `NULL`; the array family cannot distinguish them.
- For replace, no match is normal success: an unchanged copy of `h`.
- Arrays are `malloc`'ed and freed with `free()`; replace results are
  `sx_t *` freed with `sx_free()`.

## Thread safety

- A compiled `sx_reg_t` is immutable after `sx_reg_compile`, has no mutable
  state, and the wrapper keeps no global state, so one handle can serve several
  threads.
- Each match/split call creates and frees its own PCRE2 match-data block;
  replace delegates to `pcre2_substitute`, which also uses per-call match data.
  No match state is shared.
- A subject buffer is plain memory with no locking: concurrent readers are
  fine; mutation needs external synchronization.
- Views and arrays are snapshots: do not edit a subject while another thread
  reads its views, and do not free a handle while another thread uses it.

## Performance

| Call | PCRE2 work | Wrapper allocation | Result |
| --- | --- | --- | --- |
| `sx_reg_compile` | one compile | code plus handle | handle |
| `sx_reg_is_match` | one match call | match data only | `0` / `1` |
| `sx_reg_match` | one match call | match data only | one view |
| `sx_reg_match_all` | two passes over the scan | match data plus one exact-size array | array |
| `sx_reg_split` | two passes over the scan | match data plus one exact-size array | array |
| `sx_reg_replace*` | two substitute calls | internal PCRE2 match data, one temporary buffer, one `sx_t` | new buffer |

- Compile once: compilation costs far more than a single match, so compiling
  inside a loop is the one avoidable performance mistake.
- `sx_reg_is_match` is the cheapest existence test; `sx_reg_match` adds only
  the returned view.
- `sx_reg_match_all` and `sx_reg_split` scan twice (count, then fill), so the
  result array is allocated once at final size without `realloc` growth.
- Replace probes the exact output length with a first `pcre2_substitute`
  call, then fills a fitting buffer with a second; the buffer is copied once
  into the returned `sx_t`.
- Every call allocates and frees match data; nothing is cached, and the wrapper
  enables neither PCRE2 JIT nor compile options.
- The subject is never copied; only the replace family allocates a result
  buffer.

## Choose the right call

| Requirement | Call | Returns | Ownership |
| --- | --- | --- | --- |
| Does anything match? | `sx_reg_is_match` | `int` 0/1 | none |
| The leftmost match | `sx_reg_match` | view into `h` | none; view is a snapshot |
| Every match | `sx_reg_match_all` | `malloc` array plus end guard | caller: `free()` |
| Replace every match | `sx_reg_replace` | new `sx_t` | caller: `sx_free()` |
| Replace the first match only | `sx_reg_replace_first` | new `sx_t` | caller: `sx_free()` |
| Split into fields | `sx_reg_split` | `malloc` array plus end guard | caller: `free()` |

## PCRE2 semantics

The wrapper fixes a small, deliberately plain configuration:

- **8-bit, non-UTF**: pattern and subject are bytes, character classes are
  ASCII, and no UTF validation happens. In the default (no `DOTALL`) mode `.`
  does not match newline.
- **No option flags**: no compile options, match options or match context are
  passed. In-pattern features such as inline options still work, so
  case-insensitive and multiline matching use PCRE2's own syntax.
- **PCRE2 defaults govern**: alternation preference, greedy/lazy quantifiers,
  anchors, backreferences and all other pattern semantics are PCRE2's, not the
  wrapper's.
- The wrapper does not expose PCRE2 error codes or compile error offsets.

Pattern syntax and semantics are documented by PCRE2 at
[pcre.org](https://www.pcre.org/); this library does not re-document them. The
bundled PCRE2 version is 10.46; the dependency is BSD-3-Clause, attributed in
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

---

[← Previous](api-search.md) · [Docs home](README.md) · [Next →](design.md)
