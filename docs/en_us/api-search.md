English · [中文](../zh_cn/api-search.md)

[Docs home](README.md) › API reference: search, compare & split

# Search, compare and split

The complete reference for the search family (`sx_find*`, `sx_rfind*`,
`sx_find_all*`), the comparison family (`sx_cmp*`, `sx_starts_with*`,
`sx_ends_with*`) and `sx_split`. All of them are byte-exact and work on
content lengths, so embedded NUL bytes are ordinary data. Buffer
creation, mutation and views are documented in
[Buffers and views](api-buffer.md).

## Conventions

**Needle forms.** Every search family has the same four needle shapes:

| Suffix | Argument type | Bytes matched | NUL behavior |
| --- | --- | --- | --- |
| none | `const sx_t *` | the buffer's content bytes | embedded NUL is data |
| `_view` | `sx_view_t` | `v.len` bytes at `v.ref->ptr + v.offset` | embedded NUL is data |
| `_cstr` | `const char *` | `strlen(...)` bytes | the first NUL ends the needle |
| `_char` | `char` | exactly one byte | `'\0'` means a NUL content byte |

The owned and view forms are binary-safe: they read exactly the
needle's length, so an embedded `'\0'` is matched as data. The `_cstr`
forms use `strlen`, so `sx_find_cstr(h, "a\0c")` actually searches for
`"a"`; use an owned or view needle to search binary patterns. An empty
needle (length 0) is never found.

**Bounded regions.** With `H` = the haystack's content length and a needle of `m` bytes, a
match is a window `[p, p + m)` that must lie entirely inside the
searched region:

| Function | Search region | Window requirement |
| --- | --- | --- |
| `sx_find*`, `sx_rfind*` (unbounded) | `[0, H)` | `p + m <= H` |
| `sx_find_view_b(h, needle, boundary)` | `[boundary, H)` | `p >= boundary` and `p + m <= H` |
| `sx_rfind_view_b(h, needle, boundary)` | `[0, min(boundary, H))` | `p + m <= min(boundary, H)` |

The two bounded forms take a `size_t boundary` and differ in direction:
`sx_find_view_b` bounds the region on the left and excludes any window
that starts before `boundary`; `sx_rfind_view_b` bounds it on the right
and excludes any window that ends after the boundary. Concrete cases
from the tests, with `h = "ababa"` and needle `"ba"`:

- `sx_find_view_b(h, nd, 2)` is 3 — the match at offset 1 starts before
  the boundary and does not count; boundary 4 gives `(size_t)-1`
  because the remaining region `"a"` is too small for one window.
- `sx_rfind_view_b(h, nd, 3)` is 1 — the match at 3 ends at 5, past the
  boundary; boundary 2 gives `(size_t)-1`; with needle `"aba"` and
  boundary 3 the window `[0, 3)` ends exactly at the boundary and the
  result is 0.
- For `sx_find_view_b`, `boundary >= H` is an empty region and returns
  `(size_t)-1`. For `sx_rfind_view_b`, a boundary at or above `H`
  clamps to `H`, so it behaves like an unbounded rightmost search.

**Binary safety.** The haystack is always searched by content length
(`H`), never as a C string: embedded `'\0'` bytes are ordinary bytes,
and the terminator at `ptr[H]` is outside the searched region. In
particular `sx_find_char(h, '\0')` reports only embedded NUL content
bytes, never the terminator. Owned and view needles are read by length
too; only `_cstr` needles stop at the first NUL.

**Sentinels and NULL.**

| Family | Missing result / rejected input |
| --- | --- |
| `sx_find*`, `sx_rfind*` | `(size_t)-1` |
| `sx_find_all*` | `NULL` |
| `sx_cmp*` | `0` when an operand is NULL |
| `sx_starts_with*`, `sx_ends_with*` | `0` |
| `sx_split` | `NULL` |

The search functions check their pointer arguments: a NULL `h` or a
NULL needle argument returns the family sentinel instead of crashing.
For the view variants, `v.ref == NULL` is out of contract — the
pointer is formed before any length check, so always pass a view of a
live buffer. Allocation failure is reported with the same sentinel as
"no match" by `sx_find_all*` and `sx_split`; the two cases are
indistinguishable by contract. `(size_t)-1` cannot collide with a real
offset because offsets are below `INT_MAX` content bytes.

**Search strategy.** One-byte needles use `memchr` (forward) or a
byte-wise backward scan, glibc's `memrchr` when available, and allocate
nothing. Longer needles use KMP: a temporary prefix table of `m`
`size_t` entries is allocated for the duration of the call, so one
search is O(n + m) worst case. `sx_find_all*` additionally returns a
malloc'ed result array. `sx_find_all*` reports every occurrence,
overlapping ones included.

## Choosing the right call

| You need | Call |
| --- | --- |
| leftmost occurrence in the whole string | `sx_find*` |
| rightmost occurrence in the whole string | `sx_rfind*` |
| every occurrence, overlaps included | `sx_find_all*` |
| leftmost occurrence at or after a boundary | `sx_find_view_b` |
| rightmost occurrence ending at or before a boundary | `sx_rfind_view_b` |
| equality or ordering | `sx_cmp*` |
| prefix or suffix test | `sx_starts_with*`, `sx_ends_with*` |
| split into tokens | `sx_split` |

## Leftmost search: `sx_find*`

### sx_find

```c
size_t sx_find(const sx_t *h, const sx_t *needle);
```

Finds the leftmost occurrence of another buffer's content in `h`.

- **Parameters:** `h` — haystack; `needle` — buffer whose content is
  searched for.
- **Returns:** the byte offset of the leftmost match, or `(size_t)-1`
  when there is none.
- **Ownership:** none; neither buffer is modified. A temporary KMP
  prefix table is allocated and freed inside the call for needles longer
  than one byte.
- **Edge cases:** NULL `h` or `needle` returns `(size_t)-1`; an empty
  needle returns `(size_t)-1`; a needle longer than `h` never matches.
  The match is byte-exact over content lengths, so embedded NULs match.

### sx_find_view

```c
size_t sx_find_view(const sx_t *h, sx_view_t needle);
```

Finds the leftmost occurrence of a view's bytes in the whole string.

- **Parameters:** `h` — haystack; `needle` — window whose `needle.len`
  bytes are searched for.
- **Returns:** the byte offset of the leftmost match, or `(size_t)-1`.
- **Ownership:** none; only reads.
- **Edge cases:** equivalent to `sx_find_view_b(h, needle, 0)`. An
  empty view (`len == 0`) is never found. `needle.ref == NULL` or an
  out-of-bounds window is out of contract.

### sx_find_view_b

```c
size_t sx_find_view_b(const sx_t *h, sx_view_t needle, size_t boundary);
```

Finds the leftmost occurrence of a view's bytes at or after `boundary`.

- **Parameters:** `h` — haystack; `needle` — window to search for;
  `boundary` — region start offset.
- **Returns:** the byte offset of the leftmost match inside
  `[boundary, H)`, or `(size_t)-1`.
- **Ownership:** none; only reads.
- **Edge cases:** a match must start at or after `boundary` and end at
  or before `H`; a window that straddles the boundary is not a match.
  `boundary >= H`, a region too small for one window, and an empty
  needle all return `(size_t)-1`. `needle.ref == NULL`, an
  out-of-bounds window, or a `boundary + needle.len` that overflows
  `size_t` is out of contract.

```c
sx_t *h = sx_new_from_cstr("ababa");
sx_t *ba = sx_new_from_cstr("ba");
size_t at = sx_find_view_b(h, sx_view(ba), 2); /* 3, not 1 */
sx_free(ba);
sx_free(h);
```

### sx_find_cstr

```c
size_t sx_find_cstr(const sx_t *h, const char *needle);
```

Finds the leftmost occurrence of a C string in `h`.

- **Parameters:** `h` — haystack; `needle` — NUL-terminated pattern,
  read with `strlen`.
- **Returns:** the byte offset of the leftmost match, or `(size_t)-1`.
- **Ownership:** none; only reads.
- **Edge cases:** NULL `h` or `needle` returns `(size_t)-1`; an empty
  needle returns `(size_t)-1`. The needle cannot contain a literal NUL;
  use `sx_find_view` for binary patterns. The haystack may contain NULs.

### sx_find_char

```c
size_t sx_find_char(const sx_t *h, char c);
```

Finds the leftmost occurrence of a single byte.

- **Parameters:** `h` — haystack; `c` — the byte to search for.
- **Returns:** the byte offset of the leftmost occurrence, or
  `(size_t)-1`.
- **Ownership:** none; only reads.
- **Edge cases:** NULL `h` returns `(size_t)-1`. The search covers the
  content only (`[0, H)`), so `c == '\0'` finds embedded NUL content
  bytes but never the terminator.

## Rightmost search: `sx_rfind*`

### sx_rfind

```c
size_t sx_rfind(const sx_t *h, const sx_t *needle);
```

Finds the rightmost occurrence of another buffer's content in `h`.

- **Parameters:** `h` — haystack; `needle` — buffer whose content is
  searched for.
- **Returns:** the byte offset of the rightmost match, or `(size_t)-1`.
- **Ownership:** none; neither buffer is modified. A temporary KMP
  prefix table is allocated and freed inside the call for needles longer
  than one byte.
- **Edge cases:** NULL `h` or `needle` returns `(size_t)-1`; an empty
  needle returns `(size_t)-1`; a needle longer than `h` never matches.
  Byte-exact and binary-safe.

### sx_rfind_view

```c
size_t sx_rfind_view(const sx_t *h, sx_view_t needle);
```

Finds the rightmost occurrence of a view's bytes in the whole string.

- **Parameters:** `h` — haystack; `needle` — window whose
  `needle.len` bytes are searched for.
- **Returns:** the byte offset of the rightmost match, or `(size_t)-1`.
- **Ownership:** none; only reads.
- **Edge cases:** equivalent to `sx_rfind_view_b(h, needle, H)`. An
  empty view is never found. `needle.ref == NULL` or an out-of-bounds
  window is out of contract.

### sx_rfind_view_b

```c
size_t sx_rfind_view_b(const sx_t *h, sx_view_t needle, size_t boundary);
```

Finds the rightmost occurrence of a view's bytes that ends at or before
`boundary`.

- **Parameters:** `h` — haystack; `needle` — window to search for;
  `boundary` — region end offset (exclusive).
- **Returns:** the byte offset of the rightmost match whose window ends
  inside `[0, min(boundary, H))`, or `(size_t)-1`.
- **Ownership:** none; only reads.
- **Edge cases:** a match must end at or before `boundary`; one that
  sticks out past the boundary is not a match. A boundary of 0, a
  region too small for one window, and an empty needle all return
  `(size_t)-1`. A boundary at or above `H` clamps to `H` and behaves
  like an unbounded rightmost search. `needle.ref == NULL` and
  out-of-bounds windows are out of contract.

```c
sx_t *h = sx_new_from_cstr("ababa");
sx_t *aba = sx_new_from_cstr("aba");
size_t at = sx_rfind_view_b(h, sx_view(aba), 3); /* 0: ends exactly at 3 */
sx_free(aba);
sx_free(h);
```

### sx_rfind_cstr

```c
size_t sx_rfind_cstr(const sx_t *h, const char *needle);
```

Finds the rightmost occurrence of a C string in `h`.

- **Parameters:** `h` — haystack; `needle` — NUL-terminated pattern,
  read with `strlen`.
- **Returns:** the byte offset of the rightmost match, or `(size_t)-1`.
- **Ownership:** none; only reads.
- **Edge cases:** NULL `h` or `needle` returns `(size_t)-1`; an empty
  needle returns `(size_t)-1`. The needle cannot contain a literal NUL.

### sx_rfind_char

```c
size_t sx_rfind_char(const sx_t *h, char c);
```

Finds the rightmost occurrence of a single byte.

- **Parameters:** `h` — haystack; `c` — the byte to search for.
- **Returns:** the byte offset of the rightmost occurrence, or
  `(size_t)-1`.
- **Ownership:** none; only reads.
- **Edge cases:** NULL `h` returns `(size_t)-1`. The search covers the
  content only, so `c == '\0'` never matches the terminator.

## All occurrences: `sx_find_all*`

These functions return a malloc'ed `size_t` array in "count first" form:

```text
r[0]          number of matches
r[1 .. r[0]]  ascending byte offsets of every match
```

Overlapping matches are all reported: `"aa"` inside `"aaaa"` is found at
offsets 0, 1 and 2. There is no end guard (unlike `sx_split` and
`sx_reg_match_all`); the count is authoritative.

- **Returns:** `NULL` when there is no match — including an empty needle
  — when an argument is NULL, or on allocation failure. The two failure
  causes are indistinguishable by contract.
- **Ownership:** the caller owns the array and releases it with `free`
  (not `sx_free`). Do not read past `r[0] + 1` entries.

```c
sx_t *h = sx_new_from_cstr("ababab");
size_t *hits = sx_find_all_cstr(h, "ab");
/* hits[0] == 3; hits[1] == 0, hits[2] == 2, hits[3] == 4 */
free(hits);
sx_free(h);
```

### sx_find_all

```c
size_t *sx_find_all(const sx_t *h, const sx_t *needle);
```

Collects every occurrence of another buffer's content.

- **Parameters:** `h` — haystack; `needle` — buffer whose content is
  searched for.
- **Returns:** the count-first `size_t` array, or `NULL` (no match,
  empty needle, NULL argument, allocation failure).
- **Ownership:** caller-owned; release with `free`.
- **Edge cases:** binary-safe; offsets are ascending and include
  overlapping matches.

### sx_find_all_view

```c
size_t *sx_find_all_view(const sx_t *h, sx_view_t needle);
```

Collects every occurrence of a view's bytes.

- **Parameters:** `h` — haystack; `needle` — window to search for.
- **Returns:** the count-first `size_t` array, or `NULL` (no match,
  empty view, NULL argument, allocation failure).
- **Ownership:** caller-owned; release with `free`.
- **Edge cases:** `needle.ref == NULL` or an out-of-bounds window is out
  of contract.

### sx_find_all_cstr

```c
size_t *sx_find_all_cstr(const sx_t *h, const char *needle);
```

Collects every occurrence of a C string.

- **Parameters:** `h` — haystack; `needle` — NUL-terminated pattern,
  read with `strlen`.
- **Returns:** the count-first `size_t` array, or `NULL` (no match,
  empty needle, NULL argument, allocation failure).
- **Ownership:** caller-owned; release with `free`.
- **Edge cases:** the needle cannot contain a literal NUL; the haystack
  can.

### sx_find_all_char

```c
size_t *sx_find_all_char(const sx_t *h, char c);
```

Collects every occurrence of a single byte.

- **Parameters:** `h` — haystack; `c` — the byte to search for.
- **Returns:** the count-first `size_t` array, or `NULL` (no match, NULL
  argument, allocation failure).
- **Ownership:** caller-owned; release with `free`.
- **Edge cases:** searches the content only, so a `'\0'` needle finds
  embedded NUL content bytes, never the terminator.

## Comparison: `sx_cmp*`

### sx_cmp

```c
int sx_cmp(const sx_t *h, const sx_t *other);
```

Compares two buffers lexicographically over their content bytes.

- **Parameters:** `h` — left operand; `other` — right operand.
- **Returns:** a negative value if `h` sorts before `other`, 0 if they
  are equal, a positive value if `h` sorts after. Only the sign is
  meaningful.
- **Ownership:** none; neither buffer is modified.
- **Edge cases:** comparison is byte-wise in `memcmp` order (unsigned
  byte values), so `"10" < "2"` and `"Z" < "中"`. A shorter string that
  is a prefix of the other sorts first (`"abc" < "abcd"`). Embedded NUL
  bytes compare as data. If either operand is NULL the result is 0
  (treated as equal) — never rely on that as a meaningful order.

### sx_cmp_view

```c
int sx_cmp_view(const sx_t *h, sx_view_t other);
```

Compares a buffer with a view's bytes.

- **Parameters:** `h` — left operand; `other` — right operand window;
  `other.len` bytes are compared.
- **Returns:** negative, 0 or positive as in `sx_cmp`.
- **Ownership:** none; only reads.
- **Edge cases:** NULL `h` returns 0. `other.ref == NULL` or an
  out-of-bounds window is out of contract.

### sx_cmp_cstr

```c
int sx_cmp_cstr(const sx_t *h, const char *s);
```

Compares a buffer with a C string.

- **Parameters:** `h` — left operand; `s` — NUL-terminated right
  operand, read with `strlen`.
- **Returns:** negative, 0 or positive as in `sx_cmp`.
- **Ownership:** none; only reads.
- **Edge cases:** NULL `h` or `s` returns 0. An embedded NUL in `s`
  shortens the compared range.

## Prefix and suffix predicates

### sx_starts_with

```c
int sx_starts_with(const sx_t *h, const sx_t *prefix);
```

Tests whether `h` starts with another buffer's content.

- **Parameters:** `h` — buffer; `prefix` — buffer whose content is the
  prefix.
- **Returns:** 1 if the first `len(prefix)` content bytes of `h` equal
  the prefix byte for byte, otherwise 0.
- **Ownership:** none; only reads.
- **Edge cases:** an empty prefix always matches; a prefix longer than
  `h` never matches. Byte-exact and binary-safe. NULL `h` or `prefix`
  returns 0.

### sx_starts_with_view

```c
int sx_starts_with_view(const sx_t *h, sx_view_t prefix);
```

Tests whether `h` starts with a view's bytes.

- **Parameters:** `h` — buffer; `prefix` — window read by
  `prefix.len` bytes.
- **Returns:** 1 on a match, otherwise 0.
- **Ownership:** none; only reads.
- **Edge cases:** an empty view always matches. NULL `h` returns 0;
  `prefix.ref == NULL` or an out-of-bounds window is out of contract.

### sx_starts_with_cstr

```c
int sx_starts_with_cstr(const sx_t *h, const char *prefix);
```

Tests whether `h` starts with a C string.

- **Parameters:** `h` — buffer; `prefix` — NUL-terminated prefix, read
  with `strlen`.
- **Returns:** 1 on a match, otherwise 0.
- **Ownership:** none; only reads.
- **Edge cases:** the empty string always matches; NULL `h` or `prefix`
  returns 0.

### sx_ends_with

```c
int sx_ends_with(const sx_t *h, const sx_t *suffix);
```

Tests whether `h` ends with another buffer's content.

- **Parameters:** `h` — buffer; `suffix` — buffer whose content is the
  suffix.
- **Returns:** 1 if the last `len(suffix)` content bytes of `h` equal
  the suffix byte for byte, otherwise 0.
- **Ownership:** none; only reads.
- **Edge cases:** an empty suffix always matches; a suffix longer than
  `h` never matches. Byte-exact and binary-safe. NULL `h` or `suffix`
  returns 0.

### sx_ends_with_view

```c
int sx_ends_with_view(const sx_t *h, sx_view_t suffix);
```

Tests whether `h` ends with a view's bytes.

- **Parameters:** `h` — buffer; `suffix` — window read by
  `suffix.len` bytes.
- **Returns:** 1 on a match, otherwise 0.
- **Ownership:** none; only reads.
- **Edge cases:** an empty view always matches. NULL `h` returns 0;
  `suffix.ref == NULL` or an out-of-bounds window is out of contract.

### sx_ends_with_cstr

```c
int sx_ends_with_cstr(const sx_t *h, const char *suffix);
```

Tests whether `h` ends with a C string.

- **Parameters:** `h` — buffer; `suffix` — NUL-terminated suffix, read
  with `strlen`.
- **Returns:** 1 on a match, otherwise 0.
- **Ownership:** none; only reads.
- **Edge cases:** the empty string always matches; NULL `h` or `suffix`
  returns 0.

## Splitting: sx_split

### sx_split

```c
sx_view_t *sx_split(const sx_t *h, const char *sep);
```

Splits a buffer on a C-string separator into an array of token views.
The declaration lives in `sx.h` (the advanced layer), not in
`sx_core.h`.

- **Parameters:** `h` — buffer to split; `sep` — separator C string,
  read with `strlen`. An empty separator is refused, not treated as
  "every byte".
- **Returns:** a malloc'ed `sx_view_t` array with the classic `char **`
  shape, or `NULL` for NULL `h`, NULL `sep`, an empty `sep`, or
  allocation failure.
- **Ownership:** the caller owns the array and releases it with `free`
  (not `sx_free`). The tokens are views into `h` and must never be
  freed individually.
- **Contract notes:**
  - The array has exactly `count + 1` elements: `arr[0] .. arr[count-1]`
    are the tokens in order, and `arr[count]` is the end guard whose
    `.ref == NULL`. Iterate with
    `for (size_t i = 0; arr[i].ref != NULL; i++)`.
  - `count == 1 + (number of separator occurrences)`. Separators are
    matched left to right and are non-overlapping: after a hit the scan
    resumes past it, so `"aaaa"` split on `"aa"` yields three empty
    tokens, not two.
  - Empty fields are kept at every edge and in the interior: `"a,,b"`
    gives `"a"`, `""`, `"b"`; a leading or trailing separator produces
    an empty first or last token; an empty `h` splits into the single
    token `""`; a string equal to the separator splits into two empty
    tokens.
  - An empty token is not the end guard: it has `ref != NULL` and
    `len == 0`, while the guard has `ref == NULL`.
  - Tokens alias `h`'s content (`ref == h->ref`, `offset`, `len`):
    copying nothing, modifying nothing, and reallocating nothing during
    the call, so the content pointer is bitwise unchanged. Later edits
    of `h` invalidate the tokens; do not edit `h` while they are in
    use.
  - Matching is byte-exact, so tokens may contain embedded NUL bytes
    (the separator itself, being a C string, cannot). Print such a
    token with `fwrite` or `"%.*s"` and its length, not with `"%s"`.

The regex layer's `sx_reg_split` returns exactly this shape; see
[Regular expressions](regex.md).

Full example — split, iterate to the end guard, then release the array
and the buffer:

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  sx_t *csv = sx_new_from_cstr("alpha,beta,,gamma,");
  sx_view_t *fields = sx_split(csv, ",");

  if (!fields)
    return 1;

  for (size_t i = 0; fields[i].ref != NULL; i++)
    printf("field %zu: \"%.*s\" (offset %zu, len %zu)\n", i,
           (int)fields[i].len, sx_buf_view_mut(fields[i]),
           fields[i].offset, fields[i].len);

  free(fields); /* the array, not the tokens */
  sx_free(csv);
  return 0;
}
```

Expected output (the trailing separator produces an empty last field):

```text
field 0: "alpha" (offset 0, len 5)
field 1: "beta" (offset 6, len 4)
field 2: "" (offset 11, len 0)
field 3: "gamma" (offset 12, len 5)
field 4: "" (offset 18, len 0)
```

---

[← Previous](api-buffer.md) · [Docs home](README.md) · [Next →](regex.md)
