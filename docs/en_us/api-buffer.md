English · [中文](../zh_cn/api-buffer.md)

[Docs home](README.md) › API reference: buffers & views

# Buffers and views

The core buffer and view API of `include/sx_core.h`: creation, state,
mutation, substrings, view navigation and trimming. Search, comparison and
splitting are in [Search, compare and split](api-search.md). Behavior
statements come from the public headers, the implementation and the
`tests/*.c` boundary comments.

## Conventions

**Headers and layers.** Include the one public header:

```c
#include <sx.h>
```

`sx.h` includes `sx_core.h` (buffers, views, search, comparison) and
declares `sx_split` and `sx_reg_*`. Link with `-lstringx`. Every function
on this page is declared in `sx_core.h`; `sx_raw.h` is an internal
allocation/debug API, not part of the supported public interface.

**One allocation per buffer.** An `sx_t` handle owns one NUL-terminated
byte array: `ptr[0 .. len-1]` are the content bytes, `ptr[len]` is always
`'\0'`, `cap` is the allocation size and `cap >= len + 1`. `len` counts
content bytes, so an embedded `'\0'` is data, not a terminator — the
model is binary-safe by construction.

**Ownership.**

| Value | Owner | Released with |
| --- | --- | --- |
| buffer from `sx_new*`, `sx_dup`, `sx_substr` | caller | `sx_free` |
| `owned` argument of `sx_append`, `sx_write`, `sx_insert` | transferred to the library | nothing: the call frees it |
| buffer returned by an edit | the same handle as `self` | one `sx_free` for the original handle |
| `sx_view_t` | none: it aliases a buffer | nothing; views are never freed |
| array from `sx_find_all*` or `sx_split` | caller | `free` (see [api-search.md](api-search.md)) |

Every edit returns `self`, so calls chain and the handle stays stable. The
backing array may move on growth: `sx_buf_mut`/`sx_buf_view_mut` pointers
become stale after any call that can reallocate and must be re-fetched. The
`owned` argument is consumed even when the edit itself fails: the call returns
`NULL`, frees the argument, and leaves `self` unchanged.

**NULL and out of contract.** Documented NULL handling:
`sx_new_from_cstr(NULL)` and `sx_dup(NULL)` return `NULL`; the `owned`
forms of `sx_append`, `sx_write`, `sx_insert` and all `_cstr` forms return
`NULL` and leave `self` untouched when their second argument is `NULL`.
Everything else dereferences its buffer or window: `sx_free`, `sx_len`,
`sx_cap`, `sx_buf_mut`, `sx_view`, the view navigation family and the edit
functions reject NULL handles, and `sx_new_from_view`, `sx_buf_view_mut`
and `sx_view_*` reject `ref == NULL` (`sx_len_view` only reads the stored
length). **Out of contract** — undefined behavior, not an error return:
a NULL handle or `v.ref`, a freed handle, an out-of-bounds view, a double
free, use after a consuming call, `self` as `owned`, and indices that
violate an entry below.

**Sentinels.**

| Situation | Value |
| --- | --- |
| allocation failure | `NULL` |
| rejected NULL operand (where documented) | `NULL` |
| "no view" | `.ref == NULL` |
| not found / no match (search page) | `(size_t)-1` |
| no matches / refused split (search page) | `NULL` |

**Sizes are `int`.** `sx_len`, `sx_len_view` and `sx_cap` return `int`;
view fields and search offsets are `size_t`. Content over `INT_MAX` bytes
is outside the tested contract; cast to `(size_t)` when mixing, e.g.
`fwrite(sx_buf_mut(h), 1, (size_t)sx_len(h), stdout)`.

**Views are snapshots.** `sx_view_t {ref, offset, len}` is the byte window
`[offset, offset + len)` inside the buffer behind `ref`; it copies nothing
and modifies nothing, and a valid window satisfies
`offset + len <= len(ref)`. A later edit that reallocates or changes the
length invalidates it: re-derive views afterwards, and do not edit a
buffer while views into it (such as a `sx_split` array) are in use.
`ref == NULL` means "no view", not a usable buffer.

**Capacity growth.** `sx_new` starts at capacity 32; growth is geometric
(double, then `+0x10000` at 64 KiB and above), and a debug build with
`-DSX_FORCE_EXACT_RESERVE` sizes every allocation exactly to `need + 1`.
Capacity never shrinks (`sx_truncate` and `sx_clear` keep it), and no
function promises spare capacity beyond its stated guarantee.

## Creation and destruction

### sx_new

```c
sx_t *sx_new(void);
```

Creates an empty buffer of capacity 32 bytes.

- **Parameters:** none.
- **Returns:** a new buffer (`len == 0`, `cap == 32`, `ptr[0] == '\0'`), or
  `NULL` on allocation failure.
- **Ownership:** caller-owned; release with `sx_free`.
- **Edge cases:** only `cap >= 1` is guaranteed; the exact 32-byte start is
  an implementation detail.

### sx_new_from_cstr

```c
sx_t *sx_new_from_cstr(const char *s);
```

Creates a buffer holding a copy of the C string `s`.

- **Parameters:** `s` — NUL-terminated string, read with `strlen`.
- **Returns:** a new caller-owned buffer, or `NULL` when `s == NULL` or on
  allocation failure.
- **Ownership:** caller-owned; release with `sx_free`.
- **Edge cases:** `strlen` semantics — an embedded `'\0'` truncates the
  copy; use `sx_new_from_view` for binary content.

### sx_new_from_view

```c
sx_t *sx_new_from_view(sx_view_t v);
```

Copies a view's bytes into a new, independent buffer.

- **Parameters:** `v` — source window; `v.len` bytes at
  `v.ref->ptr + v.offset`.
- **Returns:** a new caller-owned buffer, or `NULL` on allocation failure.
- **Ownership:** caller-owned; release with `sx_free`.
- **Edge cases:** byte-exact and binary-safe (embedded `'\0'` bytes are
  copied); no aliasing — mutating either buffer never affects the other.
  `v.ref == NULL` or an out-of-bounds window is out of contract.

### sx_new_with_cap

```c
sx_t *sx_new_with_cap(size_t n);
```

Creates an empty buffer with exactly `n` allocated bytes.

- **Parameters:** `n` — total allocation size in bytes.
- **Returns:** a new caller-owned buffer with `cap == n` for `n >= 1`, or
  `NULL` on allocation failure.
- **Ownership:** caller-owned; release with `sx_free`.
- **Edge cases:** one byte is reserved for the terminator, so at most
  `n - 1` content bytes fit; `n == 0` still allocates one byte
  (`cap == 1`), so the handle is always valid.

```c
sx_t *buf = sx_new_with_cap(64); /* room for 63 content bytes */
buf = sx_append_cstr(buf, "hello"); /* no reallocation */
sx_free(buf);
```

### sx_dup

```c
sx_t *sx_dup(const sx_t *src);
```

Creates a deep, independent copy of a buffer.

- **Parameters:** `src` — source buffer.
- **Returns:** a new caller-owned buffer with the same content, or `NULL`
  when `src == NULL` or on allocation failure.
- **Ownership:** caller-owned; copy and source are freed separately with
  `sx_free`.
- **Edge cases:** binary-safe; in v0.5 the copy's capacity is `len + 1`,
  not the source's capacity.

```c
sx_t *src = sx_new_from_cstr("hello");
sx_t *copy = sx_dup(src); /* independent: free both */
sx_free(copy);
sx_free(src);
```

### sx_free

```c
void sx_free(sx_t *h);
```

Releases a buffer: the content array and the handle.

- **Parameters:** `h` — a live handle owned by the caller.
- **Returns:** nothing.
- **Ownership:** consumes `h`; every view and `sx_buf_mut` pointer into it
  becomes invalid.
- **Edge cases:** `h == NULL` is out of contract — the handle is
  dereferenced (the regex layer's `sx_reg_free(NULL)` no-op does not
  generalize). Double free and use after free are out of contract.

## Accessors

### sx_len

```c
int sx_len(sx_t *h);
```

Returns the number of content bytes.

- **Parameters:** `h` — buffer.
- **Returns:** `len`, counting embedded `'\0'` bytes; the terminator is
  never counted; the buffer is not modified.
- **Edge cases:** `h == NULL` is out of contract; the result is an `int`
  (see the size note above).

### sx_len_view

```c
int sx_len_view(sx_view_t v);
```

Returns the view's stored length.

- **Parameters:** `v` — the view whose length is wanted.
- **Returns:** `(int)v.len`; the backing buffer is not inspected and the
  window is not validated.
- **Edge cases:** `v.len` above `INT_MAX` is outside the tested contract;
  the "no view" sentinel (`ref == NULL`) has `len == 0`.

### sx_cap

```c
int sx_cap(sx_t *h);
```

Returns the total bytes allocated for the content array.

- **Parameters:** `h` — buffer.
- **Returns:** `cap`, always at least `len + 1` (the single-NUL invariant);
  the buffer is not modified.
- **Edge cases:** `h == NULL` is out of contract; spare capacity beyond
  `len + 1` depends on the growth policy and is not a contract.

### sx_buf_mut

```c
char *sx_buf_mut(sx_t *h);
```

Returns a mutable pointer to the content array.

- **Parameters:** `h` — buffer.
- **Returns:** `ptr`; `ptr[len] == '\0'` at all times.
- **Ownership:** borrowed; bytes inside `[0, len)` may be rewritten without
  changing `len`.
- **Edge cases:** do not write at or beyond `len` without a
  length-changing call, and do not keep the pointer across any call that
  can reallocate (`sx_append*`, `sx_write*`, `sx_insert*`, `sx_reserve`,
  a growing `sx_truncate`). `h == NULL` is out of contract.

```c
char *p = sx_buf_mut(buf);
p[0] = 'H'; /* len unchanged; p[len] must stay '\0' */
```

### sx_buf_view_mut

```c
char *sx_buf_view_mut(sx_view_t v);
```

Returns a mutable pointer to the first byte of a view's window.

- **Parameters:** `v` — view into a buffer.
- **Returns:** `v.ref->ptr + v.offset`.
- **Ownership:** borrowed; writes reach the backing buffer.
- **Edge cases:** the pointer carries no length — pair it with
  `sx_len_view(v)` and stay inside the window. `v.ref == NULL` or an
  out-of-bounds window is out of contract; the pointer is stale after the
  backing buffer reallocates.

```c
sx_view_t word = sx_view_range(sx_view(buf), 6, 5);
char *w = sx_buf_view_mut(word); /* == sx_buf_mut(buf) + 6 */
w[0] = 'W';
```

### sx_view

```c
sx_view_t sx_view(const sx_t *h);
```

Returns the whole-buffer view.

- **Parameters:** `h` — buffer.
- **Returns:** `{ref: h->ref, offset: 0, len: sx_len(h)}`; the view aliases
  `h` and is never freed.
- **Edge cases:** zero-copy and read-only; `h == NULL` is out of contract.

## Capacity

### sx_reserve

```c
sx_t *sx_reserve(sx_t *self, size_t min_cap);
```

Ensures room for at least `min_cap` content bytes plus the terminator.

- **Parameters:** `self` — buffer; `min_cap` — content bytes that must fit
  without a later reallocation.
- **Returns:** `self` on success (including a no-op request), or `NULL` on
  allocation failure.
- **Ownership:** `self` is returned and stays caller-owned; content and
  length are preserved.
- **Edge cases:** on success `cap >= min_cap + 1`; a request the current
  capacity already satisfies does not shrink it. Capacity only grows —
  `sx_truncate` and `sx_clear` never release it — and the backing array may
  move, so re-fetch all `sx_buf_mut` pointers.

```c
buf = sx_reserve(buf, 4096); /* cap >= 4097 */
```

## Appending

### sx_append

```c
sx_t *sx_append(sx_t *self, sx_t *owned);
```

Appends another buffer's content, consuming that buffer.

- **Parameters:** `self` — destination; `owned` — source buffer.
- **Returns:** `self`, or `NULL` when `owned == NULL` or on allocation
  failure.
- **Ownership:** ownership transfer — the call copies `owned`'s content
  and frees `owned`; do not use or free it afterwards.
- **Edge cases:** byte-exact by content length, so embedded `'\0'` bytes
  are appended; `owned == NULL` changes nothing; `self` may reallocate;
  passing `self` as `owned` is out of contract (it would be freed in use).

### sx_append_cstr

```c
sx_t *sx_append_cstr(sx_t *self, const char *s);
```

Appends a C string.

- **Parameters:** `self` — destination; `s` — NUL-terminated source.
- **Returns:** `self`, or `NULL` when `s == NULL` or on allocation failure.
- **Ownership:** none; `s` is only read.
- **Edge cases:** `strlen` semantics — an embedded `'\0'` truncates the
  appended bytes; appending `""` is a no-op returning `self`.

### sx_append_view

```c
sx_t *sx_append_view(sx_t *self, sx_view_t v);
```

Appends a view's bytes.

- **Parameters:** `self` — destination; `v` — source window; `v.len` bytes
  at `v.ref->ptr + v.offset`.
- **Returns:** `self`, or `NULL` on allocation failure.
- **Ownership:** none; `v` is only read.
- **Edge cases:** byte-exact and binary-safe; `v` may alias `self`,
  including when the append grows the buffer — in-place doubling with
  `sx_append_view(h, sx_view(h))` is supported. `v.ref == NULL` or an
  out-of-bounds window is out of contract.

```c
buf = sx_append_view(buf, sx_view(buf)); /* doubles in place */
```

## Writing

### sx_write

```c
sx_t *sx_write(sx_t *self, size_t idx, sx_t *owned);
```

Overwrites bytes at `idx`, growing the length if needed, and consumes the
source buffer.

- **Parameters:** `self` — destination; `idx` — first byte to overwrite;
  `owned` — source buffer.
- **Returns:** `self`; `NULL` when `owned == NULL`, when the write range
  cannot be reserved, or on allocation failure.
- **Ownership:** ownership transfer — the content is copied and `owned` is
  freed; do not use or free it afterwards. `owned` is freed even if the
  write fails.
- **Edge cases:** `len' = max(len, idx + owned.len)` — a write never shrinks
  the buffer. `idx == len` is an append; `idx < len` overwrites in place;
  `idx > len` extends and `[len, idx)` is **unspecified**. If `idx + n`
  overflows `size_t`, or the resulting end cannot be reserved (a total
  content length of `SIZE_MAX` cannot include a terminator), the call returns
  `NULL` before copying and leaves `self` unchanged. Passing `self` as
  `owned` is out of contract.

```c
buf = sx_new_from_cstr("abc");
buf = sx_write_cstr(buf, 6, "XY"); /* len 8; bytes 3..5 unspecified */
```

### sx_write_cstr

```c
sx_t *sx_write_cstr(sx_t *self, size_t idx, const char *s);
```

Writes a C string at `idx`.

- **Parameters:** `self` — destination; `idx` — first byte to write; `s` —
  NUL-terminated source.
- **Returns:** `self`; `NULL` when `s == NULL`, when the write range cannot
  be reserved, or on allocation failure.
- **Ownership:** none; `s` is only read.
- **Edge cases:** `strlen` semantics (an embedded `'\0'` truncates the
  written bytes); on `s == NULL`, `self` is untouched. The same range
  validation, padding and `idx` rules as `sx_write` apply.

### sx_write_view

```c
sx_t *sx_write_view(sx_t *self, size_t idx, sx_view_t v);
```

Writes a view's bytes at `idx`.

- **Parameters:** `self` — destination; `idx` — first byte to write; `v` —
  source window.
- **Returns:** `self`; `NULL` when the write range cannot be reserved or on
  allocation failure.
- **Ownership:** none; `v` is only read.
- **Edge cases:** byte-exact and binary-safe; `v` may alias `self`,
  including when the write must grow the buffer (the source bytes survive
  the reservation). The overflow check runs before the alias-preserving
  pre-reserve, so a rejected range cannot change the target's capacity. Same
  `idx`/padding rules as `sx_write`; an out-of-bounds window is out of
  contract.

## Inserting

### sx_insert

```c
sx_t *sx_insert(sx_t *self, size_t idx, sx_t *owned);
```

Inserts another buffer's content at `idx`, shifting the tail right, and
consumes that buffer.

- **Parameters:** `self` — destination; `idx` — insertion point with
  `0 <= idx <= len`; `owned` — source buffer.
- **Returns:** `self`, or `NULL` when `owned == NULL` or on allocation
  failure.
- **Ownership:** ownership transfer — the content is copied and `owned` is
  freed; do not use or free it afterwards.
- **Edge cases:** the result is `[0, idx)` ++ inserted bytes ++
  `[idx, len)`, with `len' = len + n`; `idx == len` is append-like.
  `idx > len` is out of contract (the tail shift underflows), and passing
  `self` as `owned` is out of contract.

```c
buf = sx_insert_cstr(buf, 3, "-"); /* shift the tail right */
```

### sx_insert_cstr

```c
sx_t *sx_insert_cstr(sx_t *self, size_t idx, const char *s);
```

Inserts a C string at `idx`.

- **Parameters:** `self` — destination; `idx` — insertion point with
  `0 <= idx <= len`; `s` — NUL-terminated source.
- **Returns:** `self`, or `NULL` when `s == NULL` or on allocation failure.
- **Ownership:** none; `s` is only read.
- **Edge cases:** `strlen` semantics; on `s == NULL`, `self` is untouched.
  `idx > len` is out of contract.

### sx_insert_view

```c
sx_t *sx_insert_view(sx_t *self, size_t idx, sx_view_t v);
```

Inserts a view's bytes at `idx`.

- **Parameters:** `self` — destination; `idx` — insertion point with
  `0 <= idx <= len`; `v` — source window.
- **Returns:** `self`, or `NULL` on allocation failure.
- **Ownership:** none; `v` is only read.
- **Edge cases:** byte-exact and binary-safe; `v` may alias `self`,
  including when the insert grows the buffer, as long as its window starts
  at or before `idx` (the tested case: a slice taken at the insertion
  point). A self-aliased window starting after `idx` lies in the region the
  tail shift overwrites and is **out of contract** — copy it first with
  `sx_new_from_view`. `idx > len` and out-of-bounds views are out of
  contract.

## Truncating, erasing and clearing

### sx_truncate

```c
sx_t *sx_truncate(sx_t *self, size_t new_len);
```

Sets the content length to `new_len`.

- **Parameters:** `self` — buffer; `new_len` — the new length in content
  bytes.
- **Returns:** `self` on success; `NULL` on allocation failure while
  growing.
- **Edge cases:** a shorter `new_len` keeps the prefix and drops the tail;
  the same `new_len` is a no-op. A longer `new_len` grows the buffer: `len`
  becomes `new_len`, `ptr[new_len] == '\0'`, and `[old_len, new_len)` is
  **unspecified** — do not read it until overwritten. A shrink does not
  reduce capacity.

### sx_erase

```c
sx_t *sx_erase(sx_t *self, size_t idx, size_t n);
```

Removes `n` bytes at `idx`, shifting the tail left.

- **Parameters:** `self` — buffer; `idx` — first byte to remove; `n` —
  number of bytes to remove.
- **Returns:** `self`; `self` itself must be valid.
- **Edge cases:** `idx + n == len` erases the suffix; `idx + n > len` is a
  no-op returning `self` unchanged; `n == 0` is a no-op. No allocation
  happens, so the call cannot fail. An `idx + n` overflowing `size_t` is
  out of contract.

```c
buf = sx_erase(buf, 2, 100); /* idx+n > len: no-op, returns self */
```

### sx_clear

```c
sx_t *sx_clear(sx_t *self);
```

Empties the buffer, keeping its capacity.

- **Parameters:** `self` — buffer.
- **Returns:** `self` (still caller-owned).
- **Edge cases:** sets `len = 0` and `ptr[0] = '\0'`; the allocation and
  its capacity are kept, so the buffer is immediately reusable.

```c
buf = sx_clear(buf); /* len 0, capacity kept */
```

## Substrings

### sx_substr

```c
sx_t *sx_substr(const sx_t *h, size_t idx, size_t n);
```

Copies `[idx, idx + n)` into a new, independent buffer.

- **Parameters:** `h` — source buffer; `idx` — start offset; `n` — number
  of bytes, with the precondition `idx + n <= len(h)`.
- **Returns:** a new caller-owned buffer, or `NULL` on allocation failure.
  (A NULL `h` or out-of-bounds range is out of contract, not an error
  value.)
- **Ownership:** caller-owned; release with `sx_free`.
- **Edge cases:** `n == 0` yields an empty buffer; byte-exact and
  binary-safe, including embedded `'\0'` bytes. Unlike view navigation,
  this copies: mutations on either side never affect the other.

```c
sx_t *word = sx_substr(buf, 6, 5); /* a copy, not a view */
sx_free(word);
```

## View navigation

All four functions re-derive the `(offset, len)` window over an existing
buffer. None copies, modifies or transfers ownership; each returns a new
view value. Inputs must be valid views (`ref != NULL`, window inside
the buffer); invalid input is out of contract.

### sx_view_advance

```c
sx_view_t sx_view_advance(sx_view_t v, size_t n);
```

Moves the window start right by `n`.

- **Parameters:** `v` — input view; `n` — bytes to advance.
- **Returns:** the adjusted view over the same buffer.
- **Edge cases:** with `H = len(ref)`:
  - if `offset + len + n < H`, the end is kept and `len` shrinks by `n`;
  - otherwise, if `offset + n < H`, the start becomes `offset + n` and the
    end clamps to the buffer end (`len = H - offset`);
  - otherwise the result is the empty view `(H, 0)`.
  It never moves past the buffer end, even for a large `n`; advancing a
  sub-view beyond its own end widens it to the buffer end (second case).

```c
sx_view_t rest = sx_view_advance(sx_view(buf), 2); /* start +2 */
```

### sx_view_retreat

```c
sx_view_t sx_view_retreat(sx_view_t v, size_t n);
```

Moves the window start left by `n`, keeping the end fixed.

- **Parameters:** `v` — input view; `n` — bytes to retreat.
- **Returns:** the adjusted view over the same buffer.
- **Edge cases:** `len` grows by the amount the offset shrinks; the move
  saturates at offset 0 (`len` becomes the original `offset + len`), so the
  window never starts before the buffer.

### sx_view_shift

```c
sx_view_t sx_view_shift(sx_view_t v, ptrdiff_t off);
```

Moves the window by a signed offset, preserving its length.

- **Parameters:** `v` — input view; `off` — signed byte offset.
- **Returns:** the adjusted view over the same buffer.
- **Edge cases:** `len` is preserved (unlike advance/retreat). A positive
  `off` that would push the end past the buffer clamps to
  `offset = H - len` (the window ends at the buffer end); a negative `off`
  saturates the start at 0. On an empty buffer the result is `(0, 0)`.

### sx_view_range

```c
sx_view_t sx_view_range(sx_view_t v, size_t off, size_t len);
```

Re-anchors the window at absolute coordinates, ignoring the input view's
offset and length.

- **Parameters:** `v` — supplies only the backing buffer; `off` —
  absolute start offset, with the precondition `off <= H`; `len` —
  requested length.
- **Returns:** the view `(off, min(len, H - off))`.
- **Edge cases:** the length clamps to the buffer end; `off == H` gives
  `(H, 0)`; `off > H` is out of contract (`H - off` underflows).

```c
sx_view_t slice = sx_view_range(sx_view(buf), 2, 3); /* absolute (2, 3) */
```

## Trimming views

The trim family removes ASCII whitespace from a view's edges. The set is
exactly `{space, '\t', '\v', '\f', '\r', '\n'}`. No bytes are modified,
no ownership is transferred, and only bytes inside the window are
inspected.

### sx_view_trim

```c
sx_view_t sx_view_trim(sx_view_t v);
```

Trims both edges of the view.

- **Parameters:** `v` — input view.
- **Returns:** the view with leading and trailing whitespace removed.
- **Edge cases:** equivalent to
  `sx_view_trim_back(sx_view_trim_front(v))`. A view with no edge
  whitespace is returned unchanged. An all-whitespace view becomes the
  empty view at the window's end (offset `offset + len`, length 0) —
  `(H, 0)` for a whole-buffer view.

```c
sx_view_t field = sx_view_trim(sx_view(buf)); /* strip ASCII whitespace */
```

### sx_view_trim_front

```c
sx_view_t sx_view_trim_front(sx_view_t v);
```

Drops leading whitespace.

- **Parameters:** `v` — input view.
- **Returns:** the view with leading whitespace removed.
- **Edge cases:** the offset moves right and `len` shrinks by the same
  amount, so the window end stays fixed. The scan is bounded by the view's
  own length and never runs past the window's end, even when whitespace
  continues in the buffer after it.

### sx_view_trim_back

```c
sx_view_t sx_view_trim_back(sx_view_t v);
```

Drops trailing whitespace.

- **Parameters:** `v` — input view.
- **Returns:** the view with trailing whitespace removed.
- **Edge cases:** only the window's last byte (`offset + len - 1`) is
  inspected and `len` shrinks leftwards; the offset is unchanged, so an
  all-whitespace window becomes `(offset, 0)`. Bytes outside the window are
  never examined.

## Complete example

This program builds a string, trims a view of it, copies the trimmed
window into a new buffer, edits that buffer in place, and releases both
buffers:

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *h = sx_new_from_cstr("  hello, world  ");

  sx_view_t trimmed = sx_view_trim(sx_view(h)); /* (2, 12) */
  sx_t *clean = sx_new_from_view(trimmed);      /* independent copy */

  clean = sx_append_cstr(clean, "!");
  clean = sx_insert_cstr(clean, 0, "> ");

  printf("%s (%d bytes)\n", sx_buf_mut(clean), sx_len(clean));

  sx_free(clean);
  sx_free(h);
  return 0;
}
```

Expected output:

```text
> hello, world! (15 bytes)
```

---

[← Previous](guide.md) · [Docs home](README.md) · [Next →](api-search.md)
