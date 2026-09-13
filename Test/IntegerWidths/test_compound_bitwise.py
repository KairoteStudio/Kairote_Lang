"""Compound bitwise operations must execute, type-check and evaluate lvalues once."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def wrap(value, bits, unsigned):
    value %= 1 << bits
    return value if unsigned or value < 1 << (bits-1) else value - (1 << bits)


class CompoundBitwise(unittest.TestCase):
    def source(self, text, diagnostic=None):
        with tempfile.TemporaryDirectory(prefix="kairote-compound-") as directory:
            path = Path(directory)
            code, binary = path/"Test.krt", path/"Test"
            code.write_text(text)
            result = subprocess.run([str(COMPILER), str(code), "output", str(binary)],
                                    cwd=path, capture_output=True, text=True, timeout=30)
            self.assertNotIn("AddressSanitizer", result.stderr)
            self.assertNotIn("runtime error:", result.stderr)
            if diagnostic:
                self.assertGreater(result.returncode, 0, result.stdout + result.stderr + text)
                self.assertIn(diagnostic, result.stdout + result.stderr)
                self.assertFalse(binary.exists())
                return
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr + text)
            executed = subprocess.run([str(binary)], capture_output=True, timeout=5)
            self.assertEqual(executed.returncode, 0, f"check {executed.returncode}\n{text}\n{executed.stderr!r}")

    def test_all_even_widths_in_parameters_locals_and_array_elements(self):
        for bits in range(2, 129, 2):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                start = wrap((1 << (bits-1)) | 1, bits, unsigned)
                rhs = wrap((1 << (bits-2)) | 1, bits, unsigned)
                steps = [("^=", rhs, lambda a, b: a ^ b), ("|=", rhs, lambda a, b: a | b),
                         ("&=", rhs, lambda a, b: a & b), ("<<=", 1, lambda a, b: a << b),
                         (">>=", 1, lambda a, b: a >> b)]
                body, array = [], []
                value = start
                for check, (op, operand, fn) in enumerate(steps, 1):
                    value = wrap(fn(value, operand), bits, unsigned)
                    rhs_text = str(operand) if op in ("<<=", ">>=") else f"({kind}){operand}"
                    body.append(f"v {op} {rhs_text}; if (v != ({kind}){value}) {{ return {check}; }}")
                    array.append(f"a[0] {op} {rhs_text}; if (a[0] != ({kind}){value}) {{ return {check+10}; }}")
                with self.subTest(type=kind):
                    self.source(f"""
int32 Check({kind} v) {{ {' '.join(body)} return 0; }}
int32 main() {{
    {kind} v = {start};
    int32 result = Check(v); if (result != 0) {{ return result; }}
    {' '.join(body)}
    {kind}[] a = new {kind}[1]; a[0] = ({kind}){start};
    {' '.join(array)}
    return 0;
}}
""")

    def test_shift_bounds_signedness_and_wide_counts(self):
        for bits in (2, 30, 32, 64, 66, 96, 128):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                value = wrap((1 << (bits-1)) | 1, bits, unsigned)
                checks = []
                for count in (0, 1, bits-1, bits, bits+1, 127, 128, 129, 1 << 100):
                    left = 0 if count >= bits else wrap(value << count, bits, unsigned)
                    right = (-1 if value < 0 else 0) if count >= bits else value >> count
                    checks.append(f"v = ({kind}){value}; v <<= (uint128){count}; "
                                  f"if (v != ({kind}){left}) {{ return 1; }}")
                    checks.append(f"v = ({kind}){value}; v >>= (uint128){count}; "
                                  f"if (v != ({kind}){right}) {{ return 2; }}")
                with self.subTest(type=kind):
                    self.source(f"int32 main() {{ {kind} v = 0; {' '.join(checks)} return 0; }}")

    def test_pointer_reference_global_and_index_evaluation_order(self):
        self.source("""
static int64 state = 9;
int64 Change() { state = 2; return 3; }
int32 Index(ref int32 calls) { calls++; return 1; }
int64 Rhs(ref int64 value) { value = 2; return 3; }
void Update(ref uint128 value) { value <<= 100; value |= 5; value ^= 3; value &= (uint128)-1; value >>= 2; }
int64* Address(int64* p, ref int32 calls) { calls++; return p; }
int32 main() {
    state ^= Change(); if (state != 10) { return 1; }
    uint128 wide = 1; Update(ref wide);
    if (wide != ((uint128)1 << 98) + 1) { return 2; }
    int32 calls = 0;
    int64[] array = new int64[2]; array[0] = 77; array[1] = 9;
    array[Index(ref calls)] ^= Rhs(ref array[1]);
    if (calls != 1 || array[1] != 10 || array[0] != 77) { return 3; }
    unsafe(using krt.mem;) {
        int64 value = 9;
        *Address(&value, ref calls) ^= Rhs(ref value);
        if (value != 10 || calls != 2) { return 4; }
        int30* small = stackalloc int30[2]; small[0] = 0; small[1] = -1;
        small[0] |= 15; small[0] &= 7; small[0] <<= 28; small[0] >>= 29;
        if (small[0] != -1 || small[1] != -1) { return 5; }
    }
    return 0;
}
""")

    def test_for_increment_and_mixed_arithmetic(self):
        self.source("""
int32 main() {
    int32 n = 0;
    for (uint32 bit=1; bit<1024; bit<<=1) { n++; }
    for (uint32 bit=1024; bit>0; bit>>=1) { n++; }
    if (n != 21) { return 1; }
    int32 x = 3; x += 5; x *= 3; x -= 3; x /= 2; x %= 6;
    if (x != 4) { return 2; }
    x |= 1 << 4; x ^= 3 + 1; x &= 31;
    if (x != 16) { return 3; }
    float64 f = 1.5; f += 2; f *= 4; f -= 2; f /= 3;
    if (f != 4.0) { return 4; }
    return 0;
}
""")

    def test_integer_mixing_loop_has_every_operation(self):
        expected = 12345
        for _ in range(1000000):
            expected = (expected*1664525 + 1013904223) & 0xffffffff
            expected ^= expected >> 13
        self.source(f"""
uint32 Mix(uint32 x, int32 n) {{
    for (int32 i=0; i<n; i++) {{ x = x*1664525 + 1013904223; x ^= x >> 13; }}
    return x;
}}
int32 main() {{
    if (Mix(12345,1) != 87635340 || Mix(12345,2) != (uint32)2254193080 ||
        Mix(12345,1000000) != (uint32){expected}) {{ return 1; }}
    return 0;
}}
""")

    def test_noninteger_operands_are_rejected(self):
        for op in ("&=", "|=", "^=", "<<=", ">>="):
            for declarations, target, value in (
                ("float64 x = 1.0;", "x", "1"), ("bool x = true;", "x", "1"),
                ("int32 x = 1;", "x", "0.5"), ("int32 x = 1;", "x", "true"),
                ("float64[] x = new float64[1];", "x[0]", "1"),
                ("int32[] x = new int32[1];", "x[0]", "0.5"),
                ("float64 x = 1.0;", "*(&x)", "1"), ("int32 x = 1;", "x", "&x"),
            ):
                with self.subTest(op=op, declarations=declarations, target=target, value=value):
                    self.source(f"int32 main() {{ unsafe(using krt.mem;) {{ {declarations} {target} {op} {value}; }} return 0; }}",
                                "Bitwise compound assignment requires integer operands")
            self.source(f"int32 main() {{ unsafe(using krt.mem;) {{ int32 x=0; int32* p=&x; p {op} 1; }} return 0; }}",
                        "Pointer compound assignment requires += or -= integer")

    def test_malformed_binary_operations_are_not_silently_dropped(self):
        for expression in ("x ^ = 1", "x & = 1", "x | = 1", "x << = 1", "x >> = 1", "x +", "x *"):
            with self.subTest(expression=expression):
                self.source(f"int32 main() {{ int32 x=0; {expression}; return 0; }}",
                            "Expected right operand of binary operator")
        self.source("int32 main() { int32[] a=new int32[1]; a ^= 1; return 0; }",
                    "Compound assignment requires a scalar or array element")
        self.source("int32 main() { int32 x=0; for (; false; (x+1) ^= 2) {} return 0; }",
                    "Unsupported assignment target")


if __name__ == "__main__":
    unittest.main()
