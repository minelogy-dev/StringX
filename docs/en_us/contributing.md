English · [中文](../zh_cn/contributing.md)

[Docs home](README.md) › Contributing

# Contributing

String X is a small C23 library with a detailed contract. Keep documentation,
tests and changes in sync. This page covers the workflow and project
conventions.

## Workflow

1. Fork the repository and branch from `main`; name it for the change
   (`fix/split-end-guard`, `docs/build-targets`).
2. Make the change, with behavior tests and English/Chinese documentation
   updates.
3. Run the local checks before opening a pull request:

   ```sh
   ./make test -j 2
   bash .github/scripts/check-c.sh
   bash .github/scripts/check-c.sh analyze   # GCC only
   ```

4. Open a pull request against `main` describing the behavior change, its tests
   and updated documentation pages.
5. Wait for the required checks: merge protection on `main` requires `CI` and
   `CodeQL (C/C++)` and up-to-date branches — see
   [Continuous integration](ci.md). Path-filtered write-safety,
   search-performance and benchmark workflows also run for matching changes.
6. Address review comments with additional commits; keep the pull request
   focused.

`bash .github/scripts/configure.sh` reproduces CI's compiler selection but
rewrites `compiler=`, `compiler_path=` and `sanitize=` in `build.conf`; save
local changes first.

## Contract-first documentation

Behavior boundaries live in two places:

- the comments in the public headers `include/sx.h` and `include/sx_core.h`;
- the `Behavior boundaries` comment atop every `tests/*.c`, with its
  `In scope (contract)` and `Out of contract (not exercised)` lists.

A behavior change updates all of them together:

1. the header comment of every affected function;
2. the test implementation and its behavior-boundaries comment;
3. the English and Chinese pages under `docs/en_us/` and `docs/zh_cn/`.

Never leave a behavior undocumented or stale. When sources disagree, tests win
for run-time behavior, headers for signatures, and `build.c` for build and
packaging — report a real contradiction instead of picking one silently.

## Code style

- C23; the library and tests build with `-std=c23`.
- Two-space indent; opening braces on the same line.
- Follow existing naming and layering: core in `src/sx_core.c`, regex in
  `src/sx_adv.c`, allocation in `src/sx_raw.c`. Do not add public functions
  outside `sx.h`/`sx_core.h`.
- Use `size_t` for offsets and lengths and the existing sentinel conventions
  (`(size_t)-1`, a view with `ref == NULL`, a NULL array).
- Treat warnings as errors: `./make check` uses
  `-Wall -Wextra -Wpedantic -Werror` and, on GCC, `-fanalyzer`; CI's direct
  compile check uses `-std=c23 -O1 -Wall -Wextra -Werror`. Do not suppress a
  diagnostic; fix the code or explain the analyzer finding in review.

## Tests

Every behavior change needs a test. Tests under `tests/` are self-contained
programs that compile the library sources and declare dependencies in the
header DSL (`// Include`, `// Source`, `// Link`, `// Library`, `// Option`);
see [Build, test & package](build-and-test.md) for the DSL and an example.

When adding or changing one:

- keep the `Behavior boundaries` comment accurate — it is the contract the test
  enforces, including `Out of contract (not exercised)`;
- assert observable results (bytes, lengths, offsets, sentinels), not just
  non-NULL;
- free every buffer with `sx_free` and every array with `free`; the suite runs
  under AddressSanitizer, UBSan and LeakSanitizer, so a leak or out-of-bounds
  access fails;
- keep sanitizer flags and `-fno-omit-frame-pointer` in the test header so local
  and CI runs match;
- if you change allocation or growth, run the exact-reserve config in
  `tests/reserve_stress.c` (`-DSX_FORCE_EXACT_RESERVE`), which exposes
  off-by-one accesses slack capacity can hide.

Run the suite with CI's environment:

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
./make test -j 2
```

## Commits and pull requests

- Keep one logical change per commit and a focused diff; avoid unrelated
  reformatting.
- Use a short imperative subject; the body explains why and which boundaries or
  docs change. Reference the issue when one exists.
- Put tests and both language versions of the docs in the same pull request.
- Leave build artifacts out: `build/`, `test/`, `./make` and `.ci-deps/` are
  generated and gitignored.
- A pull request is ready when required checks pass and review comments are
  resolved.

## Licensing and attribution

- The library and project sources are MIT licensed. New C source and header
  files start with `// SPDX-License-Identifier: MIT`, as `include/sx.h`,
  `src/sx_adv.c` and `tests/reg.c` do; keep existing SPDX headers.
- The build script and Forge-derived `src/export.h` fall under Forge's
  build-script exception,
  `GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception`.
  `build.conf` and `src/export.h` carry that identifier and `build.c` is
  covered by the same exception; do not relicense this build tooling as MIT or
  remove the headers.
- The regex layer uses PCRE2 (BSD-3-Clause), attributed in
  [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). Add new third-party
  dependencies and their licenses there too.
- The project license is in [LICENSE](../../LICENSE).

## Where things live

| Path | Contents |
| --- | --- |
| [include/](../../include) | public headers: `sx.h` (advanced layer and `sx_split`) and the `sx_core.h` it includes |
| [src/](../../src) | implementation: `sx_core.c`, `sx_adv.c`, `sx_raw.c`, plus the internal `_sx.h` and `export.h` |
| [tests/](../../tests) | the self-contained test suite |
| [benchmarks/](../../benchmarks) | audited StringX / SDS / bstrlib benchmarks and rerun tooling |
| [build.c](../../build.c), [build.conf](../../build.conf) | the build script and its configuration |
| [.github/workflows/](../../.github/workflows) | the CI and CodeQL workflows |
| [.github/scripts/](../../.github/scripts) | compiler, analyzer and tooling checks used by CI |
| [docs/](../) | this documentation, in English and Simplified Chinese |
| [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) | third-party licenses and attribution |
| [README.md](../../README.md) | the project overview and quick start |

---

[← Previous](build-and-test.md) · [Docs home](README.md) · [Next →](ci.md)
