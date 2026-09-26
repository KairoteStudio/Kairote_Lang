"""Integer math contracts at every compiler optimization level."""
import math
import random
import unittest

from Support import LibraryTestCase


def literal(value, kind):
    return f"({kind}){value}"


def wrap(value, bits):
    value %= 1 << bits
    return value if value < 1 << (bits - 1) else value - (1 << bits)


class MathLibrary(LibraryTestCase):
    def program(self, statements):
        return "using System;\nint32 main() {\n" + "\n".join(statements) + "\nreturn 0;\n}\n"

    def test_absolute_sign_min_max_and_clamp(self):
        checks = []
        for kind, bits in (("int32", 32), ("int64", 64)):
            low, high = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
            checks.append(f"{kind} result{bits} = 99;")
            for value in (low, low + 1, -1, 0, 1, high):
                value_text = literal(value, kind)
                checks.append(f"if (MathOps.Abs({value_text}) != {literal(wrap(abs(value), bits), kind)}) {{ return 1; }}")
                checks.append(f"if (MathOps.Sign({value_text}) != {(value > 0) - (value < 0)}) {{ return 2; }}")
                valid = value != low
                checks.append(f"if (MathOps.TryAbs({value_text}, ref result{bits}) != {'true' if valid else 'false'} || result{bits} != {literal(abs(value) if valid else 0, kind)}) {{ return 3; }}")
            checks += [
                f"if (MathOps.Min({literal(low, kind)}, {literal(high, kind)}) != {literal(low, kind)} || MathOps.Max({literal(low, kind)}, {literal(high, kind)}) != {literal(high, kind)}) {{ return 4; }}",
                f"if (MathOps.Clamp(({kind})-7, ({kind})-2, ({kind})3) != -2 || MathOps.Clamp(({kind})7, ({kind})-2, ({kind})3) != 3) {{ return 5; }}",
                f"if (!MathOps.TryClamp(({kind})7, ({kind})3, ({kind})3, ref result{bits}) || result{bits} != 3) {{ return 6; }}",
                f"if (MathOps.TryClamp(({kind})7, ({kind})3, ({kind})-2, ref result{bits}) || result{bits} != 0) {{ return 7; }}",
                f"result{bits} = -7; if (!MathOps.TryAbs(result{bits}, ref result{bits}) || result{bits} != 7) {{ return 8; }}",
            ]
        self.run_source(self.program(checks))

    def test_mixed_width_arguments_choose_a_non_narrowing_overload(self):
        self.run_source('''
using System.Math;
public static class MixedWidth {
    public static int64 Select(int64 a, int64 b, int64 c) { return a + b + c; }
    public static int32 Select(int32 a, int32 b, int32 c) { return a + b + c; }
    public static int64 Run(int64 value) { return Select(value, 1, 2); }
}
int32 main() {
    int64 value = 5000000000;
    if (MathOps.Max(value, 1) != value || MathOps.Max(1, value) != value) { return 1; }
    if (MathOps.Min(-value, 1) != -value || MathOps.Min(1, -value) != -value) { return 2; }
    if (MathOps.Clamp(value, 0, 100) != 100 || MathOps.Clamp(-value, -100, 0) != -100) { return 3; }
    int64 result = 0;
    if (!MathOps.TryClamp(value, 0, 100, ref result) || result != 100) { return 4; }
    if (MixedWidth.Run(value) != value + 3 || MixedWidth.Select(1, value, 2) != value + 3) { return 5; }
    bool choose = true;
    if (MathOps.Max(choose ? value : 1, 0) != value) { return 6; }
    choose = false;
    if (MathOps.Max(choose ? value : 1, 0) != 1) { return 7; }
    return 0;
}
''')

    def test_square_roots_at_boundaries_and_random_values(self):
        randomizer = random.Random(205)
        checks = []
        for kind, bits in (("int32", 32), ("int64", 64)):
            high = (1 << (bits - 1)) - 1
            root = math.isqrt(high)
            values = {0, 1, 2, 3, 4, 8, 9, 15, 16, 17, high, high - 1,
                      root * root, root * root - 1}
            values.update(randomizer.randrange(high + 1) for _ in range(40))
            checks.append(f"{kind} result{bits} = 99;")
            for value in sorted(values):
                checks.append(f"if (MathOps.Sqrt({literal(value, kind)}) != {literal(math.isqrt(value), kind)}) {{ return 1; }}")
            checks += [
                f"if (MathOps.Sqrt(({kind})-1) != 0 || MathOps.TrySqrt(({kind})-1, ref result{bits}) || result{bits} != 0) {{ return 2; }}",
                f"if (!MathOps.TrySqrt(({kind})0, ref result{bits}) || result{bits} != 0) {{ return 3; }}",
                f"if (!MathOps.TrySqrt({literal(high, kind)}, ref result{bits}) || result{bits} != {literal(root, kind)}) {{ return 4; }}",
            ]
        self.run_source(self.program(checks))

    def test_wrapped_and_checked_powers(self):
        checks = []
        for kind, bits in (("int32", 32), ("int64", 64)):
            low, high = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
            checks.append(f"{kind} result{bits} = 99;")
            for base in (low, -46341, -3, -2, -1, 0, 1, 2, 3, 46341, high):
                for exponent in (0, 1, 2, 3, 7, 31, 63):
                    expected = base ** exponent
                    valid = low <= expected <= high
                    checks.append(f"if (MathOps.Pow({literal(base, kind)}, {exponent}) != {literal(wrap(expected, bits), kind)}) {{ return 1; }}")
                    checks.append(f"if (MathOps.TryPow({literal(base, kind)}, {exponent}, ref result{bits}) != {'true' if valid else 'false'} || result{bits} != {literal(expected if valid else 0, kind)}) {{ return 2; }}")
            checks += [
                f"if (MathOps.Pow(({kind})2, -1) != 1 || MathOps.TryPow(({kind})2, -1, ref result{bits}) || result{bits} != 0) {{ return 3; }}",
                f"if (MathOps.Pow(({kind})-1, 2147483647) != -1 || MathOps.Pow(({kind})0, 2147483647) != 0) {{ return 4; }}",
                f"if (!MathOps.TryPow(({kind})1, 2147483647, ref result{bits}) || result{bits} != 1) {{ return 5; }}",
            ]
        self.run_source(self.program(checks))

    def test_gcd_lcm_signed_extremes_and_primality(self):
        checks = []
        for kind, bits in (("int32", 32), ("int64", 64)):
            low, high = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
            checks.append(f"{kind} result{bits} = 99;")
            pairs = [(0, 0), (0, -7), (12, 18), (-12, 18), (-12, -18),
                     (low, low), (low, 0), (low, -1), (low, 2), (high, 2),
                     (46341, 46349), (high, high)]
            for a, b in pairs:
                gcd = math.gcd(a, b)
                lcm = math.lcm(a, b)
                valid = lcm <= high
                wrapped_lcm = wrap(abs(wrap(lcm, bits)), bits)
                arguments = f"{literal(a, kind)}, {literal(b, kind)}"
                checks += [
                    f"if (MathOps.GCD({arguments}) != {literal(wrap(gcd, bits), kind)}) {{ return 1; }}",
                    f"if (MathOps.LCM({arguments}) != {literal(wrapped_lcm, kind)}) {{ return 2; }}",
                    f"if (MathOps.TryLCM({arguments}, ref result{bits}) != {'true' if valid else 'false'} || result{bits} != {literal(lcm if valid else 0, kind)}) {{ return 3; }}",
                ]
        for value, prime in [(-1, False), (0, False), (1, False), (2, True), (3, True),
                             (4, False), (49, False), (2147483646, False), (2147483647, True)]:
            checks.append(f"if (MathOps.IsPrime({value}) != {'true' if prime else 'false'}) {{ return 4; }}")
        self.run_source(self.program(checks))


if __name__ == "__main__":
    unittest.main()
