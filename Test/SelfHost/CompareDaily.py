"""Reproducible daily-use comparison; expected behavior is independent of either compiler.

Failures are evidence, not an assertion that Re.KrtC defines the language. Exit 0
means the comparison completed, not that all probes passed. Use --fail-on-wrong
for a gate against silent wrong code, crashes and acceptance of invalid programs.
Compile times include the public CLI (Python wrapper for SelfHost) and linking;
run times include process startup. These microbenchmarks are not broad claims.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
SYSCALL = "public static extern int64 syscall(int64 n,int64 a,int64 b,int64 c,int64 d,int64 e,int64 f);\n"


def case(name, category, source, expected=0, stdout="", invalid=False):
    return dict(name=name, category=category, sources=[source] if isinstance(source, str) else source,
                expected_exit=expected, expected_stdout=stdout, invalid=invalid)


CASES = [
    case("hello_syscall", "io", SYSCALL + 'int32 main(){ syscall(1,1,(int64)"hello\\n",6,0,0,0); return 0; }', stdout="hello\n"),
    case("hello_intrinsic", "io", 'int32 main(){ syscall(1,1,(int64)"hello\\n",6,0,0,0); return 0; }', stdout="hello\n"),
    case("hello_library", "imports", 'using System; int32 main(){ Console.WriteLine("hello"); return 0; }', stdout="hello\n"),
    case("hello_library_exact", "imports", 'using System.Console; int32 main(){ Console.WriteLine("hello"); return 0; }', stdout="hello\n"),
    case("library_numeric_overload", "imports", 'using System.Console; int32 main(){ Console.WriteLine(17); return 0; }', stdout="17\n"),
    case("hex_literal", "literals", 'int32 main(){ return 0x2a; }', 42),
    case("char_literal", "literals", "int32 main(){ char c='A'; if(c==65){ return 0; } return 1; }"),
    case("char_escape", "literals", "int32 main(){ char c='\\n'; if(c==10){ return 0; } return 1; }"),
    case("int8_signed", "numeric_width", 'int32 main(){ int8 x=-7; if(x<0 && x==-7){ return 0; } return 1; }'),
    case("int16_signed", "numeric_width", 'int32 main(){ int16 x=-30000; if(x<0 && x==-30000){ return 0; } return 1; }'),
    case("uint32_range", "numeric_width", 'int32 main(){ uint32 x=4000000000; if(x>0 && x==4000000000){ return 0; } return 1; }'),
    case("int32_overflow", "numeric_width", 'int32 main(){ int32 x=2147483647; x=x+1; if(x<0){ return 0; } return 1; }'),
    case("cast_int8", "numeric_width", 'int32 main(){ int32 x=255; int8 y=(int8)x; if(y==-1){ return 0; } return 1; }'),
    case("cast_uint32", "numeric_width", 'int32 main(){ int64 x=-1; uint32 y=(uint32)x; if(y==4294967295){ return 0; } return 1; }'),
    case("uint64_high_compare", "numeric_width", 'int32 main(){ uint64 x=(uint64)-1; if(x>0 && x>9223372036854775807){ return 0; } return 1; }'),
    case("uint64_high_division", "numeric_width", 'int32 main(){ uint64 x=(uint64)-1; if(x/3==6148914691236517205 && x%3==0){ return 0; } return 1; }'),
    case("uint64_high_shift", "numeric_width", 'int32 main(){ uint64 x=(uint64)-1; if(x>>63==1){ return 0; } return 1; }'),
    case("uint64_max_literal", "numeric_width", 'int32 main(){ uint64 x=18446744073709551615; if(x==(uint64)-1){ return 0; } return 1; }'),
    case("float_arithmetic", "numeric_width", 'int32 main(){ float64 x=1.5; float64 y=2.25; if(x+y==3.75){ return 0; } return 1; }'),
    case("string_length_property", "strings", 'int32 main(){ string x="abc"; return x.Length; }', 3),
    case("string_length_method", "strings", 'int32 main(){ string x="abc"; return x.Length(); }', 3),
    case("string_equality", "strings", 'int32 main(){ string x="same"; string y="same"; if(x==y){ return 0; } return 1; }'),
    case("string_concat", "strings", 'int32 main(){ string x="ab"; string y="cd"; string z=x+y; if(z=="abcd"){ return 0; } return 1; }'),
    case("string_subtract_invalid", "diagnostics", 'int32 main(){ string x="abc"; return (int32)(x-1); }', invalid=True),
    case("string_multiply_invalid", "diagnostics", 'int32 main(){ string x="abc"; return (int32)(x*2); }', invalid=True),
    case("array_new", "arrays", 'int32 main(){ int32[] x=new int32[3]; x[0]=3; x[1]=5; x[2]=7; return x[0]+x[1]+x[2]; }', 15),
    case("array_length_property", "arrays", 'int32 main(){ int32[] x=new int32[3]; return x.Length; }', 3),
    case("array_length_method", "arrays", 'int32 main(){ int32[] x=new int32[3]; return x.Length(); }', 3),
    case("array_length_assignment_invalid", "diagnostics", 'int32 main(){ int32[] x=new int32[3]; x.Length=4; return 0; }', invalid=True),
    case("array_length_reference_invalid", "diagnostics", 'void set(ref int32 n){ n=4; } int32 main(){ int32[] x=new int32[3]; set(ref x.Length); return 0; }', invalid=True),
    case("array_length_postfix_invalid", "diagnostics", 'int32 main(){ int32[] x=new int32[3]; x.Length++; return 0; }', invalid=True),
    case("array_literal", "arrays", 'int32 main(){ int32[] x={3,5,7}; return x[0]+x[1]+x[2]; }', 15),
    case("array_foreach", "arrays", 'int32 main(){ int32[] x=new int32[3]; x[0]=3; x[1]=5; x[2]=7; int32 n=0; foreach(int32 v in x){ n+=v; } return n; }', 15),
    case("object_fields", "objects", 'class Box{ public int32 value; } int32 main(){ Box b=new Box(); b.value=17; return b.value; }', 17),
    case("empty_class_allocation", "objects", 'class Empty{} int32 main(){ Empty a=new Empty(); Empty b=new Empty(); if(a==null || b==null || a==b){ return 1; } return 0; }'),
    case("returned_object", "objects", 'class Box{ public int32 value; } Box make(int32 n){ Box b=new Box(); b.value=n; return b; } int32 main(){ return make(17).value; }', 17),
    case("instance_method", "objects", 'class Box{ int32 value; public void Set(int32 n){ value=n; } public int32 Get(){ return value; } } int32 main(){ Box b=new Box(); b.Set(17); return b.Get(); }', 17),
    case("constructor", "objects", 'class Box{ int32 value; function Box(int32 n){ value=n; } public int32 Get(){ return value; } } int32 main(){ Box b=new Box(17); return b.Get(); }', 17),
    case("static_method", "objects", 'static class MathTest{ public static int32 Add(int32 a,int32 b){ return a+b; } } int32 main(){ return MathTest.Add(12,5); }', 17),
    case("static_overload", "objects", 'static class MathTest{ public static int32 Add(int32 a){ return a+1; } public static int32 Add(int32 a,int32 b){ return a+b; } } int32 main(){ return MathTest.Add(11)+MathTest.Add(2,3); }', 17),
    case("instance_overload", "objects", 'class Box{ int32 value; public void Set(int32 n){ value=n; } public int32 Get(){ return value; } public int32 Get(int32 extra){ return value+extra; } } int32 main(){ Box b=new Box(); b.Set(6); return b.Get()+b.Get(5); }', 17),
    case("class_constant", "names", 'static class Limits{ public const int32 Answer=17; } int32 main(){ return Limits.Answer; }', 17),
    case("namespace", "names", 'namespace Demo{ class MathTest{ public static int32 Add(int32 a,int32 b){ return a+b; } } } int32 main(){ return Demo.MathTest.Add(12,5); }', 17),
    case("global_constant", "names", 'const int32 answer=17; int32 main(){ return answer; }', 17),
    case("global_variable", "names", 'static int32 count=16; int32 main(){ count+=1; return count; }', 17),
    case("generic", "advanced", 'class Box<T>{ T value; public void Set(T x){ value=x; } public T Get(){ return value; } } int32 main(){ Box<int32> b=new Box<int32>(); b.Set(17); return b.Get(); }', 17),
    case("extern_unresolved", "diagnostics", 'public static extern int32 unavailable(int32 n); int32 main(){ return unavailable(1); }', invalid=True),
    case("missing_return", "diagnostics", 'int32 main(){ int32 n=1; }', invalid=True),
    case("missing_branch_return", "diagnostics", 'int32 f(int32 n){ if(n>0){ return n; } } int32 main(){ return f(0); }', invalid=True),
    case("type_mismatch", "diagnostics", 'int32 main(){ int32 n="wrong"; return n; }', invalid=True),
    case("break_outside_loop", "diagnostics", 'int32 main(){ break; return 0; }', invalid=True),
    case("unknown_name", "diagnostics", 'int32 main(){ return missing; }', invalid=True),
    case("malformed_hex", "diagnostics", 'int32 main(){ return 0x; }', invalid=True),
    case("malformed_hex_digit", "diagnostics", 'int32 main(){ return 0xGG; }', invalid=True),
    case("unterminated_string", "diagnostics", 'int32 main(){ string value="unfinished; return 0; }', invalid=True),
    case("unterminated_comment", "diagnostics", 'int32 main(){ /* unfinished return 0; }', invalid=True),
    case("invalid_char_width", "diagnostics", "int32 main(){ char value='ab'; return 0; }", invalid=True),
    case("integer_literal_overflow", "diagnostics", 'int32 main(){ uint64 value=18446744073709551616; return 0; }', invalid=True),
    case("function_arguments", "functions", 'int32 sum(int32 a,int32 b,int32 c,int32 d,int32 e,int32 f,int32 g,int32 h,int32 i){ return a+b+c+d+e+f+g+h+i; } int32 main(){ return sum(1,2,3,4,5,6,7,8,9); }', 45),
    case("implicit_void_main", "functions", 'void main(){}'),
    case("implicit_void_helper", "functions", 'void worker(){} int32 main(){ worker(); return 0; }'),
    case("multifile", "workflow", ['int32 add(int32 a,int32 b){ return a+b; }', 'int32 main(){ return add(12,5); }'], 17),
]
BENCHMARKS = [
    case("fib_recursive", "benchmark", 'int32 fib(int32 n){ if(n<2){ return n; } return fib(n-1)+fib(n-2); } int32 main(){ if(fib(24)==46368){ return 0; } return 1; }'),
    case("fib_iterative", "benchmark", 'int32 fib(int32 n){ int32 a=0; int32 b=1; int32 i=0; while(i<n){ int32 t=a+b; a=b; b=t; i++; } return a; } int32 main(){ int32 i=0; int64 total=0; while(i<100000){ total+=fib(20); i++; } if(total==676500000){ return 0; } return 1; }'),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Comparison:
    def __init__(self, args):
        self.args = args
        self.work = args.work.resolve()
        self.work.mkdir(parents=True, exist_ok=True)
        self.tools = {"selfhost": args.selfhost.resolve(), "rekrtc": args.rekrtc.resolve(),
                      "wrapper": ROOT / "SelfHost/Compile.py", "comparison": Path(__file__).resolve()}
        for path in self.tools.values():
            if not path.is_file():
                raise FileNotFoundError(path)
        self.report = {"schema": 1, "complete": False, "environment": {"platform": platform.platform(), "python": sys.version,
                       "cpu_count": os.cpu_count()}, "tools": {k: {"path": str(p), "sha256": sha(p)} for k, p in self.tools.items()},
                       "method": "Independent expected exits/stdout; Re.KrtC is a comparator, not an oracle. Public CLI timing includes linking and Python startup. Runtime includes process startup. Re object measurement uses retained KRO because target eo also links. Length property and method forms are tested separately. Overflow probe expects fixed-width two's-complement wrapping.",
                       "cases": [], "benchmarks": [], "commands": []}
        self.report["library_sources"] = {str(path.relative_to(ROOT)): sha(path) for path in sorted((ROOT / "libs").rglob("*.krt"))}

    def save(self):
        (self.work / "report.json").write_text(json.dumps(self.report, indent=2) + "\n")

    def command(self, argv, cwd, timeout):
        start = time.perf_counter()
        record = {"argv": [str(x) for x in argv], "cwd": str(cwd)}
        process = subprocess.Popen(record["argv"], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   start_new_session=True)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
            record.update(returncode=process.returncode, stdout=stdout.decode(errors="replace"), stderr=stderr.decode(errors="replace"), timeout=False)
        except subprocess.TimeoutExpired:
            # Kill wrapper and compiler together; a malformed source must not leave
            # a spinning compiler behind or hold inherited output pipes open.
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
            record.update(returncode=None, stdout=stdout.decode(errors="replace"), stderr=stderr.decode(errors="replace"), timeout=True)
        record["seconds"] = time.perf_counter() - start
        self.report["commands"].append(record)
        return len(self.report["commands"]) - 1, record

    def compile(self, label, paths, folder, object_only=False):
        output = folder / ("output.kro" if object_only else "program")
        for p in (output, folder / "program.exe", folder / "source0.kro"):
            if p.exists():
                p.unlink()
        if label == "selfhost":
            argv = [sys.executable, self.tools["wrapper"], *paths, "--compiler", self.tools[label], "-o", output]
            if object_only:
                argv.append("-c")
        else:
            argv = [self.tools[label], "-O2", *paths, "output", output]
            if object_only:
                argv.extend(["target", "eo", "--keep-temp"])
        command, result = self.command(argv, folder, self.args.compile_timeout)
        # Re.KrtC's multifile driver names its executable <basename>.exe in cwd.
        if not output.exists() and len(paths) > 1 and (folder / "program.exe").exists():
            output = folder / "program.exe"
        if object_only and label == "rekrtc":
            # The Re.KrtC `target eo` CLI also links; measure its retained KRO,
            # never compare an ELF named output.kro with a SelfHost KRO.
            output = folder / "source0.kro"
        return command, result, output

    def evaluate(self, probe, label, repetition=0, object_only=False):
        folder = self.work / probe["name"] / label / ("object" if object_only else str(repetition))
        folder.mkdir(parents=True, exist_ok=True)
        paths = []
        for index, source in enumerate(probe["sources"]):
            path = folder / f"source{index}.krt"
            path.write_text(source + "\n")
            paths.append(path)
        ci, compile_result, output = self.compile(label, paths, folder, object_only)
        result = {"compile_command": ci, "compile_seconds": compile_result["seconds"], "sources": [{"path": str(p), "sha256": sha(p)} for p in paths]}
        if compile_result["timeout"]:
            result["status"] = "compile_timeout"
        elif compile_result["returncode"] < 0 or re.search(r'E_COMPILER: compiler exited with status -\d+', compile_result["stderr"]):
            result["status"] = "compiler_crash"
        elif compile_result["returncode"] != 0:
            result["status"] = "diagnostic_pass" if probe["invalid"] and (compile_result["stdout"] or compile_result["stderr"]) else "compile_rejected"
        elif not output.is_file():
            result["status"] = "missing_artifact"
        elif probe["invalid"]:
            result["status"] = "invalid_accepted"
        elif object_only:
            result["status"] = "object_emitted" if output.read_bytes()[:4] == b"KRO\x00" else "wrong_artifact_format"
        else:
            output.chmod(0o755)
            ri, runtime = self.command([output], folder, self.args.run_timeout)
            result.update(run_command=ri, run_seconds=runtime["seconds"], actual_exit=runtime["returncode"], actual_stdout=runtime["stdout"])
            if runtime["timeout"]:
                result["status"] = "runtime_timeout"
            elif runtime["returncode"] < 0:
                result["status"] = "runtime_crash"
            elif runtime["returncode"] != probe["expected_exit"] or runtime["stdout"] != probe["expected_stdout"]:
                result["status"] = "wrong_result"
            else:
                result["status"] = "pass"
        if output.is_file():
            result.update(artifact=str(output), artifact_bytes=output.stat().st_size, artifact_sha256=sha(output))
        return result

    def run(self):
        for probe in CASES:
            if self.args.only and probe["name"] not in self.args.only:
                continue
            record = dict(probe, results={})
            for label in ("selfhost", "rekrtc"):
                record["results"][label] = self.evaluate(probe, label)
            self.report["cases"].append(record)
            self.save()
            print(probe["name"], *(f"{k}={v['status']}" for k, v in record["results"].items()), flush=True)
        for probe in BENCHMARKS:
            if self.args.only and probe["name"] not in self.args.only:
                continue
            record = dict(probe, results={})
            for label in ("selfhost", "rekrtc"):
                runs = [self.evaluate(probe, label, index) for index in range(self.args.repetitions)]
                obj = self.evaluate(probe, label, object_only=True)
                record["results"][label] = {"samples": runs, "object": obj, "all_pass": all(r["status"] == "pass" for r in runs),
                                           "compile_median_seconds": statistics.median(r["compile_seconds"] for r in runs),
                                           "runtime_median_seconds": statistics.median(r["run_seconds"] for r in runs) if all("run_seconds" in r for r in runs) else None}
            self.report["benchmarks"].append(record)
            self.save()
        self.report["summary"] = {label: dict(Counter(c["results"][label]["status"] for c in self.report["cases"])) for label in ("selfhost", "rekrtc")}
        self.report["tool_hashes_after"] = {k: sha(p) for k, p in self.tools.items()}
        self.report["tools_changed_during_run"] = [k for k, p in self.tools.items() if sha(p) != self.report["tools"][k]["sha256"]]
        self.report["library_sources_after"] = {str(path.relative_to(ROOT)): sha(path) for path in sorted((ROOT / "libs").rglob("*.krt"))}
        self.report["complete"] = (not self.report["tools_changed_during_run"] and
                                   self.report["library_sources"] == self.report["library_sources_after"])
        self.save()
        print(json.dumps(self.report["summary"], indent=2))
        print("Report:", self.work / "report.json")
        bad = {"wrong_result", "runtime_crash", "runtime_timeout", "compiler_crash", "compile_timeout",
               "missing_artifact", "invalid_accepted", "wrong_artifact_format"}
        measured = [c["results"]["selfhost"] for c in self.report["cases"]]
        for benchmark in self.report["benchmarks"]:
            measured.extend(benchmark["results"]["selfhost"]["samples"])
            measured.append(benchmark["results"]["selfhost"]["object"])
        return int(not self.report["complete"] or
                   (self.args.fail_on_wrong and any(result["status"] in bad for result in measured)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=ROOT / "build/selfhost-comparison")
    parser.add_argument("--selfhost", type=Path, default=ROOT / "build/selfhost/stage2/program")
    rekrtc = ROOT / "build/Re.KrtC/KrtC"
    parser.add_argument("--rekrtc", type=Path, default=rekrtc if rekrtc.exists() else ROOT / "Re.KrtC/build/KrtC")
    parser.add_argument("--only", nargs="+")
    parser.add_argument("--repetitions", type=int, choices=range(1, 6), default=3)
    parser.add_argument("--compile-timeout", type=float, default=10)
    parser.add_argument("--run-timeout", type=float, default=2)
    parser.add_argument("--fail-on-wrong", action="store_true")
    args = parser.parse_args()
    try:
        return Comparison(args).run()
    except (OSError, ValueError) as error:
        print(f"comparison failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
