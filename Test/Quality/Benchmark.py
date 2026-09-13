"""Compare the saved pre-optimization compiler and memory sources with the current build."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True, help="Directory containing KrtC and src/Core/Memory snapshots")
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument("--cpu", type=int, default=0)
    args = parser.parse_args()
    if args.rounds < 1: parser.error("rounds must be positive")
    os.sched_setaffinity(0, {args.cpu})
    baseline = args.baseline.resolve()
    compilers = {"before": baseline / "KrtC", "after": ROOT / "Re.KrtC/build/KrtC"}
    report = {"date_utc": datetime.now(timezone.utc).isoformat(), "rounds": args.rounds, "cpu": args.cpu,
              "platform": platform.platform(),
              "cpu_model": next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                                 if line.startswith("model name")), "unknown"),
              "policy": "serial alternating order, no warmup or excluded samples; verify every executable result",
              "compiler_sha256": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in compilers.items()},
              "memory_sources_sha256": {
                  name: {file: hashlib.sha256((directory / file).read_bytes()).hexdigest()
                         for file in ("Allocator.c", "Allocator.h", "Arena.c", "Arena.h")}
                  for name, directory in (("before", baseline / "src/Core/Memory"),
                                          ("after", ROOT / "Re.KrtC/src/Core/Memory"))},
              "benchmark_sha256": hashlib.sha256((HERE / "test_memory_benchmark.c").read_bytes()).hexdigest(),
              "component_flags": "cc -std=gnu11 -O2 -pthread", "cases": {}}
    with tempfile.TemporaryDirectory(prefix="kairote-quality-bench-") as temporary:
        directory = Path(temporary)
        memory = {}
        for name in compilers:
            source = ROOT / "Re.KrtC"
            if name == "before":
                source = directory / "before"
                shutil.copytree(ROOT / "Re.KrtC/src/Core", source / "src/Core")
                for path in (baseline / "src/Core/Memory").glob("*"):
                    shutil.copy2(path, source / "src/Core/Memory" / path.name)
            output = directory / (name + "-memory")
            subprocess.run(["cc", "-std=gnu11", "-O2", "-pthread", "-I" + str(source / "src"),
                            str(HERE / "test_memory_benchmark.c"), str(source / "src/Core/Memory/Allocator.c"),
                            str(source / "src/Core/Memory/Arena.c"), "-o", str(output)], check=True, timeout=60)
            memory[name] = output
        cases = ["free", "arena_reset", "compile_200_functions", "compile_1000_functions"]
        for case in cases:
            samples = {name: [] for name in compilers}
            source = directory / "Program.krt"
            if case.startswith("compile_"):
                count = int(case.split("_")[1])
                source.write_text("\n".join(f"int32 Function{i}(int32 n) {{ return n + {i}; }}" for i in range(count)) +
                                  f"\nint32 main() {{ return Function{count-1}(2) == {count+1} ? 0 : 1; }}\n")
            for index in range(args.rounds):
                for name in (["before", "after"] if index % 2 == 0 else ["after", "before"]):
                    if case.startswith("compile_"):
                        binary = directory / ("program-" + name)
                        start = time.perf_counter_ns()
                        result = subprocess.run([str(compilers[name]), str(source), "output", str(binary)], cwd=directory,
                                                capture_output=True, timeout=120)
                        duration = (time.perf_counter_ns() - start) / 1e6
                        if result.returncode: raise RuntimeError(result.stdout + result.stderr)
                        subprocess.run([str(binary)], check=True, timeout=10)
                    else:
                        duration = float(subprocess.check_output([str(memory[name]), case], text=True, timeout=120))
                    samples[name].append(duration)
                print(case, index + 1, {name: values[-1] for name, values in samples.items()}, flush=True)
            report["cases"][case] = {name: {"samples_ms": values, "mean_ms": statistics.mean(values),
                                                    "min_ms": min(values), "max_ms": max(values)} for name, values in samples.items()}
            (HERE / "PerformanceResults.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
