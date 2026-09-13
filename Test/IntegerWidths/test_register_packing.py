"""Native register packing, isolation, and calling-convention regression tests."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_integer_widths import COMPILER, wrap


class RegisterPacking(unittest.TestCase):
    def compile_run(self, source, disassemble=False):
        with tempfile.TemporaryDirectory(prefix="kairote-registers-") as directory:
            path = Path(directory)
            code = path / "test.krt"
            binary = path / "test"
            code.write_text(source)
            compiled = subprocess.run(
                [str(COMPILER), str(code), "output", str(binary)],
                cwd=path, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr + "\n" + source)
            result = subprocess.run([str(binary)], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 0, f"check/exit {result.returncode}\n{source}\n{result.stderr!r}")
            if disassemble:
                return subprocess.run(
                    ["objdump", "-d", "-Mintel", str(binary)],
                    capture_output=True, text=True, check=True, timeout=10,
                ).stdout

    @unittest.skipUnless(shutil.which("objdump"), "objdump is needed to check physical registers")
    def test_four_values_share_one_physical_register(self):
        assembly = self.compile_run("""
int32 main() {
    int2 a = -1;
    int30 b = 536870911;
    uint6 c = 63;
    uint26 d = 67108863;
    a = 1;
    if (b != 536870911 || c != 63 || d != 67108863) { return 1; }
    b += 1;
    if (a != 1 || b != -536870912 || c != 63 || d != 67108863) { return 2; }
    c += 1;
    if (a != 1 || b != -536870912 || c != 0 || d != 67108863) { return 3; }
    d = 123;
    if (a != 1 || b != -536870912 || c != 0 || d != 123) { return 4; }
    return 0;
}
""", disassemble=True)
        writes = re.findall(r"\bor\s+(rbx|r12|r13|r14|r15),r10\b", assembly)
        self.assertGreaterEqual(len(writes), 8, assembly)
        self.assertEqual(len(set(writes)), 1, assembly)
        clear_masks = re.findall(r"\bmovabs\s+r11,0x([0-9a-f]+)", assembly)
        widths = {((~int(mask, 16)) & ((1 << 64) - 1)).bit_count() for mask in clear_masks}
        self.assertTrue({2, 30, 6, 26}.issubset(widths), assembly)

    def test_all_partial_widths_preserve_neighbors(self):
        for bits in range(2, 128, 2):
            if bits == 64:
                continue
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                other_bits = 64 - bits % 64
                other_kind = "uint" + str(other_bits)
                maximum = (1 << (bits if unsigned else bits - 1)) - 1
                other_maximum = (1 << other_bits) - 1
                wrapped = wrap(maximum + 1, bits, unsigned)
                with self.subTest(type=kind, neighbor=other_kind):
                    self.compile_run(f"""
int32 main() {{
    {kind} value = {maximum};
    {other_kind} neighbor = {other_maximum};
    value += 1;
    if (value != ({kind}){wrapped} || neighbor != ({other_kind}){other_maximum}) {{ return 1; }}
    neighbor = ~neighbor;
    if (neighbor != 0 || value != ({kind}){wrapped}) {{ return 2; }}
    value = ({kind})-1;
    if (neighbor != 0 || value != ({kind})-1) {{ return 3; }}
    neighbor = {other_maximum};
    if (neighbor != ({other_kind}){other_maximum} || value != ({kind})-1) {{ return 4; }}
    return 0;
}}
""")

    @staticmethod
    def recursive_function():
        declarations = "\n".join(f"uint30 local{i} = {i * 12345 + 67};" for i in range(14))
        checks = "\n".join(
            f"if (local{i} != {i * 12345 + 67}) {{ return {i + 1}; }}" for i in range(14)
        )
        return f"""
int32 Recurse(int32 depth) {{
    {declarations}
    if (depth == 0) {{ return 0; }}
    int32 inner = Recurse(depth - 1);
    if (inner != 0) {{ return 20; }}
    {checks}
    return 0;
}}
"""

    def test_register_pressure_loops_and_recursive_calls(self):
        count = 180  # More than the five registers can hold even at two bits each.
        declarations = "\n".join(f"uint2 value{i} = {i % 4};" for i in range(count))
        updates = "\n".join(f"value{i} += 1;" for i in range(count))
        checks = "\n".join(f"if (value{i} != {(i + 5) % 4}) {{ return {i + 1}; }}" for i in range(count))
        self.compile_run(self.recursive_function() + f"""
int32 main() {{
    {declarations}
    for (int32 iteration = 0; iteration < 5; iteration++) {{
        {updates}
        if (Recurse(3) != 0) {{ return 200; }}
    }}
    {checks}
    return 0;
}}
""")

    def test_mixed_parameters_and_wide_high_fragments(self):
        self.compile_run(self.recursive_function() + """
bool Check(int2 a, int30 b, uint66 c, int6 d, int126 e, uint2 f, uint10 g, int32 tail) {
    if (Recurse(3) != 0) { return false; }
    return a == -1 && b == -536870912 && c == (uint66)36893488147419103237 &&
        d == -32 && e == (int126)-42535295865117307932921825928971026432 &&
        f == 3 && g == 1023 && tail == 98765;
}
int32 main() {
    if (!Check(-1, -536870912, 36893488147419103237, -32,
               -42535295865117307932921825928971026432, 3, 1023, 98765)) { return 1; }
    return 0;
}
""")

    def test_long_names_and_shadowed_variables(self):
        name = "packed_integer_with_a_long_identifier_" * 3
        self.compile_run(f"""
int32 main() {{
    int2 {name}a = -1;
    int30 {name}b = 123456;
    {{
        int6 {name}a = -32;
        {name}a += 1;
        if ({name}a != -31 || {name}b != 123456) {{ return 1; }}
    }}
    if ({name}a != -1 || {name}b != 123456) {{ return 2; }}
    {name}b += 1;
    if ({name}a != -1 || {name}b != 123457) {{ return 3; }}
    return 0;
}}
""")


if __name__ == "__main__":
    unittest.main()
