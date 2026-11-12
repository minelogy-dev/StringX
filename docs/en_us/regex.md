English · [中文](../zh_cn/regex.md)

[Docs home](README.md) › Regular expressions

# Regular expressions

## Overview

`<sx.h>` adds one advanced layer on top of the core buffer API: a thin,
fixed-configuration wrapper around the **PCRE2 8-bit library** (`pcre2-8`,
version 10.46). It exposes a compile/free pair and three families — match,
replace and split — and deliberately adds no policy of its own: everything
this page does not state is PCRE2's default behavior.

The layer is designed around one lifecycle:

1. compile a pattern into an opaque `sx_reg_t *` handle;
2. run any number of calls against any number of `sx_t` subjects;
3. release the handle once with `sx_reg_free`.

A compiled handle is immutable and may be shared across threads; every call
uses its own match data. Subjects are matched by **byte length**, so an
embedded NUL is ordinary data (binary safe); patterns and replacement
templates are C strings and cannot contain a literal NUL.

The layer is part of `libstringx`, which links `pcre2-8` itself: consumers
link `-lstringx` and pass no PCRE2 flags. PCRE2 is BSD-3-Clause; attribution
lives in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). For the
`$n` substitution rules see [Substitution templates](#substitution-templates);
for a task-oriented tour see the [user guide](guide.md).

The families at a glance:

| Family | Functions | Result |
| --- | --- | --- |
| Compile / release | `sx_reg_compile`, `sx_reg_free` | handle |
| Match | `sx_reg_is_match`, `sx_reg_match`, `sx_reg_match_all` | 0/1, one view, or an array |
| Replace | `sx_reg_replace`, `sx_reg_replace_first` | new `sx_t` |
| Split | `sx_reg_split` | array of views |

## Lifecycle

Compile the pattern once, before the loop that uses it, and keep the handle.
The pattern string itself is not retained, so it can be freed right after
compilation. When the handle is no longer needed, release it with
`sx_reg_free`.

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

The three points that make the API safe to use in long-lived code are:

- the handle is independent of the subjects: one compiled pattern serves any
  number of buffers;
- results that alias a subject are views, so they stay valid only while that
  subject lives and keeps its length;
- arrays returned by `sx_reg_match_all` and `sx_reg_split` are
  caller-owned: release them with `free()`, not `sx_free`.

### sx_reg_compile

```c
sx_reg_t *sx_reg_compile(const char *pattern);
```

Compiles a pattern with PCRE2 and returns an opaque handle.

| Parameter | Meaning |
| --- | --- |
| `pattern` | NUL-terminated PCRE2 pattern; not retained by the handle |

Returns a new handle, or `NULL` when `pattern` is `NULL`, the pattern is
syntactically invalid, or allocation fails. The wrapper exposes no PCRE2
error code or error offset, so these cases cannot be told apart: validate a
pattern during development and treat `NULL` as "do not use this handle".

Notes:

- The pattern string may be freed or modified immediately after the call.
- No compile options are passed to PCRE2. PCRE2 features selected inside the
  pattern itself (an inline option such as `(?i)`) still apply.
- Compilation is the expensive step of the layer relative to a single match
  call, so compile once and reuse the handle ([performance](#performance)).

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

Notes:

- Free a handle exactly once, after the last call that uses it.
- Data returned by earlier calls does not depend on the handle: match views
  alias the subject, and arrays are caller-owned.
- Using a freed handle again is out of contract.

```c
sx_reg_free(re); /* release the handle after its last use */
```

## Match family

All three calls scan the subject's content bytes from the left and follow
PCRE2's **leftmost-first** semantics: the match that starts earliest wins,
and among alternatives that start at the same byte PCRE2's preference order
decides. A zero-length match is a real match. Views alias the subject: no
bytes are copied and `h` is never modified.

### sx_reg_is_match

```c
int sx_reg_is_match(const sx_reg_t *r, const sx_t *h);
```

Answers "does the pattern match anywhere in `h`?".

| Parameter | Meaning |
| --- | --- |
| `r` | compiled pattern |
| `h` | subject buffer |

Returns `1` if some match exists anywhere in `h`, otherwise `0` — including
when `r` or `h` is `NULL`, or when an internal allocation fails. A
zero-length match counts as a match, so `a*` matches an empty subject.

Notes:

- The cheapest existence test: one PCRE2 match call and no result
  allocation. Each call still creates and frees its own small PCRE2
  match-data block.
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

On success the result is a view into `h`'s buffer: `.ref` points at the
subject, and `.offset` / `.len` select the matched bytes. No match, a `NULL`
argument, or allocation failure returns the null view
`{ref = NULL, offset = 0, len = 0}`. A zero-length match is still a match:
`.ref != NULL` with `.len == 0`.

Notes:

- Leftmost match, not "the longest match": alternatives prefer PCRE2's
  order, so `b|bc` against `abc` matches `b` at offset 1.
- One PCRE2 match call; the view is not a copy.
- Anchors follow PCRE2's defaults: `^` matches at the subject start
  (offset 0) and `$` at the subject end; by default `$` also matches before
  a final newline.
- Using the view after `h` is freed, or after an edit changes `h`'s length,
  is out of contract.

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

Returns a `malloc`'ed array of views with the same shape as `sx_split`:
`arr[0 .. count-1]` are the matches and `arr[count].ref == NULL` is the end
guard. Release the array with `free()`; the views alias `h` and stay valid
while `h` lives and keeps its length.

Returns `NULL` when there is no match at all, when `r` or `h` is `NULL`, or
when allocation fails. These three cases are not distinguishable by this
API.

Notes:

- Matches do not overlap: after each match the scan resumes at the match end,
  or one byte further right for a zero-length match
  ([the scan rule](#the-scan-rule)).
- Counting and filling are two passes over the same deterministic scan, so
  the array is allocated exactly once at its final size.
- `sx_find_all*` reports overlapping positions instead; `sx_reg_match_all`
  reports regex matches, which cannot overlap.

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
  of the match start, so a pattern that can match empty cannot stall the
  scan;
- a match may end exactly at `len(h)` (for example `$`); the scan then
  stops because PCRE2 rejects a start offset beyond the subject.

| Pattern | Subject | Reported matches (offset, length) |
| --- | --- | --- |
| `aa\|aaa` | `aaaa` | (0,2), (2,2) — `aaa` never gets a chance |
| `a*` | `bbb` | (0,0), (1,0), (2,0), (3,0) |
| `$` | `abc` | (3,0) |
| `bc` | `abcb` | (1,2) |

The same rule determines the token count of `sx_reg_split`: it is always
`number_of_matches + 1`.

## Replace family

Both replace calls substitute a C-string template into the match(es) and
return a **new** caller-owned `sx_t`; `h` is never modified. Free the result
with `sx_free`, like any other buffer.

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

Returns a new buffer, or `NULL` when `r`, `h` or `repl` is `NULL`, PCRE2
rejects the template, or allocation fails. With valid arguments and no match
the result is an **unchanged copy** of `h`, never `NULL`.

Notes:

- The template expands `$0` / `$n`; see
  [Substitution templates](#substitution-templates).
- Zero-length matches are substituted too: `a*` against `bbb` with the
  template `-` yields `-b-b-b-`.
- An empty template deletes every match, so a pattern that matches the whole
  subject produces an empty buffer.
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

Same contract as `sx_reg_replace`, except that only the **leftmost** match is
substituted; every other byte is copied unchanged.

```c
sx_reg_t *re = sx_reg_compile("x");
sx_t *h = sx_new_from_cstr("xax");
sx_t *out = sx_reg_replace_first(re, h, "<$0>"); /* "<x>ax" */
sx_free(out);
sx_free(h);
sx_reg_free(re);
```

## Substitution templates

Both replace calls interpret `repl` with PCRE2's substitution syntax, with
two options fixed by the wrapper that make missing captures harmless.

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

Notes:

- The wrapper passes PCRE2's `PCRE2_SUBSTITUTE_UNSET_EMPTY` and
  `PCRE2_SUBSTITUTE_UNKNOWN_UNSET`: a group that did not participate, and a
  group number that does not exist, both expand to the empty string. A
  template is therefore never rejected just because a capture is missing.
- `$n` refers to capture groups of each match as it is substituted; group 0
  is the whole match.
- `repl` is a C string, so a literal NUL cannot appear in it. Matched NUL
  bytes still reach the result through `$0` / `$n`.
- The full substitution syntax is PCRE2's; see the
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

Returns a `malloc`'ed array with exactly the `sx_split` shape:
`arr[0 .. count-1]` are the tokens in order, `arr[count].ref == NULL` is the
end guard, and `count == number_of_matches + 1`. Empty tokens are kept at
every edge and in the interior. The caller frees the array with `free()`; the
tokens are views into `h`.

Returns `NULL` for a `NULL` argument or allocation failure. No match is not
an error: the array then contains the whole subject as its single token.

Notes:

- Zero-length matches split at every position: `a*` against `bbb` gives
  `""`, `"b"`, `"b"`, `"b"`, `""` — five tokens.
- A match that covers the whole subject yields two empty tokens.
- Leading and trailing matches yield empty edge tokens; interior empty tokens
  come from adjacent matches.
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
  match ASCII only. Bytes that form a non-ASCII character (for example the
  UTF-8 bytes `C2 A5`) never match `\d`.
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
failure; each family reports it with a fixed sentinel.

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

Notes:

- For `sx_reg_match_all` and `sx_reg_split`, "no match" and allocation
  failure both yield `NULL`; the array family cannot distinguish them.
- For the replace family, "no match" is a normal success: you get an
  unchanged copy of `h`.
- Arrays are `malloc`'ed and released with `free()`; buffers returned by the
  replace family are `sx_t *` and released with `sx_free()`.

## Thread safety

- A compiled `sx_reg_t` is immutable after `sx_reg_compile`. It has no
  mutable state and the wrapper keeps no global state, so one handle can be
  used by several threads at the same time.
- Each match/split call creates its own PCRE2 match-data block
  (`pcre2_match_data_create_from_pattern`) and frees it before returning; the
  replace calls delegate to `pcre2_substitute`, which also uses per-call
  match data. Two threads never share match state.
- A subject buffer is plain memory with no locking: concurrent readers are
  fine, but any mutation requires external synchronization.
- Views and arrays are snapshots into a subject. Do not edit the subject
  while a thread is still reading views from it, and do not free a handle
  while another thread is using it.

## Performance

| Call | PCRE2 work | Wrapper allocation | Result |
| --- | --- | --- | --- |
| `sx_reg_compile` | one compile | code plus handle | handle |
| `sx_reg_is_match` | one match call | match data only | `0` / `1` |
| `sx_reg_match` | one match call | match data only | one view |
| `sx_reg_match_all` | two passes over the scan | match data plus one exact-size array | array |
| `sx_reg_split` | two passes over the scan | match data plus one exact-size array | array |
| `sx_reg_replace*` | two substitute calls | internal PCRE2 match data, one temporary buffer, one `sx_t` | new buffer |

Notes:

- Compile once: pattern compilation costs far more than a single match, so
  compiling inside a loop is the one avoidable performance mistake.
- `sx_reg_is_match` is the cheapest existence test; `sx_reg_match` adds only
  the returned view.
- `sx_reg_match_all` and `sx_reg_split` scan twice (count, then fill) so the
  result array is allocated exactly once and never grows by `realloc`.
- The replace family probes the exact output length with a first
  `pcre2_substitute` call, then fills a fitting buffer with a second call;
  the buffer is copied once into the returned `sx_t`.
- Every call allocates and frees match data; no match data is cached between
  calls, and the wrapper does not enable PCRE2 JIT or any compile option.
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

- **8-bit, non-UTF**: the pattern and subject are bytes, character classes
  are ASCII, and no UTF validation happens. In the default (no `DOTALL`)
  mode `.` does not match newline.
- **No option flags**: the wrapper passes no compile options and no match
  options, and no match context. PCRE2 features that are expressed inside
  the pattern, such as inline options, still work; case-insensitive
  matching, multiline matching and similar behavior are requested with
  PCRE2's in-pattern syntax.
- **PCRE2 defaults govern**: leftmost-first alternation, greedy/lazy
  quantifiers, anchors, backreferences and all other pattern semantics are
  PCRE2's, not the wrapper's.
- The wrapper does not expose PCRE2 error codes or compile error offsets.

Pattern syntax and semantics are documented by the PCRE2 project at
[pcre.org](https://www.pcre.org/); this library does not re-document them.
The bundled PCRE2 version is 10.46; the dependency is BSD-3-Clause, with
attribution in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

---

[← Previous](api-search.md) · [Docs home](README.md) · [Next →](design.md)
