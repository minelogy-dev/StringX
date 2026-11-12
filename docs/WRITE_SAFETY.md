# Write offset arithmetic

The write APIs accept a size_t offset. The sum of that offset and the
input length must fit in size_t and leave room for the terminating NUL.
For example, sx_write_cstr(s, SIZE_MAX, "X") previously wrapped the
reserve request to zero, then formed an out-of-bounds destination pointer.

The shared write helper rejects arithmetic overflow before computing a
destination. sx_write_view also checks before its alias-preserving
pre-reserve, so an invalid range cannot mutate the target capacity first.
The existing reserve helper rejects a total content length of SIZE_MAX,
which cannot include a terminator. Owned write inputs are still consumed
on failure. Valid writes, binary data and view aliases retain their semantics.

Tests exercise all three write APIs near SIZE_MAX with real small input
buffers, unchanged target contents/capacity, borrowed views, owned failures,
binary writes and valid aliased growth. Both normal and exact-reserve
configurations run under the existing sanitizer suite.

The Write safety and cost workflow compiles the same regression against
the immutable old implementation, requires a sanitizer failure there, and
requires both fixed configurations to pass with GCC 14 and Clang 19.

The focused performance check reuses the pinned audited harness from
7f8a624af4809c9dc606c41533991acd1b51917d. It generates an overwrite-only
harness and saves that exact source in the artifact. Source/target lengths
are equal (8/64/4096/65536 bytes), buffers are preallocated, and binary
contents are compared in full outside timing. The APIs are sx_write_view,
sdscpylen and bassignblk, with raw memcpy using the same SDS buffer as a
control. This contract is an equal-length overwrite; it does not compare
their different truncation/extension semantics.

Before/after StringX and peers run on the same pinned CPU, with three
fresh processes and seven samples per group. Separate translation units,
no LTO, verification-only call counters and release disassembly preserve
the audited call structure. Hot and CLFLUSH-based data conditions are
reported separately; flushing cost is excluded. No counting is enabled in
timed release adapters. This is a targeted write-cost check; it makes no
new claim about search or growth performance.

Other mutator arithmetic (such as insert/erase/view ranges) remains a
separate audit target. This change is limited to write-range validation.
