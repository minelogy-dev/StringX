#!/usr/bin/env python3
"""Search diagnosis: unchanged library, allocation ablation, logical traces and assembly."""
import argparse
import csv
import ctypes
import errno
import json
import os
from pathlib import Path
import re
import statistics
import struct
import shutil
import subprocess
import sys

from run import ROOT, COMMON, PINS, invoke, output, quantile

def perf_probe():
    if os.uname().machine != "x86_64":
        return {"allowed": None, "reason": "probe only implemented for x86_64"}
    libc = ctypes.CDLL(None, use_errno=True)
    libc.syscall.restype = ctypes.c_long
    attr = ctypes.create_string_buffer(128)
    struct.pack_into("IIQ", attr, 0, 0, 128, 0)
    struct.pack_into("Q", attr, 40, (1 << 0) | (1 << 5) | (1 << 6))
    fd = libc.syscall(ctypes.c_long(298), ctypes.byref(attr), ctypes.c_int(0),
                      ctypes.c_int(-1), ctypes.c_int(-1), ctypes.c_ulong(0))
    if fd < 0:
        code = ctypes.get_errno()
        return {"allowed": False, "errno": code, "reason": os.strerror(code)}
    os.close(fd)
    return {"allowed": True, "note": "availability probe only; this suite does not collect hardware counters"}

def disassemble_libc(out, log):
    class DlInfo(ctypes.Structure):
        _fields_ = [("filename", ctypes.c_char_p), ("base", ctypes.c_void_p),
                    ("symbol", ctypes.c_char_p), ("symbol_address", ctypes.c_void_p)]
    libc = ctypes.CDLL("libc.so.6")
    libc.dladdr.argtypes = [ctypes.c_void_p, ctypes.POINTER(DlInfo)]
    libc.dladdr.restype = ctypes.c_int
    resolved = {}
    for name in ("memchr", "memmem"):
        address = ctypes.cast(getattr(libc, name), ctypes.c_void_p).value
        info = DlInfo()
        assert libc.dladdr(ctypes.c_void_p(address), ctypes.byref(info))
        offset = address - info.base
        path = info.filename.decode()
        target = out / f"libc-{name}.asm.txt"
        invoke(["objdump", "-d", "-Mintel", f"--start-address={offset}",
                f"--stop-address={offset + 768}", path], log, stdout=target)
        resolved[name] = {"library": path, "offset": offset,
                          "resolved_symbol": info.symbol.decode() if info.symbol else None}
        print("LIBC_ASM_JSON=" + json.dumps({"name": name, "assembly": target.read_text()}))
    return resolved


def make_counted_bstrlib(deps, out):
    """Instrument the pinned source at build time, preserving its notice."""
    assert output(["git", "rev-parse", "HEAD"], deps / "bstrlib") == PINS["bstrlib"]
    source = (deps / "bstrlib/bstrlib.c").read_text()
    start = source.index("\nint binstr (")
    end = source.index("\n}\n", start) + 3
    function = source[start:end].replace("int binstr (", "int diag_binstr_counted (", 1)
    edits = [
        ("c1 != d1[i]", "(++diag_bstr_comparisons, c1 != d1[i])"),
        ("c1 != d1[1+i]", "(++diag_bstr_comparisons, c1 != d1[1+i])"),
        ("c1 == d1[i]", "(++diag_bstr_comparisons, c1 == d1[i])"),
        ("i -= j;", "diag_bstr_resets++; i -= j;"),
    ]
    for old, new in edits:
        assert function.count(old) == 1, old
        function = function.replace(old, new, 1)
    notice = source[:source.index("#include")]
    target = out / "counted-bstrlib.c"
    target.write_text(notice + "\n#include <bstrlib.h>\n#include <stdint.h>\n"
                      "uint64_t diag_bstr_comparisons, diag_bstr_resets;\n" + function + "\n")
    shutil.copyfile(deps / "bstrlib/LICENSE", out / "bstrlib-license.txt")
    return target

def build(cc, folder, deps, log, counted, sanitize=False):
    folder.mkdir(parents=True, exist_ok=True)
    flags = COMMON + (["-O1", "-g", "-fsanitize=address,undefined",
                       "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
                      if sanitize else ["-O3", "-DNDEBUG", "-g"])
    includes = ["-I" + str(ROOT / "include"), "-I" + str(deps / "bstrlib")]
    sources = [ROOT / "src/sx_core.c", ROOT / "src/sx_raw.c",
               deps / "bstrlib/bstrlib.c", counted]
    objects = []
    for i, source in enumerate(sources):
        obj = folder / f"{i}.o"
        remarks = []
        if not sanitize and i == 0:
            remarks = ([f"-fopt-info-vec-all={folder / 'vectorization.txt'}"]
                       if "gcc" in Path(cc).name else
                       ["-Rpass=loop-vectorize", "-Rpass-missed=loop-vectorize",
                        "-Rpass-analysis=loop-vectorize"])
        invoke([cc, *flags, *includes, *remarks, "-c", str(source), "-o", str(obj)], log)
        objects.append(str(obj))
    for wrapped in ([False] if sanitize else [False, True]):
        driver = folder / ("driver-wrapped.o" if wrapped else "driver.o")
        macros = ["-DDIAG_WRAP_ALLOC"] if wrapped else []
        invoke([cc, *flags, *includes, "-Wall", "-Wextra", "-Werror", *macros,
                "-c", str(ROOT / "benchmarks/search_diag.c"), "-o", str(driver)], log)
        binary = folder / ("wrapped" if wrapped else "plain")
        wrap = ["-Wl,--wrap=malloc,--wrap=free"] if wrapped else []
        invoke([cc, *flags, *objects, str(driver), *wrap, "-o", str(binary)], log)
    if not sanitize:
        for obj, label in [(Path(objects[0]), "sx-core"), (folder / "driver.o", "kernels")]:
            target = folder / f"{label}.asm.txt"
            invoke(["objdump", "-drwCS", "-Mintel", str(obj)], log, stdout=target)
            text = target.read_text()
            symbols = re.findall(r"^[0-9a-f]+ <([^>]+)>:", text, re.M)
            print("ASM_SYMBOLS_JSON=" + json.dumps({"compiler": cc, "unit": label, "symbols": symbols}))
            if label == "sx-core":
                selected = []
                lines = text.splitlines()
                needles = ["while (q && t[i] != needle[q])", "if (t[i] == needle[q])"]
                for i, line in enumerate(lines):
                    if any(needle in line for needle in needles):
                        selected.append("\n".join(lines[max(0, i - 4):i + 40]))
                        if len(selected) == 2:
                            break
                print("KMP_ASM_JSON=" + json.dumps({"compiler": cc, "snippets": selected}))

            else:
                clean = output(["objdump", "-d", "-Mintel", str(obj)])
                selected = {}
                for name in ("kernel_scalar_probe", "kernel_kmp_scan", "kernel_kmp_first"):
                    match = re.search(r"^[0-9a-f]+ <" + name + r">:\n(.*?)(?=\n\n|\Z)", clean, re.M | re.S)
                    if match:
                        selected[name] = match.group(0)[:6500]
                print("KERNEL_ASM_JSON=" + json.dumps({"compiler": cc, "functions": selected}))

        loop_line = next(i for i, line in enumerate((ROOT / "src/sx_core.c").read_text().splitlines(), 1)
                         if "for (size_t i = l; i < er; i++)" in line)
        remark_file = folder / "vectorization.txt"
        remarks = remark_file.read_text() if remark_file.exists() else log.read_text()
        selected_remarks = []
        for line in remarks.splitlines():
            position = re.search(r"sx_core.c:(\d+):", line)
            if position and abs(int(position.group(1)) - loop_line) <= 8:
                selected_remarks.append(line)
        print("VECTORIZATION_JSON=" + json.dumps({"compiler": cc, "forward_loop_line": loop_line,
                                                  "remarks": selected_remarks[-24:]}))
    return folder / "plain", folder / "wrapped"

def report(csvs):
    groups = {}
    for path in csvs:
        for row in csv.DictReader(path.open()):
            groups.setdefault((row["case"], row["variant"]), []).append(float(row["ns_per_op"]))
    results = []
    for (case, variant), values in groups.items():
        assert len(values) == 9, (case, variant, len(values))
        results.append({"case": case, "variant": variant, "samples": 9,
                        "median_ns": statistics.median(values),
                        "p10_ns": quantile(values, .1), "p90_ns": quantile(values, .9)})
    return results

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", action="append", required=True)
    ap.add_argument("--out", type=Path, default=ROOT / "build/benchmarks/results/search")
    ap.add_argument("--deps", type=Path, default=ROOT / "build/benchmarks/deps")
    args = ap.parse_args()
    args.out = args.out.resolve()
    args.deps = args.deps.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    cpu = min(os.sched_getaffinity(0))
    log = args.out / "diagnostics.log"
    counted = make_counted_bstrlib(args.deps, args.out)
    metadata = {
        "commit": output(["git", "rev-parse", "HEAD"]), "pinned_cpu": cpu,
        "flags": COMMON + ["-O3", "-DNDEBUG", "-g"], "samples": 9,
        "hardware_perf": perf_probe(),
        "counter_interpretation": "logical C-level comparisons, not retired machine instructions",
        "control_interpretation": "byte probes and component timings do not implement a full substring search; timings are not additive",
        "libc": disassemble_libc(args.out, log),
    }
    (args.out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print("SEARCH_META_JSON=" + json.dumps(metadata), flush=True)
    all_results = []
    for cc in args.cc:
        folder = args.out / Path(cc).name
        folder.mkdir(parents=True, exist_ok=True)
        check, _ = build(cc, folder / "check", args.deps, log, counted, True)
        env = os.environ.copy()
        env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        invoke([str(check), "--check"], log, env=env, stdout=folder / "counts.log", cpu=cpu)
        counts = [json.loads(line.split("=", 1)[1]) for line in
                  (folder / "counts.log").read_text().splitlines() if line.startswith("COUNTS_JSON=")]
        assert len(counts) == 8
        (folder / "counts.json").write_text(json.dumps(counts, indent=2) + "\n")
        plain, wrapped = build(cc, folder / "timing", args.deps, log, counted)
        invoke([str(plain), "--unwrapped-only", str(folder / "unwrapped.csv")],
               log, stdout=folder / "unwrapped.log", cpu=cpu)
        invoke([str(wrapped), "--measure", str(folder / "components.csv")],
               log, stdout=folder / "components.log", cpu=cpu)
        result = {"compiler": cc, "results": report([folder / "unwrapped.csv", folder / "components.csv"]),
                  "counts": counts}
        (folder / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
        print("SEARCH_DIAG_JSON=" + json.dumps(result, separators=(",", ":")), flush=True)
        all_results.append(result)
    lines = ["# Search diagnosis", "",
             "The production implementation is unchanged. sx_heap and sx_arena use the same compiled library object.",
             "Arena mode substitutes only the prefix-table allocation/free; prefix construction and scanning remain.",
             "Standalone first-only and precomputed-table kernels are controlled algorithm references, not production patches.",
             "Component timings cannot be added to obtain an exact original-runtime breakdown.",
             "Counters count logical source comparisons; they are not retired-instruction or branch-miss measurements.", ""]
    for item in all_results:
        lines += [f"## {item['compiler']}", "",
                  "| Case | Variant | Median ns | p10–p90 ns |",
                  "| --- | --- | ---: | ---: |"]
        for r in item["results"]:
            lines.append(f"| {r['case']} | {r['variant']} | {r['median_ns']:.2f} | {r['p10_ns']:.2f}–{r['p90_ns']:.2f} |")
        lines.append("")
    (args.out / "summary.md").write_text("\n".join(lines) + "\n")

if __name__ == "__main__":
    main()
