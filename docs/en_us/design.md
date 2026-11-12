English · [中文](../zh_cn/design.md)

[Docs home](README.md) › Design & internals

# Design and internals

This page is for contributors who need to reason about the library's
guarantees or port it: the data layout, algorithms, ownership model and
trade-offs behind the `<sx.h>` contracts. The contracts themselves are in the
[API reference](api-buffer.md), [search reference](api-search.md) and
[regular expressions](regex.md) pages.

## Goals and non-goals

The design follows from a short set of goals:

- **Byte-exact contract.** Operations are defined on bytes and their
  boundaries written down. Search, comparison and regex work by length, so
  embedded NULs are ordinary data.
- **Zero-copy access.** Slices are `sx_view_t` values aliasing a buffer;
  inspecting subslices copies nothing.
- **One stable handle per buffer.** An `sx_t *` identifies its buffer for
  life; edits return `self`, so calls chain even when the array is reallocated.
- **Predictable failure.** Documented invalid input yields a fixed sentinel
  instead of crashing; other inputs are out of contract. No exceptions, error
  codes or global error state.
- **Small surface.** One header `<sx.h>`, one library, one advanced dependency
  (PCRE2) that the library links itself.

Equally deliberate non-goals:

- **No text encoding layer.** No UTF-8 decoding, normalization or character
  and grapheme operations. Strings are bytes; the regex layer runs PCRE2 in
  8-bit non-UTF mode.
- **No locale awareness.** Comparison, trimming and character classes are
  byte- and ASCII-defined, independent of `LC_*`.
- **No thread-safe buffers.** Compiled regexes are immutable and shareable;
  mutable buffers are plain memory needing external synchronization.
- **No custom allocators, ropes/streaming or lazy views.** Every buffer is one
  contiguous allocation.
- **No regex engine of its own.** Pattern semantics are PCRE2's; the library
  supplies only lifecycle, scanning and substitution plumbing.

## Layers and headers

### Header map

| Header | Layer | Contents | Audience |
| --- | --- | --- | --- |
| `include/sx_core.h` | core | `sx_t`, `sx_view_t`, buffer/view/edit/search/compare prototypes | public, included by `<sx.h>` |
| `include/sx.h` | advanced | includes `sx_core.h`; declares `sx_split` and `sx_reg_*` | public entry point |
| `include/sx_raw.h` | internal | opaque `_sx` plus the raw allocation helpers | not supported |
| `src/_sx.h` | internal | definition of `struct _sx` | library only |
| `src/export.h` | Forge-derived | `SYMBOL_PUBLIC` visibility macro (checked in) | library only |

Consumers include `<sx.h>`; `sx_core.h` is public only because `<sx.h>`
includes it. `sx_split` and the regex layer are declared only in `sx.h`.

The implementation mirrors the header split:

| Translation unit | Implements |
| --- | --- |
| `src/sx_core.c` | buffer, view, mutation, search, compare |
| `src/sx_raw.c` | allocation primitives (`sx__new`, `sx__reserve`, ...) |
| `src/sx_adv.c` | `sx_split` and the PCRE2 wrapper |

All three library TUs use the private `_sx` layout directly; client code
consumes only `sx.h` and `sx_core.h`.

### Why sx_split lives in the advanced layer

`sx_split` is declared in `sx.h` after `sx_core.h` and implemented in
`src/sx_adv.c`; the placement is intentional:

- It returns a `malloc`'ed array of views using the same iterator convention
  as `sx_reg_match_all`/`sx_reg_split` (tokens in order, `.ref == NULL` end
  guard, caller frees). Grouping the array-returning view APIs keeps that
  ownership convention in one place instead of adding an array-allocating API
  to the core.
- Its scan is self-contained: the core's KMP primitive is `static` in
  `sx_core.c` and unavailable to the advanced layer, and a split scan is
  bounded by the output size anyway. The source comment states the trade: the
  `O(n*m)` byte-exact scan "is fine for the advanced layer and keeps this TU
  independent".
- It has no PCRE2 dependency: it is a plain separator search that lives in the
  advanced TU by API shape, not dependency.

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

- it hands out `_sx`, the private buffer struct that `sx_core.h` only
  forward-declares;
- its functions use a reserved double-underscore prefix and bypass the public
  error model (`sx__reserve` returns `int`, not a sentinel `sx_t *`); they
  serve the library's TUs, not clients;
- it carries no stability promise. The installed header set includes it
  because the build copies every `include/*.h`, but it is not supported API and
  must not be used by client code. The four `sx__*` helpers are declared
  `SYMBOL_PUBLIC`, so they appear in the shared library's dynamic symbol
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
2. `len + 1 <= cap`: room for the terminator.
3. `ptr[len] == '\0'` always. Content may contain `'\0'`; the terminator is
   the first NUL *after* the content.
4. `len` counts content bytes, not the terminator; `sx_len` returns it as
   `int` (see [integer accessors](#integer-accessors-and-size_max)).
5. A returned view satisfies `ref = buffer.ref` and
   `offset + len <= buffer len`. Caller-built views are not validated; one
   exceeding its buffer is out of contract.
6. The `sx_t *` handle address is stable for the buffer's life; `ptr` from
   `sx_buf_mut` is **not** stable across growth because `realloc` can move it.
7. Bytes in `[len, cap)` are unspecified (stale or uninitialized); only
   `ptr[len]` is contractual.

The invariants are enforced structurally: every mutating function ends with
`ptr[len] = '\0'`, and `sx__reserve` is the only place that changes `cap` or
`ptr`. `sx_buf_mut` may rewrite `[0, len)` without changing `len`; the
terminator lies outside that range, so the invariant holds.

## Allocation and growth

### Initial capacities

Every buffer is one allocation from `sx__new(cap)`: the `_sx` header plus a
`cap`-byte array (minimum 1, so `ptr[0]` has a home). Constructor
capacities:

| Constructor | Initial `cap` | Maximum content bytes |
| --- | --- | --- |
| `sx_new()` | 32 | 31 |
| `sx_new_with_cap(n)` | `n`, or 1 when `n == 0` | `n - 1` (0 when `n <= 1`) |
| `sx_new_from_cstr(s)` | `strlen(s) + 1` | `strlen(s)` |
| `sx_new_from_view(v)` | `v.len + 1` | `v.len` |
| `sx_dup(src)` | `src->len + 1` | `src->len` |
| `sx_substr(h, idx, n)` | `n + 1` | `n` |

`sx_new()` goes through `sx_new_with_cap(32)`: the 32-byte start covers the
first few appends without wasting much memory. Content constructors allocate
exactly `content + 1`, so a fresh or copied buffer is tight.

### Growth policy

All growth goes through `sx__reserve(_sx *s, size_t need)`, where `need` is
the number of **content bytes** required afterwards. It must produce
`cap > need` — room for `need` bytes plus the terminator:

| Situation | New `cap` |
| --- | --- |
| `cap > need` | unchanged (no-op) |
| `need == SIZE_MAX` | rejected (integer overflow guard) |
| default, `cap < 0x10000` | `max(cap * 2, need + 1)` |
| default, `cap >= 0x10000` | `max(cap + 0x10000, need + 1)` |
| `-DSX_FORCE_EXACT_RESERVE` | `need + 1` |

Default growth doubles while the allocation is below 64 KiB, then adds a
fixed `+0x10000`. Doubling gives amortized constant cost per byte; the fixed
step avoids doubling a large allocation (say 8 MiB to 16 MiB) just to append a
few bytes. The `max` with `need + 1` covers a single call needing more than the
next geometric step.

The public `sx_reserve(self, n)` forwards `n` unchanged, guaranteeing
`cap >= n + 1`: after the call the buffer holds `n` content bytes total
without another reallocation. `sx_append`, `sx_write` and `sx_insert` call
`sx__reserve(final_content_length)` before copying:

- append: `need = len + appended`;
- write: `need = idx + written`; `len` afterwards is `max(len, idx + n)`;
- insert: `need = len + inserted`; the tail is shifted with `memmove`.

Growth is monotonic; shrinking never reallocates: `sx_truncate` reserves only
when growing, `sx_erase` moves bytes in place, and `sx_clear` rewrites
`ptr[0]`. The exact-reserve test asserts both properties
(`tests/reserve_stress.c`).

### SX_FORCE_EXACT_RESERVE

`-DSX_FORCE_EXACT_RESERVE` replaces the policy with `cap = need + 1` on
every growth. The allocation ends exactly at the terminator, so an access past
`len` lands in the AddressSanitizer redzone instead of hiding behind slack
capacity. The cost is a reallocation per growth (per-byte appends become
quadratic-ish in copies) and no slack, so it is a debugging/testing mode.
`tests/reserve_stress.c` is its dedicated configuration: every buffer starts at
`cap == 1`; a fixed LCG drives 1500 pseudo-random edits against a byte-exact
model; it asserts `cap == len + 1` after every reallocation and that shrinking
never reallocates.

## Views and aliasing

Views are the core abstraction: `sx_view_t` stores a buffer reference plus an
`(offset, len)` window. Returning a view never copies, allocates or modifies
the buffer. The same representation serves whole-buffer views (`sx_view`),
navigation (`sx_view_advance`, `sx_view_range`, ...), split tokens and regex
match arrays.

The rules a caller must respect:

- **Views are snapshots.** Edits do not adjust captured views; after a length
  change, re-derive them.
- **Growth invalidates raw pointers.** `sx_buf_mut`/`sx_buf_view_mut` borrow
  the array, and a later growing edit may `realloc` it. The `sx_t *` handle is
  stable, the pointer is not.
- **A view must not outlive its buffer.** `ref` is a raw `_sx *`; freeing the
  buffer frees the referent. Returning a view array does not transfer ownership
  of the buffer.
- **Do not edit while views are in use** — for `sx_split` tokens,
  `sx_reg_match_all` arrays and user-held views alike.
- **A `NULL` view means "none".** `{ref = NULL, offset = 0, len = 0}` is the
  sentinel; other library-returned views point into a live buffer.

Aliasing edits need care. When the source view refers to the same buffer as
the destination, `sx_append_view`, `sx_write_view` and `sx_insert_view` copy
with `memmove` rather than `memcpy`. Append and write are plain overlap-safe
moves, so any contract-valid source view works. `sx_insert_view` is
narrower:

1. reserve space for the **final** content length (keeps the source pointer
   valid: `realloc` updates `_sx.ptr` in place, so a pointer taken afterwards
   sees the new allocation);
2. shift the tail right by `v.len`, writing `[idx + n, len + n)`;
3. write the inserted bytes at `idx` with `memmove`.

The tail shift makes self-aliasing conditional: a source window starting at
or before the insertion point (`v.offset <= idx`) ends at or before `idx + n`,
so the shift cannot overwrite it and step 3's `memmove` handles the remaining
overlap. The exercised case is `v.offset == idx`: inserting view `[2, 4)`
(`"cd"`) into `"abcdef"` at index 2 gives `"abcdcdef"`. A window starting
**after** `idx` is overwritten before it is read: inserting `[3, 5)` (`"de"`)
into `"abcdef"` at index 2 yields `"abdccdef"`, not `"abdecdef"`; that call is
out of contract. The regression at the end of `tests/reserve_stress.c` covers
the `v.offset == idx` case with forced growth, pinning the
reserve-before-pointer ordering. The owned variants (`sx_append(self, self)`
and friends) free their argument, so self-aliasing is out of contract for
them.

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
  `sx_insert` attempt the edit, then free `owned` unconditionally; if the edit
  fails, the call returns `NULL` but the argument is still consumed. A `NULL`
  argument returns `NULL` and frees nothing.
- **Write-range rejection is documented.** If `idx + n` overflows `size_t`,
  or the resulting end cannot be reserved (`SIZE_MAX` content cannot include
  a terminator), `sx_write*` returns `NULL` before copying and leaves `self`
  unchanged. `sx_write_view` checks before its alias-preserving pre-reserve,
  so a rejected range cannot change capacity. `tests/write_overflow.c` and
  its exact-reserve sibling pin this contract.
- **Handle identity.** Every edit returns `self`, so successful edits chain:
  `sx_append_cstr(sx_append_cstr(s, "a"), "b") == s`. The handle is stable
  across growth; only the array moves.
- **Arrays do not own their elements' buffer.** Freeing a `sx_split` or
  `sx_reg_match_all` array does not free the subject; the views merely become
  unreachable.

The error model is sentinel-based: no `errno`, error object or error return
code.

| Situation | Sentinel |
| --- | --- |
| offset search (`sx_find*`, `sx_rfind*`) | `(size_t)-1` |
| "no occurrences" (`sx_find_all*`) | `NULL` array |
| "no match" view (`sx_reg_match`) | `{ref = NULL, offset = 0, len = 0}` |
| "no match" array (`sx_reg_match_all`) | `NULL` |
| invalid/empty separator, invalid regex, OOM in an array/string factory | `NULL` |
| write range overflow or unreservable end | `NULL`; `self` unchanged (owned input consumed) |
| boolean predicates and `sx_reg_is_match` | `0` |

Two consequences are intentional and documented:

- **"No result" and "out of memory" can be indistinguishable.** The
  `find_all` and `sx_reg_*` array families return `NULL` for both, having no
  third channel. Replace distinguishes them: no match returns an unchanged
  copy; `NULL` means invalid input or failure.
- **NULL handle arguments are mostly out of contract.** The explicitly
  checking families — `sx_new_from_cstr`, `sx_dup`, cstr/owned mutation
  arguments, `sx_find*`, `sx_rfind*`, `sx_find_all*`,
  `sx_cmp*`/`sx_starts_with*`/`sx_ends_with*`, `sx_split` and `sx_reg_*` —
  have defined `NULL` behavior. Accessors (`sx_len`, `sx_cap`, `sx_buf_mut`,
  `sx_view`, ...), view movers and `sx_free` dereference their handle and
  reject `NULL`; headers state the boundaries per function.

Errors never leave a partially built buffer: a failed growth happens before
any copy, so `self` keeps its content and the single-NUL invariant. The one
exception is the consumed `owned` argument.

## Search internals

The whole search family is built on one `static` primitive in
`src/sx_core.c`:

```c
static inline find_ret_t find(const sx_t *h, const char *needle, int len,
                              size_t boundary, int opt);
```

The return type is a position or a pointer to a result array, so the
implementation uses an integer type wide enough to round-trip a pointer:

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
`boundary = 0`, `sx_rfind*` use `boundary = len(h)` with the right flag, and
`sx_find_view_b`/`sx_rfind_view_b` pass the caller's boundary. `len <= 0`, an
empty region or a region shorter than the needle is "not found" —
`(size_t)-1` for scalar wrappers, a count-0 array internally (converted to
`NULL` by `sx_find_all*`) for array wrappers.

`len` is the needle content length (`sx_t` needle), `v.len` (view needle) or
`(int)strlen(s)` (cstr needle — the narrowing cast is one practical `int`
limit). Needle bytes are read directly from the view or buffer; search is
byte-exact like `memcmp`.

### One-byte needles: memchr and memrchr

For `m == 1` the primitive uses the C library's vectorized scans:

- **leftmost:** `memchr(t + l, c, er - l)`;
- **rightmost:** `memrchr(t, c, er)` on glibc. `memrchr` is a GNU extension,
  not ISO C, so `sx_core.c` declares it by hand and uses it only when
  `__GLIBC__` is defined and `SX_FORCE_NO_MEMRCHR` is not. The fallback is a
  byte-wise backward loop keeping the last (rightmost) hit before `l`.

The rightmost fast path may search from `0` because a right search always
sets `l = 0`; `er` already carries the boundary. `SX_FORCE_NO_MEMRCHR` exists
to build and test the portable path on glibc.

For `m == 1` in "all" mode the implementation calls `memchr` repeatedly,
starting at the previous hit + 1. That reports **overlapping** positions,
which is exactly what `sx_find_all*` promises (`"aa"` in `"aaaa"` reports 0, 1
and 2).

### Native forward fast path

Leftmost first-match calls (`sx_find`, `sx_find_view`, `sx_find_view_b`,
`sx_find_cstr`) go through `find_first()`; on glibc it may take a native path
before the portable KMP implementation. `try_native_find_first` is
`noinline`, so its branches do not alter the fallback's optimization:

1. reject an empty/negative needle or a region too short for one window;
2. `m == 1`: one `memchr`;
3. compare the window at the region start (front hits need no preprocessing);
4. `memchr` for the needle's first byte from the next offset, then `memchr`
   for its last byte, narrowing the candidate start to where a window can end;
5. for `m <= 32`, `memmem` over the remaining region; otherwise decline.

A decline falls back to KMP. The native path allocates nothing; only the
fallback's prefix table does. It is compiled out on non-glibc targets and with
`-DSX_FORCE_NO_MEMMEM`; `tests/find_differential_fallback.c` runs the same
differential cases through the forced-portable build.

### Longer needles: KMP

For `m >= 2` the fallback (and the primitive used by `sx_rfind*` and
`sx_find_all*`) computes a KMP prefix table (`pi`, one
`malloc(m * sizeof(size_t))`) and scans the region in `O(n + m)` time:

- **leftmost-first and all mode** use forward KMP over `[l, er)`. On a full
  match, first mode returns; all mode records the start and resumes with
  `q = pi[m - 1]`, which reports overlapping occurrences.
- **rightmost-first** uses reverse KMP: the same prefix table over the
  *reversed* needle by index mapping (no copy), scanning from `er - 1` down to
  `l`. When the matched suffix reaches `m`, the current index is the window
  start.

The all-mode array starts at capacity 16, doubles with an overflow guard
(`cap > SIZE_MAX / 2 / sizeof(*r)` aborts with `NULL`), then shrinks once with
`realloc`. Layout: `{count, pos0, pos1, ...}` — `arr[0]` is the count, not a
position.

### Complexity

| Case | Technique | Time | Extra memory |
| --- | --- | --- | --- |
| one-byte needle, first left | `memchr` | `O(n)` | none |
| one-byte needle, first right | `memrchr`, or backward scan | `O(n)` | none |
| needle length >= 2, first left (native) | byte checks + `memmem` (`m <= 32`) | `O(n + m)` typical | none |
| needle length >= 2, first left (fallback) | KMP | `O(n + m)` | `O(m)` prefix table |
| needle length >= 2, first right | reverse KMP | `O(n + m)` | `O(m)` prefix table |
| `sx_find_all*`, one byte | repeated `memchr` | `O(n)` scan | `O(k)` result |
| `sx_find_all*`, longer needle | KMP | `O(n + m)` scan | `O(m) + O(k)` result |
| `sx_split` | two passes of `is_sep` | `O(H * m)` worst case | `O(tokens)` result |

`n` is the region length, `m` the needle length, `k` the number of matches.
Array wrappers allocate only the result. `sx_find*` allocate nothing on the
native path; the KMP fallback (and every multi-byte `sx_rfind*`) allocates a
temporary prefix table, freed before returning.

## Regex wrapper internals

The advanced regex layer is intentionally thin. Its whole state is one
compiled PCRE2 code object:

```c
typedef struct sx_reg_t {
  pcre2_code *code;
} sx_reg_t;
```

### Compile and release

`sx_reg_compile` calls
`pcre2_compile(pattern, PCRE2_ZERO_TERMINATED, 0, ...)`. The `0` is the
option word (no `CASELESS`, no `UTF`, nothing) and `PCRE2_ZERO_TERMINATED`
means a C-string pattern. The PCRE2 error code and offset are captured only
for the signature and discarded, so every compile failure collapses to
`NULL`. Compilation allocates the code object and the small `sx_reg_t`;
`sx_reg_free` frees both and accepts `NULL`.

### Per-call match data and the scan rule

PCRE2 separates the compiled pattern from per-call match data. The wrapper
creates match data with `pcre2_match_data_create_from_pattern(r->code, NULL)`
and frees it before returning, so no match state lives in the handle — hence a
compiled handle is thread-shareable.

A match step is `pcre2_match(code, subject, len, offset, 0, md, NULL)`: the
subject is passed with an explicit **length**, not as a C string, and the
`NULL` match context means PCRE2's default match limits apply. A negative
return (no match or a match-time error such as a match limit) means "no further
match"; match-time error codes are not exposed.

The scan shared by `sx_reg_match_all` and `sx_reg_split` advances with:

```c
static size_t reg_next_at(size_t start, size_t end) {
  return end > start ? end : start + 1;
}
```

The next search starts at the match end, or one byte past the start for a
zero-length match, so the scan always advances. `pcre2_match` gets `offset` on
every step; once it passes the subject length PCRE2 errors and the loop ends.
Hence `a*` on `"bbb"` reports empty matches at offsets 0–3, and `$` on `"abc"`
one empty match at offset 3.

### Two-pass sizing for match_all and split

Both array families run the scan twice:

```text
pass 1: count matches             -> cnt
malloc((cnt + 1) * sizeof view)   -> exact size, no realloc
pass 2: repeat the same scan      -> fill views, then arr[cnt] = {0} guard
```

The scan is deterministic (same pattern, subject and start offsets), so both
passes see identical positions. `sx_reg_split` also tracks the gap start
`seg`: each token is `[seg, match_start)` and `seg` moves to `match_end`; the
final token is `[seg, len)`. That gives exactly `matches + 1` tokens, including
empty edge tokens and empty tokens from adjacent or zero-length matches.

### Substitution: two pcre2_substitute calls

Replace is built from PCRE2's substitution routine, with two fixed flags:

```c
options |= PCRE2_SUBSTITUTE_UNSET_EMPTY | PCRE2_SUBSTITUTE_UNKNOWN_UNSET;
```

`UNSET_EMPTY` expands a group that did not participate to nothing;
`UNKNOWN_UNSET` expands a non-existent group number to nothing instead of
failing. `sx_reg_replace` adds `PCRE2_SUBSTITUTE_GLOBAL`;
`sx_reg_replace_first` does not.

Because output length is unknown beforehand, the wrapper substitutes
twice:

```text
call 1: output buffer NULL, PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, &needed
        -> PCRE2_ERROR_NOMEMORY, `needed` = required size including NUL
malloc(needed)                                       (single temporary)
call 2: fill the buffer, &used = needed
        -> rc >= 0, `used` = bytes written excluding NUL
sx_new_with_cap(used + 1)                            (new public buffer)
memcpy(ptr, buf, used); len = used; ptr[used] = '\0'
```

The temporary buffer is freed after the copy. A first call not returning
`PCRE2_ERROR_NOMEMORY` means the template/replacement pair was rejected: the
wrapper returns `NULL` without touching the subject. With no match, PCRE2
substitutes nothing and the output is an unchanged copy — normal success, not
an error.

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

The wrapper enables no PCRE2 JIT (`pcre2_jit_compile` is never called) and
exposes no compile or match options; pattern-level features such as inline
options still work because they belong to the pattern, not the wrapper's
policy.

## Concurrency

- **Compiled regex handles are immutable and shareable.** Per-call state
  lives in caller-owned match data created inside the call, and the wrapper has
  no globals; one `sx_reg_t *` may serve concurrent threads.
- **Buffers are not synchronized.** They are ordinary heap memory: concurrent
  reads are fine while nobody mutates; mutation needs external synchronization.
  Views make this stricter — a view read concurrent with a length-changing edit
  is a data race (out of contract).
- **Ownership must be coordinated.** A single-owner buffer must not be consumed
  by two threads; arrays are single-owner results.

The library never mutates global state, so it is safe in multithreaded
programs provided the caller synchronizes each buffer's access.

## Portability and limits

### memrchr, memmem and the portable switches

The search path uses two glibc extensions: `memrchr` (rightmost one-byte scan)
and `memmem` (native forward fast path). `sx_core.c` declares both by hand and
compiles them in only when `__GLIBC__` is defined. `-DSX_FORCE_NO_MEMRCHR` and
`-DSX_FORCE_NO_MEMMEM` force the portable paths; the in-tree suite runs the
glibc path plus a forced-portable differential search test, so both fallbacks
are built and checked on glibc too.

### Integer accessors and SIZE_MAX

The public accessors return `int` while offsets and needles use `size_t`:

| Function | Return type |
| --- | --- |
| `sx_len`, `sx_len_view` | `int` (content byte count) |
| `sx_cap` | `int` (total allocation) |
| offsets (`sx_find*`, view fields, regex views) | `size_t` |

A buffer over `INT_MAX` content bytes is outside the tested contract. This is
a deliberate simplification for small-to-medium strings, not a bug; the
practical ceiling is ~2 GiB per buffer. The same `int` narrowing appears where
`strlen`-based needle lengths are cast to `int` in search wrappers.

`SIZE_MAX` guards appear where a size arithmetic could wrap:

- `sx__reserve` rejects `need == SIZE_MAX` (it could not add the terminator);
- `sx_find_all*` growth checks `cap > SIZE_MAX / 2 / sizeof(*r)` before
  doubling;
- there is no general overflow checking: extreme `n` or `off` can wrap unsigned
  arithmetic, which is outside the tested contract.

### Platform assumptions

- The library is C23, with GCC 14+ and Clang 19+ on Linux as reference
  toolchains (CI: Ubuntu 24.04, GCC 14, Clang 19). Sources avoid
  compiler-specific extensions apart from `SYMBOL_PUBLIC` and glibc `memrchr`.
- `src/export.h` is a checked-in, Forge-derived header defining
  `SYMBOL_PUBLIC` as `__attribute__((visibility("default")))` for GCC/Clang and
  `_declspec(dllexport)` for MSVC. The build uses hidden default visibility, so
  exactly the `SYMBOL_PUBLIC` functions are exported from `libstringx.so`.
- The regex layer needs PCRE2 10.46 (`pcre2-8`, 8-bit, non-UTF). The build
  links it into `libstringx`, so consumers add no PCRE2 flags, but it remains a
  runtime dependency.
- Install and Debian packaging targets are guarded by the `LINUX` macro in
  `build.c`; the library itself compiles as ordinary C23.

## Testing strategy

Every file in `tests/` is a self-contained program. Its header comments
declare how to build it (`// Include`, `// Source`, `// Option`, `// Link`,
`// Library` lines consumed by the build system) and document its
**behavior boundaries**: an "In scope (contract)" list and an "Out of contract
(not exercised)" list. The header is part of the contract — behavior changes
must update the boundary lists (see [Contributing](contributing.md)).

The suite (24 tests) maps one topic per file:

| Test | Pins |
| --- | --- |
| `access.c` | `sx_len` / `sx_cap` / buffer accessors / whole-buffer view |
| `append.c` | append family, ownership transfer, aliased appends |
| `chain.c` | edit chaining, handle identity, zero-leak ownership |
| `cmp.c` | lexicographic byte comparison |
| `ends_with.c` | suffix predicate |
| `find.c` | leftmost search, bounded regions, `strlen` needles |
| `find_all.c` | all occurrences, overlapping positions included |
| `find_differential.c` | leftmost search against an independent window oracle (native path) |
| `find_differential_fallback.c` | the same cases with `-DSX_FORCE_NO_MEMMEM` (portable KMP path) |
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
| `write_overflow.c` | `SIZE_MAX` write ranges return `NULL`, leave `self` unchanged, consume owned input |
| `write_overflow_exact.c` | the same cases under `-DSX_FORCE_EXACT_RESERVE` |

Some tests share a local case header between the normal and forced
configuration (`find_differential.h`, `write_overflow_cases.h`); each `.c` is
still its own target.

How tests run:

- Each test compiles the library **sources directly** with its header
  options. Sanitizers are on: AddressSanitizer, UBSan and (on Linux, through
  ASan) LeakSanitizer — the leak sanitizer witnesses every ownership rule. CI
  sets `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
- `tests/reserve_stress.c` and `tests/write_overflow_exact.c` are built with
  `-DSX_FORCE_EXACT_RESERVE`. The first drives a deterministic LCG through
  append, insert, erase, write, truncate and aliased-view edits against a
  byte-exact model, including the aliased `insert_view` growth regression; the
  second repeats the overflow cases with exact-size allocations.
- Dedicated audit workflows also run: Write safety reproduces the overflow
  against the immutable old implementation and compares write cost; Search
  performance compares the baseline; benchmarks rerun the audited
  StringX/SDS/bstrlib harness (see [ci.md](ci.md)).
- CI also installs the library into a temporary prefix, links `tests/reg.c`
  against the **installed shared library** via `pkg-config` (with rpath), runs
  it, and inspects the generated Debian package — verifying public headers,
  exported symbols and the PCRE2 link.
- Fault injection is absent, so allocation-failure branches are not exercised;
  the reserve test notes such a test is not expressible without it. Documented
  OOM sentinels are reasoned from code, not observed.
- The `./make check` target compiles production code with
  `-Wall -Wextra -Wpedantic -Werror`; GCC builds also run `-fanalyzer` (with
  `-O1 -fno-analyzer-state-merge` in CI), and CodeQL scans with
  `security-and-quality` queries.
- Tests disable LTO (`set(TARGET_LTO, 1, DISABLE)` in `build.c`) because the
  pinned build tool emits GCC-only `-flto=N` for parallel Clang links;
  production library targets keep LTO.

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
| Native first-match fast path | faster common-case search on glibc, allocation-free for `m <= 32` | extra libc-specific code and a forced-portable test |
| Checked write-range arithmetic | arithmetic overflow returns a documented `NULL` instead of corrupting memory | one branch per write; owned input is still consumed |
| KMP + `memchr`/`memrchr` | guaranteed `O(n + m)` worst case, simple code, vectorized one-byte case | prefix table allocation for `m >= 2`; average-case speed below optimized substring search |
| Overlapping results from `sx_find_all*` | complete position set; useful for byte scanning | callers must expect overlapping offsets |
| Non-overlapping regex scan | standard PCRE2 behavior; forward progress guaranteed | cannot report overlapping regex matches |
| Two-pass exact sizing | one allocation, no realloc growth, deterministic | roughly twice the matching work for array APIs |
| Thin PCRE2 wrapper, no options/JIT | tiny surface, full PCRE2 semantics, one dependency | no wrapper-level flags or tuning; PCRE2 linked at runtime |
| Explicit immutability of compiled regex, no internal locks | shareable handles without synchronization | buffers still need caller synchronization |
| Build-time configuration macros | zero runtime cost for growth/memrchr policy | a policy change requires a rebuild |

These are trade-offs, not accidents: the tests pin the observable
consequences and the header comments name each rule's out-of-contract side.

## Licensing

Project sources are MIT (see [LICENSE](../../LICENSE) and SPDX headers). The
Forge-derived build tooling follows the build-script exception: `build.conf`,
`build.c` and `src/export.h` carry the
`GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception` SPDX
identifier; the library sources remain MIT. The regex layer uses PCRE2
(BSD-3-Clause); see
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

---

[← Previous](regex.md) · [Docs home](README.md) · [Next →](build-and-test.md)
