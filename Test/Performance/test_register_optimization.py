"""O3 copy coalescing, permanent registers and native register comparisons."""
import operator
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def wrap(value, bits, unsigned=False):
    value %= 1 << bits
    return value if unsigned or value < 1 << (bits - 1) else value - (1 << bits)


class RegisterOptimization(unittest.TestCase):
    def run_source(self, source, levels=(2, 3)):
        result = {}
        with tempfile.TemporaryDirectory(prefix="kairote-registers-") as directory:
            path = Path(directory)
            code = path / "Test.krt"
            code.write_text(source)
            for level in levels:
                binary = path / f"Program{level}"
                built = subprocess.run(
                    [str(COMPILER), f"-O{level}", str(code), "-o", str(binary)],
                    cwd=path, capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr + source)
                self.assertNotIn("AddressSanitizer", built.stderr)
                self.assertNotIn("runtime error:", built.stderr)
                executed = subprocess.run([str(binary)], capture_output=True, timeout=10)
                self.assertEqual(executed.returncode, 0,
                                 f"O{level}, exit {executed.returncode}\n{source}\n{executed.stderr!r}")
                result[level] = subprocess.check_output(
                    ["objdump", "-d", "-w", "-Mintel", str(binary)], text=True)
        return result

    def function_body(self, assembly, name):
        match = re.search(r"^[0-9a-f]+ <_ZN" + str(len(name)) + re.escape(name) +
                          r"E[^>]*>:\n(.*?)(?=^[0-9a-f]+ <(?:_ZN|main>|_start>)|\Z)",
                          assembly, re.M | re.S)
        self.assertIsNotNone(match, assembly)
        return match.group(1)

    def test_call_snapshot_shares_preserved_register(self):
        assembly = self.run_source("""
static uint64 effects=0;
uint64 Effect(uint64 n) { effects++; return n*3; }
uint64 Snapshot(uint64 n) { if (n == 0) { return 0; } return n+Effect(n); }
int32 main() {
    if (Snapshot(7) != 28 || Snapshot(19) != 76 || effects != 2) { return 1; }
    return 0;
}
""")
        bodies = {}
        for level in (2, 3):
            bodies[level] = self.function_body(assembly[level], "Snapshot")
        saves = r"mov\s+QWORD PTR \[rbp-[^\]]+\],(?:rbx|r1[2-5])\b"
        self.assertLess(len(re.findall(saves, bodies[3])), len(re.findall(saves, bodies[2])))

    def test_snapshot_before_parameter_and_local_rewrite(self):
        self.run_source("""
int64 Before(int64 n) { return n+n++; }
int64 After(int64 n) { return n+(++n); }
int64 Local(int64 n) { int64 x=n; return x+(x++); }
int64 Branch(int64 n, bool choose) { return n+(choose ? n+1 : n+2); }
int32 main() {
    if (Before(5) != 10 || After(5) != 11 || Local(5) != 10) { return 1; }
    if (Branch(5,true) != 11 || Branch(5,false) != 12) { return 2; }
    int64 sum=0;
    for (int64 i=0; i<80; i++) { sum += i+(i++); }
    return sum == 3120 ? 0 : 3;
}
""")

    def test_reference_pointer_global_and_indirect_side_effects(self):
        self.run_source("""
static int64 global=7;
int64 GlobalWrite() { global=100; return global; }
int64 Increment(ref int64 x) { x++; return x; }
int64 Direct(int64 n) { return n+Increment(ref n); }
int64 Indirect(int64 n) {
    unsafe(using krt.mem;) {
        int64 x=n;
        let f: fn(ref int64) -> int64 = &Increment;
        return x+f(ref x);
    }
}
int64 Pointers(int64 n) {
    unsafe(using krt.mem;) {
        int64 x=n;
        let p=&x;
        let old=*p;
        *p=99;
        return old+x;
    }
}
int32 main() {
    if (global+GlobalWrite() != 107 || global != 100) { return 1; }
    if (Direct(5) != 11 || Indirect(5) != 11 || Pointers(5) != 104) { return 2; }
    return 0;
}
""")

    def test_snapshots_at_every_integer_width(self):
        for unsigned in (False, True):
            definitions, checks = [], []
            for bits in range(2, 129, 2):
                kind = ("uint" if unsigned else "int") + str(bits)
                value = wrap((1 << (bits - 1)) + 1, bits, unsigned)
                expected = wrap(value * 4, bits, unsigned)
                definitions.append(f"{kind} Change{bits}({kind} n) {{ effects++; return n*({kind})3; }}")
                definitions.append(f"{kind} Saved{bits}({kind} n) {{ return n+Change{bits}(n); }}")
                checks.append(f"if (Saved{bits}(({kind}){value}) != ({kind}){expected}) {{ return 1; }}")
            with self.subTest(unsigned=unsigned):
                self.run_source("static int32 effects=0;\n" + "\n".join(definitions) +
                                "\nint32 main() {\n" + "\n".join(checks) +
                                "\nreturn effects == 64 ? 0 : 2; }")

    def test_packed_neighbours_and_register_pressure(self):
        self.run_source("""
static int32 effects=0;
int64 Touch(int64 x) { effects++; return x*3; }
int64 Packed(int2 a, int30 b, uint32 c, int64 d) {
    return (int64)a+Touch((int64)b)+(int64)c+d;
}
int64 Pressure(int64 a, int64 b, int64 c, int64 d, int64 e, int64 f, int64 g, int64 h) {
    return a+Touch(b)+c+Touch(d)+e+Touch(f)+g+Touch(h);
}
int32 main() {
    if (Packed(-1,-123,(uint32)4294967295,19) != 4294966944) { return 1; }
    if (Pressure(1,2,3,4,5,6,7,8) != 76 || effects != 5) { return 2; }
    return 0;
}
""")

    def test_comparisons_between_registers_at_boundaries(self):
        relations = (("Lt", "<", operator.lt), ("Le", "<=", operator.le),
                     ("Gt", ">", operator.gt), ("Ge", ">=", operator.ge),
                     ("Eq", "==", operator.eq), ("Ne", "!=", operator.ne))
        for bits in (32, 64):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                values = tuple(wrap(value, bits, unsigned) for value in
                               (0, 1, -1, -(1 << (bits - 1)), (1 << (bits - 1)) - 1))
                definitions = [f"bool {name}({kind} a, {kind} b) {{ return a {symbol} b; }}"
                               for name, symbol, _ in relations]
                checks = []
                for a in values:
                    for b in values:
                        for name, _, operation in relations:
                            expected = "true" if operation(a, b) else "false"
                            checks.append(f"if ({name}(({kind}){a},({kind}){b}) != {expected}) {{ return 1; }}")
                with self.subTest(bits=bits, unsigned=unsigned):
                    self.run_source("\n".join(definitions) + "\nint32 main() {\n" +
                                    "\n".join(checks) + "\nreturn 0; }")

    def test_dominating_value_survives_branches_and_calls(self):
        assembly = self.run_source("""
static int32 effects=0;
int64 Touch(int64 n) { effects++; return n*3; }
int64 Across(int64 n, int64 m, bool choose) { return (n+m)+Touch(choose ? 10 : 20); }
int32 main() {
    if (Across(7,11,true) != 48 || Across(7,11,false) != 78 || effects != 2) { return 1; }
    return 0;
}
""")
        temporary_store = r"mov\s+QWORD PTR \[rbp-[^\]]+\],rax\b"
        stores = [len(re.findall(temporary_store, self.function_body(assembly[level], "Across")))
                  for level in (2, 3)]
        self.assertLess(stores[1], stores[0])

    def test_cross_block_pointer_snapshot_and_packed_neighbours(self):
        self.run_source("""
int64 Alter(int64* p) { unsafe(using krt.mem;) { *p=100; return *p; } }
int64 Observe(int64* p) { unsafe(using krt.mem;) { return *p; } }
int64 Saved(int64* p, bool choose) {
    unsafe(using krt.mem;) { return *p+(choose ? Alter(p) : Observe(p)); }
}
static int32 effects=0;
int64 Touch(int64 n) { effects++; return n*3; }
int64 PackedAcross(int2 a, int30 b, uint32 c, int64 d, bool choose) {
    return (int64)a+(int64)b+(int64)c+(choose ? Touch(d) : Touch(d+1));
}
int32 main() {
    unsafe(using krt.mem;) {
        int64 x=7;
        if (Saved(&x,true) != 107 || x != 100) { return 1; }
        x=7;
        if (Saved(&x,false) != 14 || x != 7) { return 2; }
    }
    if (PackedAcross(-1,-123,(uint32)4294967295,19,true) != 4294967228) { return 3; }
    if (PackedAcross(-1,-123,(uint32)4294967295,19,false) != 4294967231 || effects != 2) { return 4; }
    return 0;
}
""")

    def test_cross_block_wide_integer_pairs_and_pressure_fallback(self):
        value = (1 << 126) + 5
        for unsigned in (False, True):
            kind = "uint128" if unsigned else "int128"
            expected_true = wrap(value * 3 + 7, 128, unsigned)
            expected_false = wrap(value * 3 + 9, 128, unsigned)
            with self.subTest(unsigned=unsigned):
                self.run_source(f"""
{kind} Wide({kind} n, bool choose) {{ return n*({kind})3+(choose ? ({kind})7 : ({kind})9); }}
{kind} Crowded({kind} n, int64 a, int64 b, int64 c, int64 d, bool choose) {{
    return n*({kind})3+({kind})(a+b+c+d)+(choose ? ({kind})7 : ({kind})9);
}}
int32 main() {{
    {kind} n=({kind}){value};
    if (Wide(n,true) != ({kind}){expected_true} || Wide(n,false) != ({kind}){expected_false}) {{ return 1; }}
    if (Crowded(n,1,2,3,4,true) != ({kind}){wrap(expected_true+10,128,unsigned)}) {{ return 2; }}
    return 0;
}}
""")

    def test_tail_reentry_cycles_and_large_cfg_fallback(self):
        a, b, total = 3, 7, 0
        for _ in range(25):
            total, a, b = total + a, b, a + b
        expected = total + (a ^ b)
        expression = "7"
        for number in range(100, 0, -1):
            expression = f"(choose ? {number} : {expression})"
        self.run_source(f"""
uint64 Chain(uint64 n, uint64 a, uint64 b) {{
    if (n == 0) {{ return a^b; }} return a+Chain(n-1,b,a+b);
}}
int64 Large(int64 n, bool choose) {{ return n+{expression}; }}
int64 Loop(int64 n) {{
    int64 sum=0;
    for (int64 i=0; i<n; i++) {{ sum += i+(i%2 == 0 ? i+1 : i+2); }}
    return sum;
}}
int32 main() {{
    if (Chain(25,3,7) != (uint64){expected}) {{ return 1; }}
    if (Large(3,false) != 10 || Large(3,true) != 4) {{ return 2; }}
    if (Loop(80) != 6440) {{ return 3; }}
    return 0;
}}
""")

    def test_ir_dominance_redefinition_and_loop_fallbacks(self):
        with tempfile.TemporaryDirectory(prefix="kairote-register-ir-") as directory:
            binary = Path(directory) / "RegisterChecks"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-ffunction-sections", "-fdata-sections", "-I" + str(ROOT / "Re.KrtC/src"),
                str(Path(__file__).with_name("test_register_allocation.c")),
                str(ROOT / "Re.KrtC/src/compiler/Frontend/Lexer/Tokenizer.c"),
                "-Wl,--gc-sections", "-lm", "-o", str(binary)],
                check=True, capture_output=True, text=True, timeout=60)
            checked = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10,
                                     env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                                              UBSAN_OPTIONS="halt_on_error=1"))
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)


if __name__ == "__main__":
    unittest.main()
