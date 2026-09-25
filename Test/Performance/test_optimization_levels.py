"""Optimization options, integer inlining, native encodings and frame reuse."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT/"Re.KrtC/build/KrtC")).resolve()


def wrap(value, bits, unsigned=False):
    value %= 1 << bits
    return value if unsigned or value < 1 << (bits-1) else value - (1 << bits)


class OptimizationLevels(unittest.TestCase):
    def run_source(self, source, levels=(2,3), library=False):
        result = {}
        with tempfile.TemporaryDirectory(prefix="kairote-levels-") as directory:
            path = Path(directory)
            code = path/"Test.krt"
            code.write_text(source)
            if library:
                for original in (ROOT/"libs").rglob("*.krt"):
                    target = path/"libs"/original.relative_to(ROOT/"libs")
                    target.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copy2(original,target)
            for level in levels:
                binary = path/f"Program{level}"
                built = subprocess.run([str(COMPILER),f"-O{level}",str(code),"-o",str(binary)],
                                       cwd=path,capture_output=True,text=True,timeout=30)
                self.assertEqual(built.returncode,0,built.stdout+built.stderr+source)
                self.assertNotIn("AddressSanitizer",built.stderr)
                self.assertNotIn("runtime error:",built.stderr)
                execution = subprocess.run([str(binary)],capture_output=True,timeout=10)
                self.assertEqual(execution.returncode,0,f"O{level}, exit {execution.returncode}\n{source}\n{execution.stderr!r}")
                result[level] = {"stdout":execution.stdout,"asm":subprocess.check_output(["objdump","-d","-w","-Mintel",str(binary)],text=True)}
        return result

    def test_level_selection_and_tail_calls(self):
        source = """
int32 Steps(int32 n) { if (n <= 0) { return 1; } return Steps(n-1)+Steps(n-3); }
int32 main() { return Steps(12) == 129 ? 0 : 1; }
"""
        results = self.run_source(source,(0,1,2,3))
        for level in (0,1):
            self.assertEqual(len(re.findall(r"call\s+[^\n]*<_ZN5StepsEi>",results[level]["asm"])),3)
        self.assertEqual(len(re.findall(r"call\s+[^\n]*<_ZN5StepsEi>",results[2]["asm"])),2)
        # Recursive expansion may introduce more static call sites at O3 while
        # reducing dynamic calls. Verify bounded stack use instead of fixing its
        # layout to the pre-inlining instruction count.
        self.run_source("""
int32 Advance(int32 n, int32 sum) {
    if (n <= 0) { return sum; }
    return Advance(n-1,sum+1);
}
int32 main() { return Advance(1000000,0) == 1000000 ? 0 : 1; }
""", (2,3))
        with tempfile.TemporaryDirectory(prefix="kairote-flags-") as directory:
            path=Path(directory)
            code=path/"Test.krt"
            code.write_text("int32 main() { return 0; }")
            for flag in ("-O", "-O4", "-O30", "-Ofast"):
                result=subprocess.run([str(COMPILER),str(code),flag,"output",str(path/"Out")],cwd=path,capture_output=True,text=True,timeout=10)
                self.assertGreater(result.returncode,0,result.stdout+result.stderr)
                self.assertIn("-O0",result.stderr)

    def test_pure_helper_inlining_changes_callers(self):
        results=self.run_source("""
int64 Calculate(int64 a, int64 b) { return (a+b)*(a-b); }
int64 Echo(int64 a) { return a; }
int32 main() { int64 n=7; return Calculate(n,3)+Echo(n) == 47 ? 0 : 1; }
""")
        self.assertRegex(results[2]["asm"],r"call\s+[^\n]*<_ZN9CalculateEll>")
        self.assertNotRegex(results[3]["asm"],r"call\s+[^\n]*<_ZN(?:9CalculateEll|4EchoEl)>")

    def test_inlined_integer_conversions_at_every_width(self):
        for bits in range(2,129,2):
            for unsigned in (False,True):
                kind=("uint" if unsigned else "int")+str(bits)
                value=wrap((1 << (bits-1))+1,bits,unsigned)
                expected=wrap(value*value+value,bits,unsigned)
                with self.subTest(type=kind):
                    self.run_source(f"""
{kind} Mix({kind} value) {{ return value*value+value; }}
{kind} Echo({kind} value) {{ return value; }}
int32 main() {{ {kind} n=({kind}){value};
    if (Mix(n) != ({kind}){expected} || Echo(n) != n) {{ return 1; }} return 0;
}}
""")

    def test_64bit_immediate_sign_extension_and_wrap(self):
        constants=(0,1,127,128,-128,-129,2147483647,2147483648,-2147483648,-2147483649,
                   4294967295,4294967296,9223372036854775807,-9223372036854775808,-1)
        operations={"Add":("+",lambda a,b:a+b),"Sub":("-",lambda a,b:a-b),
                    "Mul":("*",lambda a,b:a*b),"And":("&",lambda a,b:a&b),
                    "Or":("|",lambda a,b:a|b),"Xor":("^",lambda a,b:a^b)}
        for unsigned in (False,True):
            kind="uint64" if unsigned else "int64"
            for value in (0,7,-1,1 << 63):
                definitions,checks=[],[]
                for index,constant in enumerate(constants):
                    for name,(op,fn) in operations.items():
                        function=f"{name}{index}"
                        definitions.append(f"{kind} {function}({kind} n) {{ return n {op} ({kind}){constant}; }}")
                        expected=wrap(fn(wrap(value,64,unsigned),wrap(constant,64,unsigned)),64,unsigned)
                        checks.append(f"if ({function}(n) != ({kind}){expected}) {{ return 1; }}")
                with self.subTest(type=kind,value=value):
                    self.run_source("\n".join(definitions)+f"\nint32 main() {{ {kind} n=({kind}){value};\n"+
                                    "\n".join(checks)+"\nreturn 0; }")

    def test_side_effects_and_frame_addresses_remain_observable(self):
        self.run_source("""
static int64 state=0;
int64 Read() { return state; }
int64 Effect(int64 n) { state=state*10+n; return n; }
int64 Walk(int64 n) { if (n == 0) { return 0; } return Effect(n)+Walk(n-1); }
int64 Update(ref int64 n) { n++; return n; }
int32 Address(int32 n, uint64 old) {
    unsafe(using krt.mem;) { int32 x=n; uint64 current=(uint64)&x;
        if (old == current) { return 100; }
        if (n == 0) { return 0; }
        return Address(n-1,current)+x;
    }
}
int32 main() {
    if (Walk(4) != 10 || state != 4321 || Read() != 4321) { return 1; }
    int64 x=3; if (Update(ref x)+Update(ref x) != 9 || x != 5) { return 2; }
    if (Address(5,0) != 15) { return 3; }
    return 0;
}
""")

    def test_tail_parameter_register_cycles_and_fallback(self):
        self.run_source("""
int64 Cycle(int64 n, int64 a, int64 b, int64 c, int64 d) {
    if (n == 0) { return a+10*b+100*c+1000*d; }
    return Cycle(n-1,b,c,d,a);
}
int64 Packed(int64 n, int2 a, int30 b, uint32 c) {
    if (n == 0) { return (int64)a+(int64)b+(int64)c; }
    return Packed(n-1,(int2)b,(int30)c,(uint32)a);
}
int32 main() {
    if (Cycle(20001,1,2,3,4) != 1432 || Packed(30000,-1,-1,4294967295) != 4294967293) { return 1; }
    return 0;
}
""")

    def test_function_storage_initialization_isolated(self):
        definitions,checks=[],[]
        for i in range(150):
            definitions.append(f"int64 Value{i}(int64 n) {{ int2 a=1; int30 b={i}; int64 x=n; return x+(int64)a+(int64)b; }}")
            checks.append(f"if (Value{i}(7) != {i+8}) {{ return 1; }}")
            definitions.append(f"int64 Plain{i}(int64 n) {{ return n+{i}; }}")
            checks.append(f"if (Plain{i}(7) != {i+7}) {{ return 2; }}")
        self.run_source("\n".join(definitions)+"\nint32 main() {\n"+"\n".join(checks)+"\nreturn 0; }",(0,2,3))

    def test_constant_shift_counts_use_immediate_encoding(self):
        widths=((8,"int8","uint8"),(16,"int16","uint16"),(32,"int32","uint32"),(64,"int64","uint64"))
        for bits,signed,unsigned_kind in widths:
            for kind,unsigned in ((signed,False),(unsigned_kind,True)):
                definitions,checks=[],[]
                values=(0,1,-1,3,(1 << (bits-1)),(1 << bits)-1,-(1 << (bits-1)))
                counts=(0,1,bits//2,bits-1,bits,bits+3,127)
                for index,value in enumerate(values):
                    start=wrap(value,bits,unsigned)
                    for position,count in enumerate(counts):
                        for name,op in (("Left","<<"),("Right",">>")):
                            function=f"{name}{index}_{position}"
                            definitions.append(f"{kind} {function}({kind} n) {{ return n {op} {count}; }}")
                            if count >= bits:
                                expected=0 if op == "<<" or unsigned or start >= 0 else -1
                            elif op == "<<":
                                expected=wrap(start << count,bits,unsigned)
                            else:
                                expected=wrap((start & ((1 << bits)-1)) >> count if unsigned else start >> count,
                                              bits,unsigned)
                            checks.append(f"if ({function}(({kind}){start}) != ({kind}){expected}) {{ return 1; }}")
                with self.subTest(type=kind):
                    self.run_source("\n".join(definitions)+"\nint32 main() {\n"+"\n".join(checks)+"\nreturn 0; }",
                                    (0,1,2,3))
        source="""
uint32 Mix(uint32 x) { x = x*1664525; return x ^ (x >> 13); }
int32 main() { return Mix(12345) == 3368840884 ? 0 : 1; }
"""
        results=self.run_source(source,(2,3))
        bodies={level:re.search(r"<_ZN3Mix\w*>:(.*?)\n\n",results[level]["asm"],re.S).group(1) for level in (2,3)}
        self.assertRegex(bodies[3],r"shr\s+\w+,0xd")
        self.assertRegex(bodies[2],r"shr\s+\w+,cl")
        self.assertNotRegex(bodies[3],r"shr\s+\w+,cl")

    def test_tail_guards_and_effectful_base_cases(self):
        for kind in ("int2", "int30", "uint32", "int64", "uint64", "int128"):
            for condition in ("n <= 0", "n == 0", "0 >= n", "n < 1", "0 == n", "1 > n"):
                with self.subTest(type=kind,condition=condition):
                    self.run_source(f"""
static int64 visits=0;
{kind} Pure({kind} n, {kind} a) {{
    if ({condition}) {{ return a; }} return Pure(n-1,a+n);
}}
{kind} Observed({kind} n) {{
    if ({condition}) {{ visits++; return 1; }}
    return Observed(n-1)+Observed(n-1);
}}
int32 main() {{
    if (Pure(1,0) != 1 || Pure(0,1) != 1 || Observed(1) != ({kind})2 || visits != 2) {{ return 1; }}
    return 0;
}}
""")

    def test_multifile_and_project_levels(self):
        with tempfile.TemporaryDirectory(prefix="kairote-build-levels-") as directory:
            path=Path(directory)
            (path/"Main.krt").write_text("int64 Step(int64 x) { return x*3+1; }\nint32 main() { int64 n=7; return Step(n)==22 ? 0 : 1; }")
            (path/"Extra.krt").write_text("int64 Extra(int64 n) { return n+1; }")
            (path/"project.krt").write_text('function Configure() { Name("LevelTest"); Output("Program"); Sources("Main.krt", "Extra.krt"); }')
            for project in (False,True):
                for level in (0,2,3):
                    command=[str(COMPILER),f"-O{level}"]
                    command += ["build","project.krt"] if project else ["Main.krt","Extra.krt","-o","Program"]
                    built=subprocess.run(command,cwd=path,capture_output=True,text=True,timeout=30)
                    self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                    binary=path/"bin/linux/Program" if project else path/"Program.exe"
                    result=subprocess.run([str(binary)],capture_output=True,timeout=10)
                    self.assertEqual(result.returncode,0,result.stderr)
                    assembly=subprocess.check_output(["objdump","-d","-w","-Mintel",str(binary)],text=True)
                    pattern=r"call\s+[^\n]*<_ZN4StepEl>"
                    if level == 3: self.assertNotRegex(assembly,pattern)
                    else: self.assertRegex(assembly,pattern)

    def test_automatic_stdlib_uses_module_optimization(self):
        with tempfile.TemporaryDirectory(prefix="kairote-cache-levels-") as directory:
            path=Path(directory)
            (path/"bin").mkdir()
            library=path/"stdlib"
            library.mkdir()
            compiler=path/"bin/KrtC"
            shutil.copy2(COMPILER,compiler)
            (library/"Helper.krt").write_text("int64 CachedHelper(int64 n) { return n+1; }")
            code=path/"Main.krt"
            code.write_text("int32 main() { int64 n=7; return CachedHelper(n)==8 ? 0 : 1; }")
            for level in (0,3,2,0):
                built=subprocess.run([str(compiler),f"-O{level}",str(code),"-o",str(path/"Program")],cwd=path,capture_output=True,text=True,timeout=30)
                self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                self.assertNotIn("KrtError",built.stderr)
                assembly=subprocess.check_output(["objdump","-d","-w","-Mintel",str(path/"Program")],text=True)
                pattern=r"call\s+[^\n]*<_ZN12CachedHelperEl>"
                if level == 3: self.assertNotRegex(assembly,pattern)
                else: self.assertRegex(assembly,pattern)
                self.assertEqual(subprocess.run([str(path/"Program")],capture_output=True,timeout=10).returncode,0)
            self.assertFalse(list(library.glob("*.kro")))

    def test_standard_library_compiles_from_sources(self):
        results=self.run_source((ROOT/"Test/SyntaxAudit/T00_Hello.krt").read_text(),(0,2,3),library=True)
        for result in results.values(): self.assertEqual(result["stdout"],b"Hello, KairoteLang!\n")
        self.run_source("""
using System;
int32 main() {
    unsafe(using krt.mem;) {
        let p=(byte*)Memory.Allocate(32);
        let s=p+16;
        for (int32 i=0; i<4; i++) { s[i]=(byte)(i+1); }
        Memory.Set(p,(char)127,16);
        Memory.Copy(p+3,s,4);
        Memory.Copy(p,s,0);
        Memory.Set(p,(char)0,0);
        for (int32 i=0; i<16; i++) {
            if (p[i] != (byte)(i>=3 && i<7 ? i-2 : 127)) { return 1; }
        }
        Memory.Free(p,32);
    }
    return 0;
}
""",(0,2,3),library=True)


if __name__ == "__main__":
    unittest.main()
