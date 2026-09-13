"""Guard hoisting, scratch-register forwarding and 32-bit arithmetic regressions."""
import operator
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def wrap32(value, unsigned=False):
    value &= 0xffffffff
    return value if unsigned or value < 0x80000000 else value - 0x100000000


class ScalarCodegen(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory(prefix="kairote-scalar-codegen-") as directory:
            path = Path(directory)
            code, binary = path / "Test.krt", path / "Test"
            code.write_text(source)
            compiled = subprocess.run([str(COMPILER), str(code), "output", str(binary)],
                                      cwd=path, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr + source)
            self.assertNotIn("runtime error:", compiled.stderr)
            self.assertNotIn("AddressSanitizer", compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, f"exit {result.returncode}\n{source}\n{result.stderr!r}")
            return subprocess.check_output(["objdump", "-d", "-w", "-Mintel", str(binary)], text=True)

    def test_guard_has_no_leaf_frame(self):
        assembly = self.compile_run("""
int32 Guard(int32 n) {
    if (n <= 1) { return n; }
    return n * 3 + 1;
}
int32 main() {
    if (Guard(-2147483648) != -2147483648 || Guard(-1) != -1 || Guard(0) != 0 ||
        Guard(1) != 1 || Guard(2) != 7 || Guard(20) != 61) { return 1; }
    unsafe(using krt.mem;) {
        let f: fn(int32) -> int32 = &Guard;
        if (f(20) != 61 || f(-17) != -17) { return 2; }
    }
    return 0;
}
""")
        body = assembly.split("<_ZN5GuardEi>:", 1)[1].split("<main>:", 1)[0]
        leaf = body.split("ret", 1)[0]
        self.assertNotRegex(leaf, r"\b(push|pop|call)\b|\[|\brsp\b|\brbp\b")
        self.assertNotRegex(body, r"\bcall\b")
        self.assertEqual(len(re.findall(r"\bcmp\b", body)), 1)
        self.assertRegex(body, r"sub\s+rsp,0x10\b")
        self.assertNotRegex(body, r"\bjmp\b")
        self.assertLessEqual(len(re.findall(r"\bmovsxd\b", body)), 6)

    def test_all_scalar_guard_relations_and_boundaries(self):
        relations = [("<", operator.lt), ("<=", operator.le), (">", operator.gt),
                     (">=", operator.ge), ("==", operator.eq), ("!=", operator.ne)]
        for bits in (2, 8, 16, 30, 32, 64):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                low = 0 if unsigned else -(1 << (bits - 1))
                high = (1 << (bits if unsigned else bits - 1)) - 1
                values = sorted({low, high, 0, 1, high // 2})
                definitions, checks = [], []
                for index, (symbol, relation) in enumerate(relations):
                    definitions.append(f"{kind} Pick{index}({kind} a, {kind} b) {{ "
                                       f"if (a {symbol} b) {{ return a; }} return b; }}")
                    for a in values:
                        for b in values:
                            expected = a if relation(a, b) else b
                            checks.append(f"if (Pick{index}(({kind}){a}, ({kind}){b}) != "
                                          f"({kind}){expected}) {{ return {index + 1}; }}")
                with self.subTest(type=kind):
                    self.compile_run("\n".join(definitions) + "\nint32 main() {\n" +
                                     "\n".join(checks) + "\nreturn 0; }")

    def test_false_leaf_constants_and_loop_body(self):
        self.compile_run("""
int32 FalseLeaf(int32 n, int32 value) {
    if (n > 0) { return n + value; } else { return value; }
}
int32 Constant(int32 n) { if (n == 0) { return -123; } return n * 3; }
int32 Count(int32 n) {
    if (n <= 0) { return n; }
    int32 sum = 0;
    while (n > 0) { sum += n; n--; }
    return sum;
}
int32 main() {
    if (FalseLeaf(0, -7) != -7 || FalseLeaf(6, -7) != -1 || Constant(0) != -123 ||
        Constant(9) != 27 || Count(-7) != -7 || Count(100) != 5050) { return 1; }
    return 0;
}
""")

    def test_guard_preserves_all_incoming_registers_and_stack_arguments(self):
        self.compile_run("""
int64 Mixed(int128 pad, int64 a, int64 b, int64 c, int64 d, int64 e) {
    if (c < d) { return b; }
    return (int64)pad + a + b + c + d + e;
}
int64 Stack(int64 a, int64 b, int64 c, int64 d, int64 e, int64 f, int64 g) {
    if (g < 0) { return g; }
    return a + b + c + d + e + f + g;
}
uint64 Huge(uint64 n) {
    if (n >= 9223372036854775808) { return n; }
    return n + 1;
}
int32 main() {
    if (Mixed((int128)18446744073709551617, 2, 3, 4, 5, 6) != 3 ||
        Mixed((int128)18446744073709551617, 2, 3, 5, 4, 6) != 21 ||
        Stack(1, 2, 3, 4, 5, 6, -7) != -7 || Stack(1, 2, 3, 4, 5, 6, 7) != 28 ||
        Huge((uint64)18446744073709551615) != (uint64)18446744073709551615 ||
        Huge(17) != 18) { return 1; }
    return 0;
}
""")

    def test_guard_does_not_skip_effects_or_conversions(self):
        self.compile_run("""
int32 Effect(int32 n, ref int32 count) {
    count++;
    if (n <= 1) { return n; }
    return n + count;
}
int64 Widen(int32 n) { if (n < 0) { return (int64)n; } return (int64)n + 1; }
int32 Narrow(int64 n) { if (n < 0) { return (int32)n; } return (int32)n + 1; }
float64 Floating(float64 n) { if (n < 0.0) { return n; } return n + 0.5; }
int32 main() {
    int32 count = 0;
    if (Effect(1, ref count) != 1 || count != 1 || Effect(5, ref count) != 7 || count != 2) { return 1; }
    if (Widen(-2147483648) != -2147483648 || Widen(2147483647) != 2147483648 ||
        Narrow(-4294967297) != -1 || Narrow(4294967296) != 1 ||
        Floating(-1.5) != -1.5 || Floating(1.5) != 2.0) { return 2; }
    return 0;
}
""")

    def test_32bit_arithmetic_against_integer_oracle(self):
        randomizer = random.Random(0x3240)
        operations = [("Add", "+", operator.add), ("Sub", "-", operator.sub),
                      ("Mul", "*", operator.mul), ("And", "&", operator.and_),
                      ("Or", "|", operator.or_), ("Xor", "^", operator.xor)]
        for unsigned in (False, True):
            kind = "uint32" if unsigned else "int32"
            values = [0, 1, -1, -128, 127, 128, 0x7fffffff, 0x80000000, 0xffffffff]
            values += [randomizer.getrandbits(32) for _ in range(15)]
            values = [wrap32(value, unsigned) for value in values]
            for index, (name, symbol, operation) in enumerate(operations):
                definitions, checks = [], []
                definitions.append(f"{kind} {name}({kind} a, {kind} b) {{ return a {symbol} b; }}")
                for a, b in zip(values, reversed(values)):
                    expected = wrap32(operation(a, b), unsigned)
                    checks.append(f"if ({name}(({kind}){a}, ({kind}){b}) != "
                                  f"({kind}){expected}) {{ return {index + 1}; }}")
                for constant in (1, -1, 127, 128, -128, -129, 0x80000000):
                    suffix = str(constant).replace("-", "Neg")
                    definitions.append(f"{kind} {name}{suffix}({kind} a) {{ return a {symbol} ({kind}){constant}; }}")
                    for a in values[:9]:
                        expected = wrap32(operation(a, wrap32(constant, unsigned)), unsigned)
                        checks.append(f"if ({name}{suffix}(({kind}){a}) != ({kind}){expected}) {{ return {index + 1}; }}")
                with self.subTest(type=kind, operation=name):
                    self.compile_run("\n".join(definitions) + "\nint32 main() {\n" + "\n".join(checks) + "\nreturn 0; }")

    def test_forwarding_and_live_values_across_calls(self):
        self.compile_run("""
int32 Id(int32 n) { return n; }
int32 Many(int32 a, int32 b, int32 c, int32 d, int32 e, int32 f, int32 g, int32 h) {
    return a + b*2 + c*3 + d*4 + e*5 + f*6 + g*7 + h*8;
}
int32 Expr(int32 n) { return Id(n+1) - Id(n+2) + Id(n+3)*Id(n+4); }
int32 main() {
    if (Expr(7) != 109 || Many(Id(1), Id(2), Id(3), Id(4), Id(5), Id(6), Id(7), Id(8)) != 204) { return 1; }
    unsafe(using krt.mem;) {
        let f: fn(int32) -> int32 = &Id;
        if (f(Id(3) + 7) != 10 || Id(17) - f(4) != 13 || (Id(1) + Id(2)) * (f(3) + f(4)) != 21) { return 2; }
    }
    return 0;
}
""")

    def test_mixed_width_extensions_and_packed_neighbors(self):
        self.compile_run("""
int32 Mixed(int8 a, uint16 b, int32 c) { return a + b - c; }
int32 Packed(int32 a, int32 b) { return (a + 128) - (b - 129); }
bool Less(int8 a, uint32 b) { return a < b; }
int32 main() {
    if (Mixed(-128, 65535, 2147483647) != -2147418240 ||
        Packed(2147483647, -2147483648) != 256 || Less(-1, 1) || !Less(1, 4294967295)) { return 1; }
    int2 a = -1; int30 b = -536870912; uint32 c = 4294967295;
    int32 x = (int32)b + 128;
    if (x != -536870784 || a != -1 || b != -536870912 || c != 4294967295) { return 2; }
    return 0;
}
""")

    def test_upper_words_are_kept_for_promotions_and_shared_uses(self):
        self.compile_run("""
int32 Id(int32 n) { return n; }
uint32 Unsigned(uint32 n) { return n; }
int64 Widen(int32 n) { return (int64)Id(n); }
int128 Wide(int32 n) { return (int128)Id(n) * 4294967296; }
int32 Bits(int32 n) { return (Id(n) & Id(15)) | (Id(32) ^ Id(8)); }
int32 main() {
    int32 shared = Id(-1);
    if ((int64)shared != -1 || (int128)shared != -1 || shared + 1 != 0 ||
        Widen(-2147483648) != -2147483648 || Wide(-1) != -(int128)4294967296 ||
        (uint64)Unsigned(4294967295) != (uint64)4294967295 || Bits(19) != 43 ||
        Id(-2147483648) >= Id(-1) || Id(2147483647) - Id(-1) != -2147483648) { return 1; }
    return 0;
}
""")

    def test_unused_results_keep_side_effects_and_zero_local_storage(self):
        self.compile_run("""
int32 Touch(ref int32 n) { n++; return n; }
int32 Zero(int32 a) { int32 b; return b + a; }
int32 Dead(int32 n) { return n + 1; n += 100; }
int32 main() {
    int32 n = 0;
    Touch(ref n); Touch(ref n);
    if (n != 2 || Zero(-17) != -17 || Dead(8) != 9) { return 1; }
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
