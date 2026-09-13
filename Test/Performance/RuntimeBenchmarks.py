"""Run native Kairote workloads ten times and report arithmetic mean durations."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import struct
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()
CASES = {
    "empty_loop": {"expected": 100000, "iterations": 100000},
    "output": {"expected": 100000, "writes": 100000, "stdout_bytes": 488890},
    "file_io": {"expected": 819200000, "writes": 100000, "reads": 100000,
                "block_bytes": 4096, "file_bytes": 409600000, "fsync": True,
                "read_cache": "POSIX_FADV_DONTNEED after fsync; advisory, no global cache drop"},
    "fib35": {"expected": 9227465, "n": 35, "algorithm": "naive recursive, no memoization"},
    "fib40": {"expected": 102334155, "n": 40, "algorithm": "naive recursive, no memoization"},
}


def checked(command, **kwargs):
    result = subprocess.run([str(arg) for arg in command], capture_output=True,
                            text=True, timeout=60, **kwargs)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def verify_machine_code(name, assembly):
    if name == "empty_loop":
        jumps = re.findall(r"^\s*([0-9a-f]+):.*\b(j[a-z]+)\s+([0-9a-f]+)\s", assembly, re.M)
        if not any(int(target, 16) < int(address, 16) for address, _, target in jumps):
            raise RuntimeError("Empty loop has no backward jump in native code")
    if name.startswith("fib"):
        calls = re.findall(r"\bcall\s+[0-9a-f]+\s+<[^>]*Fib[^>]*>", assembly)
        if len(calls) < 3:
            raise RuntimeError("Fibonacci did not retain its recursive calls")


def verify_output(name, directory, stdout_path):
    if name == "output":
        expected = "".join(str(i) for i in range(100000)).encode("ascii")
        actual = stdout_path.read_bytes()
        if actual != expected:
            raise RuntimeError(f"Output mismatch: {len(actual)} bytes, expected {len(expected)}")
    elif stdout_path.stat().st_size:
        raise RuntimeError(f"Unexpected stdout in {name}")
    if name == "file_io":
        payload = directory / "payload.bin"
        expected_block = bytes(i % 251 for i in range(4096))
        if payload.stat().st_size != CASES[name]["file_bytes"]:
            raise RuntimeError("Wrong I/O file size")
        with payload.open("rb") as file:
            if file.read(4096) != expected_block:
                raise RuntimeError("Wrong first I/O block")
            file.seek(-4096, os.SEEK_END)
            if file.read(4096) != expected_block:
                raise RuntimeError("Wrong last I/O block")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=240)
    parser.add_argument("--only", nargs="+", choices=CASES)
    parser.add_argument("--output", type=Path, default=HERE / "RuntimeResults.json")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("These fixtures use Linux x86-64 syscalls")
    selected = args.only or list(CASES)
    cpu = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                if line.startswith("model name")), "unknown")
    report = {
        "date_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "cpu": cpu,
        "compiler": str(COMPILER.relative_to(ROOT)) if COMPILER.is_relative_to(ROOT) else str(COMPILER),
        "compiler_sha256": hashlib.sha256(COMPILER.read_bytes()).hexdigest(),
        "rounds": args.rounds,
        "clock": "CLOCK_MONOTONIC via syscall, nanoseconds",
        "timed_region": "workload and in-loop checks; excludes compilation, process startup, auxiliary buffer setup and post-run validation",
        "stdout": "regular file; Console.Write(i++), no labels, spaces or newlines",
        "run_policy": "serial; no warmup; no discarded samples",
        "filesystem": checked(["df", "-T", HERE]).strip(),
        "cases": {},
    }
    with tempfile.TemporaryDirectory(prefix=".Runtime-", dir=HERE) as temporary:
        directory = Path(temporary)
        shutil.copytree(ROOT / "libs", directory / "libs", ignore=shutil.ignore_patterns("*.kro"))
        binaries = {}
        for name in selected:
            code, binary = directory / ("test_" + name + ".krt"), directory / name
            shutil.copyfile(HERE / "Runtime" / code.name, code)
            checked([COMPILER, code, "output", binary], cwd=directory)
            verify_machine_code(name, checked(["objdump", "-d", "-Mintel", binary]))
            binaries[name] = binary
        print(f"Compiled {len(binaries)} workloads; {args.rounds} serial runs each.", flush=True)
        for name in selected:
            samples = []
            for index in range(args.rounds):
                stdout_path = directory / "stdout.bin"
                with stdout_path.open("wb") as stdout:
                    wall_start = time.perf_counter_ns()
                    result = subprocess.run([str(binaries[name])], cwd=directory, stdout=stdout,
                                            stderr=subprocess.PIPE, timeout=args.timeout)
                    process_ns = time.perf_counter_ns() - wall_start
                if result.returncode or len(result.stderr) != 16:
                    raise RuntimeError(f"{name} run {index + 1}: exit={result.returncode}, stderr={result.stderr!r}")
                duration_ns, value = struct.unpack("<qq", result.stderr)
                if value != CASES[name]["expected"] or not 0 < duration_ns <= process_ns:
                    raise RuntimeError(f"{name}: invalid timing/result: {duration_ns}, {value}")
                verify_output(name, directory, stdout_path)
                samples.append({"run": index + 1, "duration_ns": duration_ns,
                                "process_wall_ns": process_ns, "result": value})
                print(f"{name} {index + 1:2d}/{args.rounds}: {duration_ns / 1e6:.6f} ms", flush=True)
            durations = [sample["duration_ns"] for sample in samples]
            report["cases"][name] = {
                **CASES[name], "samples": samples,
                "mean_ms": statistics.mean(durations) / 1e6,
                "min_ms": min(durations) / 1e6,
                "max_ms": max(durations) / 1e6,
                "stdev_ms": statistics.stdev(durations) / 1e6 if len(durations) > 1 else 0,
                "mean_process_wall_ms": statistics.mean(sample["process_wall_ns"] for sample in samples) / 1e6,
            }
            args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
            print(f"{name} mean: {report['cases'][name]['mean_ms']:.6f} ms", flush=True)
    print(f"Saved results: {args.output}", flush=True)


if __name__ == "__main__":
    main()
