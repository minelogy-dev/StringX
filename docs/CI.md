# Continuous integration

Changes to `main`, pull requests, and manual runs use GitHub Actions.
The workflows have read-only repository access, except for the CodeQL job's
permission to publish code-scanning results. Pull-request code never runs via
`pull_request_target`, and checkout credentials are not persisted.

## Checks

| Check | What it verifies |
| --- | --- |
| Build and test (gcc-14 / clang-19) | A real build on Ubuntu 24.04 with GCC 14 and Clang 19, the complete test suite, ASan, UBSan and leak detection |
| Compiler warnings | Every production C translation unit compiles with `-Wall -Wextra -Werror` |
| GCC static analysis | `-fanalyzer` checks production translation units; diagnostics fail the job |
| Workflow and secret checks | Actionlint, ShellCheck, whitespace errors and Gitleaks over the complete Git history, with redacted output |
| CodeQL (C/C++) | Manual build tracing with `security-and-quality` queries, findings in the Security tab and on pull requests |
| CI | A stable required check that fails if any build, analyzer or quality job fails or is cancelled |

CodeQL also runs every Monday at 11:17 Asia/Shanghai (03:17 UTC).
Its successful job status means the analysis completed; it does **not** mean
there are no alerts. Use a code-scanning merge rule to enforce alert severity.

GCC analysis uses `-O1 -fno-analyzer-state-merge`: GCC 14's default state merging
produces spurious allocation/cleanup diagnostics in this code. No diagnostic
categories are suppressed. An analyzer failure should be investigated, not
worked around with `continue-on-error`.

All external Actions use immutable commit SHAs. Dependabot proposes weekly
GitHub Actions updates. The Actionlint and Gitleaks CLIs have pinned versions
and SHA-256 checksums in `.github/scripts/install-tools.sh`; update both values
together when upgrading them.

## Enable merge protection

After the first successful run, create a branch ruleset for `main`:

1. Require pull requests and the `CI` and `CodeQL (C/C++)` status checks.
2. Require branches to be up to date before merging.
3. Add “Require code scanning results”, select CodeQL, and block high/critical
   security alerts and error-level quality alerts. Tighten these thresholds
   after triaging existing findings.
4. Use the checked-in advanced CodeQL workflow rather than enabling a duplicate
   default-setup scan.

Workflow files alone do not enable branch protection. These repository settings
require a one-time maintainer configuration. Changes to CI permissions, checks
and dependency pins still deserve human review.

## Reproduce locally

Install Linux build dependencies and a C23 compiler (GCC 14+ or Clang 19+).
Save local `build.conf` changes before running `configure.sh`: it updates the
compiler selection and enables Forge test instrumentation in that file.

```sh
CC=gcc bash .github/scripts/configure.sh  # or CC=clang
CC=gcc bash .github/scripts/check-c.sh
CC=gcc bash .github/scripts/check-c.sh analyze
```

Use GCC for `analyze`. Runtime checks set:

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

The build dependency is Forge commit
`b6826ba762a1e9e337895e01ee604b943c682d23`, checked out under
`.ci-deps/forge` in CI and bootstrapped without installing system files.
Update this reviewed pin in **both** workflow files when changing Forge.
It is intentionally independent of Forge's moving `main` branch and is not
updated by Dependabot. PCRE2 comes from Ubuntu's `libpcre2-dev` package.

With a bootstrapped Forge checkout available locally:

```sh
export PATH="/path/to/forge/build/output:$PATH"
export CPATH="/path/to/forge/build/output/include"
forge .
./make
./make test -j 2
```

Tests compile the library sources with sanitizers, including the exact-reserve
stress test. The build jobs also install into a temporary prefix, compile and
run the existing regex suite against the **installed shared library** using
`pkg-config`, and inspect the generated Debian package. This checks public
headers, exported symbols, package metadata and PCRE2 linkage in addition to
source-level behavior.

## Review and failures

Open the failed check for compiler diagnostics, sanitizer stack traces or
secret-scan locations. Build failures retain dependency and command metadata
for seven days; the Actions log contains the full build/test output.
CodeQL alerts include the affected lines and data flow where available.

Automation covers mechanical regressions and many common memory-safety bugs.
Human review should concentrate on requirements, API contracts, ownership,
architecture and the findings reported by these tools.
