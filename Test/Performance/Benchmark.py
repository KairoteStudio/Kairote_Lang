"""Measure compiler hotspots and native .text size; emit reproducible JSON."""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
import time

from test_compiler_hotspots import COMPILER, ROOT, build_harness


def run(command, timeout=120, **kwargs):
    result = subprocess.run([str(arg) for arg in command], capture_output=True,
                            text=True, timeout=timeout, **kwargs)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def measure_components(harnesses, directory, rounds):
    samples = {name: {} for name in harnesses}
    for mode in ("preprocessor", "symbols", "cache", "parallel"):
        values = {name: [] for name in harnesses if mode != "cache" or name == "current"}
        for _ in range(rounds):
            for name in values:
                output = run([harnesses[name], "bench_" + mode, directory])
                values[name].append(next(json.loads(line) for line in reversed(output.splitlines())
                                         if line.startswith("{")))
        for name, runs in values.items():
            samples[name][mode] = {
                key: statistics.median(value[key] for value in runs)
                for key in runs[0]
            }
    return samples


def measure_native(compilers, directory, rounds):
    fixtures = {
        "empty_main": ("int32 main() { return 0; }", ""),
        "hello": ((ROOT / "Test/SyntaxAudit/T00_Hello.krt").read_text(), "Hello, KairoteLang!\n"),
        "expression": ("""
int64 Sum(int64 a, int64 b) { return (a+b)*(a-b)+(a^b); }
int32 main() { return Sum(9,4) == 78 ? 0 : 1; }
""", ""),
    }
    measurements = {name: {} for name in compilers}
    for name, (source, expected) in fixtures.items():
        code = directory / (name + ".krt")
        code.write_text(source)
        durations = {version: [] for version in compilers}
        for _ in range(rounds):
            for version, compiler in compilers.items():
                binary = directory / (name + "-" + version)
                start = time.perf_counter()
                run([compiler, code, "output", binary], cwd=directory)
                durations[version].append(time.perf_counter() - start)
        for version in compilers:
            binary = directory / (name + "-" + version)
            output = run([binary], cwd=directory, timeout=5)
            if output != expected:
                raise RuntimeError(f"{name}: expected {expected!r}, got {output!r}")
            sections = run(["objdump", "-h", binary])
            assembly = run(["objdump", "-d", "-Mintel", binary])
            code_sections = re.findall(r"^\s+\d+\s+\S+\s+([0-9a-fA-F]+)[^\n]*\n\s+([^\n]+)", sections, re.M)
            size = sum(int(value, 16) for value, flags in code_sections if "CODE" in flags)
            measurements[version][name] = {
                "code_bytes": size,
                "primary_text_bytes": int(re.search(r"\s+\.text\s+([0-9a-fA-F]+)\s", sections)[1], 16),
                "compile_seconds": statistics.median(durations[version]),
                "frame_accesses": len(re.findall(r"\[rbp-", assembly)),
                "epilogues": len(re.findall(r"\bpop\s+rbp\b", assembly)),
                "syscalls": len(re.findall(r"\bsyscall\b", assembly)),
            }
    return measurements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--baseline-ref", help="Git ref for the three original hotspot C files")
    parser.add_argument("--baseline-compiler", type=Path, help="Saved compiler executable from before changes")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    report = {"rounds": args.rounds}
    with tempfile.TemporaryDirectory(prefix="kairote-benchmark-") as temporary:
        directory = Path(temporary)
        (directory / "libs").symlink_to(ROOT / "libs", target_is_directory=True)
        library = directory / "library"
        library.mkdir()
        current = directory / "current"
        build_harness(current)
        harnesses = {"current": current}
        if args.baseline_ref:
            replacements = {}
            for relative in ("Driver/Preprocessor.c", "Frontend/Semantic/SymbolTable.c",
                             "Driver/ParallelCompiler.c"):
                source = "Re.KrtC/src/compiler/" + relative
                saved = directory / Path(relative).name
                saved.write_text(run(["git", "show", args.baseline_ref + ":" + source], cwd=ROOT))
                replacements[relative] = saved
            baseline = directory / "baseline"
            build_harness(baseline, replacements)
            harnesses["baseline"] = baseline
            report["baseline_ref"] = run(["git", "rev-parse", args.baseline_ref], cwd=ROOT).strip()
        for version, values in measure_components(harnesses, library, args.rounds).items():
            report[version + "_components"] = values
        compilers = {"current": COMPILER}
        if args.baseline_compiler:
            compilers["baseline"] = args.baseline_compiler.resolve()
        for version, values in measure_native(compilers, directory, args.rounds).items():
            report[version + "_native"] = values
    encoded = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")


if __name__ == "__main__":
    main()
