"""Recheck the retained budget-selection binaries without replacing the original report."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct
import subprocess

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--cpu", type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument("--output", type=Path, default=HERE / "Rerun.json")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    original = json.loads((HERE / "Results.json").read_text())
    os.sched_setaffinity(0, {args.cpu})
    for case in original["workloads"].values():
        for version in case["variants"].values():
            binary = HERE / version["binary"]
            if hashlib.sha256(binary.read_bytes()).hexdigest() != version["binary_sha256"]:
                raise RuntimeError(f"Recorded binary changed: {binary}")
    report = {"created_utc": datetime.now(timezone.utc).isoformat(), "cpu": args.cpu,
              "rounds": args.rounds, "results": {}}
    for name, case in original["workloads"].items():
        samples = {key: [] for key in case["variants"]}
        for run in range(args.rounds):
            order = list(samples)
            start = run % len(order)
            for key in order[start:] + order[:start]:
                binary = HERE / case["variants"][key]["binary"]
                result = subprocess.run([str(binary)], capture_output=True, timeout=120)
                if result.returncode or result.stdout or len(result.stderr) != 16:
                    raise RuntimeError(f"Invalid result from {name}/{key}: {result.returncode}, {result.stderr!r}")
                duration, value = struct.unpack("<qQ", result.stderr)
                if duration < 0 or value != case["expected"]:
                    raise RuntimeError(f"Incorrect output from {name}/{key}: {value}")
                samples[key].append(duration / 1_000_000)
        report["results"][name] = {
            key: {"samples_ms": values, "median_ms": statistics.median(values),
                  "mean_ms": statistics.mean(values)} for key, values in samples.items()
        }
        print(name, {key: round(statistics.median(values), 6) for key, values in samples.items()}, flush=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
