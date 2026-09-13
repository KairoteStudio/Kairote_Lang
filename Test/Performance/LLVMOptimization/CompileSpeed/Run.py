"""Measure fresh compiler processes on identical inputs, including native linking."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import statistics
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    cases = {
        "Empty": ("int32 main() { return 0; }\n", ""),
        "Fib": ("int32 Recur(int32 n) { if (n <= 1) { return n; } return Recur(n-1)+Recur(n-2); }\n"
                "int32 main() { return Recur(20) == 6765 ? 0 : 1; }\n", ""),
        "Hello": ((ROOT/"Test/SyntaxAudit/T00_Hello.krt").read_text(), "Hello, KairoteLang!\n"),
        "SysImport": ("using System.Sys;\nint32 main() { return Sys.InternalStringLength(\"abc\") == 3 ? 0 : 1; }\n", ""),
        "Pointers": ((ROOT/"Test/Pointers/test_unsafe_pointers.krt").read_text(), ""),
    }
    for count in (200,1000):
        body = "\n".join(f"int32 Function{i}(int32 n) {{ return n + {i}; }}" for i in range(count))
        cases[f"Functions{count}"] = (body+f"\nint32 main() {{ return Function{count-1}(2) == {count+1} ? 0 : 1; }}\n", "")
    count = 500
    body = "\n".join(f"int64 Recur{i}(int64 n) {{ if (n <= 0) {{ return {i}; }} return n + Recur{i}(n-1); }}"
                     for i in range(count))
    cases["Recursive500"] = (body+"\nint32 main() { return Recur499(10) == 554 ? 0 : 1; }\n", "")
    branches = "\n".join(
        f"int64 Branch{i}(int64 n) {{ if (n <= 1) {{ return {i+1}; }} "
        f"return Branch{i}(n-1)+Branch{i}(n-2); }}" for i in range(500))
    cases["BranchRecursive500"] = (branches+
        "\nint32 main() { return Branch499(12) == 116500 ? 0 : 1; }\n", "")
    definitions, checks = [], []
    for bits in range(2,129,2):
        for prefix in ("int","uint"):
            kind = prefix+str(bits)
            definitions.append(f"{kind} Mix{kind}({kind} a, {kind} b) {{ return ((a+b)*(a-b)) ^ b; }}")
            checks.append(f"if (Mix{kind}(({kind})1,({kind})1) != ({kind})1) {{ return 1; }}")
    cases["IntegerWidths"] = ("\n".join(definitions)+"\nint32 main() {\n"+"\n".join(checks)+"\nreturn 0; }\n", "")
    value = 7
    for _ in range(500): value = (value*3+1) % (1 << 64)
    cases["Dense500"] = ("uint64 Dense(uint64 x) {\n"+"x = x*3+1;\n"*500+"return x; }\n"
                         f"int32 main() {{ return Dense(7) == (uint64){value} ? 0 : 1; }}\n", "")
    return cases


def summarize(samples, field):
    times = [row[field] for row in samples]
    return {"mean":statistics.mean(times),"median":statistics.median(times),
            "min":min(times),"max":max(times),"stdev":statistics.stdev(times) if len(times)>1 else 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before",type=Path,default=ROOT/"Re.KrtC/build/BeforeLLVMOptimization/KrtC")
    parser.add_argument("--after",type=Path,default=ROOT/"Re.KrtC/build/KrtC")
    parser.add_argument("--before-flag",action="append",default=[])
    parser.add_argument("--after-flag",action="append",default=[])
    parser.add_argument("--rounds",type=int,default=10)
    parser.add_argument("--cpu",type=int,default=min(os.sched_getaffinity(0)))
    parser.add_argument("--output",type=Path,default=HERE/"Results.json")
    args = parser.parse_args()
    if args.rounds < 1 or args.cpu not in os.sched_getaffinity(0): parser.error("Invalid rounds or CPU")
    os.sched_setaffinity(0,{args.cpu})
    compilers = {"Before":args.before.resolve(),"After":args.after.resolve()}
    inputs, work = HERE/"Sources", HERE/"Work"
    inputs.mkdir(exist_ok=True)
    work.mkdir(exist_ok=True)
    # Imports find a private source tree, never modify repository library objects.
    for source in (ROOT/"libs").rglob("*.krt"):
        target = inputs/"libs"/source.relative_to(ROOT/"libs")
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
    programs = sources()
    for name,(source,_) in programs.items(): (inputs/(name+".krt")).write_text(source)
    report = {"date_utc":datetime.now(timezone.utc).isoformat(),"rounds":args.rounds,"cpu":args.cpu,
              "platform":platform.platform(), "compiler_flags":{"Before":args.before_flag,"After":args.after_flag},
              "cpu_model":next(row.split(":",1)[1].strip() for row in Path("/proc/cpuinfo").read_text().splitlines() if row.startswith("model name")),
              "compilers":{name:{"path":str(path),"sha256":digest(path)} for name,path in compilers.items()},
              "source_sha256":{str(path.relative_to(ROOT)):digest(path) for path in sorted(inputs.rglob("*.krt"))},
              "script_sha256":digest(Path(__file__)),"completed":False,"cases":{},
              "policy":"One untimed warmup per case/version; ten fresh processes, alternating order, warm filesystem cache; wall time includes process startup, compilation, linking and output writes; child CPU time also recorded; execution validation is outside timing; all measured samples retained."}
    jobs = [(name,"exe") for name in programs] + [("Functions1000","ir"),("Recursive500","ir"),("Dense500","ir")]
    for name,target in jobs:
        key = name if target == "exe" else name+"IR"
        rows = {version:[] for version in compilers}
        outputs = {}
        failures = {}
        for index in range(args.rounds+1):
            order = ["Before","After"] if index % 2 == 0 else ["After","Before"]
            for version in order:
                folder = work/version
                folder.mkdir(exist_ok=True)
                output = folder/(key+(".elf" if target == "exe" else ".ir"))
                command = [str(compilers[version]),str(inputs/(name+".krt")),"target",target,"output",str(output)]
                command += args.before_flag if version == "Before" else args.after_flag
                before = resource.getrusage(resource.RUSAGE_CHILDREN)
                started = time.perf_counter_ns()
                result = subprocess.run(command,cwd=folder,capture_output=True,timeout=120)
                duration = (time.perf_counter_ns()-started)/1e6
                after = resource.getrusage(resource.RUSAGE_CHILDREN)
                if result.returncode or not output.is_file():
                    failures[version] = {"command":command,"exit_code":result.returncode,
                                         "stdout":result.stdout.decode(),"stderr":result.stderr.decode()}
                    continue
                if index:
                    rows[version].append({"run":index,"wall_ms":duration,
                        "cpu_ms":1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),
                        "exit_code":result.returncode,"output_bytes":output.stat().st_size})
                outputs[version] = output
            if failures: break
            if index: print(key,index,{version:round(rows[version][-1]["wall_ms"],4) for version in order},flush=True)
        if failures:
            report["cases"][key] = {"source_bytes":(inputs/(name+".krt")).stat().st_size,"target":target,
                                    "failures":failures,"error":"Compilation failed; no timing summary"}
            args.output.write_text(json.dumps(report,indent=2)+"\n")
            print(key,"compile failures:",list(failures),flush=True)
            continue
        entries = {}
        for version,samples in rows.items():
            output = outputs[version]
            if target == "exe":
                checked = subprocess.run([str(output)],capture_output=True,timeout=5)
                assert checked.returncode == 0 and checked.stdout.decode() == programs[name][1], (key,version,checked)
            entries[version] = {"samples":samples,"wall_ms":summarize(samples,"wall_ms"),
                "cpu_ms":summarize(samples,"cpu_ms"),"output_sha256":digest(output),"validation":"passed"}
        report["cases"][key] = {"source_bytes":(inputs/(name+".krt")).stat().st_size,"target":target,**entries}
        args.output.write_text(json.dumps(report,indent=2)+"\n")
    report["completed"] = True
    args.output.write_text(json.dumps(report,indent=2)+"\n")
    print("Saved",args.output,flush=True)


if __name__ == "__main__":
    main()
