English · [中文](../zh_cn/ci.md)

[Docs home](README.md) › Continuous integration

# Continuous integration

Changes to `main`, pull requests and manual runs use GitHub Actions. This page
covers the workflows, local reproduction and merge protection; for build
commands, `./make` targets and the test harness see
[Build, test & package](build-and-test.md).

## Triggers

| Event | Workflows |
| --- | --- |
| `push` to `main` | [ci.yml](../../.github/workflows/ci.yml) and [codeql.yml](../../.github/workflows/codeql.yml) |
| `pull_request` on any branch | `ci.yml` and `codeql.yml` |
| `pull_request`, path-filtered | [write-safety.yml](../../.github/workflows/write-safety.yml) (`src/sx_core.c`, `tests/write_overflow*`, its script), [search-performance.yml](../../.github/workflows/search-performance.yml) (`src/sx_core.c`, its script), [benchmarks.yml](../../.github/workflows/benchmarks.yml) (`benchmarks/**`), [benchmark-report.yml](../../.github/workflows/benchmark-report.yml) (its workflow file only) |
| `workflow_dispatch` (manual run) | `ci.yml`, `codeql.yml`, `write-safety.yml`, `search-performance.yml`, `benchmarks.yml`, `benchmark-report.yml` |
| `schedule` | `codeql.yml` only (weekly) |

Workflows have read-only repository access, except CodeQL's permission to
publish code-scanning results. Pull-request code never runs via
`pull_request_target`, and checkout credentials are not persisted.

## Checks

| Check | What it verifies |
| --- | --- |
| Build and test (gcc-14 / clang-19) | A real build on Ubuntu 24.04 with GCC 14 and Clang 19, the complete test suite, ASan, UBSan and leak detection |
| Compiler warnings | Every production C translation unit compiles with `-Wall -Wextra -Werror` |
| GCC static analysis | `-fanalyzer` checks production translation units; diagnostics fail the job |
| Workflow and secret checks | Actionlint, ShellCheck, whitespace errors and Gitleaks over the complete Git history, with redacted output |
| CodeQL (C/C++) | Manual build tracing with `security-and-quality` queries, findings in the Security tab and on pull requests |
| Write safety and cost | Re-runs the write-overflow regression against the immutable old implementation (a sanitizer failure is required there), runs both fixed configurations with GCC 14 and Clang 19, and compares equal-length write cost with SDS and bstrlib |
| Search performance | Before/after search benchmark with GCC and Clang for `src/sx_core.c` changes |
| Audited benchmarks | Reruns the pinned StringX / SDS / bstrlib harness when `benchmarks/` changes |
| Benchmark results report | Renders a saved benchmark artifact as a report (manual) |
| CI | A stable required check that fails if any build, analyzer or quality job fails or is cancelled |

The four audit/benchmark workflows are path-filtered and independent of the
aggregate `CI` check; they gate merges only if branch protection requires them.

GCC analysis uses `-O1 -fno-analyzer-state-merge`: GCC 14's default state
merging produces spurious allocation/cleanup diagnostics here. No categories
are suppressed; investigate analyzer failures instead of using
`continue-on-error`.

External Actions are pinned to immutable commit SHAs; Dependabot proposes
weekly updates. The Actionlint and Gitleaks CLIs have pinned versions and
SHA-256 checksums in
[.github/scripts/install-tools.sh](../../.github/scripts/install-tools.sh);
update both together when upgrading.

## CodeQL

CodeQL covers C/C++ with manual build tracing and `security-and-quality`
queries. It analyzes code built by the pinned toolchain, publishes
findings to the Security tab and on pull requests.

CodeQL also runs every Monday at 11:17 Asia/Shanghai (03:17 UTC). Success means
the analysis completed, not that there are no alerts; enforce severity with a
code-scanning merge rule — see
[Enable merge protection](#enable-merge-protection).

## Enable merge protection

Workflow files alone do not enable branch protection; these are one-time
maintainer settings. After the first successful run, create a branch ruleset
for `main`:

1. Require pull requests and the `CI` and `CodeQL (C/C++)` status checks.
2. Require branches to be up to date before merging.
3. Add "Require code scanning results", select CodeQL, and block high/critical
   security alerts and error-level quality alerts; tighten thresholds after
   triaging existing findings.
4. Use the checked-in advanced CodeQL workflow rather than enabling a duplicate
   default-setup scan.

Changes to CI permissions, checks and dependency pins still deserve human
review.

## Reproduce locally

Install Linux build dependencies and a C23 compiler (GCC 14+ or Clang 19+).
Save local `build.conf` changes before `configure.sh`: it updates the compiler
selection and appends `sanitize=1`.

```sh
CC=gcc bash .github/scripts/configure.sh  # or CC=clang
CC=gcc bash .github/scripts/check-c.sh
CC=gcc bash .github/scripts/check-c.sh analyze
```

Use GCC for `analyze`; runtime checks set:

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

CI pins Forge to a reviewed commit (checked out under `.ci-deps/forge`); both
workflow files contain the exact pin and setup commands, and the pin must
be updated in both together. PCRE2 comes from Ubuntu's `libpcre2-dev` package.
With Forge installed:

```sh
forge .
./make
./make test -j 2
```

The focused audit scripts live in `.github/scripts/` (`check-write-safety.py`,
`compare-search.py`); `benchmarks/` documents its own harness and rerun steps.

CI also installs into a temporary prefix, compiles and runs the existing regex
suite against the **installed shared library** via `pkg-config`, and inspects
the generated Debian `.deb`. This checks public headers, exported symbols,
package metadata and PCRE2 linkage beyond source-level behavior. Commands are
in `ci.yml`; the layout is in [Build, test & package](build-and-test.md).

## Review and failures

Open the failed check for compiler diagnostics, sanitizer traces or secret-scan
locations. Build failures keep dependency and command metadata for seven days;
the Actions log has the full output. CodeQL alerts include affected lines and
data flow where available.

Automation covers mechanical regressions and many memory-safety bugs; human
review should focus on requirements, API contracts, ownership, architecture and
tool findings.

---

[← Previous](contributing.md) · [Docs home](README.md)
