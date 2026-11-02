#!/usr/bin/env python3
"""Reproduce the write overflow, then compare guarded writes on one runner."""
import argparse
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASELINE = "452314a7ca51f90a7a8fa66930947164af8fc4f6"
HARNESS = "7f8a624af4809c9dc606c41533991acd1b51917d"
PINS = {"sds": "5347739b1581fcba74fd5cab1fc21d2aef317d71",
        "bstrlib": "f0ff1e808102a42cdc7204a4bb6fe231a24c4546"}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--deps", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    before, harness, deps, out = [p.resolve() for p in
                                 (args.before, args.harness, args.deps, args.out)]
    out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("audited_bench", harness / "benchmarks/recheck.py")
    bench = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bench)
    assert bench.capture(["git", "rev-parse", "HEAD"], before) == BASELINE
    assert bench.capture(["git", "rev-parse", "HEAD"], harness) == HARNESS
    for lib, sha in PINS.items():
        assert bench.capture(["git", "rev-parse", "HEAD"], deps / lib) == sha

    generated = out / "harness" / "benchmarks"
    generated.mkdir(parents=True, exist_ok=True)
    header = (harness / "benchmarks/recheck.h").read_text()
    (generated / "recheck.h").write_text(header)
    code = (harness / "benchmarks/recheck.c").read_text()
    start = code.index("/* Independent byte-window oracle")
    end = code.index("static uint64_t prepare", start)
    code = code[:start] + code[end:]
    anchor = "uint64_t expected = reference(h, c->n, f->pattern, c->m);"
    assert code.count(anchor) == 1
    code = code.replace(anchor, "uint64_t expected = 1; /* success; bytes checked separately */")
    code = code.replace('"SDS+glibc"', '"SDS"').replace('"raw_glibc_same_buffer"', '"raw_memcpy_same_buffer"')
    code = code.replace("expected_pos", "expected_result").replace("(long long)expected-1", "(long long)expected")
    anchor = "        (void)cold_once(workers[lib], &f, lib, expected);"
    assert code.count(anchor) == 1
    code = code.replace(anchor, anchor + "\n        CHECK(memcmp(f.text_bytes[lib], f.pattern_bytes[lib], f.n) == 0);")
    anchor = "                   sample,it,elapsed,(double)elapsed/(double)it);"
    assert code.count(anchor) == 1
    code = code.replace(anchor, anchor + "\n            CHECK(memcmp(f.text_bytes[lib], f.pattern_bytes[lib], f.n) == 0);")
    (generated / "recheck.c").write_text(code)
    adapters = r'''// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "recheck.h"
#include <string.h>
#ifdef RECHECK_VERIFY_CALLS
#define RECORD() (++recheck_call_counts[LIB_INDEX])
#else
#define RECORD() ((void)0)
#endif
#if LIB_INDEX == 0
__attribute__((noinline)) uint64_t recheck_sx(recheck_input *f) {
  RECORD();
  return sx_write_view(f->sx, 0, sx_view(f->sx_pattern)) == f->sx;
}
#elif LIB_INDEX == 1
__attribute__((noinline)) uint64_t recheck_sds(recheck_input *f) {
  RECORD();
  sds result = sdscpylen(f->sd, f->pattern, f->m);
  if (!result) return 0;
  f->sd = result;
  return 1;
}
#elif LIB_INDEX == 2
__attribute__((noinline)) uint64_t recheck_bstr(recheck_input *f) {
  RECORD();
  return bassignblk(f->bs, f->bs_pattern->data, (int)f->m) == BSTR_OK;
}
#elif LIB_INDEX == 3
__attribute__((noinline)) uint64_t recheck_raw(recheck_input *f) {
  RECORD();
  memcpy(f->sd, f->pattern, f->m);
  return 1;
}
#endif
'''
    (generated / "recheck_adapter.c").write_text(adapters)
    sizes = (8, 64, 4096, 65536)
    (out / "recheck_cases.h").write_text(
        "static const Case cases[] = {\n" + "".join(
            f'  {{"write{n}", 5, {n}, {n}, 0, 0, UINT32_C(40581213), 0}},\n'
            for n in sizes) + "};\n")
    bench.ROOT = generated.parent
    cpu = min(os.sched_getaffinity(0))
    after_sha = bench.capture(["git", "rev-parse", "HEAD"], ROOT)
    env = os.environ.copy()
    env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    metadata = dict(before=BASELINE, after=after_sha, harness=HARNESS, dependencies=PINS,
                    cpu=cpu, lscpu=json.loads(bench.capture(["lscpu", "-J"])),
                    libc=bench.capture(["ldd", "--version"]).splitlines()[0],
                    flags=bench.COMMON + ["-O3", "-DNDEBUG"], sizes=sizes,
                    processes=3, samples_per_process=7,
                    contract="Overwrite an existing equal-sized binary string in place; setup excluded. Exact bytes checked outside timing. Flushed mode flushes source and target, excludes flush cost.",
                    source_hashes={name: hashlib.sha256((generated / name).read_bytes()).hexdigest()
                                   for name in ("recheck.c", "recheck.h", "recheck_adapter.c")})
    for variant, source in (("before", before), ("after", ROOT)):
        metadata[variant + "_core_sha256"] = hashlib.sha256((source / "src/sx_core.c").read_bytes()).hexdigest()
    (out / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print("WRITE_ENV_JSON=" + json.dumps(metadata), flush=True)
    report = ["# Write overflow safety and cost", "",
              "Same job/CPU; binary overwrites, no allocation in timed writes; three fresh processes.",
              "Flushed samples average eight individually flushed calls; flush cost excluded.",
              "Source/target bytes are checked outside timing. Small differences on a shared runner are inconclusive.", ""]
    for cc in ("clang-19", "gcc-14"):
        folder = out / cc
        folder.mkdir(parents=True, exist_ok=True)
        log = folder / "commands.log"
        version = bench.capture([cc, "--version"])
        (folder / "compiler.txt").write_text(version + "\n")
        flags = ["-std=c23", "-O1", "-g", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                 "-DSX_WRITE_OVERFLOW_STANDALONE"]
        def regression(source, target, exact=False):
            bench.run([cc, *flags, *(["-DSX_FORCE_EXACT_RESERVE"] if exact else []),
                       "-isystem", source / "include", ROOT / "tests/write_overflow.c",
                       source / "src/sx_core.c", source / "src/sx_raw.c", "-o", target], log)
        old = folder / "old-overflow"
        regression(before, old)
        failed = subprocess.run([str(old)], cwd=ROOT, env=env, text=True, capture_output=True)
        (folder / "baseline-overflow.log").write_text(failed.stdout + failed.stderr)
        assert failed.returncode != 0
        assert re.search(r"AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:", failed.stderr)
        for exact in (False, True):
            target = folder / ("fixed-exact" if exact else "fixed-default")
            regression(ROOT, target, exact)
            bench.run([target], log, cpu=cpu, env=env)
        print("WRITE_REPRO_JSON=" + json.dumps(dict(compiler=cc,
              baseline_exit=failed.returncode, baseline_sanitizer_failure=True,
              fixed_default_pass=True, fixed_exact_pass=True)), flush=True)
        results = {}
        for variant in (("before", "after") if cc == "clang-19" else ("after", "before")):
            source = before if variant == "before" else ROOT
            dest = folder / variant
            dest.mkdir(parents=True, exist_ok=True)
            verify = bench.build(cc, dest / "verify", "verify", source, deps, out, log)
            bench.run([verify, "--verify"], log, cpu=cpu, env=env)
            binary = bench.build(cc, dest / "release", "release", source, deps, out, log)
            assembly = bench.capture(["objdump", "-drwC", binary])
            (dest / "release.asm").write_text(assembly + "\n")
            hot = bench.capture(["objdump", "-d", "--disassemble=recheck_hot", binary])
            assert re.search(r"\bcallq?\s+\*", hot)
            for symbol, callee in (("recheck_sx", "sx_write_view"), ("recheck_sds", "sdscpylen"),
                                   ("recheck_bstr", "bassignblk"), ("recheck_raw", "memcpy")):
                text = bench.capture(["objdump", "-d", "--disassemble=" + symbol, binary])
                assert callee in text and "recheck_call_counts" not in text, text
            rows = []
            for process in range(3):
                raw = dest / f"samples-{process}.csv"
                bench.run([binary, "--measure", str(process)], log, cpu=cpu, stdout=raw)
                with raw.open() as f:
                    rows.extend(csv.DictReader(f))
            groups = {}
            for row in rows:
                assert int(row["expected_result"]) == 1
                groups.setdefault((row["case"], row["library"], row["mode"]), []).append(float(row["ns_per_op"]))
            assert len(groups) == len(sizes) * 4 * 2
            stats = [dict(case=k[0], library=k[1], mode=k[2], median_ns=statistics.median(v),
                          p10_ns=bench.quantile(v, .1), p90_ns=bench.quantile(v, .9),
                          samples=len(v)) for k, v in sorted(groups.items())]
            assert all(r["samples"] == 21 for r in stats)
            (dest / "summary.json").write_text(json.dumps(stats, indent=2) + "\n")
            results[variant] = stats
            print("WRITE_RESULT_JSON=" + json.dumps(dict(compiler=cc, variant=variant,
                  binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  results=stats)), flush=True)
        for mode in ("hot", "flushed"):
            report += [f"## {cc}: {mode}", "",
                       "| Bytes | StringX before ns | StringX after ns | SDS ns | bstrlib ns |",
                       "| --- | ---: | ---: | ---: | ---: |"]
            def value(variant, n, lib):
                return next(r["median_ns"] for r in results[variant]
                            if r["case"] == f"write{n}" and r["library"] == lib and r["mode"] == mode)
            for n in sizes:
                report.append(f"| {n} | {value('before',n,'StringX'):.2f} | "
                              f"{value('after',n,'StringX'):.2f} | {value('after',n,'SDS'):.2f} | "
                              f"{value('after',n,'bstrlib'):.2f} |")
            report.append("")
    text = "\n".join(report) + "\n"
    (out / "summary.md").write_text(text)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write(text)

if __name__ == "__main__":
    main()
