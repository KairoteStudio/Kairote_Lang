"""End-to-end integer tests. Build Re.KrtC, then run this file with Python 3."""
import os
import random
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def wrap(value, bits, unsigned):
    value %= 1 << bits
    if not unsigned and value >= 1 << (bits - 1):
        value -= 1 << bits
    return value


class IntegerWidths(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory(prefix="kairote-integers-") as directory:
            path = Path(directory)
            code = path / "test.krt"
            binary = path / "test"
            code.write_text(source)
            compiled = subprocess.run(
                [str(COMPILER), str(code), "output", str(binary)],
                cwd=path, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr + "\n" + source)
            self.assertTrue(binary.is_file(), compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 0, f"check/exit {result.returncode}\n{source}\n{result.stderr!r}")
            return result.stdout

    def test_every_even_width(self):
        for bits in range(2, 129, 2):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                with self.subTest(type=kind):
                    maximum = (1 << (bits if unsigned else bits - 1)) - 1
                    minimum = 0 if unsigned else -(1 << (bits - 1))
                    divisor = 1 if bits == 2 else 3
                    a = maximum - (1 if bits > 2 else 0)
                    b = divisor
                    checks = [
                        f"a == ({kind}){a}",
                        f"Echo(a) == a",
                        f"Add(top, one) == ({kind}){wrap(maximum + 1, bits, unsigned)}",
                        f"({kind})(a * d) == ({kind}){wrap(a * b, bits, unsigned)}",
                        f"a / d == ({kind}){a // b}",
                        f"a % d == ({kind}){a % b}",
                        "top > zero",
                        "zero < top",
                        "a <= top",
                        "top >= a",
                        "top != zero",
                        f"({kind})(a << shift) == ({kind}){wrap(a << 1, bits, unsigned)}",
                        f"a >> shift == ({kind}){a >> 1}",
                        f"({kind})~a == ({kind}){wrap(~a, bits, unsigned)}",
                        f"({kind})(a & d) == ({kind}){wrap(a & b, bits, unsigned)}",
                        f"({kind})(a | d) == ({kind}){wrap(a | b, bits, unsigned)}",
                        f"({kind})(a ^ d) == ({kind}){wrap(a ^ b, bits, unsigned)}",
                        f"({kind})(zero - one) == ({kind}){wrap(-1, bits, unsigned)}",
                        f"low == ({kind}){minimum}",
                        f"({kind})(low - one) == ({kind}){wrap(minimum - 1, bits, unsigned)}",
                        f"array[0] == top",
                        f"array[1] == low",
                        f"sizeof({kind}) == {next(n for n in (1, 2, 4, 8, 16) if n * 8 >= bits)}",
                        f"({kind})0x{maximum:x} == top",
                        f"({kind})0b{maximum:b} == top",
                    ]
                    if not unsigned:
                        quotient = -(abs(minimum) // b)
                        checks.extend([
                            "low < zero",
                            f"low / d == ({kind}){quotient}",
                            f"low % d == ({kind}){minimum - quotient * b}",
                            f"low >> shift == ({kind}){minimum >> 1}",
                            f"(int128)low == (int128){minimum}",
                        ])
                    else:
                        checks.append(f"(uint128)top == (uint128){maximum}")
                    body = "\n".join(f"if (!({check})) {{ return {i}; }}" for i, check in enumerate(checks, 1))
                    self.compile_run(f"""
{kind} Echo({kind} value) {{ return value; }}
{kind} Add({kind} a, {kind} b) {{ return a + b; }}
int32 main() {{
    {kind} a = {a}; {kind} d = {b};
    {kind} top = {maximum}; {kind} low = {minimum};
    {kind} zero = 0; {kind} one = 1;
    int32 shift = 1;
    {kind}[] array = new {kind}[2];
    array[0] = top; array[1] = low;
    {body}
    return 0;
}}
""")

    def test_wide_arithmetic(self):
        rng = random.Random(128)
        for bits in (64, 66, 96, 126, 128):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                for sample in range(8):
                    a = wrap(rng.getrandbits(bits), bits, unsigned)
                    b = wrap(rng.getrandbits(bits), bits, unsigned) or 1
                    quotient = abs(a) // abs(b) * (-1 if (a < 0) != (b < 0) else 1)
                    expressions = [
                        ("a + b", a + b), ("a - b", a - b), ("a * b", a * b),
                        ("a / b", quotient), ("a % b", a - quotient * b),
                        ("a < b", int(a < b)), ("a > b", int(a > b)),
                        ("a == b", int(a == b)), ("a != b", int(a != b)),
                    ]
                    for shift in (0, 2, 32, 63, 64, 65, 100, 127, 128, 129):
                        expressions.extend([(f"a << {shift}", a << shift), (f"a >> {shift}", a >> shift)])
                    body = "\n".join(
                        f"if (({kind})({expr}) != ({kind}){wrap(expected, bits, unsigned)}) {{ return {i}; }}"
                        for i, (expr, expected) in enumerate(expressions, 1)
                    )
                    with self.subTest(type=kind, sample=sample):
                        self.compile_run(f"int32 main() {{ {kind} a = {a}; {kind} b = {b}; {body} return 0; }}")

    def test_globals_calls_and_overloads(self):
        self.compile_run("""
static uint128 global = 340282366920938463463374607431768211455;
static int96 negative = -39614081257132168796771975168;
static uint128 expression = ((uint128)1 << 100) + 9007199254740993;
uint128 Sum(uint128 a, uint128 b, uint128 c, uint128 d, uint128 e) {
    return a + b + c + d + e;
}
int32 Pick6(int6 n) { return 6; }
int32 Pick10(int10 n) { return 10; }
int32 PickU6(uint6 n) { return 106; }
class Widths {
    public static int32 Pick(int6 n) { return 6; }
    public static int32 Pick(int10 n) { return 10; }
    public static int32 Pick(uint6 n) { return 106; }
}
int32 main() {
    if (global != (uint128)-1) { return 1; }
    if (negative != (int96)-39614081257132168796771975168) { return 2; }
    if (expression != ((uint128)1 << 100) + 9007199254740993) { return 3; }
    global += 1;
    if (global != 0) { return 4; }
    if (Sum((uint128)1 << 100, 2, 3, 4, 5) != ((uint128)1 << 100) + 14) { return 5; }
    if (Pick6((int6)1) != 6 || Pick10((int10)1) != 10 || PickU6((uint6)1) != 106) { return 6; }
    if (Widths.Pick((int6)1) != 6 || Widths.Pick((int10)1) != 10 || Widths.Pick((uint6)1) != 106) { return 7; }
    if (Forward(5) != ((uint128)1 << 100) + 5) { return 8; }
    uint128 a = (uint128)1 << 100;
    uint128 chosen = true ? a : 0;
    if (chosen != a) { return 9; }
    if (!(bool)a || (bool)(uint128)0) { return 10; }
    int6 n = 31;
    n++; if (n != -32) { return 11; }
    n--; if (n != 31) { return 12; }
    uint6 u = 63;
    if (n + u != (uint6)30) { return 13; }
    int10 wider = -200;
    if (wider + u != -137) { return 14; }
    int128[] values = new int128[2];
    values[0] = -1; values[1] = (int128)1 << 100;
    if (values[0] != -1 || values[1] != (int128)1 << 100) { return 15; }
    return 0;
}
uint128 Forward(uint128 n) { return ((uint128)1 << 100) + n; }
""")

    def test_fields_arrays_and_division_overflow(self):
        self.compile_run("""
class Box {
    int6 first;
    uint128 big;
    int96 middle;
    int6 last;
    public function Set(uint128 value) {
        first = 31; big = value; middle = -39614081257132168796771975168; last = -32;
    }
    public uint128 Get() { return big; }
    public bool Check() { return first == 31 && middle == (int96)-39614081257132168796771975168 && last == -32; }
}
uint128 Read(uint128[] values, int32 index) { return values[index]; }
int32 main() {
    Box b = new Box();
    b.Set(340282366920938463463374607431768211455);
    if (b.Get() != (uint128)-1 || !b.Check()) { return 1; }
    uint128[] values = [0, 340282366920938463463374607431768211455];
    if (Read(values, 1) != (uint128)-1 || values[0] != 0) { return 2; }
    int6[] small = [-32, 31, 33];
    if (small[0] != -32 || small[1] != 31 || small[2] != -31) { return 3; }
    int128 carray[2];
    carray[0] = -170141183460469231731687303715884105728;
    carray[1] = 170141183460469231731687303715884105727;
    if (carray[0] != (int128)-170141183460469231731687303715884105728 ||
        carray[1] != 170141183460469231731687303715884105727) { return 4; }
    int64 minimum = -9223372036854775808;
    int64 minusOne = -1;
    if (minimum / minusOne != minimum || minimum % minusOne != 0) { return 5; }
    int128 minWide = -170141183460469231731687303715884105728;
    int128 minusWide = -1;
    if (minWide / minusWide != minWide || minWide % minusWide != 0) { return 6; }
    return 0;
}
""")

    def test_unary_expressions_and_shift_counts(self):
        self.compile_run("""
int32 main() {
    int6 a = 10; int6 b = 3; int6 c = 2;
    if (a + -b * c != 4 || a - +b * c != 4) { return 1; }
    int6 n = 31;
    int6 previous = n++;
    if (previous != 31 || n != -32) { return 2; }
    int6 next = --n;
    if (next != 31 || n != 31) { return 3; }
    uint128 high = (uint128)1 << 100;
    if (!high) { return 4; }
    int6 negative = -32;
    if ((negative >> 6) != -1 || (negative << 6) != 0) { return 5; }
    if ((high >> high) != 0 || (high << high) != 0 || (negative >> high) != -1) { return 6; }
    return 0;
}
""")

    def test_ir_precision_and_legacy_backend_diagnostics(self):
        maximum = str((1 << 128) - 1)
        with tempfile.TemporaryDirectory(prefix="kairote-backends-") as directory:
            path = Path(directory)
            source = path / "test.krt"
            source.write_text(f"int32 main() {{ uint128 n = {maximum}; return n == (uint128)-1 ? 0 : 1; }}")
            for target in ("asm", "vm", "ir"):
                with self.subTest(target=target):
                    output = path / ("test." + target)
                    result = subprocess.run(
                        [str(COMPILER), str(source), "target", target, "output", str(output)],
                        cwd=path, capture_output=True, text=True, timeout=30,
                    )
                    if target == "ir":
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn(maximum, output.read_text())
                    else:
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn("extended integer widths require", result.stdout + result.stderr)

    def test_literals_larger_than_uint128_are_rejected(self):
        for literal in (str(1 << 128), "0x1" + "0" * 32, "0b1" + "0" * 128):
            with self.subTest(literal=literal), tempfile.TemporaryDirectory(prefix="kairote-invalid-") as directory:
                path = Path(directory)
                source = path / "invalid.krt"
                source.write_text(f"int32 main() {{ uint128 n = {literal}; return 0; }}")
                result = subprocess.run(
                    [str(COMPILER), str(source), "output", str(path / "invalid")],
                    cwd=path, capture_output=True, text=True, timeout=30,
                )
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("exceeds uint128", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
