English · [中文](../zh_cn/getting-started.md)

[Docs home](README.md) › Getting started

# Getting started

String X is a small C23 string library that treats one string as a single
NUL-terminated buffer and every slice of it as a zero-copy view. Buffers are
handled by byte length, so embedded NUL bytes are ordinary data and nothing is
lost to C-string truncation. The public surface is one header, `<sx.h>`, and
one library, `libstringx` (`-lstringx`); an optional advanced layer adds
regular expressions through PCRE2. This page takes you from a fresh checkout
to a running program. It describes **v0.5**.

## Requirements

| Requirement | Notes |
| --- | --- |
| Linux | Tested platform; the build and tests target Linux |
| C23 compiler | GCC 14+ or Clang 19+; CI runs GCC 14 and Clang 19 on Ubuntu 24.04 |
| `forge` | `https://github.com/minelogy-dev/forge`; compiles `build.c` into a local `./make` executable |
| `libpcre2-dev` | PCRE2 10.46, 8-bit API; needed to build; the library links `pcre2-8` |
| `pkg-config` | Recommended for consumers; the installed package ships `libstringx.pc` |

## Build from source

```sh
forge .        # compile build.c into ./make (re-run whenever build.c changes)
./make         # build -> build/output/libstringx.so
./make test    # build and run every test under tests/ (ASan + UBSan + LSan)
```

`./make` also syncs the public headers to `build/output/include`. `./make test`
runs each `tests/*.c` file as a self-contained program under AddressSanitizer,
UBSan and LeakSanitizer; use `./make test -j 2` to run tests in parallel.
`build.conf` selects the compiler and is the single source of truth for the
version.

## Install

```sh
./make install                  # default prefix: /usr/local (may need sudo)
./make install "$HOME/.local"   # user-owned prefix
```

The install target builds first, then copies the artifacts:

| Path under `<prefix>` | Contents |
| --- | --- |
| `lib/libstringx.so` | the shared library |
| `include/sx.h` | the public header, plus `sx_core.h` and `sx_raw.h` |
| `lib/pkgconfig/libstringx.pc` | pkg-config metadata |
| `share/doc/libstringx/` | `LICENSE`, `THIRD_PARTY_NOTICES.md`, `copyright` |

### Consuming the installed library

```sh
cc app.c $(pkg-config --cflags --libs libstringx) -o app
```

For a prefix that is not in the default search path, point pkg-config at the
installed metadata and, for a non-system prefix, set the runtime library path:

```sh
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig"
cc app.c $(pkg-config --cflags --libs libstringx) -o app
LD_LIBRARY_PATH="$HOME/.local/lib" ./app
```

Without pkg-config, the same build is:

```sh
cc app.c -I<prefix>/include -L<prefix>/lib -lstringx -o app
```

You link only `-lstringx`: `libstringx` links `pcre2-8` itself, so no extra
library flags are needed. The shared library does need `libpcre2-8` present at
run time.

### Debian package

```sh
./make gen_deb
sudo apt install ./build/deb/libstringx_0.5_amd64.deb
```

`gen_deb` stages `build/deb/libstringx_<version>_<arch>.deb`; the version comes
from `build.conf` with the leading `v` stripped, and the architecture from
`uname -m` (`x86_64` becomes `amd64`, `aarch64` becomes `arm64`). The package
depends on `libc6` and `libpcre2-8-0`.

## Hello, String X

This is the quick-start program from the README. It copies a C string into an
owned buffer, finds the first comma, copies the text before it into a second
owned buffer, and prints that. Both buffers are released with `sx_free`.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *msg = sx_new_from_cstr("hello, world");
  size_t at = sx_find_cstr(msg, ", ");
  sx_t *hi = sx_substr(msg, 0, at);
  fwrite(sx_buf_mut(hi), 1, (size_t)sx_len(hi), stdout);
  putchar('\n');                        /* hello */
  sx_free(hi);
  sx_free(msg);
  return 0;
}
```

After installing as shown above, build and run it with:

```sh
cc hello.c $(pkg-config --cflags --libs libstringx) -o hello
./hello
```

```text
hello
```

From the source tree, before installing, use the build output instead:

```sh
cc hello.c -Ibuild/output/include -Lbuild/output -lstringx -o hello
LD_LIBRARY_PATH=build/output ./hello
```

## Ten-minute tour

Five things users do with the library, one small program each. Every example
is complete and can be compiled the same way as `hello.c` above.

### Create a buffer

`sx_new` returns an empty buffer, `sx_new_from_cstr` copies a C string, and
`sx_dup` makes an independent deep copy. All results are caller-owned.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *empty = sx_new();                  /* len 0 */
  sx_t *hello = sx_new_from_cstr("hello");
  sx_t *copy = sx_dup(hello);
  printf("empty: len=%d cap=%d\n", sx_len(empty), sx_cap(empty));
  printf("hello: len=%d [%s]\n", sx_len(hello), sx_buf_mut(hello));
  printf("copy:  len=%d [%s]\n", sx_len(copy), sx_buf_mut(copy));
  sx_free(empty);
  sx_free(hello);
  sx_free(copy);
  return 0;
}
```

```text
empty: len=0 cap=32
hello: len=5 [hello]
copy:  len=5 [hello]
```

### Append and insert

Every edit returns the buffer it was given, so edits chain and the handle
stays the same even when the backing array grows.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *s = sx_new_from_cstr("abc");
  s = sx_append_cstr(s, "def");    /* "abcdef" */
  s = sx_insert_cstr(s, 3, "-");   /* "abc-def" */
  s = sx_write_cstr(s, 0, "A");    /* "Abc-def" */
  printf("%s (len %d)\n", sx_buf_mut(s), sx_len(s));
  sx_free(s);
  return 0;
}
```

```text
Abc-def (len 7)
```

### Find

`sx_find*` returns the leftmost byte offset, `sx_rfind*` the rightmost; the
not-found sentinel is `(size_t)-1`.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *s = sx_new_from_cstr("one two one");
  size_t first = sx_find_cstr(s, "one");
  size_t last = sx_rfind_cstr(s, "one");
  printf("first=%zu last=%zu\n", first, last);
  if (sx_find_cstr(s, "three") == (size_t)-1)
    printf("three: not found\n");
  if (sx_starts_with_cstr(s, "one"))
    printf("starts with one\n");
  sx_free(s);
  return 0;
}
```

```text
first=0 last=8
three: not found
starts with one
```

### Views and split

A view is `{ref, offset, len}` and aliases the buffer without copying.
`sx_split` returns an array of such views, terminated by an end guard whose
`ref` is `NULL`; the array is freed with `free`.

```c
#include <sx.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  sx_t *csv = sx_new_from_cstr("id,name,email");
  sx_view_t head = sx_view_advance(sx_view(csv), 0);
  head.len = 2;                        /* "id" - a view, no copy */
  printf("head: %.*s\n", sx_len_view(head), sx_buf_view_mut(head));

  sx_view_t *fields = sx_split(csv, ",");
  for (size_t i = 0; fields[i].ref != NULL; i++)
    printf("field %zu: %.*s\n", i, (int)fields[i].len,
           sx_buf_view_mut(fields[i]));
  free(fields);                        /* the array, not the views */
  sx_free(csv);
  return 0;
}
```

```text
head: id
field 0: id
field 1: name
field 2: email
```

### Regex

The advanced layer wraps PCRE2. Compile a pattern once, run it against any
number of buffers, then free it. `sx_reg_replace` returns a new buffer and
never modifies its subject.

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_reg_t *re = sx_reg_compile("(\\w+)@(\\w+\\.\\w+)");
  if (!re)
    return 1;
  sx_t *msg = sx_new_from_cstr("bob@example.com, alice@example.org");
  if (sx_reg_is_match(re, msg)) {
    sx_view_t m = sx_reg_match(re, msg);
    printf("first match: %.*s\n", (int)m.len, sx_buf_view_mut(m));
  }
  sx_t *out = sx_reg_replace(re, msg, "$1 at $2");
  printf("replaced: %s\n", sx_buf_mut(out));
  sx_free(out);
  sx_free(msg);
  sx_reg_free(re);
  return 0;
}
```

```text
first match: bob@example.com
replaced: bob at example.com, alice at example.org
```

Patterns are C strings and are matched in PCRE2's 8-bit non-UTF mode, so `\w`
and friends are ASCII. Subjects are matched by byte length and may contain any
byte, including NUL.

## Documentation map

| Document | What it covers |
| --- | --- |
| [Documentation home](README.md) | Language chooser and the full document index |
| [User guide](guide.md) | Mental model, ownership, binary safety, recipes, pitfalls |
| [API reference: buffers & views](api-buffer.md) | Creation, capacity, accessors, mutation, views |
| [API reference: search, compare & split](api-search.md) | `sx_find*`, `sx_rfind*`, `sx_cmp*`, `sx_split` |
| [Regular expressions](regex.md) | The `sx_reg_*` match, replace and split families |
| [Design & internals](design.md) | Data layout, growth policy, algorithms, limits |
| [Build, test & package](build-and-test.md) | Forge targets, test harness, installation |
| [Contributing](contributing.md) | Workflow, style, tests, licensing |
| [Continuous integration](ci.md) | Workflows, required checks, CodeQL |

---

[Docs home](README.md) · [Next →](guide.md)
