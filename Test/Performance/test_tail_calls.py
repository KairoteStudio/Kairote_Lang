"""General self-tail-call elimination: semantics, lifetime exclusions and depth."""
import operator
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def wrap(value, bits, unsigned):
    value %= 1 << bits
    return value if unsigned or value < 1 << (bits - 1) else value - (1 << bits)


class TailCalls(unittest.TestCase):
    def run_source(self, source, stack_limit=None, level=None):
        with tempfile.TemporaryDirectory(prefix="kairote-tail-") as directory:
            path = Path(directory)
            code, binary = path / "Test.krt", path / "Test"
            code.write_text(source)
            flags = [f"-O{level}"] if level is not None else []
            compiled = subprocess.run([str(COMPILER), *flags, str(code), "output", str(binary)],
                                      cwd=path, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr + source)
            self.assertNotIn("runtime error:", compiled.stderr)
            self.assertNotIn("AddressSanitizer", compiled.stderr)

            def limits():
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
                if stack_limit is not None:
                    resource.setrlimit(resource.RLIMIT_STACK, (stack_limit, stack_limit))

            result = subprocess.run([str(binary)], capture_output=True, timeout=10, preexec_fn=limits)
            self.assertEqual(result.returncode, 0, f"exit={result.returncode}\n{source}\n{result.stderr!r}")
            return subprocess.check_output(["objdump", "-d", "-w", "-Mintel", str(binary)], text=True)

    def function_body(self, assembly, symbol):
        match = re.search(r"^([0-9a-f]+) <" + re.escape(symbol) + r">:\n", assembly, re.M)
        self.assertIsNotNone(match, assembly)
        rest = assembly[match.end():]
        end = re.search(r"^[0-9a-f]+ <(?!__krt_bb_)[^>]+>:\n", rest, re.M)
        return rest[:end.start()] if end else rest

    def test_tail_sum_and_parameter_swap_use_bounded_stack(self):
        assembly = self.run_source("""
int64 Sum(int64 n, int64 acc) {
    if (n <= 0) { return acc; }
    return Sum(n - 1, acc + n);
}
int64 Rotate(int64 n, int64 a, int64 b, int64 c) {
    if (n <= 0) { return a*100 + b*10 + c; }
    return Rotate(n-1, b, c, a);
}
int32 main() {
    if (Sum(200000, 0) != 20000100000 || Rotate(200000,1,2,3) != 312) { return 1; }
    return 0;
}
""", stack_limit=256*1024)
        for symbol in ("_ZN3SumEll", "_ZN6RotateEllll"):
            body = self.function_body(assembly, symbol)
            self.assertNotRegex(body, r"\bcall\b")
            branches = re.findall(r"^\s*([0-9a-f]+):.*\bj[a-z]+\s+([0-9a-f]+)\s", body, re.M)
            self.assertTrue(any(int(target,16) < int(address,16) for address,target in branches), body)

    def test_associative_returns_with_unrelated_names_and_inputs(self):
        source = """
int32 Branches(int32 n) {
    if (n <= 0) { return 1; }
    return Branches(n-1) + Branches(n-3);
}
int64 SumRight(int64 n) { if (n <= 0) { return 0; } return SumRight(n-1) + n; }
uint64 Product(uint64 n) { if (n <= 1) { return 1; } return n * Product(n-1); }
int32 main() {
    if (SumRight(200000) != 20000100000 || Product(20) != (uint64)2432902008176640000 ||
        Branches(20) != 2745) { return 1; }
    return 0;
}
"""
        assembly = self.run_source(source, stack_limit=256*1024)
        # Inlining can increase static call sites while retaining tail backedges.
        # Keep the old exact count only at O2, whose pass set has no inlining.
        for symbol in ("_ZN8BranchesEi", "_ZN8SumRightEl", "_ZN7ProductEL"):
            body = self.function_body(assembly, symbol)
            branches = re.findall(r"^\s*([0-9a-f]+):.*\bj[a-z]+\s+([0-9a-f]+)\s", body, re.M)
            self.assertTrue(any(int(target,16) < int(address,16) for address,target in branches), body)
        baseline = self.run_source(source, stack_limit=256*1024, level=2)
        body = self.function_body(baseline, "_ZN8BranchesEi")
        self.assertEqual(len(re.findall(r"\bcall\b", body)), 1)

    def test_all_integer_widths_associative_wrap_and_identity(self):
        operations = [("Add", "+", operator.add, 0), ("Mul", "*", operator.mul, 1),
                      ("And", "&", operator.and_, -1), ("Or", "|", operator.or_, 0),
                      ("Xor", "^", operator.xor, 0)]
        for bits in range(2, 129, 2):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                high = (1 << (bits if unsigned else bits-1)) - 1
                value = high - (1 if bits > 2 else 0)
                definitions, checks = [], []
                for name, symbol, operation, identity in operations:
                    definitions.append(f"{kind} {name}(int32 n, {kind} v) {{ "
                                       f"if (n == 0) {{ return ({kind}){identity}; }} "
                                       f"return v {symbol} {name}(n-1, ({kind})(v+({kind})1)); }}")
                    expected = wrap(identity, bits, unsigned)
                    for index in reversed(range(9)):
                        expected = wrap(operation(wrap(value+index,bits,unsigned),expected),bits,unsigned)
                    checks.append(f"if ({name}(9,({kind}){value}) != ({kind}){expected}) {{ return 1; }}")
                    checks.append(f"if ({name}(0,({kind}){value}) != ({kind}){wrap(identity,bits,unsigned)}) {{ return 2; }}")
                with self.subTest(type=kind):
                    self.run_source("\n".join(definitions) + "\nint32 main() {\n" + "\n".join(checks) + "\nreturn 0; }")

    def test_wide_and_stack_parameters_copy_in_parallel(self):
        self.run_source("""
int128 Rotate(int32 n, int128 a, int128 b, int128 c, int128 d, int128 e, int128 f, int128 g) {
    if (n == 0) { return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g; }
    return Rotate(n-1, b, c, d, e, f, g, a);
}
int32 main() {
    if (Rotate(10003, (int128)18446744073709551617, 2, 3, 4, 5, 6, 7) !=
        (int128)18446744073709551756) { return 1; }
    return 0;
}
""", stack_limit=256*1024)

    def test_packed_argument_cycles_and_float_tail_calls(self):
        self.run_source("""
int32 Cycle(int32 n, int2 a, int30 b, uint32 c) {
    if (n == 0) { return (int32)a + (int32)b + (int32)c; }
    return Cycle(n-1, (int2)b, (int30)c, (uint32)a);
}
float64 Step(int32 n, float64 a, float64 b) {
    if (n == 0) { return a + b; }
    return Step(n-1, b, a+0.5);
}
int32 main() {
    if (Cycle(30000,-1,-1,4294967295) != -3 || Step(20000, 1.25, 2.5) != 10003.75) { return 1; }
    return 0;
}
""", stack_limit=256*1024)

    def test_pre_call_side_effects_keep_order(self):
        self.run_source("""
int64 Walk(int32 n, ref int64 count) {
    count = count*3 + n;
    if (n == 0) { return count; }
    return Walk(n-1, ref count);
}
int32 main() {
    int64 counter = 0;
    if (Walk(8,ref counter) != 73812 || counter != 73812) { return 1; }
    return 0;
}
""")

    def test_mixed_operators_and_mutual_recursion(self):
        self.run_source("""
int64 Mixed(int64 n) {
    if (n <= 0) { return 2; }
    if (n % 2 == 0) { return n + Mixed(n-1); }
    return n * Mixed(n-1);
}
int32 Even(int32 n) { if (n == 0) { return 1; } return Odd(n-1); }
int32 Odd(int32 n) { if (n == 0) { return 0; } return Even(n-1); }
int32 main() {
    if (Mixed(8) != 610 || Even(20) != 1 || Odd(20) != 0) { return 1; }
    return 0;
}
""")

    def test_post_call_effects_nonassociative_and_float_fallback(self):
        self.run_source("""
int32 Difference(int32 n) { if (n <= 0) { return 7; } return n - Difference(n-1); }
float64 Floating(int32 n) { if (n == 0) { return 1.0; } return 0.1 + Floating(n-1); }
int64 After(int32 n, ref int64 count) {
    if (n == 0) { return count; }
    int64 value = After(n-1,ref count);
    count = count*10+n;
    return value + count;
}
int32 main() {
    int64 counter = 0;
    if (Difference(9) != -2 || Floating(2) != 1.2000000000000002 ||
        After(4,ref counter) != 1370 || counter != 1234) { return 1; }
    return 0;
}
""")

    def test_stack_lifetimes_are_not_reused(self):
        source = """
int32 Addressed(int32 n, int32* parent) { unsafe(using krt.mem;) {
    if (n == 0) { return *parent; }
    int32 x = n;
    if (&x == parent) { return -100; }
    return Addressed(n-1, &x);
} }
int32 Stack(int32 n, int32* parent) { unsafe(using krt.mem;) {
    if (n == 0) { return *parent; }
    var p = stackalloc int32[1];
    *p = n;
    if (p == parent) { return -100; }
    return Stack(n-1,p);
} }
int32 main() { unsafe(using krt.mem;) {
    int32 marker = 99;
    if (Addressed(20,&marker) != 1 || Stack(20,&marker) != 1) { return 1; }
} return 0; }
"""
        assembly = self.run_source(source)
        # The frame addresses are observable, so actual calls must remain.
        self.assertGreaterEqual(len(re.findall(r"\bcall\b", assembly)), 5)

    def test_renaming_does_not_change_machine_instructions(self):
        bodies = []
        for name in ("Alpha", "Bravo"):
            assembly = self.run_source(f"""
int64 {name}(int64 n) {{ if (n <= 0) {{ return 0; }} return n + {name}(n-1); }}
int32 main() {{ if ({name}(123) != 7626) {{ return 1; }} return 0; }}
""")
            body = self.function_body(assembly, f"_ZN5{name}El")
            # Extract actual bytes; relocation displacements are unchanged for a rename.
            rows = re.findall(r"^\s*[0-9a-f]+:\s+((?:[0-9a-f]{2}\s+)+)", body, re.M)
            self.assertTrue(rows)
            bodies.append([bytes.fromhex(row) for row in rows])
        self.assertEqual(bodies[0], bodies[1])

    def test_global_reads_after_call_and_real_conversions_are_preserved(self):
        assembly = self.run_source("""
static int64 counter = 0;
int64 Observe(int32 n) {
    if (n == 0) { return 1; }
    counter++;
    return Observe(n-1) + counter;
}
int8 Shrink(int32 n) { if (n == 0) { return 1; } return (int8)(Shrink(n-1) + 127); }
int32 Implicit(int32 n) { if (n > 0) { return n + Implicit(n-1); } }
int32 main() {
    if (Observe(4) != 17 || counter != 4 || Shrink(10) != -9 || Implicit(10) != 55) { return 1; }
    return 0;
}
""")
        for symbol in ("_ZN7ObserveEi", "_ZN6ShrinkEi"):
            self.assertRegex(self.function_body(assembly, symbol), r"\bcall\b")

    def test_boolean_cast_after_recursive_call_is_not_an_identity_copy(self):
        self.run_source("""
bool Read(int32 n, bool* p) { unsafe(using krt.mem;) {
    if (n == 0) { return *p; }
    return (bool)Read(n-1,p);
} }
int32 main() { unsafe(using krt.mem;) {
    var p = stackalloc byte[1];
    *p = 3;
    if ((int32)Read(2,(bool*)p) != 1) { return 1; }
} return 0; }
""")


if __name__ == "__main__":
    unittest.main()
