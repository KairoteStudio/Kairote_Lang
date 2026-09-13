"""Recursive expansion preserves integer, branch and frame semantics."""
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def wrap(value, bits, unsigned=False):
    value %= 1 << bits
    return value if unsigned or value < 1 << (bits - 1) else value - (1 << bits)


class RecursiveInlining(unittest.TestCase):
    def compile_and_run(self, source, levels=(2, 3)):
        results = {}
        with tempfile.TemporaryDirectory(prefix="kairote-recursive-inline-") as directory:
            path = Path(directory)
            code = path / "Program.krt"
            code.write_text(source)
            for level in levels:
                binary = path / f"ProgramO{level}"
                built = subprocess.run([str(COMPILER), f"-O{level}", str(code), "-o", str(binary)],
                                       cwd=path, capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr + source)
                self.assertNotIn("AddressSanitizer", built.stderr)
                self.assertNotIn("runtime error:", built.stderr)
                run = subprocess.run([str(binary)], capture_output=True, timeout=10)
                self.assertEqual(run.returncode, 0, f"O{level}: {run.returncode}\n{source}")
                results[level] = subprocess.check_output(["objdump", "-d", "-w", "-Mintel", str(binary)], text=True)
        return results

    def test_different_recursive_steps_and_base_values(self):
        for step, base in ((2, 0), (3, 1), (4, 7)):
            def reference(n):
                return n + base if n <= 1 else reference(n - 1) + reference(n - step)
            checks = "\n".join(f"if (Walk({n}) != {reference(n)}) {{ return {n+8}; }}" for n in range(-7, 18))
            with self.subTest(step=step, base=base):
                self.compile_and_run(f"""
int64 Walk(int64 n) {{
    if (n <= 1) {{ return n+{base}; }}
    return Walk(n-1)+Walk(n-{step});
}}
int32 main() {{ {checks} return 0; }}
""")

    def test_integer_widths_and_overflow(self):
        for bits in (2, 6, 14, 30, 32, 34, 62, 64, 66, 94, 126, 128):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                value = wrap((1 << (bits-1)) - 1, bits, unsigned)
                def reference(n):
                    return value if n <= 0 else wrap(reference(n-1) + reference(n-3), bits, unsigned)
                checks = "\n".join(f"if (Walk({n}) != ({kind}){reference(n)}) {{ return {n+1}; }}" for n in range(12))
                with self.subTest(kind=kind):
                    self.compile_and_run(f"""
{kind} Walk(int32 n) {{ if (n <= 0) {{ return ({kind}){value}; }} return Walk(n-1)+Walk(n-3); }}
int32 main() {{ {checks} return 0; }}
""")

    def test_multiple_parameters_and_argument_evaluation(self):
        rng = random.Random(5193)
        def reference(n, a, b):
            if n <= 1:
                return wrap(a ^ b, 64)
            return wrap(reference(n-1, b, wrap(a+b, 64)) + reference(n-2, a, wrap(b-3, 64)), 64)
        checks = []
        for index in range(30):
            n, a, b = rng.randrange(0, 12), rng.randrange(-100, 100), rng.randrange(-100, 100)
            checks.append(f"if (Branches({n},{a},{b}) != {reference(n,a,b)}) {{ return {index+1}; }}")
        self.compile_and_run("""
int64 Branches(int32 n, int64 a, int64 b) {
    if (n <= 1) { return a ^ b; }
    return Branches(n-1,b,a+b)+Branches(n-2,a,b-3);
}
int32 main() { """ + "\n".join(checks) + " return 0; }")

    def test_rematerialized_arguments_wrap_at_declared_width(self):
        for bits in (2, 6, 30, 32, 34, 64, 66, 126, 128):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                start = wrap((1 << (bits-1))-1, bits, unsigned)
                def reference(n, value):
                    if n <= 0:
                        return value
                    return wrap(reference(n-1, wrap(value+1, bits, unsigned)) +
                                reference(n-2, wrap(value-3, bits, unsigned)), bits, unsigned)
                checks = "\n".join(f"if (Walk({n},({kind}){start}) != ({kind}){reference(n,start)}) {{ return {n+1}; }}"
                                   for n in range(9))
                with self.subTest(kind=kind):
                    self.compile_and_run(f"""
{kind} Walk(int32 n, {kind} value) {{
    if (n <= 0) {{ return value; }}
    return Walk(n-1,value+({kind})1)+Walk(n-2,value-({kind})3);
}}
int32 main() {{ {checks} return 0; }}
""")

    def test_rematerialized_constants_keep_source_signedness(self):
        constants = (("int2", -1), ("uint2", 3), ("int30", -536870912),
                     ("uint30", 1073741823), ("int32", -2147483648),
                     ("uint32", 4294967295), ("int64", -9223372036854775808))
        for target in ("int64", "uint64", "int128", "uint128"):
            bits = int(target.lstrip("uint"))
            unsigned = target.startswith("uint")
            for source_type, offset in constants:
                def reference(n, value):
                    if n <= 0:
                        return value
                    return wrap(reference(n-1, wrap(value+offset, bits, unsigned)) +
                                reference(n-3, wrap(value-offset, bits, unsigned)), bits, unsigned)
                checks = "\n".join(f"if (Walk({n},({target})7) != ({target}){reference(n,7)}) {{ return {n+1}; }}"
                                   for n in range(8))
                with self.subTest(target=target, source=source_type):
                    self.compile_and_run(f"""
{target} Walk(int32 n, {target} value) {{
    if (n <= 0) {{ return value; }}
    return Walk(n-1,value+({source_type}){offset})+Walk(n-3,value-({source_type}){offset});
}}
int32 main() {{ {checks} return 0; }}
""")

    def test_mixed_associative_and_nonassociative_returns(self):
        for operator in ("-", "*", "^", "|", "&"):
            def reference(n):
                if n <= 0:
                    return 3
                a, b = reference(n-1), reference(n-3)
                return wrap({"-": lambda: a-b, "*": lambda: a*b, "^": lambda: a^b,
                             "|": lambda: a|b, "&": lambda: a&b}[operator](), 32)
            checks = "\n".join(f"if (Fold({n}) != {reference(n)}) {{ return {n+1}; }}" for n in range(13))
            with self.subTest(operator=operator):
                self.compile_and_run(f"""
int32 Fold(int32 n) {{ if (n <= 0) {{ return 3; }} return Fold(n-1) {operator} Fold(n-3); }}
int32 main() {{ {checks} return 0; }}
""")

    def test_three_return_paths(self):
        def reference(n):
            if n < 0:
                return -4
            if n == 0:
                return 7
            return reference(n-1) + reference(n-3)
        checks = "\n".join(f"if (Branch({n}) != {reference(n)}) {{ return {n+6}; }}" for n in range(-5, 15))
        self.compile_and_run(f"""
int64 Branch(int64 n) {{
    if (n < 0) {{ return -4; }}
    if (n == 0) {{ return 7; }}
    return Branch(n-1)+Branch(n-3);
}}
int32 main() {{ {checks} return 0; }}
""")

    def test_side_effect_order_excluded(self):
        self.compile_and_run("""
static int64 trace=0;
int64 Visit(int64 n) {
    trace=trace*7+n;
    if (n <= 1) { return n; }
    return Visit(n-1)+Visit(n-2);
}
int32 main() { if (Visit(4) != 3) { return 1; } return trace == 25782386 ? 0 : 2; }
""")

    def test_distinct_frame_addresses_excluded(self):
        self.compile_and_run("""
int64 Check(int64 n, int64* parent) {
    unsafe(using krt.mem;) {
        int64 local=n;
        if (&local == parent) { return 1000000; }
        if (n <= 1) { return n; }
        return Check(n-1,&local)+Check(n-2,&local);
    }
}
int32 main() { unsafe(using krt.mem;) { int64 anchor=0; return Check(10,&anchor) == 55 ? 0 : 1; } }
""")

    def test_large_body_falls_back(self):
        operations = "".join(f" + (n & {i})" for i in range(1, 65))
        self.compile_and_run(f"""
int64 Large(int64 n) {{
    if (n <= 1) {{ return n; }}
    return Large(n-1)+Large(n-2){operations};
}}
int32 main() {{ return Large(2) == 65 ? 0 : 1; }}
""")

    def test_multiple_recursive_edges_respect_growth_budget(self):
        def reference(n):
            return n if n <= 1 else reference(n-1)+reference(n-2)+reference(n-3)+reference(n-4)
        checks = "\n".join(f"if (Fork({n}) != {reference(n)}) {{ return {n+1}; }}" for n in range(12))
        self.compile_and_run(f"""
int64 Fork(int32 n) {{ if (n <= 1) {{ return n; }} return Fork(n-1)+Fork(n-2)+Fork(n-3)+Fork(n-4); }}
int32 main() {{ {checks} return 0; }}
""")


if __name__ == "__main__":
    unittest.main()
