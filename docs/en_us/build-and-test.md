English · [中文](../zh_cn/build-and-test.md)

[Docs home](README.md) › Build, test & package

# Build, test & package

String X builds with [Forge](https://github.com/minelogy-dev/forge). This page
covers prerequisites, `./make` tasks, the test harness and sanitizers,
installation, pkg-config and Debian packaging. CI is described in
[Continuous integration](ci.md).

## Prerequisites

| Requirement | Notes |
| --- | --- |
| Linux | CI uses Ubuntu 24.04; other Linux distributions work. |
| C23 compiler | GCC 14+ or Clang 19+. |
| `libpcre2-dev` | PCRE2 10.46, 8-bit, for the regex layer and every test. At run time the library needs `libpcre2-8-0`. |
| `pkg-config` | Recommended for consumers; CI also uses it to link against the installed library. |
| Forge | [github.com/minelogy-dev/forge](https://github.com/minelogy-dev/forge); `./make` is generated, not checked in. |
| `tar` and `ar` | Only for `./make gen_deb`; `dpkg-deb` is only needed to inspect the result. |

Run commands from the repository root; `build.c` and `build.conf` are read
relative to it.

## Build tool

Install [Forge](https://github.com/minelogy-dev/forge) from its repository. Once
`forge .` has generated `./make`, the everyday commands are:

```sh
forge .        # generate ./make
./make         # build -> build/output/libstringx.so (+ headers at build/output/include)
./make test    # build and run every test under tests/ (ASan + UBSan)
```

`./make` is generated and gitignored; re-run `forge .` after editing `build.c`
or `build.conf`.

## Targets and commands

| Command | What it does |
| --- | --- |
| `forge .` | Generate `./make` from `build.c` and `build.conf`. |
| `./make` | Default `build` task: compile `src/` into `build/output/libstringx.so` and sync `include/*.h` to `build/output/include/`. |
| `./make check` | Compile the library target with the warning flags and the GCC analyzer, without the header sync. |
| `./make test` | Build and run every file in `tests/` under ASan + UBSan + LSan. |
| `./make test -j 2` | The same, compiling the source files of each test in parallel (2-way here). |
| `./make install <prefix>` | Build, then install the library, headers, notices and `libstringx.pc`; the prefix defaults to `/usr/local`. |
| `./make gen_deb` | Build, then stage and produce `build/deb/libstringx_<version>_<arch>.deb`. |

`-v` prints the full compile commands for `./make test` and `./make check`;
without it only `PASSED`/`FAILED` lines and the summary appear. `-j N`
parallelizes one target's compile pass.

Artifacts:

| Path | Produced by | Contents |
| --- | --- | --- |
| `build/output/libstringx.so` | `./make`, `./make check`, `./make install`, `./make gen_deb` | the shared library |
| `build/output/include/` | `./make`, `./make install`, `./make gen_deb` | synced copies of `sx.h`, `sx_core.h` and `sx_raw.h` |
| `build/exe-obj/` | `./make`, `./make check` | per-source object files with their `.d`/`.meta` incrementality files |
| `test/<name>` | `./make test` | one executable per test file (`tests/new.c` → `test/new`) |
| `build/opt/` | `./make test` | test object files with their `.d`/`.meta` files |
| `build/deb/` | `./make gen_deb` | the Debian staging tree and the final `.deb` |

`./make test` needs no prior `./make`: every test compiles the library sources
directly.

## build.conf

`build.conf` is read when `./make` is generated; each `KEY=VALUE` line becomes a
`-DCONF_KEY=VALUE` macro, and `#` starts a comment. Re-run `forge .` after
editing it.

| Key | Value | Effect |
| --- | --- | --- |
| `compiler` | `GCC` or `CLANG` | Toolchain backend. `build.c` forwards `CONF_compiler` to `set_toolchain`, so this selects the compiler for the library and the tests. |
| `compiler_path` | an absolute path, e.g. `/usr/bin/gcc` | Compiler binary used when generating `./make`. |
| `version` | `v0.5` | The single source of truth for the project version. `build.c` stringifies it into `SX_VERSION`; install and packaging strip the leading `v`. |
| `sanitize` | `1` | Added by `.github/scripts/configure.sh` in CI. The sanitizer flags themselves are declared per test in the header DSL. |

## Running the tests

`./make test` scans `tests/` recursively, compiles each `tests/*.c` into
`test/<name>` and runs it. Success prints `PASSED: <file>`; the first failed
assertion prints `FAILED: <file>:<line>: <expression>`. The summary looks
like:

```text
forge: 24 tests, 24 passed, 0 failed
```

The task exits non-zero if a test fails to compile or run.

Tests do not link `libstringx.so`: each compiles the library sources directly
and declares its own flags, including `-fsanitize=address,undefined` and
`-fno-omit-frame-pointer`. LeakSanitizer runs with ASan, so an unfreed buffer
or array fails the test. A few tests share a local case header
(`find_differential.h`, `write_overflow_cases.h`) between the normal and a
forced-configuration sibling; each `.c` remains its own target.

Set CI's sanitizer options:

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
./make test -j 2
```

Rerun one built test directly:

```sh
./test/reserve_stress
```

### Exact-reserve stress test

`tests/reserve_stress.c` and `tests/write_overflow_exact.c` run with
`-DSX_FORCE_EXACT_RESERVE`, declared as `// Option -DSX_FORCE_EXACT_RESERVE` in
their headers. The switch remaps the internal reserve helper in `src/sx_raw.c`
from geometric growth to exact sizing (`cap == need + 1`): the allocation ends
exactly at the terminator, so any access past `len` hits an ASan redzone and
slack capacity cannot hide an off-by-one or a stale length. The stress test
drives three live buffers with a deterministic LCG and asserts full content on
every edit (models stay within 512 bytes); the write-overflow test repeats its
`SIZE_MAX` range cases under exact allocations. Every other test builds with
geometric growth.

## Test files and the header DSL

Each test is a self-contained program. Its leading consecutive `//` lines are a
header DSL read by the test runner; the first non-`//` line ends it.

| Entry | Effect |
| --- | --- |
| `// Include <dir>` | Add `-I<dir>`. Every test includes `include`, `src` and `/usr/lib/forge/include`; the last one supplies `<build.h>` when it is not on `CPATH`. |
| `// Source <file>` | Add a source file to the test executable. Tests add `src/sx_raw.c`, `src/sx_core.c` and `src/sx_adv.c`. |
| `// Link <lib>` | Add `-l<lib>`. Only `tests/reg.c` adds `pcre2-8`; the other tests do not use the regex layer. |
| `// Library <dir>` | Add `-L<dir>`. Tests add `/usr/local/lib`. |
| `// Option <flag>` | Add a compiler, linker and assembler option. Tests use `-std=c23 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, plus test-specific defines such as `-DSX_FORCE_EXACT_RESERVE`. |

A line without a keyword continues the current scope (initially `Source`); the
tests spell out every keyword.

Every test compiles `src/sx_adv.c`, so `build.c` attaches `pcre2-8` to every
test target through its reserved `LANGUAGE` override; `tests/reg.c` also
declares `// Link pcre2-8`.

The program includes the header under test (`"sx.h"`, or `"sx_core.h"` for
core-only tests) and `<build.h>` for two macros:

| Macro | Purpose |
| --- | --- |
| `TEST(expr)` | On failure print `FAILED: <file>:<line>: <expression>` and exit 1; on success print nothing and continue. |
| `PASS()` | Print `PASSED: <file>`; call it at the end of `main`. |

### Adding a test

1. Create `tests/<name>.c` — one behavior area per file.
2. Copy a header DSL block from an existing test; adjust `Source`, `Link` and
   `Option` as needed.
3. Document the behavior boundaries: what the test locks in
   (`In scope (contract)`) and what it does not exercise
   (`Out of contract (not exercised)`).
4. Include the header under test, assert with `TEST`, free every buffer and
   returned array, then finish with `PASS()`.
5. Run `./make test`; the new file is discovered automatically.

```c
// Include include
// Include src
// Include /usr/lib/forge/include
// Source src/sx_raw.c
// Source src/sx_core.c
// Source src/sx_adv.c
// Option -std=c23
// Option -O1
// Option -g
// Option -fsanitize=address,undefined
// Option -fno-omit-frame-pointer
// Library /usr/local/lib
#include "sx.h"
#include <build.h>

/*
 * Behavior boundaries — sx_erase (example)
 * ===================================================================
 * In scope (contract):
 *   · sx_erase(h, idx, n) removes the n bytes starting at idx,
 *     shifts the tail left, keeps ptr[len] == '\0', and returns h.
 *     n == 0 is a no-op; idx + n > len is a no-op returning h.
 * Out of contract (not exercised):
 *   · h == NULL or freed; erase idx > len with n such that idx + n
 *     wraps; keeping sx_buf_mut pointers across edits.
 */

int main(void) {
  sx_t *h = sx_new_from_cstr("hello");
  TEST(h != NULL);
  TEST(sx_erase(h, 1, 3) == h);
  TEST(sx_cmp_cstr(h, "ho") == 0);
  sx_free(h);
  PASS();
  return 0;
}
```

## Installing

`./make install` builds first (with the header sync), then copies:

| Destination | Contents |
| --- | --- |
| `<prefix>/lib/libstringx.so` | the shared library |
| `<prefix>/include/sx.h` | the public API header |
| `<prefix>/include/sx_core.h` | the core layer included by `sx.h` |
| `<prefix>/include/sx_raw.h` | the internal allocation header; not part of the supported API |
| `<prefix>/lib/pkgconfig/libstringx.pc` | pkg-config metadata |
| `<prefix>/share/doc/libstringx/LICENSE` | the MIT license |
| `<prefix>/share/doc/libstringx/THIRD_PARTY_NOTICES.md` | PCRE2 attribution and license |
| `<prefix>/share/doc/libstringx/copyright` | a copy of `LICENSE` under Debian's conventional name |

The default prefix is `/usr/local`; pass a writable prefix, for example:

```sh
./make install "$HOME/.local"
```

The pkg-config file is generated at install time: `prefix=` is the install
prefix, and `Version` is the `build.conf` version without the leading `v`
(`0.5`).

## Consuming the installed library

With pkg-config:

```sh
cc -std=c23 hello.c $(pkg-config --cflags --libs libstringx) -o hello
./hello
```

Without pkg-config:

```sh
cc -std=c23 hello.c -I<prefix>/include -L<prefix>/lib -lstringx -o hello
```

For a non-standard prefix, point pkg-config and the loader at it, or bake an
rpath:

```sh
export PKG_CONFIG_PATH="<prefix>/lib/pkgconfig"
cc -std=c23 hello.c $(pkg-config --cflags --libs libstringx) \
  -Wl,-rpath,"<prefix>/lib" -o hello
```

The library links `pcre2-8`, so consumers need no `-lpcre2-8`. A complete
program:

```c
#include <sx.h>
#include <stdio.h>

int main(void) {
  sx_t *h = sx_new_from_cstr("hello, world");
  if (h == NULL)
    return 1;
  printf("len=%d cap=%d: %.*s\n", sx_len(h), sx_cap(h), sx_len(h),
         sx_buf_mut(h));
  sx_free(h);
  return 0;
}
```

## Debian package

`./make gen_deb` builds first, then stages a Debian `.deb` with `tar` and `ar`
(no `dpkg-deb` dependency):

| Path | Contents |
| --- | --- |
| `build/deb/data/usr/` | the package data tree at the FHS root: `lib/`, `include/` and `share/doc/libstringx/` |
| `build/deb/control-dir/control` | the package metadata |
| `build/deb/debian-binary`, `control.tar.gz`, `data.tar.gz` | the package parts |
| `build/deb/libstringx_<version>_<arch>.deb` | the finished package |

Version comes from `build.conf` without the leading `v`; architecture from
`uname -m` (`x86_64` → `amd64`, `aarch64` → `arm64`, otherwise unchanged). The
package installs under `/usr`, declares `Depends: libc6, libpcre2-8-0`, and
bakes `/usr` into its `libstringx.pc` — the staging path never leaks in.

Inspect and install:

```sh
dpkg-deb --info build/deb/libstringx_*.deb
dpkg-deb --contents build/deb/libstringx_*.deb
sudo apt install ./build/deb/libstringx_*.deb
```

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `forge: command not found` | Forge is not on `PATH`; follow its repository's installation instructions. |
| `pcre2.h: No such file or directory` or `cannot find -lpcre2-8` | Install `libpcre2-dev` (Ubuntu: `sudo apt-get install libpcre2-dev`). |
| `./make: No such file or directory` after cloning | `./make` is generated and gitignored. Run `forge .` first. |
| Changes to `build.c` or `build.conf` have no effect | `./make` was generated from the old script. Re-run `forge .`. |
| Test link fails with Clang and `-flto` | The pinned build tool emits GCC-only `-flto=N` for parallel Clang links, so test targets in `build.c` disable LTO; production targets keep it. |
| `install` reports a permission error | The prefix is not writable. Pass one you own, e.g. `./make install "$HOME/.local"`. |
| A test fails with a leak report | A buffer or an array is not freed. Free every `sx_t` with `sx_free` and every returned array with `free`. |
| A consumer binary cannot find `libstringx.so` | Set `LD_LIBRARY_PATH` to `<prefix>/lib`, link with `-Wl,-rpath,<prefix>/lib`, or install to a standard prefix. |

Do not include `<build.h>` in library sources or applications: it is the
build-script DSL header, not part of the String X API. Applications include
`<sx.h>` only.

## Continuous integration

The workflows that run these commands on every push and pull request — build
matrix, analyzer, quality checks, CodeQL and merge protection — are in
[Continuous integration](ci.md).

---

[← Previous](design.md) · [Docs home](README.md) · [Next →](contributing.md)
