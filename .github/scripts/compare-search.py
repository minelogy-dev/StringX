#!/usr/bin/env python3
"""Same-host before/after check; timing is evidence, not a noisy merge gate."""
import csv
import importlib.util
import json
import os
from pathlib import Path
import shutil
import statistics

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "build/search-performance"
BASELINE = "452314a7ca51f90a7a8fa66930947164af8fc4f6"
HARNESS = "ecf31d3d65a45466222dfb0f358e176a22d1bc1c"

def main():
    harness = WORK / "harness"
    spec = importlib.util.spec_from_file_location("baseline_bench", harness / "benchmarks/run.py")
    bench = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bench)
    assert bench.output(["git", "rev-parse", "HEAD"], WORK / "before") == BASELINE
    assert bench.output(["git", "rev-parse", "HEAD"], harness) == HARNESS
    deps = WORK / "deps"
    for lib, sha in bench.PINS.items():
        assert bench.output(["git", "rev-parse", "HEAD"], deps / lib) == sha
    after = WORK / "after"
    for folder in ("src", "include"):
        shutil.copytree(ROOT / folder, after / folder, dirs_exist_ok=True)

    # Extend the existing 28 cases with common endpoint bytes, misses
    # across the private cutoff, and repetitive text with both bytes present.
    source = (harness / "benchmarks/strings.c").read_text()
    source = source.replace("RESERVED = 16", "RESERVED = 16, COMMON = 32, PERIODIC = 64, END_PRESENT = 128")
    anchor = '  {"copy_cstr_16"'
    extra = [
        ("common8_miss_65536", 8, "COMMON"),
        ("common8_end_65536", 8, "COMMON | HIT_END"),
        ("periodic8_miss_65536", 8, "PERIODIC"),
        ("common31_miss_65536", 31, "COMMON"),
        ("common32_miss_65536", 32, "COMMON"),
        ("common33_miss_65536", 33, "COMMON"),
        ("repeated128_end_present_miss_65536", 128, "REPEATED | END_PRESENT"),
        ("common257_miss_65536", 257, "COMMON"),
    ]
    assert anchor in source
    source = source.replace(anchor, "".join(
        f'  {{"{name}", FIND, 65536, {m}, {flags}}},\n'
        for name, m, flags in extra) + anchor, 1)
    anchor = "  f->sep = "
    assert anchor in source
    setup = """
  if (c->op == FIND) {
    if (c->flags & COMMON)
      for (size_t i = 0; i < m; ++i) f->needle[i] = (char)('a' + i % 25);
    if (c->flags & PERIODIC) {
      for (size_t i = 0; i < c->n; ++i) f->data[i] = i % 2 ? 'b' : 'a';
      memset(f->needle, 'a', m);
      f->needle[m - 1] = 'b';
    }
    if (c->flags & END_PRESENT)
      for (size_t i = m - 2; i < c->n; i += m - 1) f->data[i] = 'b';
    if (c->flags & HIT_FRONT) memcpy(f->data, f->needle, m);
    if (c->flags & HIT_END) memcpy(f->data + c->n - m, f->needle, m);
  }
"""
    source = source.replace(anchor, setup + anchor, 1)
    anchor = "    Fixture f; prepare(&f, &cases[ci]);"
    assert anchor in source
    source = source.replace(anchor,
        "    if (cases[ci].op != FIND && cases[ci].op != FIND_CHAR) continue;\\n"
        + anchor, 1)
    for variant in ("before", "after"):
        folder = WORK / variant / "benchmarks"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "strings.c").write_text(source)

    cpu = min(os.sched_getaffinity(0))
    out = WORK / "results"
    out.mkdir(parents=True, exist_ok=True)
    metadata = {
        "before": BASELINE, "after": bench.output(["git", "rev-parse", "HEAD"], ROOT),
        "harness": HARNESS, "dependencies": bench.PINS,
        "pinned_cpu": cpu, "lscpu": json.loads(bench.output(["lscpu", "-J"])),
        "libc": bench.output(["ldd", "--version"]).splitlines()[0],
        "timing_flags": bench.COMMON + ["-O3", "-DNDEBUG"],
        "portable_flags": ["-DSX_FORCE_NO_MEMMEM"],
        "samples": 11, "target_sample_ns": 20000000,
        "note": "Variants run sequentially on the same pinned CPU. Hosted-runner scheduling and frequency remain uncontrolled.",
    }
    (out / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (out / "strings.c").write_text(source)
    print("SEARCH_COMPARE_ENV=" + json.dumps(metadata), flush=True)
    report = ["# Search before/after", "",
              "Same job, pinned CPU, -O3, no LTO; medians of 11 hot-input samples.",
              "Sequential runs are subject to host scheduling/frequency changes; small differences are inconclusive.", ""]
    env = os.environ.copy()
    env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    common_flags = list(bench.COMMON)
    for ci, cc in enumerate(("gcc-14", "clang-19")):
        results = {}
        for variant in (("before", "after", "portable") if ci == 0 else ("portable", "after", "before")):
            bench.ROOT = WORK / ("before" if variant == "before" else "after")
            bench.COMMON = common_flags + (["-DSX_FORCE_NO_MEMMEM"] if variant == "portable" else [])
            folder = out / cc / variant
            folder.mkdir(parents=True, exist_ok=True)
            log = folder / "commands.log"
            check = bench.compile_binary(cc, folder / "check", "check", deps, log)
            bench.invoke([str(check), "--check"], log, env=env, cpu=cpu)
            binary = bench.compile_binary(cc, folder / "timing", "timing", deps, log)
            bench.invoke([str(binary)], log, stdout=folder / "samples.csv", cpu=cpu)
            alloc = bench.compile_binary(cc, folder / "alloc", "alloc", deps, log)
            bench.invoke([str(alloc)], log, stdout=folder / "allocations.csv", cpu=cpu)
            rows = list(csv.DictReader((folder / "samples.csv").open()))
            memory = {(r["case"], r["library"]): r
                      for r in csv.DictReader((folder / "allocations.csv").open())}
            groups = {}
            for r in rows:
                groups.setdefault((r["case"], r["library"]), []).append(float(r["ns_per_op"]))
            assert len(groups) == (8 + len(extra)) * 3, len(groups)
            stats = {}
            for (case, library), values in groups.items():
                assert len(values) == 11
                stats[case, library] = {
                    "case": case, "library": library,
                    "median_ns": statistics.median(values),
                    "p10_ns": bench.quantile(values, .1), "p90_ns": bench.quantile(values, .9),
                    "malloc_calls": int(memory[case, library]["malloc_calls"]),
                }
            (folder / "summary.json").write_text(json.dumps(list(stats.values()), indent=2) + "\n")
            results[variant] = stats
        bench.COMMON = common_flags
        report += [f"## {bench.output([cc, '--version']).splitlines()[0]}", "",
                   "| Case | Before ns | Native ns | Portable ns | Native speedup | Portable speedup |",
                   "| --- | ---: | ---: | ---: | ---: | ---: |"]
        compared = []
        for key, before in sorted(results["before"].items()):
            case, library = key
            if not library.startswith("StringX"):
                continue
            after_row = results["after"][key]
            ratio = before["median_ns"] / after_row["median_ns"]
            portable = results["portable"][key]
            portable_ratio = before["median_ns"] / portable["median_ns"]
            row = {"case": case, "before": before, "after": after_row, "speedup": ratio,
                   "portable": portable, "portable_speedup": portable_ratio}
            compared.append(row)
            report.append(f"| {case} | {before['median_ns']:.2f} | {after_row['median_ns']:.2f} | {portable['median_ns']:.2f} | {ratio:.2f}x | {portable_ratio:.2f}x |")
        print("SEARCH_COMPARE_JSON=" + json.dumps({"compiler": cc, "results": compared}), flush=True)
        report.append("")
    text = "\n".join(report) + "\n"
    (out / "summary.md").write_text(text)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as dest:
            dest.write(text)

if __name__ == "__main__":
    main()
