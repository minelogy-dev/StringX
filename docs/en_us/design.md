English · [中文](../zh_cn/design.md)

[Docs home](README.md) › Design & internals

# Design and internals

This page is for contributors and for readers who need to reason about the
library's guarantees or port it. It describes the data layout, the
algorithms, the ownership model and the deliberate trade-offs behind the
public contracts in `<sx.h>`. The contracts themselves are documented in the
[API reference](api-buffer.md), [search reference](api-search.md) and
[regular expressions](regex.md) pages.

## Goals and non-goals

The design follows from a short set of goals:

- **Byte-exact contract.** Every operation is defined on bytes and its
  behavior boundaries are written down. Search, comparison and regex work
  by length, so embedded NUL bytes are ordinary data.
- **Zero-copy access.** Slices are `sx_view_t` values that alias a buffer;
  inspecting or iterating over subslices copies nothing.
- **One stable handle per buffer.** An `sx_t *` identifies the buffer for
  its whole life; edits return `self`, so calls compose into chains even
  when the backing array is reallocated.
- **Predictable failure.** A documented invalid input produces a fixed
  sentinel instead of crashing; inputs outside the documented boundaries are
  out of contract. There are no exceptions, error codes or global error
  state.
- **Small surface.** One public header `<sx.h>`, one library, one optional
  advanced dependency (PCRE2) that the library links itself.

Equally deliberate non-goals:

- **No text encoding layer.** The library does not decode UTF-8, normalize
  text, or provide character/grapheme operations. Strings are bytes; the
  regex layer runs PCRE2 in 8-bit non-UTF mode.
- **No locale awareness.** Comparison, trimming and character classes are
  defined on bytes and fixed ASCII sets, independent of `LC_*`.
- **No thread-safe buffers.** A compiled regex is immutable and shareable;
  a mutable buffer is plain memory and needs external synchronization.
- **No custom allocator hooks, ropes/streaming, or lazy views.** Every
  buffer is one contiguous allocation.
- **No regex engine of its own.** Pattern semantics are PCRE2's; the
  library supplies only the lifecycle, scanning and substitution plumbing.

## Layers and headers

### Header map

| Header | Layer | Contents | Audience |
| --- | --- | --- | --- |
| `include/sx_core.h` | core | `sx_t`, `sx_view_t`, buffer/view/edit/search/compare prototypes | public, included by `<sx.h>` |
| `include/sx.h` | advanced | includes `sx_core.h`; declares `sx_split` and `sx_reg_*` | public entry point |
| `include/sx_raw.h` | internal | opaque `_sx` plus the raw allocation helpers | not supported |
| `src/_sx.h` | internal | definition of `struct _sx` | library only |
| `src/export.h` | Forge-derived | `SYMBOL_PUBLIC` visibility macro (checked in) | library only |

Consumers include `<sx.h>`. `sx_core.h` is public because `<sx.h>` includes
it, but it is not a second entry point: `sx_split` and the regex layer are
only declared in `sx.h`.

The implementation mirrors the header split:

| Translation unit | Implements |
| --- | --- |
| `src/sx_core.c` | buffer, view, mutation, search, compare |
| `src/sx_raw.c` | allocation primitives (`sx__new`, `sx__reserve`, ...) |
| `src/sx_adv.c` | `sx_split` and the PCRE2 wrapper |

All three library translation units use the private `_sx` layout directly;
only `sx.h` and `sx_core.h` are consumed by client code.

### Why sx_split lives in the advanced layer

`sx_split` is declared in `sx.h`, after the include of `sx_core.h`, and is
implemented in `src/sx_adv.c`. That placement is intentional:

- It returns a `malloc`'ed array of views with the iterator convention used
  by `sx_reg_match_all` and `sx_reg_split`: tokens in order, then an end
  guard whose `.ref == NULL`, and the caller frees the array with `free()`.
  Grouping the array-returning view APIs keeps that ownership convention in
  one place instead of adding an array-allocating API to the core.
- `sx_split`'s scan is self-contained. The core's search primitive is a
  `static` KMP implementation inside `sx_core.c`; the advanced layer cannot
  call it and does not need to, because a split scan is bounded by the
  output size anyway. The source comment states the trade directly: the
  `O(n*m)` byte-exact scan "is fine for the advanced layer and keeps this TU
  independent".
- The split scan is not PCRE2-dependent: it is a plain separator search.
  It lives in the advanced TU by API shape, not by dependency.

### Why sx_raw.h is internal

`include/sx_raw.h` exposes the private allocation layer:

```c
typedef struct _sx _sx;

_sx *sx__new(size_t cap);
_sx *sx__dup(const _sx *src);
int sx__reserve(_sx *s, size_t need);
void sx__free(_sx *s);
```

It is internal for three reasons:

- it hands out `_sx`, the private buffer struct, which `sx_core.h` only
  forward-declares;
- its functions are named with a reserved double-underscore prefix and
  bypass the public error model (`sx__reserve` returns `int`, not a
  sentinel `sx_t *`) — they serve the library's own TUs, not clients;
- it carries no stability promise. The installed header set includes it
  because Forge copies every `include/*.h` (the build script syncs the
  whole directory), but it is not part of the supported public API and
  must not be used by client code. The four `sx__*` helpers are declared
  `SYMBOL_PUBLIC`, so they do appear in the shared library's dynamic symbol
  table; that export does not make them supported API.

## Buffer layout and invariants

A public `sx_t` is a one-field handle pointing at a private `_sx`:

```text
sx_t (public handle)                _sx (private)
+------------+                +------------+
| ref        | -------------> | ptr        | ----> one allocation of cap bytes
+------------+                | len        |       [ H e l l o \0 ? ? ... ]
                              | cap        |
                              +------------+

content:      bytes [0, len)        terminator: ptr[len] == '\0'
capacity:     bytes [0, cap)        invariant:  len + 1 <= cap, cap >= 1
```

A view is a value, not an allocation:

```text
sx_view_t v = { ref = h->ref, offset = 2, len = 3 }

buffer:  a  b  c  d  e  f  \0        view covers  c  d  e
               ^        ^
          v.offset   v.offset + v.len
```

The invariants that every function preserves:

1. `ptr != NULL` and `cap >= 1` for every live buffer.
2. `len + 1 <= cap`: there is always room for the terminator.
3. `ptr[len] == '\0'` at all times. Content bytes may themselves be
   `'\0'`; the terminator is the first NUL *after* the content.
4. `len` counts content bytes, not the terminator. `sx_len` returns it as
   `int` (see [integer accessors](#integer-accessors-and-size_max)).
5. A returned view satisfies `ref = buffer.ref` and
   `offset + len <= buffer len`. The API does not validate caller-built
   views; a view whose bounds exceed its buffer is out of contract.
6. The `sx_t *` handle address is stable for the buffer's life. `ptr`
   returned by `sx_buf_mut` is **not** stable across growth: `realloc` can
   move it.
7. Bytes in `[len, cap)` are unspecified: they may hold stale content or
   never-initialized memory. Only `ptr[len]` is part of the contract.

The invariants are enforced structurally: every mutating function ends with
`ptr[len] = '\0'`, and `sx__reserve` is the only place that changes `cap`
or `ptr`. `sx_buf_mut` lets a caller rewrite `[0, len)` without changing
`len`; the invariant still holds because the terminator is outside that
range.

## Allocation and growth

### Initial capacities

Every buffer is one allocation from `sx__new(cap)`, which allocates the
`_sx` header and a `cap`-byte array (rounded up to 1 so `ptr[0]` has a
home). The constructors choose `cap` as follows:

| Constructor | Initial `cap` | Maximum content bytes |
| --- | --- | --- |
| `sx_new()` | 32 | 31 |
| `sx_new_with_cap(n)` | `n`, or 1 when `n == 0` | `n - 1` (0 when `n <= 1`) |
| `sx_new_from_cstr(s)` | `strlen(s) + 1` | `strlen(s)` |
| `sx_new_from_view(v)` | `v.len + 1` | `v.len` |
| `sx_dup(src)` | `src->len + 1` | `src->len` |
| `sx_substr(h, idx, n)` | `n + 1` | `n` |

`sx_new()` goes through `sx_new_with_cap(32)`; the 32-byte start keeps the
first few appends allocation-free without wasting much memory. The
construction-from-content constructors allocate exactly `content + 1`, so
a freshly built or copied buffer is tight.

### Growth policy

All growth goes through `sx__reserve(_sx *s, size_t need)`, which treats
`need` as the number of **content bytes** the caller must be able to hold
afterwards. Internally it must produce `cap > need`, i.e. room for `need`
bytes plus the terminator. The decision:

| Situation | New `cap` |
| --- | --- |
| `cap > need` | unchanged (no-op) |
| `need == SIZE_MAX` | rejected (integer overflow guard) |
| default, `cap < 0x10000` | `max(cap * 2, need + 1)` |
| default, `cap >= 0x10000` | `max(cap + 0x10000, need + 1)` |
| `-DSX_FORCE_EXACT_RESERVE` | `need + 1` |

The default policy is geometric doubling while the allocation is below
64 KiB, then a fixed `+0x10000` step. Doubling gives amortized constant
cost per appended byte; the fixed step avoids doubling a large allocation
(say 8 MiB to 16 MiB) just to append a few bytes. The `max` with
`need + 1` covers the case where a single call needs more than the next
geometric step would provide.

The public `sx_reserve(self, n)` forwards `n` unchanged, so it guarantees
`cap >= n + 1`: after the call the buffer can hold `n` content bytes in
total without another reallocation. `sx_append`, `sx_write` and `sx_insert` call
`sx__reserve(final_content_length)` before copying:

- append: `need = len + appended`;
- write: `need = idx + written`; `len` afterwards is `max(len, idx + n)`;
- insert: `need = len + inserted`; the tail is shifted with `memmove`.

Growth is monotonic. Shrinking operations never reallocate:
`sx_truncate` only reserves when the new length is larger, `sx_erase`
moves bytes inside the existing allocation, and `sx_clear` only rewrites
`ptr[0]`. The exact-reserve test asserts both properties
(`tests/reserve_stress.c`).

### SX_FORCE_EXACT_RESERVE

`-DSX_FORCE_EXACT_RESERVE` replaces the policy with `cap = need + 1` for
every growth. The allocation then ends exactly at the terminator, so any
access past `len` lands in the AddressSanitizer redzone immediately
instead of hiding behind slack capacity. The costs are a reallocation per
growth (a per-byte append workload becomes quadratic-ish in copies) and no
slack, so it is a debugging/testing mode, not a production mode.
`tests/reserve_stress.c` is the dedicated configuration: it starts every
buffer at `cap == 1`, drives 1500 pseudo-random edits from a fixed LCG
against a byte-exact model, and asserts `cap == len + 1` after every
reallocation plus "shrinking never reallocates".

## Views and aliasing

Views are the library's core abstraction: `sx_view_t` stores a buffer
reference plus an `(offset, len)` window. Returning a view never copies,
never allocates and never modifies the buffer. The same representation is
used for whole-buffer views (`sx_view`), navigation (`sx_view_advance`,
`sx_view_range`, ...), split tokens and regex match arrays.

The rules a caller must respect:

- **Views are snapshots.** An edit does not adjust captured views. If the
  length changes, offsets and lengths captured earlier may no longer
  describe the same bytes; re-derive the view afterwards.
- **Growth invalidates raw pointers.** `sx_buf_mut` and `sx_buf_view_mut`
  borrow the array; a later edit that grows the buffer may `realloc` it.
  The `sx_t *` handle is stable, the pointer is not.
- **A view must not outlive its buffer.** The `ref` is a raw `_sx *`;
  freeing the buffer frees the referent. Returning an array of views does
  not transfer ownership of the underlying buffer.
- **Do not edit while views are in use.** This is the same rule for
  `sx_split` tokens, `sx_reg_match_all` arrays and user-held views.
- **A `NULL` view means "none".** `{ref = NULL, offset = 0, len = 0}` is
  the sentinel; library-returned views otherwise point into a live buffer.

Aliasing edits are the delicate part. When the source view refers to the
same buffer as the destination, `sx_append_view`, `sx_write_view` and
`sx_insert_view` copy with `memmove` instead of `memcpy`. For append and
write the copy is a plain overlap-safe move, so any contract-valid source
view works. `sx_insert_view` is narrower, and its order of operations is
why:

1. reserve space for the **final** content length (this is what keeps the
   source pointer valid: `realloc` updates `_sx.ptr` in place, so a pointer
   taken afterwards sees the new allocation);
2. shift the tail right by `v.len`, writing `[idx + n, len + n)`;
3. write the inserted bytes at `idx` with `memmove`.

The tail shift makes self-aliasing conditional: a source window that starts
at or before the insertion point (`v.offset <= idx`) ends at or before
`idx + n`, so the shift never overwrites it and the `memmove` at step 3
handles the remaining overlap. The exercised case is `v.offset == idx`:
inserting the view `[2, 4)` (`"cd"`) into `"abcdef"` at index 2 gives
`"abcdcdef"`. A self-aliased window that starts **after** `idx` is
overwritten by the shift before it is read: inserting the view `[3, 5)`
(`"de"`) into `"abcdef"` at index 2 yields `"abdccdef"`, not
`"abdecdef"`; such a call is out of contract. The regression at the end of
`tests/reserve_stress.c` exercises the `v.offset == idx` case with growth
forced, which pins the reserve-before-pointer ordering. The owned variants
(`sx_append(self, self)` and friends) do not have this protection — they
free their argument and are out of contract for self-aliasing.

## Ownership and error model

Ownership is deliberately explicit and small: whoever receives an owning
pointer releases it.

| Operation | Ownership |
| --- | --- |
| `sx_new*`, `sx_dup`, `sx_substr`, `sx_reg_replace*` | returns caller-owned `sx_t *`; release with `sx_free` |
| `sx_append`, `sx_write`, `sx_insert` (the `owned` argument) | consume the argument: the library frees it |
| `sx_split`, `sx_find_all*`, `sx_reg_match_all`, `sx_reg_split` | return `malloc`'ed arrays; release with `free` |
| match views, tokens, whole-buffer views | borrow; nothing to release |

Details that matter:

- **Consuming calls consume on failure too.** `sx_append`/`sx_write`/
  `sx_insert` attempt the edit, then free `owned` unconditionally. If the
  edit itself fails (allocation failure), the call returns `NULL` and the
  argument has still been consumed. A `NULL` argument returns `NULL`
  without freeing anything.
- **Handle identity.** Every edit returns `self`, so successful edits chain:
  `sx_append_cstr(sx_append_cstr(s, "a"), "b") == s`. The handle is stable
  across growth; only the backing array moves.
- **Arrays do not own their elements' buffer.** Freeing the array returned
  by `sx_split` or `sx_reg_match_all` is not freeing the subject; the
  views simply become unreachable.

The error model is sentinel-based, with no `errno`, no error object and no
error return code:

| Situation | Sentinel |
| --- | --- |
| offset search (`sx_find*`, `sx_rfind*`) | `(size_t)-1` |
| "no occurrences" (`sx_find_all*`) | `NULL` array |
| "no match" view (`sx_reg_match`) | `{ref = NULL, offset = 0, len = 0}` |
| "no match" array (`sx_reg_match_all`) | `NULL` |
| invalid/empty separator, invalid regex, OOM in an array/string factory | `NULL` |
| boolean predicates and `sx_reg_is_match` | `0` |

Two consequences are intentional and documented:

- **"No result" and "out of memory" can be indistinguishable.** The
  `find_all` family and the `sx_reg_*` array family return `NULL` for both,
  because the contract gives them no third channel. The regex replace family
  does distinguish them: "no match" returns an unchanged copy, `NULL` means
  invalid input or failure.
- **NULL handle arguments are mostly out of contract.** Only the families
  that explicitly check — `sx_new_from_cstr`, `sx_dup`, the cstr/owned
  mutation arguments, `sx_find*`, `sx_rfind*`, `sx_find_all*`, the
  `sx_cmp*`/`sx_starts_with*`/`sx_ends_with*` group, `sx_split` and the
  `sx_reg_*` calls — have defined `NULL` behavior. Accessors
  (`sx_len`, `sx_cap`, `sx_buf_mut`, `sx_view`, ...), view movers and
  `sx_free` dereference their handle and do not accept `NULL`. The public
  headers state the boundaries per function.

Errors never leave a partially built buffer behind: a failed growth happens
before any byte is copied, so `self` keeps its previous content and the
single-NUL invariant. The one visible exception is the consumed `owned`
argument described above.

## Search internals

The whole search family is built on one `static` primitive in
`src/sx_core.c`:

```c
static inline find_ret_t find(const sx_t *h, const char *needle, int len,
                              size_t boundary, int opt);
```

The return type is a position or a pointer to a result array, so the
implementation picks an integer type wide enough to round-trip a pointer:

```c
#if SIZE_MAX > UINTPTR_MAX
typedef size_t find_ret_t;
#else
typedef uintptr_t find_ret_t;
#endif
```

### Region model

`opt` selects the direction and the result shape:

- bit 0: `1` = right search, `0` = left search;
- bit 1: `1` = first result (scalar), `0` = all results (array).

The searched region is computed from the caller's boundary:

```text
left search  (sx_find*):    region = [boundary, H)
right search (sx_rfind*):   region = [0, min(boundary, H))

H = content length of h
a match window [p, p+m) counts only if p >= region_start and p+m <= region_end
```

The wrappers plug in the documented boundaries: `sx_find*` use
`boundary = 0`; `sx_rfind*` use `boundary = len(h)` with the right flag;
`sx_find_view_b` and `sx_rfind_view_b` pass the caller's boundary.
`len <= 0`, an empty region, or a region shorter than the needle is
reported as "not found" in the shape the mode requires — `(size_t)-1` for
the scalar wrappers, a count-0 array internally (converted to `NULL` by
`sx_find_all*`) for the array wrappers.

`len` is the needle's content length (`sx_t` needle), `v.len` (view
needle), or `(int)strlen(s)` (cstr needle — note the narrowing cast, which
is one of the practical `int` limits). The needle bytes are read directly
from the view or buffer; the search is byte-exact, like `memcmp`.

### One-byte needles: memchr and memrchr

For `m == 1` the primitive uses the C library's vectorized scans:

- **leftmost:** `memchr(t + l, c, er - l)`;
- **rightmost:** `memrchr(t, c, er)` on glibc. `memrchr` is a GNU
  extension, not ISO C, so it is declared by hand in `sx_core.c` and used
  only when `__GLIBC__` is defined and `SX_FORCE_NO_MEMRCHR` is not. The
  fallback is a byte-wise backward loop that keeps the last (rightmost)
  hit before reaching `l`.

The rightmost fast path is allowed to search from `0` because a right
search always sets `l = 0`; `er` already carries the caller's boundary.
`SX_FORCE_NO_MEMRCHR` exists to build and test the portable path on glibc
systems.

For `m == 1` in "all" mode, the implementation repeatedly calls `memchr`
starting at the previous hit + 1. That reports **overlapping** positions,
which is exactly what `sx_find_all*` promises (`"aa"` in `"aaaa"` reports
0, 1 and 2).

### Longer needles: KMP

For `m >= 2` the primitive computes a KMP prefix table (`pi`, one
`malloc(m * sizeof(size_t))`) and scans the region in `O(n + m)` time:

- **leftmost-first and all mode** use forward KMP over
  `[l, er)`. On a full match the first mode returns immediately; the all
  mode records the start offset and resumes with `q = pi[m - 1]`, which is
  what makes it report overlapping occurrences.
- **rightmost-first** uses reverse KMP: the same prefix table computed over
  the *reversed* needle by index mapping (no copy of the needle), scanning
  the text from `er - 1` down to `l`. When the matched suffix length
  reaches `m`, the current text index is the window start.

The all-mode result array starts at capacity 16 and doubles with an
overflow guard (`cap > SIZE_MAX / 2 / sizeof(*r)` aborts with `NULL`), then
shrinks once with `realloc` before returning. Its layout is
`{count, pos0, pos1, ...}`: `arr[0]` is the count, not a position.

### Complexity

| Case | Technique | Time | Extra memory |
| --- | --- | --- | --- |
| one-byte needle, first left | `memchr` | `O(n)` | none |
| one-byte needle, first right | `memrchr`, or backward scan | `O(n)` | none |
| needle length >= 2, first | KMP | `O(n + m)` | `O(m)` prefix table |
| `sx_find_all*`, one byte | repeated `memchr` | `O(n)` scan | `O(k)` result |
| `sx_find_all*`, longer needle | KMP | `O(n + m)` scan | `O(m) + O(k)` result |
| `sx_split` | two passes of `is_sep` | `O(H * m)` worst case | `O(tokens)` result |

`n` is the region length, `m` the needle length and `k` the number of
matches. The array wrappers allocate the result only; `sx_find*` /
`sx_rfind*` return a scalar and allocate nothing (except the KMP table,
which is freed before returning).

## Regex wrapper internals

The advanced regex layer is intentionally thin. Its whole state is one
compiled PCRE2 code object:

```c
typedef struct sx_reg_t {
  pcre2_code *code;
} sx_reg_t;
```

### Compile and release

`sx_reg_compile` calls PCRE2 with fixed arguments:
`pcre2_compile(pattern, PCRE2_ZERO_TERMINATED, 0, ...)`. The `0` is the
option word — no `CASELESS`, no `UTF`, nothing — and
`PCRE2_ZERO_TERMINATED` means the pattern is a C string. The PCRE2 error
code and error offset are captured only to satisfy the signature and are
discarded, which is why every compile failure collapses to `NULL`.
Compilation allocates the PCRE2 code object and the small `sx_reg_t`
wrapper; `sx_reg_free` frees both and accepts `NULL`.

### Per-call match data and the scan rule

PCRE2 separates the compiled pattern from per-call match data. The wrapper
creates match data per call with
`pcre2_match_data_create_from_pattern(r->code, NULL)` and frees it before
returning. That is what makes a compiled handle thread-shareable: no match
state lives in the handle.

A single match step is `pcre2_match(code, subject, len, offset, 0, md,
NULL)`. Note the subject is passed with an explicit **length**, not as a C
string, and the last argument (match context) is `NULL`, so PCRE2's default
match limits apply. A negative return (no match, or a PCRE2 match-time
error such as a match limit) is treated as "no further match"; the wrapper
does not expose match-time error codes.

The scan shared by `sx_reg_match_all` and `sx_reg_split` advances with:

```c
static size_t reg_next_at(size_t start, size_t end) {
  return end > start ? end : start + 1;
}
```

The next search starts at the match end, or one byte past the start for a
zero-length match, so the scan is guaranteed to advance. `pcre2_match` is
called with `offset` on every step; when `offset` passes the subject length
PCRE2 returns an error and the loop ends. This is why `a*` on `"bbb"`
reports empty matches at offsets 0, 1, 2 and 3, and why `$` on `"abc"`
reports a single empty match at offset 3.

### Two-pass sizing for match_all and split

Both array families run the scan twice:

```text
pass 1: count matches             -> cnt
malloc((cnt + 1) * sizeof view)   -> exact size, no realloc
pass 2: repeat the same scan      -> fill views, then arr[cnt] = {0} guard
```

The scan is deterministic (same pattern, same subject, same start offsets),
so both passes observe identical positions. `sx_reg_split` additionally
tracks the gap start `seg`: each token is `[seg, match_start)` and `seg`
moves to `match_end`; the final token is `[seg, len)`. This gives exactly
`matches + 1` tokens, including the empty edge tokens and the empty tokens
produced by adjacent or zero-length matches.

### Substitution: two pcre2_substitute calls

Replace is built from PCRE2's substitution routine, with two fixed flags:

```c
options |= PCRE2_SUBSTITUTE_UNSET_EMPTY | PCRE2_SUBSTITUTE_UNKNOWN_UNSET;
```

`UNSET_EMPTY` makes a group that did not participate expand to nothing;
`UNKNOWN_UNSET` makes a group number that does not exist expand to nothing
instead of failing the whole substitution. `sx_reg_replace` adds
`PCRE2_SUBSTITUTE_GLOBAL`; `sx_reg_replace_first` does not.

Because the output length is not known before substituting, the wrapper
calls `pcre2_substitute` twice:

```text
call 1: output buffer NULL, PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, &needed
        -> PCRE2_ERROR_NOMEMORY, `needed` = required size including NUL
malloc(needed)                                       (single temporary)
call 2: fill the buffer, &used = needed
        -> rc >= 0, `used` = bytes written excluding NUL
sx_new_with_cap(used + 1)                            (new public buffer)
memcpy(ptr, buf, used); len = used; ptr[used] = '\0'
```

The temporary buffer is freed after the copy. A first call that does not
return `PCRE2_ERROR_NOMEMORY` means the template/replacement pair was
rejected; the wrapper returns `NULL` and never touches the subject. With no
match, PCRE2 substitutes nothing and the output is an unchanged copy of the
subject — a normal success, not an error.

### Allocation summary

| Call | Result allocations | Notes |
| --- | --- | --- |
| `sx_reg_compile` | PCRE2 code object + `sx_reg_t` | one-time cost |
| `sx_reg_free` | none | frees the two compile allocations |
| `sx_reg_is_match` | per-call match data | no result allocation |
| `sx_reg_match` | per-call match data | result is a view value |
| `sx_reg_match_all` | match data + exact `(cnt + 1)` array | two scan passes |
| `sx_reg_split` | match data + exact `(matches + 2)` array | two scan passes |
| `sx_reg_replace*` | PCRE2-internal match data + temporary buffer + `sx_t` | two substitute calls |

The wrapper does not enable PCRE2 JIT (`pcre2_jit_compile` is never called)
and exposes no compile or match options. Pattern-level features such as
inline options still work because they are part of the pattern, not the
wrapper's policy.

## Concurrency

- **Compiled regex handles are immutable and shareable.** All per-call
  state lives in caller-owned match data created inside the call; the
  wrapper has no global variables. One `sx_reg_t *` may serve concurrent
  calls from several threads.
- **Buffers are not synchronized.** They are ordinary heap memory. Two
  threads reading the same buffer concurrently are fine as long as nobody
  mutates it; a mutation needs external synchronization. Views make this
  stricter: a view read concurrently with a length-changing edit is a data
  race (out of contract).
- **Ownership must be coordinated.** A buffer with a single owner must not
  be consumed by two threads; arrays are single-owner results.

The library itself never mutates global state, so it is safe to use from
multi-threaded programs as long as each buffer's access is synchronized by
the caller.

## Portability and limits

### memrchr and SX_FORCE_NO_MEMRCHR

The only non-ISO C API in the search path is `memrchr`, a glibc extension.
`sx_core.c` declares it manually rather than relying on a feature-test
macro, and compiles it in only when `__GLIBC__` is defined and the build
does not define `SX_FORCE_NO_MEMRCHR`. On other platforms (and with the
forced switch) the rightmost one-byte search falls back to a portable
single-byte backward loop. The in-tree test suite runs the default glibc
path; the switch exists so the fallback can be built and checked on glibc
as well.

### Integer accessors and SIZE_MAX

The public accessors return `int` while offsets and needles use `size_t`:

| Function | Return type |
| --- | --- |
| `sx_len`, `sx_len_view` | `int` (content byte count) |
| `sx_cap` | `int` (total allocation) |
| offsets (`sx_find*`, view fields, regex views) | `size_t` |

A buffer with more than `INT_MAX` content bytes is outside the tested
contract. This is a deliberate API simplification (the intended use case is
small-to-medium strings), not a bug; the practical ceiling is about 2 GiB
per buffer on common platforms. The same `int` narrowing appears where a
`strlen`-based needle length is cast to `int` in the search wrappers.

`SIZE_MAX` guards appear where a size arithmetic could wrap:

- `sx__reserve` rejects `need == SIZE_MAX` (it would not be able to add
  the terminator);
- the `sx_find_all*` array growth checks `cap > SIZE_MAX / 2 / sizeof(*r)`
  before doubling;
- there is no general overflow checking: extreme `n` or `off` arguments can
  wrap unsigned size arithmetic, and such calls are outside the tested
  contract.

### Platform assumptions

- The library is C23. The reference toolchains are GCC 14+ and Clang 19+
  on Linux (CI uses Ubuntu 24.04 with GCC 14 and Clang 19); the sources
  avoid compiler-specific extensions apart from `SYMBOL_PUBLIC` and the
  glibc `memrchr` fast path.
- `src/export.h` is a checked-in, Forge-derived header. It defines
  `SYMBOL_PUBLIC` as `__attribute__((visibility("default")))` for
  GCC/Clang and as `_declspec(dllexport)` for MSVC; the build uses hidden
  default visibility, so exactly the `SYMBOL_PUBLIC` functions are exported
  from `libstringx.so`.
- The regex layer needs PCRE2 10.46 (`pcre2-8`), 8-bit, non-UTF mode. The
  build links it into `libstringx`, so consumers do not add PCRE2 flags,
  but PCRE2 is a runtime dependency of the shared library.
- Install and Debian packaging targets are guarded by the `LINUX` macro in
  `build.c`; the library itself compiles as ordinary C23.

## Testing strategy

Every file in `tests/` is a self-contained program. Its header comments
declare how to build it (`// Include`, `// Source`, `// Option`, `// Link`,
`// Library` lines consumed by the pinned Forge build system), and document
its **behavior boundaries**: an "In scope (contract)" list and an
"Out of contract (not exercised)" list. The header is part of the
contract: when behavior changes, the boundary lists change with it (see
[Contributing](contributing.md)).

The suite (20 tests) maps one topic per file:

| Test | Pins |
| --- | --- |
| `access.c` | `sx_len` / `sx_cap` / buffer accessors / whole-buffer view |
| `append.c` | append family, ownership transfer, aliased appends |
| `chain.c` | edit chaining, handle identity, zero-leak ownership |
| `cmp.c` | lexicographic byte comparison |
| `ends_with.c` | suffix predicate |
| `find.c` | leftmost search, bounded regions, `strlen` needles |
| `find_all.c` | all occurrences, overlapping positions included |
| `insert.c` | insertion, aliasing across growth |
| `new.c` | constructors, `sx_dup`, capacity edge cases |
| `reg.c` | regex families, binary-safe subjects, zero-length handling |
| `reserve.c` | `sx_reserve` monotonicity and no-shrink rules |
| `reserve_stress.c` | exact-reserve model-based stress (1500 random edits) |
| `rfind.c` | rightmost search and bounded regions |
| `shrink.c` | truncate / erase / clear edge cases |
| `split.c` | split shape, end guard, zero-copy tokens |
| `starts_with.c` | prefix predicate |
| `substr.c` | copy-out independence in both directions |
| `view_move.c` | advance / retreat / shift / range clamping |
| `view_trim.c` | whitespace trim set and empty-view clamp |
| `write.c` | overwrite, append-at-end and padding semantics |

How the tests run:

- Each test compiles the library **sources directly**, with the per-test
  options from its header. Sanitizers are on: AddressSanitizer, UBSan and
  (on Linux, through ASan) LeakSanitizer, so the leak sanitizer is the
  witness for every ownership rule. CI sets
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
- `tests/reserve_stress.c` is the only test built with
  `-DSX_FORCE_EXACT_RESERVE`. It drives a deterministic LCG through append,
  insert, erase, write, truncate and aliased-view edits against a byte-exact
  model, and includes the aliased `insert_view` growth regression.
- CI additionally installs the built library into a temporary prefix,
  links `tests/reg.c` against the **installed shared library** through
  `pkg-config` (with an rpath), and runs it. That verifies the public
  headers, exported symbols and the PCRE2 link in addition to source-level
  behavior; it then inspects the generated Debian package.
- Fault-injection is absent, so allocation-failure branches are not
  exercised by the suite (the reserve test comments note that such a test is
  not expressible without fault injection). The documented OOM sentinels
  are reasoned from the code, not observed in tests.
- The `./make check` target compiles production code with
  `-Wall -Wextra -Wpedantic -Werror`; GCC builds also run `-fanalyzer`
  (with `-O1 -fno-analyzer-state-merge` in CI), and CodeQL scans with
  `security-and-quality` queries.
- Tests disable LTO (`set(TARGET_LTO, 1, DISABLE)` in `build.c`) because the
  pinned Forge emits GCC-only `-flto=N` flags for parallel Clang links.
  Production library targets keep LTO.

## Design trade-offs

| Choice | Benefit | Cost |
| --- | --- | --- |
| Single NUL-terminated allocation | cheap `cstr` interop, one `free`, contiguous content | one byte overhead; cstr variants truncate at embedded NUL |
| `sx_t` handle to a private `_sx` | stable handle across growth; private layout | one indirection and per-buffer allocation of the header |
| `(ref, offset, len)` views | zero-copy slices; uniform for split and regex results | lifetime rules; stale views after edits; raw pointers invalidated by growth |
| Geometric growth | amortized constant append cost | slack capacity; peak memory above `len` |
| Exact-reserve debug mode | redzone-tight allocations expose off-by-one bugs | slow; test-only |
| `int` length accessors | compact, simple API | ~2 GiB practical buffer ceiling; narrowing casts |
| Sentinel error model | no global state, no error plumbing, deterministic | "no result" vs OOM can be ambiguous; no diagnostics |
| Caller-frees arrays, library-frees handles | explicit, predictable ownership | two release functions (`free` vs `sx_free`) to know |
| KMP + `memchr`/`memrchr` | guaranteed `O(n + m)` worst case, simple code, vectorized one-byte case | prefix table allocation for `m >= 2`; average-case speed below optimized substring search |
| Overlapping results from `sx_find_all*` | complete position set; useful for byte scanning | callers must expect overlapping offsets |
| Non-overlapping regex scan | standard PCRE2 behavior; forward progress guaranteed | cannot report overlapping regex matches |
| Two-pass exact sizing | one allocation, no realloc growth, deterministic | roughly twice the matching work for array APIs |
| Thin PCRE2 wrapper, no options/JIT | tiny surface, full PCRE2 semantics, one dependency | no wrapper-level flags or tuning; PCRE2 linked at runtime |
| Explicit immutability of compiled regex, no internal locks | shareable handles without synchronization | buffers still need caller synchronization |
| Build-time configuration macros | zero runtime cost for growth/memrchr policy | a policy change requires a rebuild |

These are trade-offs, not accidents. The tests pin the observable
consequences, and the header comments name the out-of-contract side of each
rule.

## Licensing

Project sources are MIT licensed (see [LICENSE](../../LICENSE) and the SPDX
headers in the sources). The Forge-derived build tooling follows the
build-script exception: `build.conf` and `src/export.h` carry the
`GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception` SPDX
identifier, and `build.c` is covered by the same exception; the library
sources themselves remain MIT. The regex layer uses PCRE2 (BSD-3-Clause); see
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

---

[← Previous](regex.md) · [Docs home](README.md) · [Next →](build-and-test.md)
