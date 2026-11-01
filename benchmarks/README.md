# Native string-library benchmarks

This suite benchmarks the current StringX implementation against pinned
[SDS](https://github.com/antirez/sds) and
[bstrlib](https://github.com/websnarf/bstrlib), using their normal public APIs.
It starts from StringX main at
452314a7ca51f90a7a8fa66930947164af8fc4f6; no library implementation is changed.

## Workloads and contracts

28 workloads produce 82 case/library combinations:

- Allocate/copy/free C strings at 16, 64, 1024 and 65536 bytes, with strlen
  discovery included for every library.
- Copy known-length binary strings at 64 and 65536 bytes, including embedded
  NULs. StringX uses an existing view, SDS uses sdsnewlen, bstrlib uses blk2bstr.
- Build and free a complete string by repeated known-length appends, with and
  without reserve: 4096 x 1 byte, 256 x 16 bytes, and 1024 x 1024 bytes.
- Lexicographic comparisons, equal strings and a mismatch in the final byte.
- Single-byte and multi-byte searches, front/end hits, misses and a repetitive
  prefix adversary. SDS has no native substring-search API: its search rows are
  explicitly labeled SDS+glibc and use binary-safe memmem/memchr.
- Owned substring copies and borrowed views, reported as separate workloads.
  The SDS view baseline is a borrowed pointer/length pair; bstrlib uses its
  borrowed tagbstring macro. These rows do not represent memory-copy throughput.
- Owned splitting with leading/trailing empty fields, using one- and two-byte
  separators. StringX returns views natively; its owned-output row explicitly
  adds materialization of each token and is labeled StringX+materialize.
  Native StringX view-only splitting is an additional StringX-only workload;
  it is not ranked against APIs that copy token bytes.

An append "operation" is the entire create/reserve/append/free batch.
Read-only fixtures and input generation are outside the timed region.
Ownership and cleanup are included in allocating operations.
Regex, Unicode processing, formatting and multithreaded use are outside this suite.

## Measurement

- GCC 14 and Clang 19 run sequentially on the same Linux host and pinned CPU.
- All libraries compile into separate translation units with the same
  -std=c23 -O3 -DNDEBUG -march=x86-64 -fno-lto flags. There is no header-only or
  LTO visibility advantage for StringX. Native public-header inline operations
  in other libraries remain available, as they are to their normal callers.
- A separate ASan/UBSan/leak-checked binary first validates all 82 combinations,
  including byte contents for copies/appends and semantic checksums for queries
  and tokens. The timed binary is not instrumented.
- Each case/library pair calibrates iterations to at least 20 ms in two pilot
  batches (up to an iteration cap), followed by 11 recorded samples.
  Library order is shuffled deterministically within each sample round.
- A compiler memory barrier and consumed/checked checksums prevent dead-code
  elimination and hoisting across iterations. Loop, branch and checksum overhead
  remain in the measurements; no guessed overhead is subtracted.
- Allocation counts come from a third, untimed binary using GNU ld --wrap.
  malloc/calloc/realloc calls and cumulative requested sizes are recorded for
  one complete operation, excluding fixture setup. Requested bytes are allocator
  traffic, not resident memory or peak live allocation. Allocator caches still
  affect the normal timed build.
- summary.json and summary.md report median and p10/p90 observed timings.
  These quantiles describe sample spread, not statistical confidence bounds.
  The effective MiB/s raw column uses logical workload bytes; early search hits
  and borrowed views do not physically process/copy that many bytes.

These are hot-input, single-threaded microbenchmarks on a shared hosted runner.
CPU frequency, VM scheduling and allocator behavior limit absolute timing claims.
Compare libraries within a case and compiler; avoid general-purpose rankings.
No noisy timing threshold is used as a merge gate.

## Reproduce

Install Python 3, GCC 14, Clang 19, the matching Clang sanitizer runtime,
pkg-config and PCRE2 development headers. Forge is not needed for this harness.

Clone the public dependencies and select the reviewed revisions:

```sh
mkdir -p build/benchmarks/deps
git clone https://github.com/antirez/sds build/benchmarks/deps/sds
git -C build/benchmarks/deps/sds checkout --detach 5347739b1581fcba74fd5cab1fc21d2aef317d71
git clone https://github.com/websnarf/bstrlib build/benchmarks/deps/bstrlib
git -C build/benchmarks/deps/bstrlib checkout --detach f0ff1e808102a42cdc7204a4bb6fe231a24c4546
python3 benchmarks/run.py --cc gcc-14 --cc clang-19
```

The runner verifies dependency revisions and records compiler versions, CPU
affinity, lscpu, flags, commit IDs and SHA-256 hashes of the measured StringX
sources. Outputs live under the ignored build/benchmarks/results directory.
GitHub Actions publishes a job summary and retains raw CSV/JSON/log artifacts
for 14 days. Performance runs are limited to PRs changing the benchmark or its
workflow, and manual dispatch; ordinary library PRs do not run this suite.

Dependencies are fetched separately and retain their original licenses.
Their source code is not vendored or relicensed by this harness.
