#!/usr/bin/env python3
"""Compare explicitly built old/new boolean probes, with timing separate from allocation tracing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-binary", type=Path, required=True)
    parser.add_argument("--current-binary", type=Path, required=True)
    parser.add_argument("--allocation-probe", type=Path)
    parser.add_argument("--output", type=Path, default=Path("out/benchmarks/p6-boolean/results"))
    parser.add_argument("--rounds", type=int, default=8)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    binaries = {"baseline": args.baseline_binary.resolve(), "current": args.current_binary.resolve()}
    args.output.mkdir(parents=True, exist_ok=True)
    cpu = min(os.sched_getaffinity(0))
    os.sched_setaffinity(0, {cpu})
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    samples = []
    for repetition in range(args.rounds):
        order = ["baseline", "current", "current", "baseline"]
        if repetition % 2:
            order = ["current" if version == "baseline" else "baseline" for version in order]
        for sample, version in enumerate(order):
            target = args.output / f"{repetition}-{sample}-{version}.json"
            subprocess.run([str(binaries[version]), "--benchmark", str(target.resolve())], env=env, check=True)
            samples.extend({"version": version, "round": repetition, **row} for row in json.loads(target.read_text()))
        print(f"Round {repetition + 1}/{args.rounds} finished", flush=True)
    allocations = []
    if args.allocation_probe:
        env["LD_PRELOAD"] = str(args.allocation_probe.resolve())
        for version, binary in binaries.items():
            target = args.output / f"{version}-allocations.json"
            subprocess.run([str(binary), "--benchmark", str(target.resolve())], env=env, check=True)
            rows = json.loads(target.read_text())
            if any(row.get("evaluation_allocations") != 0 for row in rows):
                raise RuntimeError(f"{version}: evaluation allocated or probe was not loaded")
            allocations.extend({"version": version, **row} for row in rows)
    summary = []
    for count, operator in sorted({(row["patterns"], row["operator"]) for row in samples}):
        selected = [row for row in samples if row["patterns"] == count and row["operator"] == operator]
        if len({row["checksum"] for row in selected}) != 1:
            raise RuntimeError(f"Result mismatch: {count}/{operator}")
        row = {"patterns": count, "operator": operator}
        for version in binaries:
            measurements = [sample for sample in selected if sample["version"] == version]
            row[version] = {metric: statistics.median(sample[metric] for sample in measurements)
                            for metric in ("construct_ns", "evaluate_ns")}
        summary.append(row)
        print(f'{count:2} {operator:3}: construct {row["baseline"]["construct_ns"]:.0f} -> '
              f'{row["current"]["construct_ns"]:.0f} ns; evaluate '
              f'{row["baseline"]["evaluate_ns"]:.2f} -> {row["current"]["evaluate_ns"]:.2f} ns')
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (args.output / "raw.json").write_text(json.dumps({
        "cpu": cpu, "rounds": args.rounds,
        "binary_sha256": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in binaries.items()},
        "samples": samples, "allocations": allocations,
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
