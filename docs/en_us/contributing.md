English · [中文](../zh_cn/contributing.md)

[Docs home](README.md) › Contributing

# Contributing

String X is a small C23 library with a tightly documented contract, so changes
are easiest to review when they stay small, keep the tests and the documentation
in step, and pass the same checks CI runs. This page describes the workflow and
the conventions the project expects.

## Workflow

1. Fork the repository and create a branch from `main`. Name the branch for the
   change (`fix/split-end-guard`, `docs/build-targets`).
2. Make the change, including tests for behavior and updates to the English and
   Chinese documentation.
3. Run the local checks before opening the pull request:

   ```sh
   ./make test -j 2
   bash .github/scripts/check-c.sh
   bash .github/scripts/check-c.sh analyze   # GCC only
   ```

4. Open a pull request against `main` and describe the behavior change, the
   tests that cover it, and the documentation pages you updated.
5. Wait for the required checks. Merge protection on `main` requires the `CI`
   and `CodeQL (C/C++)` status checks and up-to-date branches. The checks are
   described in [Continuous integration](ci.md).
6. Address review comments with additional commits; keep the pull request
   focused on one change.

`bash .github/scripts/configure.sh` reproduces CI's compiler selection, but it
rewrites `compiler=`, `compiler_path=` and `sanitize=` in `build.conf`, so save
any local changes to that file first.

## Contract-first documentation

Behavior boundaries live in two places:

- the comments in the public headers, `include/sx.h` and `include/sx_core.h`;
- the `Behavior boundaries` comment at the top of every `tests/*.c`, with its
  `In scope (contract)` and `Out of contract (not exercised)` lists.

A change to observable behavior must update all of them together:

1. the header comment for every affected function;
2. the test implementation and its behavior-boundaries comment;
3. the English and Chinese pages under `docs/en_us/` and `docs/zh_cn/`.

Do not leave a behavior undocumented or documented as it used to be. When
sources disagree, the tests win for run-time behavior, the headers win for
signatures, and `build.c` wins for build and packaging behavior — report a real
contradiction instead of silently picking one.

## Code style

- C23; the library and tests build with `-std=c23`.
- Two-space indentation; opening braces stay on the same line.
- Follow the existing naming and layering: core functions in `src/sx_core.c`,
  the advanced regex layer in `src/sx_adv.c`, internal allocation in
  `src/sx_raw.c`. Do not add public functions outside `sx.h`/`sx_core.h`.
- Use `size_t` for offsets and lengths, and the existing sentinel conventions
  (`(size_t)-1`, a view with `ref == NULL`, a NULL array).
- Treat warnings as errors. The `./make check` target compiles the library
  with `-Wall -Wextra -Wpedantic -Werror` and, on GCC, `-fanalyzer`; the
  direct compile check in CI uses
  `-std=c23 -O1 -Wall -Wextra -Werror`. Do not suppress a diagnostic to make
  a build pass; fix the code or explain the analyzer finding in review.

## Tests

Every behavior change needs a test. Tests are self-contained programs under
`tests/` that compile the library sources directly and declare their
dependencies in the header DSL (`// Include`, `// Source`, `// Link`,
`// Library`, `// Option`). See
[Build, test & package](build-and-test.md) for the DSL and a complete example.

When adding or changing a test:

- keep the `Behavior boundaries` comment accurate — it is the written contract
  the test enforces, including the `Out of contract (not exercised)` list;
- assert observable results (exact bytes, lengths, offsets and sentinels), not
  only that a call returned non-NULL;
- free every buffer with `sx_free` and every returned array with `free`; the
  suite runs under AddressSanitizer, UBSan and LeakSanitizer, and a leak or an
  out-of-bounds access fails the test;
- keep the sanitizer flags and `-fno-omit-frame-pointer` in the test header so
  the test runs the same way locally and in CI;
- if you change allocation or growth, run the exact-reserve configuration in
  `tests/reserve_stress.c` (`-DSX_FORCE_EXACT_RESERVE`), which exposes
  off-by-one accesses that slack capacity can hide.

Run the suite with the same environment CI uses:

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
./make test -j 2
```

## Commits and pull requests

- Keep one logical change per commit and a focused diff; avoid unrelated
  reformatting and drive-by changes.
- Write a short imperative subject line, then a body that explains why the
  change is needed and which behavior boundaries or documentation it touches.
  Reference the issue when one exists.
- Include tests and both language versions of the documentation in the same
  pull request as the code.
- Leave build artifacts out of the diff: `build/`, `test/`, `./make` and
  `.ci-deps/` are generated and gitignored.
- A pull request is ready to merge when the required checks pass and review
  comments are resolved.

## Licensing and attribution

- The library and the project sources are MIT licensed. New C source and header
  files should start with `// SPDX-License-Identifier: MIT`, as `include/sx.h`,
  `src/sx_adv.c` and `tests/reg.c` do; keep existing SPDX headers intact.
- The build script and the Forge-derived `src/export.h` are covered by Forge's
  build-script exception, identified as
  `GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception`. `build.conf`
  and `src/export.h` already carry that identifier and `build.c` is covered by
  the same exception; do not relicense this build tooling as MIT or remove the
  headers.
- The regex layer uses PCRE2 (BSD-3-Clause). Its attribution and license text
  live in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). If you add a
  third-party dependency, add its notice and license there as well.
- The project license is reproduced in [LICENSE](../../LICENSE).

## Where things live

| Path | Contents |
| --- | --- |
| [include/](../../include) | public headers: `sx.h` (advanced layer and `sx_split`) and the `sx_core.h` it includes |
| [src/](../../src) | implementation: `sx_core.c`, `sx_adv.c`, `sx_raw.c`, plus the internal `_sx.h` and `export.h` |
| [tests/](../../tests) | the self-contained test suite |
| [build.c](../../build.c), [build.conf](../../build.conf) | the Forge build script and its configuration |
| [.github/workflows/](../../.github/workflows) | the CI and CodeQL workflows |
| [.github/scripts/](../../.github/scripts) | compiler, analyzer and tooling checks used by CI |
| [docs/](../) | this documentation, in English and Simplified Chinese |
| [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) | third-party licenses and attribution |
| [README.md](../../README.md) | the project overview and quick start |

---

[← Previous](build-and-test.md) · [Docs home](README.md) · [Next →](ci.md)
