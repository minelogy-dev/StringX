English · [中文](../zh_cn/build-and-test.md)

[Docs home](README.md) › Build, test & package

# Build, test & package

String X builds with [Forge](https://github.com/minelogy-dev/forge), a build
system whose build script is a C program. This page covers the toolchain
requirements, every `./make` task, the test harness and sanitizers,
installation, pkg-config and Debian packaging. For the CI pipelines that run
these commands, see [Continuous integration](ci.md).

## Prerequisites

| Requirement | Notes |
| --- | --- |
| Linux | CI uses Ubuntu 24.04; other Linux distributions work. |
| C23 compiler | GCC 14+ or Clang 19+. |
| `libpcre2-dev` | PCRE2 10.46, 8-bit, for the regex layer and every test. At run time the library needs `libpcre2-8-0`. |
| `pkg-config` | Recommended for consumers; CI also uses it to link against the installed library. |
| Forge | The build tool; bootstrap it from source (next section). `./make` is generated, not checked in. |
| `tar` and `ar` | Only for `./make gen_deb`; `dpkg-deb` is only needed to inspect the result. |

Run every command from the repository root: `build.c` and `build.conf` are read
relative to it.

## Forge in one minute

Forge compiles `build.c` into a local executable, `./make`. That generated file
is the build entry point: each task below is a function in `build.c`, invoked as
`./make <task>`. It is not GNU make. Re-run `forge .` whenever `build.c` or
`build.conf` changes.

Bootstrap Forge from source and put its artifacts on the search paths:

```sh
git clone https://github.com/minelogy-dev/forge.git
cd forge
./bootstrap.sh --no-test
export PATH="$PWD/build/output:$PATH"
export CPATH="$PWD/build/output/include"
cd /path/to/StringX
forge .
```

CI pins Forge to commit
`b6826ba762a1e9e337895e01ee604b943c682d23`. To reproduce CI exactly, check out
that commit into `.ci-deps/forge` and bootstrap it without installing system
files:

```sh
git clone https://github.com/minelogy-dev/forge.git .ci-deps/forge
git -C .ci-deps/forge checkout b6826ba762a1e9e337895e01ee604b943c682d23
sh .ci-deps/forge/bootstrap.sh --no-test
export PATH="$PWD/.ci-deps/forge/build/output:$PATH"
export CPATH="$PWD/.ci-deps/forge/build/output/include"
forge .
```

Keep the pin identical in `.github/workflows/ci.yml` and
`.github/workflows/codeql.yml` when changing Forge. It is deliberately
independent of Forge's `main` branch and is not updated by Dependabot.

With Forge on `PATH`, the everyday commands are the ones from the README:

```sh
forge .        # compile build.c into ./make (re-run whenever build.c changes)
./make         # build -> build/output/libstringx.so (+ headers at build/output/include)
./make test    # build and run every test under tests/ (ASan + UBSan)
```

## Targets and commands

| Command | What it does |
| --- | --- |
| `forge .` | Compile `build.c` with the `build.conf` settings into a local `./make`. |
| `./make` | Default `build` task: compile `src/` into `build/output/libstringx.so` and sync `include/*.h` to `build/output/include/`. |
| `./make check` | Compile the library target with the warning flags and the GCC analyzer, without the header sync. |
| `./make test` | Build and run every file in `tests/` under ASan + UBSan + LSan. |
| `./make test -j 2` | The same, compiling the source files of each test in parallel (2-way here). |
| `./make install <prefix>` | Build, then install the library, headers, notices and `libstringx.pc`; the prefix defaults to `/usr/local`. |
| `./make gen_deb` | Build, then stage and produce `build/deb/libstringx_<version>_<arch>.deb`. |

`-v` makes `./make test` and `./make check` print the full compile commands;
without it only `PASSED`/`FAILED` lines and the final summary are shown. `-j N`
parallelizes one target's compile pass.

The artifacts land here:

| Path | Produced by | Contents |
| --- | --- | --- |
| `build/output/libstringx.so` | `./make`, `./make check`, `./make install`, `./make gen_deb` | the shared library |
| `build/output/include/` | `./make`, `./make install`, `./make gen_deb` | synced copies of `sx.h`, `sx_core.h` and `sx_raw.h` |
| `build/exe-obj/` | `./make`, `./make check` | per-source object files with their `.d`/`.meta` incrementality files |
| `test/<name>` | `./make test` | one executable per test file (`tests/new.c` → `test/new`) |
| `build/opt/` | `./make test` | test object files with their `.d`/`.meta` files |
| `build/deb/` | `./make gen_deb` | the Debian staging tree and the final `.deb` |

`./make test` does not need `./make` first: every test compiles the library
sources directly.

## build.conf

`build.conf` is read when Forge generates `./make`; each `KEY=VALUE` line
becomes a `-DCONF_KEY=VALUE` macro for the build script. Lines starting with
`#` are comments. Re-run `forge .` after editing it.

| Key | Value | Effect |
| --- | --- | --- |
| `compiler` | `GCC` or `CLANG` | Toolchain backend. `build.c` forwards `CONF_compiler` to `set_toolchain`, so this selects the compiler for the library and the tests. |
| `compiler_path` | an absolute path, e.g. `/usr/bin/gcc` | The compiler binary Forge uses to build the generated `./make`. |
| `version` | `v0.5` | The single source of truth for the project version. `build.c` stringifies it into `SX_VERSION`; install and packaging strip the leading `v`. |
| `sanitize` | `1` | Added by `.github/scripts/configure.sh` in CI. The sanitizer flags themselves are declared per test in the header DSL. |

## Running the tests

`./make test` scans `tests/` recursively, compiles each `tests/*.c` into
`test/<name>` and runs it. A test prints `PASSED: <file>` on success, or
`FAILED: <file>:<line>: <expression>` on the first failing assertion. The task
ends with a summary such as:

```text
forge: 20 tests, 20 passed, 0 failed
```

The task exits non-zero if any test fails to compile or to run.

Tests do not link `libstringx.so`: each one compiles the library sources
directly and declares its own flags, including `-fsanitize=address,undefined`
and `-fno-omit-frame-pointer`. LeakSanitizer runs together with ASan, so a test
that forgets to free a buffer or a returned array fails.

Set the same sanitizer options CI uses:

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
./make test -j 2
```

To rerun one built test directly:

```sh
./test/reserve_stress
```

### Exact-reserve stress test

`tests/reserve_stress.c` is the dedicated configuration for
`-DSX_FORCE_EXACT_RESERVE`, declared as
`// Option -DSX_FORCE_EXACT_RESERVE` in its header. The switch remaps the
internal reserve helper in `src/sx_raw.c` from geometric growth to exact sizing
(`cap == need + 1`), so an allocation ends exactly at the terminator and any
access past `len` hits an ASan redzone immediately — slack capacity cannot hide
an off-by-one or a stale length. The test drives a deterministic LCG over three
live buffers and asserts full content on every edit; its models stay within
512 bytes. Every other test builds in the default geometric-growth mode.

## Test files and the header DSL

Each test is a self-contained program. Its leading consecutive `//` lines form a
header DSL that Forge's test runner reads; the first non-`//` line ends the
header.

| Entry | Effect |
| --- | --- |
| `// Include <dir>` | Add `-I<dir>`. Every test includes `include`, `src` and `/usr/lib/forge/include` — Forge's headers for `<build.h>`; the last one is harmless when they are supplied through `CPATH` instead. |
| `// Source <file>` | Add a source file to the test executable. Tests add `src/sx_raw.c`, `src/sx_core.c` and `src/sx_adv.c`. |
| `// Link <lib>` | Add `-l<lib>`. Only `tests/reg.c` adds `pcre2-8`; the other tests do not use the regex layer. |
| `// Library <dir>` | Add `-L<dir>`. Tests add `/usr/local/lib`. |
| `// Option <flag>` | Add a compiler, linker and assembler option. Tests use `-std=c23 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, plus test-specific defines such as `-DSX_FORCE_EXACT_RESERVE`. |

A line without a keyword continues the current scope, which starts as `Source`;
the tests spell out every keyword for clarity.

Because every test compiles `src/sx_adv.c`, `build.c` attaches `pcre2-8` to
every test target through its reserved `LANGUAGE` override; `tests/reg.c` also
declares `// Link pcre2-8` explicitly.

The program itself includes the header under test (`"sx.h"`, or `"sx_core.h"`
for core-only tests) and `<build.h>` for two test macros:

| Macro | Purpose |
| --- | --- |
| `TEST(expr)` | On failure print `FAILED: <file>:<line>: <expression>` and exit 1; on success print nothing and continue. |
| `PASS()` | Print `PASSED: <file>`; call it at the end of `main`. |

### Adding a test

1. Create `tests/<name>.c` — one behavior area per file.
2. Copy the header DSL block from an existing test and adjust `Source`,
   `Link` and `Option` for the dependencies you need.
3. Document the behavior boundaries at the top: what the test locks in
   (`In scope (contract)`) and what it deliberately does not exercise
   (`Out of contract (not exercised)`).
4. Include the header under test, assert with `TEST`, free every buffer and
   returned array, then finish with `PASS()`.
5. Run `./make test`. The new file is discovered automatically.

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

`./make install` builds first (including the header sync), then copies:

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

The default prefix is `/usr/local`. Pass a prefix you can write to without
root, for example:

```sh
./make install "$HOME/.local"
```

The pkg-config file is generated at install time. Its `prefix=` is the install
prefix and its `Version` is the `build.conf` version without the leading `v`
(so `0.5`).

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

The library links `pcre2-8` itself, so consumers do not need `-lpcre2-8`. A
complete program:

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

`./make gen_deb` builds first, then stages and packages a Debian `.deb` with
`tar` and `ar` (no `dpkg-deb` dependency):

| Path | Contents |
| --- | --- |
| `build/deb/data/usr/` | the package data tree at the FHS root: `lib/`, `include/` and `share/doc/libstringx/` |
| `build/deb/control-dir/control` | the package metadata |
| `build/deb/debian-binary`, `control.tar.gz`, `data.tar.gz` | the package parts |
| `build/deb/libstringx_<version>_<arch>.deb` | the finished package |

The version comes from `build.conf` with the leading `v` stripped; the
architecture comes from `uname -m` (`x86_64` → `amd64`, `aarch64` → `arm64`,
anything else unchanged). The package installs under `/usr` and declares
`Depends: libc6, libpcre2-8-0`. The `libstringx.pc` inside the package bakes
`/usr` as its prefix; the staging path never leaks into it.

Inspect and install it:

```sh
dpkg-deb --info build/deb/libstringx_*.deb
dpkg-deb --contents build/deb/libstringx_*.deb
sudo apt install ./build/deb/libstringx_*.deb
```

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `forge: command not found` | Forge is not on `PATH`. Bootstrap it and export `PATH=<forge>/build/output`. |
| `build.h: No such file or directory` | Forge's build-script headers are not visible. Export `CPATH=<forge>/build/output/include` as CI does, or install Forge system-wide. `<build.h>` exists only inside Forge builds. |
| `pcre2.h: No such file or directory` or `cannot find -lpcre2-8` | Install `libpcre2-dev` (Ubuntu: `sudo apt-get install libpcre2-dev`). |
| `./make: No such file or directory` after cloning | `./make` is generated and gitignored. Run `forge .` first. |
| Changes to `build.c` or `build.conf` have no effect | `./make` was generated from the old script. Re-run `forge .`. |
| Test link fails with Clang and `-flto` | The pinned Forge emits GCC-only `-flto=N` for parallel Clang links, so the test targets in `build.c` deliberately disable LTO; production targets keep it. Keep test LTO disabled when running the suite with Clang. |
| `install` reports a permission error | The prefix is not writable. Pass one you own, e.g. `./make install "$HOME/.local"`. |
| A test fails with a leak report | A buffer or an array is not freed. Free every `sx_t` with `sx_free` and every returned array with `free`. |
| A consumer binary cannot find `libstringx.so` | Set `LD_LIBRARY_PATH` to `<prefix>/lib`, link with `-Wl,-rpath,<prefix>/lib`, or install to a standard prefix. |

Do not include `<build.h>` in the library sources or in applications: it is
Forge's build-script DSL header, not part of the String X API. Applications
include `<sx.h>` only.

## Continuous integration

The workflows that run these commands on every push and pull request — the
build matrix, the analyzer, the quality checks, CodeQL and merge protection —
are described in [Continuous integration](ci.md).

---

[← Previous](design.md) · [Docs home](README.md) · [Next →](contributing.md)
