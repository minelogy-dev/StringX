# Audited search rerun

This rerun checks the unusually large gaps in the earlier search results.
The implementation is pinned to patched StringX commit
40987da6ddaa2f31ca04d5d7f4545fa0704b7eb4. SDS and bstrlib retain the
immutable revisions in recheck.py. Production code is unchanged.

The original endpoint-absent fixtures and the period-127 fixture are retained
as reproduction controls. Text period and phase are then varied independently
of the 128-byte needle: periods 63/64/65/125/126/127/128/129 and phases 0/1/31.
Other cases cover independent random needles with two seeds, lengths around
32 and 256, binary NULs, real front/middle/tail hits, rare terminal bytes,
independent terminal-byte frequencies, and 4KiB/64KiB/1MiB text sizes.
Expected positions are recorded: periodic cases that actually match are
reported as hits, rather than being called misses.

Each fixture is checked by a byte-window naive oracle outside timing.
The actual lengths and contents of all library buffers are checked, and
fixture hashes must agree across compilers and fresh processes.

Four adapters are compiled as separate translation units with no LTO and
-fno-builtin-memmem / -fno-builtin-memchr. The timed loop calls a volatile
function pointer once per operation. A separate ASan/UBSan/leak-check build
counts nine adapter calls per fixture/library and checks the returned position.
Release disassembly is checked for the indirect loop call and the underlying
library calls. The release adapters contain no counting instrumentation.

SDS search is explicitly SDS + glibc memmem/memchr. A fourth raw-glibc adapter
uses the exact same SDS data and pattern buffers, with an explicit length.
This separates header/adapter overhead from the search implementation.

GCC 14 and Clang 19 use the same flags, job, and pinned CPU. Each uses three
fresh processes, seven samples per case/library/cache condition. Case order,
library order, and cache-condition order are shuffled.

- Hot: three warmup calls, then a calibrated batch of at least about 10ms.
- Flushed: CLFLUSH all text and pattern cache lines, MFENCE/LFENCE, then time
  one call. Each sample averages eight independently flushed calls. Flush
  cost is outside the measured interval. Handle/length metadata is not
  explicitly flushed, and this is a defined cache-flush experiment rather
  than a guarantee about every hardware cache or DRAM access.
- Empty-adapter and timestamp-pair controls are reported. Their cost is not
  subtracted from measurements.

All results are validated, including timed checksums. The workflow logs the
complete per-library statistics; artifacts retain raw CSV, manifests, compiler
versions, source/binary hashes, call-site audits and full disassembly.
p10/p90 are observed sample spread, not confidence intervals. Shared-runner
scheduling/frequency and process address layout remain limitations.

The rerun does not assume the earlier tiny times are wrong or representative.
In particular, a period of m-1 can favor glibc's pair-hash skip loop. Results
must be interpreted by corpus, hit position, and cache condition.

Reproduction on x86-64 Linux with CLFLUSH:

    python3 benchmarks/recheck.py --source PATH_TO_PINNED_STRINGX \
      --deps PATH_CONTAINING_PINNED_SDS_AND_BSTRLIB --out build/recheck/results
