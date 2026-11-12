English · [中文](../zh_cn/guide.md)

[Docs home](README.md) › User guide

# User guide

For C programmers who can build and link String X (see
[Getting started](getting-started.md)). Signatures and details are in the API
reference; this page covers the model, recipes, and out-of-contract calls.

## Mental model

### The buffer

Every string is one `sx_t` handle pointing at one backing allocation:

```text
sx_t* ──► _sx { ptr: [ h e l l o \0 . . . . ] , len: 5, cap: 32 }
```

- `ptr[0 .. len-1]` are content bytes; `ptr[len]` is always `'\0'`.
- `len` counts bytes, not characters, and does not stop at an embedded NUL.
- `cap` is the allocation size in bytes and always satisfies `cap >= len + 1`.
- `sx_new()` starts with capacity 32; `sx_new_with_cap(n)` has exactly capacity
  `n` for `n >= 1` (`n - 1` content bytes max; `n == 0` still allocates one).

The handle is stable: growth may move the backing array, never the `sx_t`.
Treat the handle as opaque, and do not copy the struct by value.

### Views

A view aliases a slice of a buffer without copying:

```text
sx_view_t { ref: the same _sx, offset: 1, len: 3 } ──► "ell"
```

- `ref == NULL` means "no view": the sentinel for a failed match or for "none".
- Creating or moving a view allocates nothing, so a view is never freed.
- A view is a snapshot of `(offset, len)`: later edits never adjust it, and a
  length change invalidates it.

## Ownership

One place owns a buffer at a time; per-call result and release:

| Call | Result | Release with |
| --- | --- | --- |
| `sx_new`, `sx_new_from_cstr`, `sx_new_from_view`, `sx_new_with_cap`, `sx_dup` | new buffer | `sx_free` |
| `sx_substr` | new buffer | `sx_free` |
| `sx_reg_replace`, `sx_reg_replace_first` | new buffer | `sx_free` |
| `sx_append`, `sx_write`, `sx_insert` (the `sx_t *owned` forms) | `self`; the argument is consumed and freed | `sx_free` on `self` |
| `sx_find_all*`, `sx_split`, `sx_reg_match_all`, `sx_reg_split` | malloc'ed array | `free` |
| `sx_view`, `sx_view_*`, `sx_reg_match`, tokens inside the arrays above | view | nothing |
| `sx_reg_compile` | compiled pattern | `sx_reg_free` |

- Edits return `self` and chain: `sx_append_cstr(sx_append_cstr(s, "a"), "b")`
  returns `s`; `sx_free` calls equal buffers created, not edits.
- A consuming call ends the source: after `sx_append(s, part)`, `part` must not
  be used or freed again (content copied; released even on allocation failure).

`sx_append_cstr`, `sx_append_view`, `sx_write_cstr`, `sx_write_view`,
`sx_insert_cstr`, `sx_insert_view` take a string or view, never a buffer.

## Binary safety and the cstr variants

String X is binary-safe wherever an explicit length is carried:

| Input form | Length rule | Embedded NUL |
| --- | --- | --- |
| `sx_*_cstr`, `sx_new_from_cstr`, `sx_split` separator, regex pattern | `strlen` | ends the input |
| `sx_view_t` forms, `sx_new_from_view`, owned `sx_t *` forms | explicit `len` | ordinary byte |
| regex subject (`sx_t *h`) | byte length of `h` | ordinary byte |

`sx_append_cstr` stops at a NUL; `sx_append_view`, `sx_append` and
`sx_new_from_view` copy every content byte. Print a binary buffer by length:
`fwrite(sx_buf_mut(h), 1, (size_t)sx_len(h), stdout)`, not `printf("%s", ...)`.

The same split applies to compare and search: `_cstr` forms use `strlen`, owned
and view forms compare content bytes. Regex subjects match by byte length;
patterns are C strings, so a pattern cannot contain a literal NUL.

## Invariants and stale pointers

- After every public call, `ptr[len] == '\0'`, `cap >= len + 1`, and `len`
  counts content bytes. Only a length-changing call moves those values.
- Any growing edit may `realloc` the backing array; every `char *` from
  `sx_buf_mut`/`sx_buf_view_mut` is then stale — re-fetch it. The `sx_t *`
  handle stays valid.
- Writes through `sx_buf_mut` must stay inside `[0, len)` unless a matching
  length-changing call follows; `ptr[len]` is the terminator, not a spare byte.
- Views are snapshots: do not edit a buffer while its views (or `sx_split` /
  `sx_reg_*` arrays) are alive; re-derive views after any length change.
- Accessors return `int` (`sx_len`, `sx_len_view`, `sx_cap`) while offsets are
  `size_t`. Buffers with more than `INT_MAX` content bytes are outside the
  tested contract.
- Capacity only grows: `sx_truncate`, `sx_erase`, `sx_clear` free nothing.

## Errors and sentinels

There are no exceptions and no error codes. Each family has a sentinel:

| Situation | Result |
| --- | --- |
| `sx_find*` / `sx_rfind*`: no match | `(size_t)-1` |
| `sx_find_all*`: no match, empty needle, or allocation failure | `NULL` array |
| `sx_split`: NULL handle or separator, empty separator, allocation failure | `NULL` array |
| `sx_reg_match`: no match | view with `ref == NULL` |
| `sx_reg_match_all` / `sx_reg_split`: no match or allocation failure | `NULL` array |
| `sx_reg_replace*`: NULL argument or rejected template | `NULL` buffer |
| `sx_write*`: range end overflows `size_t` or cannot be reserved | `NULL`; `self` unchanged (owned input consumed) |
| allocation failure | `NULL` |

`NULL` means "no result"; for the array-returning calls, allocation failure is
indistinguishable from "no match". The documented NULL-argument handling is
narrow:

- `sx_new_from_cstr(NULL)` and `sx_dup(NULL)` return `NULL`.
- The C-string and owned-buffer arguments of append/write/insert return `NULL`
  and leave `self` unchanged.
- `sx_write*` returns `NULL` when `idx + len` overflows `size_t` or the end
  cannot be reserved; `self` keeps its content and an owned argument is still
  freed.
- `sx_cmp*`, `sx_starts_with*` and `sx_ends_with*` return `0`.
- `sx_find*` / `sx_rfind*` return `(size_t)-1`; `sx_find_all*` return `NULL`.
- `sx_split` returns `NULL` for a NULL handle/separator or an empty separator.
- `sx_reg_compile(NULL)` returns `NULL`; `sx_reg_free(NULL)` is a no-op; the
  match, replace and split families return their not-found value for NULL
  arguments.

Everything else rejects NULL handles: `sx_len`, `sx_cap`, `sx_buf_mut`,
`sx_view`, `sx_free`, `sx_substr`, `sx_reserve`, `sx_erase` and the rest
dereference it, so NULL is out of contract and may crash. `sx_free(NULL)`
is not a no-op, and a `ref == NULL` view cannot be passed to view functions.

## Recipes

Each recipe is a complete C23 program; build it like `hello.c` in
[Getting started](getting-started.md), or with:

```sh
cc recipe.c $(pkg-config --cflags --libs libstringx) -o recipe
```

### Build a string from pieces

Mix C strings, views and owned buffers; the owned buffer is consumed.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *path = sx_new();
  path = sx_append_cstr(path, "/usr");
  path = sx_append_cstr(path, "/local");
  sx_t *part = sx_new_from_cstr("/bin");
  path = sx_append(path, part);            /* part is consumed */
  path = sx_insert_cstr(path, 0, "/opt:"); /* prefix */
  printf("%s (%d bytes)\n", sx_buf_mut(path), sx_len(path));
  sx_free(path);
  return 0;
}
```

```text
/opt:/usr/local/bin (19 bytes)
```

### Search and extract a substring

Check the `(size_t)-1` sentinel before using an offset. `sx_substr` copies
(independent result); `sx_find_view_b` searches only the region from a boundary.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *line = sx_new_from_cstr("key=value;flag=on");
  size_t eq = sx_find_cstr(line, "=");
  size_t semi = sx_find_cstr(line, ";");
  sx_t *nod = sx_new_from_cstr("=");
  size_t later = sx_find_view_b(line, sx_view(nod), 10); /* region [10, 17) */

  if (eq == (size_t)-1 || semi == (size_t)-1)
    return 1;
  sx_t *key = sx_substr(line, 0, eq);
  sx_t *value = sx_substr(line, eq + 1, semi - eq - 1);
  printf("first '='=%zu, next '=' from 10=%zu\n", eq, later);
  printf("key=%s value=%s\n", sx_buf_mut(key), sx_buf_mut(value));
  sx_free(key);
  sx_free(value);
  sx_free(nod);
  sx_free(line);
  return 0;
}
```

```text
first '='=3, next '=' from 10=14
key=key value=value
```

### Split and iterate

`sx_split` returns a malloc'ed view array whose end guard has `ref == NULL`;
empty fields are kept. `free` the array; keep the buffer alive while reading
tokens.

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  sx_t *csv = sx_new_from_cstr("id,,name,");
  sx_view_t *fields = sx_split(csv, ",");
  if (!fields)
    return 1;
  for (size_t i = 0; fields[i].ref != NULL; i++)
    printf("[%zu] \"%.*s\" (offset %zu, len %zu)\n", i,
           (int)fields[i].len, sx_buf_view_mut(fields[i]),
           fields[i].offset, fields[i].len);
  free(fields);
  sx_free(csv);
  return 0;
}
```

```text
[0] "id" (offset 0, len 2)
[1] "" (offset 3, len 0)
[2] "name" (offset 4, len 4)
[3] "" (offset 9, len 0)
```

### Trim input

`sx_view_trim` drops the ASCII whitespace set `{space, \t, \v, \f, \r, \n}` from
both edges without copying. Copy the trimmed view into an owned buffer when the
buffer may change later.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *raw = sx_new_from_cstr("  \t hello world \r\n");
  sx_view_t trimmed = sx_view_trim(sx_view(raw));
  printf("[%.*s]\n", sx_len_view(trimmed), sx_buf_view_mut(trimmed));

  sx_t *copy = sx_new_from_view(trimmed); /* independent copy */
  printf("[%s] len=%d\n", sx_buf_mut(copy), sx_len(copy));
  sx_free(copy);
  sx_free(raw);
  return 0;
}
```

```text
[hello world]
[hello world] len=11
```

A view that consists only of whitespace trims to an empty view at its slot's
end; for the whole-buffer view that is `(offset = len(h), len = 0)`.

### In-place edits

`sx_write` overwrites without shrinking, `sx_erase` removes a range, and
`sx_insert` shifts the tail. All of them return `self`.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *s = sx_new_from_cstr("the quick brown fox");
  s = sx_write_cstr(s, 10, "green");         /* same length: overwrite */
  s = sx_erase(s, 0, 4);                     /* remove "the " */
  s = sx_insert_cstr(s, 0, "red ");          /* prepend */
  s = sx_truncate(s, (size_t)sx_len(s) - 4); /* drop " fox" */
  printf("%s (len %d)\n", sx_buf_mut(s), sx_len(s));
  s = sx_clear(s);                           /* len 0, capacity kept */
  printf("after clear: len=%d cap=%d\n", sx_len(s), sx_cap(s));
  sx_free(s);
  return 0;
}
```

```text
red quick green (len 15)
after clear: len=0 cap=20
```

`sx_erase` with `idx + n > len` is a no-op that returns `self`, as is `n == 0`.
`sx_insert` requires `0 <= idx <= len`; `idx > len` is out of contract. Growing
`sx_truncate` and out-of-range `sx_write` leave the gap bytes unspecified.

### Regex extract, replace and split

Compile once, reuse, free last. Match and token results are views into the
subject; replace returns a new buffer and never modifies the subject.

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  sx_reg_t *re = sx_reg_compile("[a-z]+@[a-z.]+");
  if (!re)
    return 1;
  sx_t *msg = sx_new_from_cstr("bob@example.com;alice@example.org");

  sx_view_t m = sx_reg_match(re, msg); /* leftmost match */
  if (m.ref != NULL)
    printf("first: %.*s\n", (int)m.len, sx_buf_view_mut(m));

  sx_view_t *hits = sx_reg_match_all(re, msg); /* non-overlapping */
  for (size_t i = 0; hits && hits[i].ref != NULL; i++)
    printf("hit %zu: %.*s\n", i, (int)hits[i].len, sx_buf_view_mut(hits[i]));
  free(hits);

  sx_t *out = sx_reg_replace(re, msg, "<$0>"); /* $0 = whole match */
  printf("replaced: %s\n", sx_buf_mut(out));
  sx_free(out);

  sx_view_t *tokens = sx_reg_split(re, msg); /* gaps between matches */
  for (size_t i = 0; tokens && tokens[i].ref != NULL; i++)
    printf("token %zu: \"%.*s\"\n", i, (int)tokens[i].len,
           sx_buf_view_mut(tokens[i]));
  free(tokens);

  sx_free(msg);
  sx_reg_free(re);
  return 0;
}
```

```text
first: bob@example.com
hit 0: bob@example.com
hit 1: alice@example.org
replaced: <bob@example.com>;<alice@example.org>
token 0: ""
token 1: ";"
token 2: ""
```

## Pitfalls

These are the calls and habits the tests deliberately put out of contract, plus
documented edge cases that are easy to miss:

- Using a buffer after handing it to a consuming call: `sx_append(s, part)`,
  `sx_write(s, idx, part)` and `sx_insert(s, idx, part)` free `part`.
- Passing a buffer to itself via the owned forms (`sx_append(s, s)`,
  `sx_write(s, idx, s)`, `sx_insert(s, idx, s)`) is out of contract, and
  doubling in place uses `sx_append_view(s, sx_view(s))` or `sx_write_view`;
  `sx_insert_view` accepts a view into `s` only when the slice starts at or
  before `idx` (the tested case); a slice starting after `idx` is clobbered by
  the tail shift — copy it first.
- `sx_insert*` with `idx > len`: the tail shift underflows. The tested contract
  is `0 <= idx <= len`. `sx_write*` is different: it pads and extends.
- Reading unspecified bytes in `[old_len, n)` after a growing `sx_truncate`
  or `[len, idx)` after an out-of-range `sx_write`; write the gap first.
- Calling `sx_write*` with an `idx + len` near `SIZE_MAX` and assuming the
  write happened: arithmetic overflow is rejected with `NULL` and `self` stays
  unchanged. Check the return value.
- Editing a buffer while views or arrays from `sx_split`, `sx_reg_match_all`,
  `sx_reg_split` or `sx_find_all*` are alive, or keeping a view past a length
  change. Views are not adjusted and may point into freed memory after growth.
- Interpreting a `sx_find_all*` array beyond `arr[0] + 1` entries; its shape is
  `{count, positions...}`.
- Treating `sx_erase` with `idx + n > len` as an error. It is a documented no-op
  that returns `self`.
- Passing NULL handles to functions that do not check for them: `sx_free(NULL)`
  and `sx_len(NULL)` are out of contract — see
  [Errors and sentinels](#errors-and-sentinels).
- Forgetting the release call: `sx_free` for buffers, `free` for view arrays,
  `sx_reg_free` for compiled patterns. A view needs no release.
- `sx_split` with an empty separator returns `NULL`, not one token per byte.
- A regex pattern is a C string and cannot contain a literal NUL. Subjects can.
- `sx_new_from_cstr` and every `_cstr` parameter stop at the first NUL; use
  views for binary content.
- `sx_substr` requires `idx + n <= len(h)`; `sx_view_range` requires
  `off <= len(backing buffer)` (its `len` is clamped to the buffer end, `off` is
  not). Neither call clamps the precondition itself.
- Sharing one `sx_t` across threads without synchronization: no internal
  locking; only a compiled `sx_reg_t` is documented as immutable and shareable.

## Performance notes

- **Reserve for known append loops.** `sx_reserve(self, n)` guarantees
  `cap >= n + 1` without shrinking, so `n` content bytes fit without realloc;
  otherwise growth is amortized — doubling, then `+0x10000` at 64 KiB and up.
- **Views do not copy.** `sx_view`, `sx_view_advance` and the other view movers
  are O(1) and allocate nothing; use `sx_substr`/`sx_new_from_view` only for an
  independent buffer.
- **Search is linear.** One-byte needles use `memchr` / `memrchr`; longer
  needles use forward or reverse KMP, O(n + m). `sx_find_all*` reports
  overlapping matches and sizes its array to the match count.
- **Split and regex scans are two-pass.** The token or match array is counted
  first and allocated exactly once; the cost is two scans of the subject.
- **Compile a regex once.** `sx_reg_compile` is the expensive call; every match,
  replace and split reuses the compiled pattern and allocates only its own match
  data. `sx_reg_is_match` is the cheapest existence check.
- **Capacity is never returned.** `sx_truncate`, `sx_erase` and `sx_clear` keep
  the allocation: a cleared buffer is reusable, but a peak stays allocated until
  `sx_free`.

## FAQ

**Do I need PCRE2 to use buffers?** To build, yes: the source tree links
`pcre2-8` and `./make` needs `libpcre2-dev`. An installed `libstringx` needs
only `-lstringx`; the shared library carries the PCRE2 dependency.

**Is every function NULL-safe?** No. Only the cases listed in
[Errors and sentinels](#errors-and-sentinels) are defined; NULL handles are out
of contract everywhere else.

**Is String X UTF-8 aware?** No. It is a byte library: lengths, comparisons and
searches are byte-exact, and the regex layer runs in PCRE2's 8-bit non-UTF mode,
so `\d`, `\w`, `\s` and `\b` match ASCII only.

**How do I copy a view?** `sx_new_from_view(v)` returns an owned buffer with a
byte-exact copy of the view's `len` bytes.

**How do I get a substring?** `sx_view_range`/`sx_view_advance` give a zero-copy
view; `sx_substr(h, idx, n)` gives an owned copy.

**Why does `sx_len` return `int`?** That is the library's practical size limit:
content longer than `INT_MAX` bytes is outside the tested contract. Offsets are
`size_t`, so search and view positions stay full-width.

**Can I share a compiled regex between threads?** Yes: a compiled `sx_reg_t` is
immutable and each call uses its own match data. Sharing an `sx_t` buffer
between threads is not supported.

**What does a zero-length match look like?** `sx_reg_match` returns a view with
`ref != NULL` and `len == 0`; `sx_reg_match_all` and `sx_reg_split` report it
and scan one byte on.

---

[← Previous](getting-started.md) · [Docs home](README.md) · [Next →](api-buffer.md)
