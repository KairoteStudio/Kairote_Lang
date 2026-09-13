"""Ten-sample cross-program comparison, retaining equivalent sources and all results."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import resource
import statistics
import struct
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MASK = (1 << 64) - 1

FIB = """
int32 Recur(int32 n) {
    if (n <= 1) { return n; }
    return Recur(n-1) + Recur(n-2);
}
uint64 Kernel(uint64 a, uint64 b) { return (uint64)Recur((int32)(a+b)); }
"""
SUM = """
uint64 Sum(uint64 n, uint64 value) {
    if (n == 0) { return value; }
    return Sum(n-1, value+n);
}
uint64 Kernel(uint64 a, uint64 b) {
    uint64 result = 0;
    for (uint64 i=0; i<b; i++) { result += Sum(a+(i&7),0); }
    return result;
}
"""
PRODUCT = """
uint64 Product(uint64 n) {
    if (n <= 1) { return 1; }
    return n * Product(n-1);
}
uint64 Kernel(uint64 a, uint64 b) {
    uint64 result = 0;
    for (uint64 i=0; i<b; i++) { result += Product(a+(i&7)); }
    return result;
}
"""
GCD = """
uint64 Divisor(uint64 a, uint64 b) {
    if (b == 0) { return a; }
    return Divisor(b,a%b);
}
uint64 Kernel(uint64 a, uint64 b) {
    uint64 result = 0;
    for (uint64 i=0; i<b; i++) { result += Divisor(a+i,57885161); }
    return result;
}
"""
BRANCHES = """
int64 Paths(int64 n) {
    if (n <= 0) { return 1; }
    return Paths(n-1) + Paths(n-3);
}
uint64 Kernel(uint64 a, uint64 b) { return (uint64)Paths((int64)(a+b)); }
"""
TREE = """
int64 Tree(int64 n) {
    if (n <= 0) { return 1; }
    return Tree(n-1) + Tree(n-1);
}
uint64 Kernel(uint64 a, uint64 b) { return (uint64)Tree((int64)(a+b)); }
"""
MIX = """
uint64 Kernel(uint64 a, uint64 b) {
    uint32 x = (uint32)a;
    for (uint64 i=0; i<b; i++) { x = x*1664525 + 1013904223; x ^= x >> 13; }
    return (uint64)x;
}
"""
HELPERS = """
uint32 Step(uint32 x) { return x*1664525 + 1013904223; }
uint32 Scatter(uint32 x) { return x ^ (x >> 13); }
uint64 Kernel(uint64 a, uint64 b) {
    uint32 x = (uint32)a;
    for (uint64 i=0; i<b; i++) { x = Scatter(Step(x)); }
    return (uint64)x;
}
"""


def cases():
    counts = {n: 1 for n in (-2, -1, 0)}
    for n in range(1, 43):
        counts[n] = counts[n-1] + counts[n-3]
    mix = 12345
    for _ in range(1000000):
        mix = (mix*1664525 + 1013904223) & 0xffffffff
        mix ^= mix >> 13
    return {
        "Fib35": (FIB, 35, 0, 9227465), "Fib40": (FIB, 40, 0, 102334155),
        "TailSum": (SUM, 10000, 50, sum((10000+(i&7))*(10001+(i&7))//2 for i in range(50))),
        "Product": (PRODUCT, 15, 40000, sum(math.factorial(15+(i&7)) for i in range(40000)) & MASK),
        "Gcd": (GCD, 982451653, 80000, sum(math.gcd(982451653+i,57885161) for i in range(80000))),
        "Branching": (BRANCHES, 42, 0, counts[42]), "BinaryTree": (TREE, 22, 0, 1 << 22),
        "IntegerLoop": (MIX, 12345, 1000000, mix),
        # Equivalent long form also supplies a valid pre-fix loop comparison.
        "ExpandedLoop": (MIX.replace("x ^= x >> 13;", "x = x ^ (x >> 13);"), 12345, 1000000, mix),
        "HelperLoop": (HELPERS, 12345, 1000000, mix),
    }


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def checked(command, **kwargs):
    result = subprocess.run(list(map(str, command)), capture_output=True, timeout=120, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout!r}\n{result.stderr!r}")
    return result.stdout.decode()


def measure(binary, expected):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    result = subprocess.run([str(binary)], capture_output=True, timeout=120)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if result.stdout or len(result.stderr) != 16:
        return {"valid": False, "exit_code": result.returncode,
                "error": "Missing or malformed timing/result report",
                "stdout_hex": result.stdout.hex(), "stderr_hex": result.stderr.hex()}
    duration, value = struct.unpack("<qQ", result.stderr)
    return {"valid": result.returncode == 0 and value == expected and duration >= 0,
            "exit_code": result.returncode, "duration_ns": duration, "result": value,
            "process_cpu_ms": 1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime)}


def prepare(args):
    artifacts = HERE / "Artifacts"
    artifacts.mkdir(exist_ok=True)
    compilers = {"Before": args.baseline.resolve(), "O2": args.compiler.resolve(), "O3": args.compiler.resolve()}
    gcc = os.environ.get("CC", "gcc")
    flags = ["-O3", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fwrapv", "-fno-lto", "-fno-pie"]
    report = {"prepared_utc": datetime.now(timezone.utc).isoformat(), "platform": platform.platform(),
              "cpu_model": next(row.split(":",1)[1].strip() for row in Path("/proc/cpuinfo").read_text().splitlines()
                                if row.startswith("model name")),
              "compilers": {name: {"path": str(path), "sha256": digest(path)} for name,path in compilers.items()},
              "gcc": checked([gcc,"--version"]).splitlines()[0], "gcc_flags": flags, "krtc_flags": {"Before": ["-O3"], "O2": ["-O2"], "O3": ["-O3"]},
              "source_sha256": {}, "commands": [], "workloads": {}, "cases": {}, "completed": False,
              "timing_policy": "One validated warmup per case/variant, then ten measured Kernel invocations, syscall CLOCK_MONOTONIC; excludes startup/compilation/output; serial rotated variants, all ten measured samples retained; an incorrect variant has no timing summary",
              "backend_source_sha256": {str(path.relative_to(ROOT)): digest(path)
                                        for path in sorted((ROOT/"Re.KrtC/src/compiler/Backend/Kro").iterdir())
                                        if path.suffix in (".c", ".h", ".inc")},
              "compiler_source_sha256": {str(path.relative_to(ROOT)): digest(path)
                  for base in (ROOT/"Re.KrtC/src",ROOT/"Re.KrtC/Shared",ROOT/"ArkLink/src",ROOT/"ArkLink/include")
                  for path in sorted(base.rglob("*")) if path.suffix in (".c", ".h", ".inc")}}

    def build(command):
        report["commands"].append(list(map(str, command)))
        return checked(command, cwd=artifacts)

    timing = (HERE.parent/"Runtime/test_fib35.krt").read_text().split("int32 Fib(",1)[0]
    mapping = {"int32": "int32_t", "uint32": "uint32_t", "int64": "int64_t", "uint64": "uint64_t"}
    for name, (source, a, b, expected) in cases().items():
        krt = artifacts/(name+".krt")
        krt.write_text(timing + source + f"""
int32 main() {{
    int64[] stamp = new int64[2];
    int64 started = BenchmarkNow((int64)stamp);
    uint64 result = Kernel({a},{b});
    int64 stopped = BenchmarkNow((int64)stamp);
    int32 reported = BenchmarkFinish(started, stopped, (int64)result);
    if (result != (uint64){expected}) {{ return 1; }}
    return reported;
}}
""")
        c = artifacts/(name+".c")
        c_text = re.sub(r"\b(int32|uint32|int64|uint64)\b", lambda match: mapping[match[1]], source)
        c.write_text("#include <stdint.h>\n" + c_text)
        for path in (krt,c,HERE/"Driver.c",HERE/"Run.py"):
            report["source_sha256"][str(path.relative_to(ROOT))] = digest(path)
        report["workloads"][name] = {"input": [a,b], "expected": expected, "binaries": {}}
        for variant,compiler in compilers.items():
            stem = artifacts/f"{name}{variant}"
            binary = stem.with_suffix(".elf")
            build([compiler, *(["-"+variant] if variant in ("O2", "O3") else ["-O3"]), krt,"output",binary])
            # KrtC writes the object beside its source, so retain each version.
            stem.with_suffix(".kro").write_bytes(krt.with_suffix(".kro").read_bytes())
            stem.with_suffix(".asm").write_text(checked(["objdump","-d","-w","-Mintel",binary]))
            report["workloads"][name]["binaries"][variant] = str(binary.relative_to(ROOT))
        assembly, obj = artifacts/(name+"Gcc.s"), artifacts/(name+"Gcc.o")
        build([gcc,*flags,"-S","-masm=intel","-fverbose-asm",c,"-o",assembly])
        build([gcc,"-c",assembly,"-o",obj])
        binary = artifacts/(name+"Gcc.elf")
        build([gcc,*flags,"-no-pie",f"-DBENCH_A={a}ULL",f"-DBENCH_B={b}ULL",f"-DEXPECTED={expected}ULL",
               HERE/"Driver.c",obj,"-o",binary])
        binary.with_suffix(".asm").write_text(checked(["objdump","-d","-w","-Mintel",binary]))
        report["workloads"][name]["binaries"]["Gcc"] = str(binary.relative_to(ROOT))
    report["artifact_sha256"] = {str(path.relative_to(ROOT)): digest(path) for path in sorted(artifacts.iterdir()) if path.is_file()}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline",type=Path,default=ROOT/"Re.KrtC/build/BeforeLLVMOptimization/KrtC")
    parser.add_argument("--compiler",type=Path,default=ROOT/"Re.KrtC/build/KrtC")
    parser.add_argument("--rounds",type=int,default=10)
    parser.add_argument("--cpu",type=int,default=min(os.sched_getaffinity(0)))
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare-only",action="store_true")
    mode.add_argument("--measure-only",action="store_true")
    args = parser.parse_args()
    if args.rounds < 1 or args.cpu not in os.sched_getaffinity(0):
        parser.error("Positive rounds and an available CPU are required")
    output = HERE/"Results.json"
    report = json.loads(output.read_text()) if args.measure_only else prepare(args)
    output.write_text(json.dumps(report,indent=2)+"\n")
    if args.prepare_only:
        print(f"Prepared {len(report['workloads'])} workloads x 4 variants and retained sources/assembly.",flush=True)
        return
    for path,expected in report["artifact_sha256"].items():
        if digest(ROOT/path) != expected: raise RuntimeError("Artifact changed: "+path)
    for entry in report["compilers"].values():
        if digest(entry["path"]) != entry["sha256"]: raise RuntimeError("Compiler changed")
    os.sched_setaffinity(0,{args.cpu})
    report.update(cpu=args.cpu,rounds=args.rounds,completed=False,cases={},warmups={},
                  measured_utc=datetime.now(timezone.utc).isoformat())
    names = ["Before","O2","O3","Gcc"]
    for name,workload in report["workloads"].items():
        rows = {variant: [] for variant in names}
        report["warmups"][name] = {}
        for variant in names:
            warmup = measure(ROOT/workload["binaries"][variant], workload["expected"])
            report["warmups"][name][variant] = warmup
            if not warmup["valid"]:
                output.write_text(json.dumps(report,indent=2)+"\n")
                raise RuntimeError(f"Invalid warmup {name} {variant}: {warmup}")
        for index in range(args.rounds):
            order = names[index%len(names):] + names[:index%len(names)]
            for variant in order:
                sample = {"run":index+1,**measure(ROOT/workload["binaries"][variant],workload["expected"])}
                rows[variant].append(sample)
                outcome = f"{sample['duration_ns']/1e6:.6f} ms" if sample["valid"] else f"INVALID {sample}"
                print(f"{name} {variant} {index+1}/{args.rounds}: {outcome}",flush=True)
        report["cases"][name] = {}
        for variant,samples in rows.items():
            if not all(sample["valid"] for sample in samples):
                report["cases"][name][variant] = {"samples":samples,"valid":False,
                    "error":"Incorrect result or failed execution; timing comparison is invalid"}
                continue
            times = [sample["duration_ns"]/1e6 for sample in samples]
            report["cases"][name][variant] = {"samples":samples,"valid":True,"mean_ms":statistics.mean(times),
                "median_ms":statistics.median(times),"min_ms":min(times),"max_ms":max(times),
                "stdev_ms":statistics.stdev(times) if len(times)>1 else 0}
        output.write_text(json.dumps(report,indent=2)+"\n")
    report["completed"] = True
    report["correctness"] = {variant: all(case[variant]["valid"] for case in report["cases"].values()) for variant in names}
    output.write_text(json.dumps(report,indent=2)+"\n")
    print("Saved",output,flush=True)
    if not all(report["correctness"].values()):
        raise SystemExit("Current compiler or reference failed correctness checks")


if __name__ == "__main__":
    main()
