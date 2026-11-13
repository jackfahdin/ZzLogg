#!/usr/bin/env python3
"""Compare the default Qt matcher against a Git revision without changing the checkout."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess


ROOT = Path(__file__).resolve().parents[2]
HEADER = "src/regex/include/hsregularexpression.h"


def capture(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True)


def run_probe(executable, allocations=False, reverse=False):
    env = os.environ.copy()
    # Timing runs must not inherit allocation instrumentation.
    env.pop("LD_PRELOAD", None)
    command = [str(executable)]
    if reverse:
        command.append("--reverse")
    if allocations:
        env["LD_PRELOAD"] = str(executable.parent / "libregex_alloc_probe.so")
        command.append("--allocations")
    output = subprocess.check_output(command, cwd=ROOT, env=env, text=True)
    return [json.loads(line) for line in output.splitlines()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-root", required=True, type=Path)
    parser.add_argument("--mimalloc-library", required=True, type=Path)
    parser.add_argument("--baseline", default="302a354")
    parser.add_argument("--current-ref", help="Use a Git header instead of the worktree (e.g. baseline for an identical-code control)")
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--cpu", type=int, help="Pin measurements to this CPU (default: first allowed CPU)")
    parser.add_argument("--output", type=Path, default=ROOT / "out/benchmarks/p5-regex-decode")
    parser.add_argument("--require-savings", action="store_true",
                        help="Fail if nonempty multi-pattern cases do not allocate less")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    if platform.system() != "Linux" or platform.libc_ver()[0] != "glibc":
        parser.error("This runner's allocation probe currently requires Linux/glibc")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    baseline = capture("git", "rev-parse", "--verify", f"{args.baseline}^{{commit}}").strip()
    sources = {
        "baseline": subprocess.check_output(["git", "show", f"{baseline}:{HEADER}"], cwd=ROOT),
        "current": (ROOT / HEADER).read_bytes(),
    }
    if args.current_ref:
        current_ref = capture("git", "rev-parse", "--verify", f"{args.current_ref}^{{commit}}").strip()
        sources["current"] = subprocess.check_output(["git", "show", f"{current_ref}:{HEADER}"], cwd=ROOT)
    snapshots = {}
    for name, source in sources.items():
        snapshot = output / name / "include"
        snapshot.mkdir(parents=True, exist_ok=True)
        (snapshot / "hsregularexpression.h").write_bytes(source)
        snapshots[name] = snapshot
    build = output / "build"
    with (output / "build.log").open("w") as log:
        subprocess.run([
            "cmake", "-S", str(ROOT / "tools/benchmarks/regex_decode"),
            "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_PREFIX_PATH={args.qt_root.resolve()}",
            f"-DBASELINE_INCLUDE_DIR={snapshots['baseline']}",
            f"-DCURRENT_INCLUDE_DIR={snapshots['current']}",
            f"-DMIMALLOC_LIBRARY={args.mimalloc_library.resolve()}",
        ], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["cmake", "--build", str(build), "--parallel", "4"],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    executable = build / "regex_decode_benchmark"

    allowed = os.sched_getaffinity(0)
    cpu = min(allowed) if args.cpu is None else args.cpu
    if cpu not in allowed:
        parser.error(f"CPU {cpu} is outside the allowed affinity mask")
    os.sched_setaffinity(0, {cpu})
    measurements = []
    for round_index in range(args.rounds):
        for row in run_probe(executable, reverse=bool(round_index % 2)):
            measurements.append({"round": round_index, **row})
        print(f"Timing round {round_index + 1}/{args.rounds} finished", flush=True)
    allocations = run_probe(executable, allocations=True)
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    metadata = {
        "baseline": baseline, "head": capture("git", "rev-parse", "HEAD").strip(),
        "current_ref": args.current_ref or "working-tree",
        "platform": platform.platform(), "rounds": args.rounds,
        "pinned_cpu": cpu,
        "compiler": capture(cache["CMAKE_CXX_COMPILER"], "--version").splitlines()[0],
        "release_flags": cache.get("CMAKE_CXX_FLAGS_RELEASE", ""),
        "common_flags": cache.get("CMAKE_CXX_FLAGS", ""),
        "measurement_order": "Both versions warmed, ABBA within each round, reversed construction order every other round",
        "qt_enable_regexp_jit": os.environ.get("QT_ENABLE_REGEXP_JIT", "Qt default"),
        "cmake": capture("cmake", "--version").splitlines()[0],
        "cpu": capture("lscpu"),
        "header_sha256": {name: hashlib.sha256(source).hexdigest() for name, source in sources.items()},
        "scope": "Hot in-memory Qt matching; timing without allocation hooks; separate glibc allocation requests, not peak memory",
    }
    (output / "raw.json").write_text(json.dumps({
        "metadata": metadata, "timing": measurements, "allocations": allocations,
    }, indent=2) + "\n")

    summary = []
    allocation_map = {(row["version"], row["corpus"], row["patterns"]): row for row in allocations}
    for corpus, count in sorted({(row["corpus"], row["patterns"]) for row in measurements}):
        rows = [row for row in measurements + allocations
                if row["corpus"] == corpus and row["patterns"] == count]
        for key in ("input_sha256", "result_sha256", "checksum_per_pass", "qt"):
            if len({row[key] for row in rows}) != 1:
                raise RuntimeError(f"Inconsistent {key}: {corpus}/{count}")
        result = {"corpus": corpus, "patterns": count}
        for name in sources:
            samples = [row["ns_per_line"] for row in measurements
                       if row["version"] == name and row["corpus"] == corpus and row["patterns"] == count]
            allocation = allocation_map[name, corpus, count]
            result[name] = {
                "ns_per_line_median": statistics.median(samples),
                "ns_per_line_min": min(samples), "ns_per_line_max": max(samples),
                "allocations_per_line": allocation["allocations_per_line"],
                "requested_bytes_per_line": allocation["requested_bytes_per_line"],
            }
        result["speedup"] = result["baseline"]["ns_per_line_median"] / result["current"]["ns_per_line_median"]
        summary.append(result)
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for row in summary:
        old, new = row["baseline"], row["current"]
        print(f'{row["corpus"]:16} patterns={row["patterns"]:2} '
              f'{old["ns_per_line_median"]:9.1f} -> {new["ns_per_line_median"]:9.1f} ns/line '
              f'({row["speedup"]:.3f}x); allocations '
              f'{old["allocations_per_line"]:.2f} -> {new["allocations_per_line"]:.2f}')
    if args.require_savings:
        failures = [row for row in summary if row["patterns"] > 1 and row["corpus"] != "empty"
                    and row["current"]["allocations_per_line"] >= row["baseline"]["allocations_per_line"]]
        if failures:
            raise SystemExit("FAIL: multi-pattern allocation counts did not decrease")
        if any(row["patterns"] <= 1 and row["current"]["allocations_per_line"]
               != row["baseline"]["allocations_per_line"] for row in summary):
            raise SystemExit("FAIL: zero/single-pattern allocation counts changed")


if __name__ == "__main__":
    main()
