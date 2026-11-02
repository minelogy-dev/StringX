#!/usr/bin/env python3
"""Reproducible native-library microbenchmarks; no third-party Python packages."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "452314a7ca51f90a7a8fa66930947164af8fc4f6"
PINS = {
    "sds": "5347739b1581fcba74fd5cab1fc21d2aef317d71",
    "bstrlib": "f0ff1e808102a42cdc7204a4bb6fe231a24c4546",
}
COMMON = ["-std=c23", "-march=x86-64", "-fno-lto"]

def output(args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()

def invoke(args, log, *, env=None, stdout=None, cpu=None):
    print("+", " ".join(map(str, args)), flush=True)
    hook = (lambda: os.sched_setaffinity(0, {cpu})) if cpu is not None else None
    with log.open("a") as err:
        if stdout is None:
            result = subprocess.run(args, cwd=ROOT, env=env, stdout=err, stderr=err, preexec_fn=hook)
        else:
            with stdout.open("w") as out:
                result = subprocess.run(args, cwd=ROOT, env=env, stdout=out, stderr=err, preexec_fn=hook)
    if result.returncode:
        print(log.read_text()[-20000:], file=sys.stderr)
        raise RuntimeError(f"command failed ({result.returncode}): {args}")

def compile_binary(cc, dest, mode, deps, log):
    flags = COMMON + (["-O1", "-g", "-fsanitize=address,undefined",
                       "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
                      if mode == "check" else ["-O3", "-DNDEBUG"])
    includes = ["-I" + str(ROOT / "include"), "-I" + str(deps / "sds"),
                "-I" + str(deps / "bstrlib")]
    pkg_cflags = output(["pkg-config", "--cflags", "libpcre2-8"]).split()
    pkg_libs = output(["pkg-config", "--libs", "libpcre2-8"]).split()
    sources = [ROOT / "src/sx_core.c", ROOT / "src/sx_raw.c", ROOT / "src/sx_adv.c",
               deps / "sds/sds.c", deps / "bstrlib/bstrlib.c", ROOT / "benchmarks/strings.c"]
    objects = []
    dest.mkdir(parents=True, exist_ok=True)
    for i, source in enumerate(sources):
        obj = dest / f"{i}.o"
        options = flags + includes + pkg_cflags
        if source == ROOT / "benchmarks/strings.c":
            options += ["-Wall", "-Wextra", "-Werror"]
            if mode == "alloc":
                options += ["-DBENCH_ALLOC"]
        invoke([cc, *options, "-c", str(source), "-o", str(obj)], log)
        objects.append(str(obj))
    binary = dest / "strings"
    wrap = ["-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"] if mode == "alloc" else []
    invoke([cc, *flags, *objects, *wrap, *pkg_libs, "-o", str(binary)], log)
    return binary

def quantile(values, fraction):
    ordered = sorted(values)
    point = (len(ordered) - 1) * fraction
    lo = int(point)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (point - lo)

def summarize(samples, allocations, compiler):
    rows = list(csv.DictReader(samples.open()))
    alloc = {(r["case"], r["library"]): r for r in csv.DictReader(allocations.open())}
    groups = {}
    for r in rows:
        groups.setdefault((r["case"], r["library"]), []).append(r)
    stats = []
    for (case, library), data in groups.items():
        values = [float(r["ns_per_op"]) for r in data]
        assert len(values) == 11, (case, library, len(values))
        memory = alloc[(case, library)]
        stats.append({
            "case": case, "library": library, "bytes": int(data[0]["bytes"]),
            "part": int(data[0]["part"]), "samples": len(values),
            "median_ns": statistics.median(values),
            "p10_ns": quantile(values, 0.1), "p90_ns": quantile(values, 0.9),
            "malloc_calls": int(memory["malloc_calls"]),
            "calloc_calls": int(memory["calloc_calls"]),
            "realloc_calls": int(memory["realloc_calls"]),
            "free_calls": int(memory["free_calls"]),
            "requested_bytes": int(memory["requested_bytes"]),
        })
    assert len(stats) == 82, len(stats)
    return {"compiler": compiler, "results": stats}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", action="append", required=True)
    parser.add_argument("--deps", type=Path, default=ROOT / "build/benchmarks/deps")
    parser.add_argument("--out", type=Path, default=ROOT / "build/benchmarks/results")
    args = parser.parse_args()
    args.deps = args.deps.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    for lib, sha in PINS.items():
        assert output(["git", "rev-parse", "HEAD"], args.deps / lib) == sha, lib
    affinity = sorted(os.sched_getaffinity(0))
    assert affinity
    cpu = affinity[0]
    hardware = {
        "platform": platform.platform(), "machine": platform.machine(),
        "date_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "benchmark_commit": output(["git", "rev-parse", "HEAD"]),
        "initial_library_baseline": BASELINE,
        "dependency_commits": PINS, "allowed_cpus": affinity, "pinned_cpu": cpu,
        "lscpu": json.loads(output(["lscpu", "-J"])),
        "timing_flags": COMMON + ["-O3", "-DNDEBUG"],
        "samples": 11, "target_sample_ns": 20000000,
        "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in [ROOT / "src/sx_core.c", ROOT / "src/sx_raw.c", ROOT / "src/sx_adv.c"]},
    }
    (args.out / "environment.json").write_text(json.dumps(hardware, indent=2) + "\n")
    print("BENCH_ENVIRONMENT_JSON=" + json.dumps(hardware, separators=(",", ":")), flush=True)
    all_results = []
    for cc in args.cc:
        folder = args.out / Path(cc).name
        folder.mkdir(parents=True, exist_ok=True)
        log = folder / "commands.log"
        version = output([cc, "--version"])
        (folder / "compiler.txt").write_text(version + "\n")
        sanitizer = compile_binary(cc, folder / "check", "check", args.deps, log)
        env = os.environ.copy()
        env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        invoke([str(sanitizer), "--check"], log, env=env, cpu=cpu)
        binary = compile_binary(cc, folder / "timing", "timing", args.deps, log)
        invoke([str(binary)], log, stdout=folder / "samples.csv", cpu=cpu)
        counter = compile_binary(cc, folder / "alloc", "alloc", args.deps, log)
        invoke([str(counter)], log, stdout=folder / "allocations.csv", cpu=cpu)
        result = summarize(folder / "samples.csv", folder / "allocations.csv", cc)
        result["compiler_version"] = version.splitlines()[0]
        (folder / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
        all_results.append(result)
        print("BENCH_RESULT_JSON=" + json.dumps(result, separators=(",", ":")), flush=True)
    lines = [
        "# StringX baseline benchmark", "",
        f"Measured commit: {hardware['benchmark_commit']}; initial library baseline: {BASELINE}. See environment.json for CPU and flags.", "",
        "Each value is median ns/op from 11 samples; p10–p90 describes observed spread, not a confidence interval.",
        "An append operation builds and frees the entire output string. Allocation totals are from a separate untimed binary.",
        "SDS search uses glibc memmem/memchr. StringX owned split materializes its returned views; view-only split is reported separately.",
        "Borrowed substring views do not copy bytes. All fixtures are reused in-process; these are hot-input microbenchmarks.",
        "Shared hosted-runner scheduling and frequency changes limit absolute timing claims.", "",
    ]
    for result in all_results:
        lines += [f"## {result['compiler_version']}", "",
                  "| Case | Library | Median ns/op | p10–p90 ns | malloc + calloc | realloc | Requested bytes/op |",
                  "| --- | --- | ---: | ---: | ---: | ---: | ---: |"]
        for row in result["results"]:
            lines.append(f"| {row['case']} | {row['library']} | {row['median_ns']:.2f} | "
                         f"{row['p10_ns']:.2f}–{row['p90_ns']:.2f} | "
                         f"{row['malloc_calls'] + row['calloc_calls']} | {row['realloc_calls']} | {row['requested_bytes']} |")
        lines.append("")
    report = "\n".join(lines) + "\n"
    (args.out / "summary.md").write_text(report)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as out:
            out.write(report)

if __name__ == "__main__":
    main()
