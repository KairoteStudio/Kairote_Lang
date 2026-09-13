"""Compare native compilers on the same recursive Fibonacci program."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import statistics
import struct
import subprocess
import tempfile

from RuntimeBenchmarks import COMPILER, HERE, verify_machine_code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", action="append", nargs=2, metavar=("NAME", "COMPILER"), default=[])
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument("--n", type=int, choices=(35, 40), nargs="+", default=[35, 40])
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--output", type=Path, default=HERE / "FibComparison.json")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    compilers = {name: Path(path).resolve() for name, path in args.baseline}
    if "current" in compilers:
        parser.error("'current' is reserved for the current compiler")
    compilers["current"] = COMPILER
    report = {
        "date_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "rounds": args.rounds,
        "cpu_affinity": sorted(os.sched_getaffinity(0)),
        "run_policy": "serial, rotating version order, no warmup, no discarded samples",
        "timing": "workload CLOCK_MONOTONIC wall time; additionally record whole-process CPU time",
        "compiler_sha256": {name: hashlib.sha256(path.read_bytes()).hexdigest()
                            for name, path in compilers.items()},
        "cases": {},
    }
    with tempfile.TemporaryDirectory(prefix="kairote-fib-compare-") as temporary:
        directory = Path(temporary)
        for n in args.n:
            code = directory / "Fib.krt"
            code.write_text((HERE / "Runtime" / f"test_fib{n}.krt").read_text())
            runs = {}
            for name, compiler in compilers.items():
                binary = directory / name
                compiled = subprocess.run([str(compiler), str(code), "output", str(binary)],
                                          cwd=directory, capture_output=True, text=True, timeout=60)
                if compiled.returncode:
                    raise RuntimeError(compiled.stdout + compiled.stderr)
                assembly = subprocess.check_output(["objdump", "-d", "-Mintel", str(binary)], text=True)
                verify_machine_code("fib" + str(n), assembly)
                body = assembly.split("<_ZN3FibEi>:", 1)[1].split("<main>:", 1)[0]
                instructions = re.findall(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*[a-z]+", body, re.M)
                runs[name] = {"samples": [], "fib_instruction_count": len(instructions)}
            for index in range(args.rounds):
                order = list(compilers)
                offset = index % len(order)
                for name in order[offset:] + order[:offset]:
                    before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    result = subprocess.run([str(directory / name)], capture_output=True, timeout=240)
                    after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    if result.returncode or len(result.stderr) != 16:
                        raise RuntimeError((name, result.returncode, result.stderr))
                    duration_ns, value = struct.unpack("<qq", result.stderr)
                    if value != (9227465 if n == 35 else 102334155):
                        raise RuntimeError("Wrong Fibonacci result")
                    cpu_ms = ((after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)) * 1000
                    runs[name]["samples"].append({"run": index + 1, "duration_ns": duration_ns,
                                                  "process_cpu_ms": cpu_ms, "result": value})
                    print(f"fib{n} {name} {index+1}/{args.rounds}: {duration_ns/1e6:.6f} ms"
                          f" (CPU {cpu_ms:.3f} ms)", flush=True)
            for entry in runs.values():
                samples = entry["samples"]
                times = [sample["duration_ns"] / 1e6 for sample in samples]
                entry.update(mean_ms=statistics.mean(times), min_ms=min(times), max_ms=max(times),
                             mean_process_cpu_ms=statistics.mean(sample["process_cpu_ms"] for sample in samples))
            report["cases"]["fib" + str(n)] = runs
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps({name: {"mean_ms": value["mean_ms"], "cpu_ms": value["mean_process_cpu_ms"]}
                              for name, value in runs.items()}), flush=True)


if __name__ == "__main__":
    main()
