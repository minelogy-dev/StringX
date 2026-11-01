# Search-cost diagnosis

The library implementation is unchanged. This experiment separates measured
components and logical algorithm work before choosing an optimization.

Run the same pinned dependency setup as the parent benchmark, then:

```sh
python3 benchmarks/search_diag.py --cc gcc-14 --cc clang-19
```

The diagnostic covers the original 8-byte absent-first-byte case at three input
sizes, the original 128-byte repeated-prefix case, common first bytes with a
missing/full end match, a periodic-text counterexample, and a front hit.

- sx_unwrapped: original sx_find with the normal allocator.
- sx_heap / sx_arena: the same compiled library object, linked with allocation
  wrappers. The arena changes only prefix-table malloc/free; the table is still
  rebuilt and the original generic search still executes. Comparing these
  shuffled variants isolates allocation more directly than timing malloc alone.
- kmp_first_only_heap: a diagnostic-only forward-FIRST specialization of KMP.
  It omits reverse/ALL configuration and has its own malloc/prefix construction.
- kmp_scan_precomputed: only the forward scan, with a prefix table prepared
  outside the timed region.
- prefix_build_only / malloc_free_only: component controls. These timings
  cannot be added to reconstruct an exact whole-call breakdown.
- bstrlib / glibc_memmem: native comparison paths.
- scalar/glibc byte probes: necessary-condition controls, not full substring
  search implementations. Their returned positions are checked separately.

All full-search results are checked against the reference result. A separate
sanitizer build checks the diagnostic kernels. Nine shuffled timing samples
use the same pinned CPU and optimized flags as the parent suite.

Logical counters trace the forward KMP algorithm and an instrumented copy of
the pinned bstrlib function. The latter is generated in the ignored build tree
with its original notice; bstrlib source is not vendored into this directory.
Logical comparison counts are not machine instructions or branch misses.

The runner preserves source-interleaved disassembly, compiler vectorization
remarks, component CSV/JSON, and disassembly at resolved libc memchr/memmem
addresses. A perf_event_open probe records whether hardware counters are
available; this harness does not collect those counters. Do not infer
branch-misprediction rates or memory-stall fractions from wall time alone.

All output goes under build/benchmarks/results/search and is included in the
workflow artifact. No public API, allocator layout, or production search path
is modified by this experiment.
