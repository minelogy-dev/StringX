#!/usr/bin/env python3
"""Independent-oracle, opaque-call, hot/flushed search benchmark recheck."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = "40987da6ddaa2f31ca04d5d7f4545fa0704b7eb4"
PINS = {"sds": "5347739b1581fcba74fd5cab1fc21d2aef317d71",
        "bstrlib": "f0ff1e808102a42cdc7204a4bb6fe231a24c4546"}
COMMON = ["-std=c23", "-march=x86-64", "-fno-lto",
          "-fno-builtin-memmem", "-fno-builtin-memchr"]
PROCESSES, SAMPLES = 3, 7

def capture(args, cwd=ROOT):
    return subprocess.check_output(list(map(str, args)), cwd=cwd, text=True).strip()

def run(args, log, *, stdout=None, cpu=None, env=None):
    args = list(map(str, args))
    print("+", " ".join(args), flush=True)
    hook = (lambda: os.sched_setaffinity(0, {cpu})) if cpu is not None else None
    with log.open("a") as err:
        if stdout is None:
            code = subprocess.run(args, cwd=ROOT, stdout=err, stderr=err,
                                  preexec_fn=hook, env=env).returncode
        else:
            with stdout.open("w") as out:
                code = subprocess.run(args, cwd=ROOT, stdout=out, stderr=err,
                                      preexec_fn=hook, env=env).returncode
    if code:
        print(log.read_text()[-18000:], flush=True)
        raise RuntimeError(f"exit {code}: {args}")

def cases():
    result = []
    def add(name, kind, n=65536, m=128, period=0, phase=0, seed=0x12ab34cd, hit=0):
        result.append(dict(name=name, kind=kind, n=n, m=m, period=period,
                           phase=phase, seed=seed, hit=hit))
    add("legacy_absent8", 0, m=8)
    add("legacy_absent128", 2)
    add("legacy_common8", 1, m=8)
    add("legacy_common33", 1, m=33)
    add("byte_absent", 0, m=1)
    for period in (63, 64, 65, 125, 126, 127, 128, 129):
        for phase in (0, 1, 31):
            add(f"period{period}_phase{phase}", 3, period=period, phase=phase)
    for m in (8, 16, 31, 32, 33, 64, 127, 128, 129, 256, 257):
        for i, seed in enumerate((0x18273645, 0x4f295a17)):
            add(f"random{m}_seed{i}", 4, m=m, seed=seed)
    for m in (8, 33, 128):
        for hit, label in ((1, "front"), (2, "middle"), (3, "tail")):
            add(f"random{m}_hit_{label}", 4, m=m, hit=hit)
        add(f"binary{m}_miss", 5, m=m)
        add(f"binary{m}_hit_tail", 5, m=m, hit=3)
    for phase, label in enumerate(("tail_hit", "tail_nearmiss", "early_nearmiss")):
        add(f"rare_b_{label}", 6, phase=phase)
    for denominator in (2, 16, 256, 4096):
        add(f"terminal_frequency_1in{denominator}", 7, period=denominator)
    for n in (4096, 1048576):
        add(f"size{n}_random33", 4, n=n, m=33)
        for period in (126, 127):
            add(f"size{n}_period{period}", 3, n=n, period=period)
    assert len({c["name"] for c in result}) == len(result)
    return result

def build(cc, folder, mode, source, deps, generated, log):
    folder.mkdir(parents=True, exist_ok=True)
    flags = COMMON + (["-O1", "-g", "-fsanitize=address,undefined",
                       "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                       "-DRECHECK_VERIFY_CALLS"] if mode == "verify"
                      else ["-O3", "-DNDEBUG"])
    includes = ["-isystem", str(source / "include"), "-isystem", str(deps / "sds"),
                "-isystem", str(deps / "bstrlib"), "-I" + str(ROOT / "benchmarks"),
                "-I" + str(generated)]
    sources = [source / "src/sx_core.c", source / "src/sx_raw.c",
               deps / "sds/sds.c", deps / "bstrlib/bstrlib.c"]
    objects = []
    for i, src in enumerate(sources):
        obj = folder / f"lib-{i}.o"
        run([cc, *flags, *includes, "-c", src, "-o", obj], log)
        objects.append(obj)
    for i in range(4):
        obj = folder / f"adapter-{i}.o"
        run([cc, *flags, *includes, "-Wall", "-Wextra", "-Wpedantic", "-Werror",
             f"-DLIB_INDEX={i}", "-c", ROOT / "benchmarks/recheck_adapter.c",
             "-o", obj], log)
        objects.append(obj)
    main = folder / "main.o"
    run([cc, *flags, *includes, "-Wall", "-Wextra", "-Wpedantic", "-Werror",
         "-c", ROOT / "benchmarks/recheck.c", "-o", main], log)
    binary = folder / "benchmark"
    run([cc, *flags, *objects, main, "-o", binary], log)
    return binary

def quantile(values, p):
    v = sorted(values)
    point = (len(v) - 1) * p
    lo = int(point)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (point - lo)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--deps", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    source, deps, out = args.source.resolve(), args.deps.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    assert capture(["git", "rev-parse", "HEAD"], source) == SOURCE
    for lib, sha in PINS.items():
        assert capture(["git", "rev-parse", "HEAD"], deps / lib) == sha
    configs = cases()
    (out / "cases.json").write_text(json.dumps(configs, indent=2) + "\n")
    header = ["static const Case cases[] = {"]
    for c in configs:
        header.append("  {%s, %d, %d, %d, %d, %d, UINT32_C(%d), %d}," %
                      (json.dumps(c["name"]), c["kind"], c["n"], c["m"],
                       c["period"], c["phase"], c["seed"], c["hit"]))
    header.append("};")
    (out / "recheck_cases.h").write_text("\n".join(header) + "\n")
    cpu = min(os.sched_getaffinity(0))
    tracked = [ROOT / "benchmarks" / p for p in
               ("recheck.c", "recheck.h", "recheck_adapter.c", "recheck.py")]
    metadata = dict(
        benchmark_commit=capture(["git", "rev-parse", "HEAD"]),
        stringx_commit=SOURCE, dependencies=PINS, cases=len(configs),
        compilers=["gcc-14", "clang-19"], processes=PROCESSES,
        samples_per_process=SAMPLES, samples_per_group=PROCESSES * SAMPLES,
        hot_target_ns=10000000, flushed_calls_per_sample=8, cpu=cpu,
        platform=platform.platform(), libc=capture(["ldd", "--version"]).splitlines()[0],
        lscpu=json.loads(capture(["lscpu", "-J"])),
        timing_flags=COMMON + ["-O3", "-DNDEBUG"],
        cold_method="CLFLUSH each text/needle cache line; MFENCE+LFENCE before timing; flush cost excluded; metadata not explicitly flushed",
        source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in tracked + [source / "src/sx_core.c", source / "src/sx_raw.c"]},
        limitations="Shared runner; reused hot data and flush-based data-cache conditions; no subtraction of timer/adapter overhead.")
    (out / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print("RECHECK_ENV_JSON=" + json.dumps(metadata), flush=True)
    common_manifest = None
    selected = ["legacy_absent8", "legacy_absent128", "legacy_common8", "legacy_common33",
                "byte_absent", "period125_phase0", "period126_phase0", "period127_phase0",
                "period127_phase1", "period128_phase0", "period129_phase0",
                "rare_b_tail_nearmiss", "random32_seed0", "random33_seed0"]
    report = ["# Audited search rerun", "",
              "Four adapters, identical bytes, independent naive oracle, separate translation units, no LTO.",
              "Hot samples use repeated calls. Flushed samples average eight individually timed calls after CLFLUSH.",
              "Values are medians of 21 batch samples across three fresh processes; p10/p90 are observed spread.",
              "SDS and raw glibc use the same SDS buffer; raw glibc receives an explicit length.", ""]
    for cc in ("clang-19", "gcc-14"):
        folder = out / cc
        folder.mkdir(parents=True, exist_ok=True)
        log = folder / "commands.log"
        version = capture([cc, "--version"])
        (folder / "compiler.txt").write_text(version + "\n")
        sanitizer = build(cc, folder / "verify", "verify", source, deps, out, log)
        env = os.environ.copy()
        env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        run([sanitizer, "--verify"], log, cpu=cpu, env=env)
        binary = build(cc, folder / "release", "release", source, deps, out, log)
        assembly = capture(["objdump", "-drwC", binary])
        (folder / "release.asm").write_text(assembly + "\n")
        hot = capture(["objdump", "-d", "--disassemble=recheck_hot", binary])
        assert re.search(r"\bcallq?\s+\*", hot), hot
        snippets = {"recheck_hot": hot}
        for symbol, expected in (("recheck_sx", "sx_find"),
                                 ("recheck_sds", "memmem"),
                                 ("recheck_bstr", "binstr"),
                                 ("recheck_raw", "memmem")):
            text = capture(["objdump", "-d", f"--disassemble={symbol}", binary])
            assert expected in text, text
            assert "recheck_call_counts" not in text, text
            snippets[symbol] = text
        (folder / "call-audit.asm").write_text("\n".join(snippets.values()))
        audit = dict(compiler=cc, indirect_call_in_hot_loop=True,
                     separate_adapter_calls_verified=True,
                     release_has_no_call_counter=True,
                     binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        print("RECHECK_AUDIT_JSON=" + json.dumps(audit), flush=True)
        print("RECHECK_HOT_ASM=" + json.dumps(hot), flush=True)
        run([binary, "--describe"], log, cpu=cpu, stdout=folder / "fixtures.jsonl")
        manifest = [json.loads(s) for s in (folder / "fixtures.jsonl").read_text().splitlines()]
        assert len(manifest) == len(configs)
        if common_manifest is not None:
            assert manifest == common_manifest
        common_manifest = manifest
        rows = []
        for process in range(PROCESSES):
            raw = folder / f"samples-{process}.csv"
            run([binary, "--measure", str(process)], log, cpu=cpu, stdout=raw)
            with raw.open() as f:
                rows.extend(csv.DictReader(f))
        groups = {}
        for row in rows:
            groups.setdefault((row["case"], row["library"], row["mode"]), []).append(row)
        assert len(groups) == len(configs) * 4 * 2
        stats = []
        for (case, library, mode), group in sorted(groups.items()):
            assert len(group) == PROCESSES * SAMPLES
            values = [float(r["ns_per_op"]) for r in group]
            stats.append(dict(case=case, library=library, mode=mode,
                              n=int(group[0]["n"]), m=int(group[0]["m"]),
                              expected_pos=int(group[0]["expected_pos"]),
                              samples=len(values), median_ns=statistics.median(values),
                              p10_ns=quantile(values, .1), p90_ns=quantile(values, .9)))
        text = log.read_text()
        signatures = re.findall(r"RECHECK_MANIFEST_SIGNATURE=([0-9a-f]+)", text)
        assert len(signatures) == PROCESSES + 2 and len(set(signatures)) == 1, signatures
        controls = [json.loads(s) for s in
                    re.findall(r"RECHECK_CONTROL_JSON=(\{[^\n]*\})", text)]
        assert len(controls) == PROCESSES
        result = dict(compiler=cc, version=version.splitlines()[0],
                      fixture_signature=signatures[0], controls=controls, results=stats)
        (folder / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
        print("RECHECK_RESULT_JSON=" + json.dumps(result, separators=(",", ":")), flush=True)
        for mode in ("hot", "flushed"):
            report += [f"## {cc}: {mode}", "",
                       "| Case | Expected offset | StringX us | SDS + glibc us | bstrlib us | raw glibc us |",
                       "| --- | ---: | ---: | ---: | ---: | ---: |"]
            for case in selected:
                data = {r["library"]: r for r in stats if r["case"] == case and r["mode"] == mode}
                vals = [data[lib] for lib in ("StringX", "SDS+glibc", "bstrlib", "raw_glibc_same_buffer")]
                report.append(f'| {case} | {vals[0]["expected_pos"]} | ' +
                              " | ".join(f'{r["median_ns"]/1000:.3f}' for r in vals) + " |")
            report.append("")
    (out / "summary.md").write_text("\n".join(report) + "\n")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write("\n".join(report) + "\n")

if __name__ == "__main__":
    main()
