# String X

A small C string library with a **single-header API** and a byte-exact,
zero-copy view model.  Version **v0.5**.  The library itself is built and
linked as **libstringx** (`-lstringx`); the project lives at
[github.com/minelogy-dev/StringX](https://github.com/minelogy-dev/StringX).

To use it you only need:

```c
#include <sx.h>
```

and link `-lstringx` — one header, one library.

**Documentation:** the full manual lives in [`docs/`](docs/README.md) —
[Getting started](docs/en_us/getting-started.md), [User guide](docs/en_us/guide.md),
[API reference](docs/en_us/api-buffer.md), [Regex](docs/en_us/regex.md),
[Design](docs/en_us/design.md), [Build & test](docs/en_us/build-and-test.md).
The Chinese edition is under [`docs/zh_cn/`](docs/zh_cn/README.md).

## Design

- **Buffer model.** Every `sx_t` owns a single NUL-terminated buffer:
  `ptr[0 .. len-1]` is the content, `ptr[len]` is always `'\0'`, and `cap`
  is the total allocation.  `len` counts **content bytes**, not the
  terminator, and matching/searching is by length — embedded NULs are
  ordinary data (binary-safe).
- **Views.** `sx_view_t {ref, offset, len}` aliases a slice of a buffer with
  **no copy and no modification** of the original.  Many APIs return views
  (the `ref == NULL` sentinel marks "none").
- **Layers.** `sx_core.h` provides the core buffer, view, search and
  compare operations (reached through `sx.h`); the advanced layer adds
  splitting (`sx_split`) and regex through **PCRE2** (`\d \w \b …` are
  ASCII in 8-bit non-UTF mode).
- **Deterministic contracts.** Every function documents its exact behavior
  boundaries in `sx.h`: documented miss/error cases return sentinel values,
  and anything outside those boundaries is out of contract.

## Requirements

- A C23 compiler (GCC and Clang), Linux
- [Forge](https://github.com/minelogy-dev/forge) to build from source
- `libpcre2-dev` (PCRE2 8-bit) — the regex layer's dependency; the shared
  library links `pcre2-8` itself, so consumers only need `-lstringx`
- `pkg-config` (or `pkgconf`) recommended for consumers — the installed
  package ships `libstringx.pc`

## Build

```sh
forge .        # generate ./make
./make         # build -> build/output/libstringx.so (+ headers at build/output/include)
./make test    # build and run every test under tests/ (ASan + UBSan)
```

See [Build, test & package](docs/en_us/build-and-test.md) for installation,
packaging and the test harness.

## Quick start

Edits return the handle they were given, so the whole chain is one
expression: `sx_append` consumes its `sx_t *` argument, and one `sx_free`
releases everything.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  /* One expression: every call returns the same handle, and the temporary
     buffer is consumed by sx_append, so there is nothing else to free. */
  sx_t *msg = sx_append(sx_append_cstr(sx_new(), "Hello, "),
                        sx_new_from_cstr("String X!"));

  printf("%s (%d bytes)\n", sx_buf_mut(msg), sx_len(msg));
  sx_free(msg);   /* one buffer, one free */
  return 0;
}
```

It prints `Hello, String X! (16 bytes)`.

Compile with (manually, or via pkg-config):

```sh
cc app.c -I<path-to-include> -L<path-to-lib> -lstringx
cc app.c $(pkg-config --cflags --libs libstringx)   # after install
```

## Regex (advanced layer: `sx_reg_*`)

Compile a pattern once, run it against any number of buffers, free it:

```c
sx_reg_t *re = sx_reg_compile("(\\w+)@(\\w+)");
sx_t *h = sx_new_from_cstr("bob@example.com and alice@example.org");

if (sx_reg_is_match(re, h)) { ... }            /* 0/1, cheapest */
sx_view_t m = sx_reg_match(re, h);             /* leftmost match as a view  */
sx_view_t *all = sx_reg_match_all(re, h);      /* array, end guard ref==NULL */
sx_t *out = sx_reg_replace(re, h, "[$2: $1]"); /* new string, $n backrefs   */
sx_view_t *tok = sx_reg_split(re, h);          /* tokens between matches    */

sx_reg_free(re);
```

Contract highlights (full detail in `sx.h`):

- **Match family** — `sx_reg_is_match` / `sx_reg_match` (leftmost, one match
  call) / `sx_reg_match_all` (non-overlapping left-to-right scan).
- **Replace family** — `sx_reg_replace` (all) / `sx_reg_replace_first`; the
  template uses PCRE2 substitution syntax (`$0`/`$n`); unset or non-existent
  groups expand to the empty string; h is never modified.
- **Split family** — `sx_reg_split`; same shape as `sx_split` (empty tokens
  kept, count == matches + 1).
- Subjects are bytes: binary-safe in both directions.  Zero-length matches
  are reported and the scan resumes one byte further right (a pattern like
  `a*` cannot stall it).  A compiled regex is immutable and thread-shareable.

## API map

| Group | Functions |
|---|---|
| Allocate / free | `sx_new`, `sx_new_from_cstr`, `sx_new_from_view`, `sx_new_with_cap`, `sx_dup`, `sx_free` |
| Accessors | `sx_len`, `sx_len_view`, `sx_cap`, `sx_buf_mut`, `sx_buf_view_mut`, `sx_view` |
| Mutate | `sx_append*`, `sx_write*`, `sx_insert*`, `sx_truncate`, `sx_erase`, `sx_clear`, `sx_reserve`, `sx_substr` |
| Views | `sx_view_advance`, `sx_view_retreat`, `sx_view_shift`, `sx_view_range`, `sx_view_trim…` |
| Search | `sx_find*`, `sx_rfind*`, `sx_find_all*` |
| Compare | `sx_cmp*`, `sx_starts_with*`, `sx_ends_with*` |
| Split | `sx_split(sep)` |
| Regex | `sx_reg_compile`, `sx_reg_free`, `sx_reg_is_match`, `sx_reg_match`, `sx_reg_match_all`, `sx_reg_replace`, `sx_reg_replace_first`, `sx_reg_split` |

This table is an index only: [API reference: buffers & views](docs/en_us/api-buffer.md)
and [API reference: search, compare & split](docs/en_us/api-search.md) document
every function's exact contract.

## Tests

Pull requests run compiler, sanitizer, static-analysis and secret checks.
See [Continuous integration](docs/en_us/ci.md) for coverage and merge-protection setup.

```sh
./make test
```

Every file in `tests/` is a self-contained program (dependencies declared
in its header comments) that compiles the library sources directly and runs
under AddressSanitizer + UBSan + LeakSanitizer.  Each test file documents
the exact behavior boundaries it locks in.

## License

MIT — see [LICENSE](LICENSE) and the SPDX headers in the source files.  The
regex layer depends on **PCRE2** (BSD-3-Clause); its attribution and license
are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
