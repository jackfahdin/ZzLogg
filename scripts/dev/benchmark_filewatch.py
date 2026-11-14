#!/usr/bin/env python3
"""Run the explicit filewatch probe in ABBA order; measurements are not replacement gates."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("out/benchmarks/p9-filewatch/results"))
    parser.add_argument("--rounds", type=int, default=1)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    executable = args.binary.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for iteration in range(args.rounds):
        for sample, backend in enumerate(("efsw", "qt", "qt", "efsw")):
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM="offscreen", ZZLOGG_WATCH_BACKEND=backend)
            result = subprocess.run([str(executable), "metrics"], env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
            (args.output / f"{iteration}-{sample}-{backend}.log").write_text(result.stdout)
            idle = re.findall(r"WATCH_IDLE (\w+) files=128 cpu_ms= ([0-9.eE+-]+) wall_ms= (\d+)",
                              result.stdout)
            if {row[0] for row in idle} != {"native", "poll", "hybrid"}:
                raise RuntimeError("Missing measurement modes")
            for mode, cpu, wall in idle:
                stream = re.search(r"WATCH_STREAM " + mode
                                   + r" writes= (\d+) notifications= (\d+) final_delay_ms= (\d+)",
                                   result.stdout)
                if stream is None or float(cpu) < 0:
                    raise RuntimeError("Incomplete measurement or process CPU clock unavailable")
                rows.append(dict(backend=backend, round=iteration, sample=sample, mode=mode,
                                 cpu_ms=float(cpu), wall_ms=int(wall), writes=int(stream[1]),
                                 notifications=int(stream[2]), final_delay_ms=int(stream[3])))
            print(f"Completed round {iteration + 1}, sample {sample + 1}: {backend}", flush=True)
    (args.output / "results.json").write_text(json.dumps({
        "platform": platform.platform(),
        "binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "test_root": os.environ.get("ZZLOGG_WATCH_TEST_ROOT", "system temporary directory"),
        "samples": rows,
    }, indent=2) + "\n")
    for backend in ("efsw", "qt"):
        for mode in ("native", "poll", "hybrid"):
            selected = [row for row in rows if row["backend"] == backend and row["mode"] == mode]
            print(backend, mode, {key: statistics.median(row[key] for row in selected)
                                  for key in ("cpu_ms", "notifications", "final_delay_ms")})


if __name__ == "__main__":
    main()
