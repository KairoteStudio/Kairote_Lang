"""Compare saved pre-optimization KrtC, current KrtC and equivalent GCC Fibonacci."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import statistics

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
COMPARISON = HERE.parent / "CodegenComparison"


def helper(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


NATIVE = helper("NativeFibComparison", COMPARISON / "Run.py")
PATHS = helper("NativeFibPaths", COMPARISON / "Analyze.py")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def path_counts(details, loop):
    result = {"code_bytes": details["code_bytes"], "static_instructions": details["static_instructions"],
              "entry_mod_16": details["start_address"] % 16,
              "entry_mod_64": details["start_address"] % 64,
              "recursive_call_sites": details["recursive_call_sites"]}
    for n, key in ((0, "leaf"), (2, "nonleaf_n2")):
        counts, children, stack = PATHS.local_path(details, n, loop)
        result[key] = {**counts, "recursive_calls": len(children), "additional_stack_bytes": stack}
    fibonacci = [0, 1]
    for _ in range(40):
        fibonacci.append(fibonacci[-1] + fibonacci[-2])
    totals = []
    for n in range(41):
        counts, children, _ = PATHS.local_path(details, n, loop)
        counts["function_entries"] = 1
        for child in children:
            counts.update(totals[child])
        expected = fibonacci[n + 1] if loop else 2 * fibonacci[n + 1] - 1
        if counts["function_entries"] != expected:
            raise RuntimeError("Unexpected recursive call count")
        totals.append(Counter(counts))
        if n in (35, 40):
            result[f"fib{n}"] = dict(counts)
    return result


def prepare(args):
    artifacts = HERE / "Artifacts"
    artifacts.mkdir(exist_ok=True)
    compilers = {"KrtBefore": args.baseline.resolve(), "KrtAfter": args.compiler.resolve()}
    gcc = os.environ.get("CC", "gcc")
    flags = ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fwrapv", "-fno-lto", "-fno-pie"]
    variants = {"GccRecursive": ["-fno-optimize-sibling-calls"], "GccO2": []}
    report = {"date_utc": datetime.now(timezone.utc).isoformat(), "platform": platform.platform(),
              "cpu_model": next(line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                                if line.startswith("model name")),
              "compilers": {name: {"path": str(path), "sha256": digest(path)} for name, path in compilers.items()},
              "gcc": NATIVE.checked([gcc, "--version"]).splitlines()[0], "flags": flags,
              "variant_extra_flags": variants, "commands": [], "binaries": {}, "machine_code": {},
              "operations": {}, "cases": {}, "source_sha256": {}, "completed": False,
              "backend_source_sha256": {str(path.relative_to(ROOT)): digest(path)
                                        for path in sorted((ROOT / "Re.KrtC/src/compiler/Backend/Kro").iterdir())
                                        if path.suffix in (".c", ".h", ".inc")},
              "clock": "CLOCK_MONOTONIC via syscall on both sides",
              "timing_policy": "One fib(n), excluding compilation/startup/output; one CPU, serial rotating order; every sample retained",
              "operation_method": "Offline accounting of exact retained disassembly, not hardware performance counters; stack excludes incoming return address"}

    def build(command):
        report["commands"].append(list(map(str, command)))
        return NATIVE.checked(command, cwd=artifacts)

    for name, compiler in compilers.items():
        for n in (35, 40):
            stem = artifacts / f"{name}{n}"
            program, binary = stem.with_suffix(".krt"), stem.with_suffix(".elf")
            shutil.copy2(HERE.parent / "Runtime" / f"test_fib{n}.krt", program)
            report["source_sha256"][str(program.relative_to(ROOT))] = digest(program)
            build([compiler, program, "output", binary])
            details = NATIVE.save_disassembly(binary, NATIVE.FIB_SYMBOL, stem)
            details["kro_link_check"] = NATIVE.check_kro(program.with_suffix(".kro"), binary, details, artifacts)
            report["machine_code"][name + str(n)] = details
            report["binaries"][name + str(n)] = str(binary.relative_to(ROOT))
    source = HERE.parent / "NativeFib/Fib.c"
    driver = COMPARISON / "Driver.c"
    for path in (source, driver):
        report["source_sha256"][str(path.relative_to(ROOT))] = digest(path)
    for name, extra in variants.items():
        assembly, obj = artifacts / (name + ".s"), artifacts / (name + ".o")
        build([gcc, *flags, *extra, "-DFIB_NAME=Fib", "-S", "-masm=intel", "-fverbose-asm", source, "-o", assembly])
        build([gcc, "-c", assembly, "-o", obj])
        for n in (35, 40):
            stem = artifacts / f"{name}{n}"
            binary = stem.with_suffix(".elf")
            build([gcc, *flags, "-no-pie", f"-DFIB_INPUT={n}", driver, obj, "-o", binary])
            report["machine_code"][name + str(n)] = NATIVE.save_disassembly(binary, "Fib", stem)
            report["binaries"][name + str(n)] = str(binary.relative_to(ROOT))
        for n, expected in ((-100, -100), (0, 0), (1, 1), (2, 1), (10, 55)):
            NATIVE.run(ROOT / report["binaries"][name + "35"], expected, n)
    for name in (*compilers, *variants):
        before = report["machine_code"][name + "35"]
        after = report["machine_code"][name + "40"]
        if [row["bytes"] for row in before["instructions"]] != [row["bytes"] for row in after["instructions"]]:
            raise RuntimeError(f"fib35/40 bodies differ for {name}")
        if before["recursive_call_sites"] != (1 if name == "GccO2" else 2):
            raise RuntimeError("Unexpected recursion shape")
        report["operations"][name] = path_counts(before, name == "GccO2")
    report["artifact_sha256"] = {str(path.relative_to(ROOT)): digest(path) for path in sorted(artifacts.iterdir()) if path.is_file()}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=ROOT / "Re.KrtC/build/CodegenBaseline/KrtC")
    parser.add_argument("--compiler", type=Path, default=Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")))
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument("--cpu", type=int, default=min(os.sched_getaffinity(0)))
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare-only", action="store_true")
    mode.add_argument("--measure-only", action="store_true")
    args = parser.parse_args()
    if args.rounds < 1 or args.cpu not in os.sched_getaffinity(0):
        parser.error("Positive rounds and an available logical CPU are required")
    output = HERE / "Results.json"
    report = json.loads(output.read_text()) if args.measure_only else prepare(args)
    output.write_text(json.dumps(report, indent=2) + "\n")
    if args.prepare_only:
        print("Prepared executables, assembly, relocation checks and instruction counts.", flush=True)
        return
    for path, expected in report["artifact_sha256"].items():
        if digest(ROOT / path) != expected:
            raise RuntimeError(f"Artifact changed since preparation: {path}")
    for compiler in report["compilers"].values():
        if digest(compiler["path"]) != compiler["sha256"]:
            raise RuntimeError("Compiler changed since preparation")
    os.sched_setaffinity(0, {args.cpu})
    report.update(cpu=args.cpu, rounds=args.rounds, completed=False, cases={},
                  measurement_date_utc=datetime.now(timezone.utc).isoformat())
    names = ["KrtBefore", "KrtAfter", "GccRecursive", "GccO2"]
    for n, expected in ((35, 9227465), (40, 102334155)):
        samples = {name: [] for name in names}
        for index in range(args.rounds):
            order = names[index % len(names):] + names[:index % len(names)]
            for name in order:
                sample = {"run": index + 1, **NATIVE.run(ROOT / report["binaries"][name + str(n)], expected)}
                samples[name].append(sample)
                print(f"fib{n} {name} {index+1}/{args.rounds}: {sample['duration_ns']/1e6:.6f} ms", flush=True)
        report["cases"][f"fib{n}"] = {}
        for name, rows in samples.items():
            times = [row["duration_ns"] / 1e6 for row in rows]
            report["cases"][f"fib{n}"][name] = {"samples": rows, "mean_ms": statistics.mean(times),
                "median_ms": statistics.median(times), "min_ms": min(times), "max_ms": max(times),
                "stdev_ms": statistics.stdev(times) if len(times) > 1 else 0}
        output.write_text(json.dumps(report, indent=2) + "\n")
    report["completed"] = True
    output.write_text(json.dumps(report, indent=2) + "\n")
    print("Saved", output, flush=True)


if __name__ == "__main__":
    main()
