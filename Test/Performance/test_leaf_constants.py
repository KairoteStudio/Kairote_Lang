"""Fast integer returns preserve conversion order and tail accumulator values."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def wrap(value, bits, unsigned):
    value %= 1 << bits
    return value if unsigned or value < 1 << (bits - 1) else value - (1 << bits)


class LeafConstants(unittest.TestCase):
    def compile_run(self, source):
        assembly = {}
        with tempfile.TemporaryDirectory(prefix="kairote-leaf-constants-") as directory:
            path = Path(directory)
            code = path / "Program.krt"
            code.write_text(source)
            for level in (2, 3):
                binary = path / f"Program{level}"
                built = subprocess.run([str(COMPILER), f"-O{level}", str(code), "-o", str(binary)],
                                       cwd=path, capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr + source)
                self.assertNotIn("AddressSanitizer", built.stderr)
                self.assertNotIn("runtime error:", built.stderr)
                run = subprocess.run([str(binary)], capture_output=True, timeout=10)
                self.assertEqual(run.returncode, 0, f"O{level}: {run.returncode}\n{source}")
                assembly[level] = subprocess.check_output(
                    ["objdump", "-d", "-w", "-Mintel", str(binary)], text=True)
        return assembly

    def test_converted_literal_returns_before_frame(self):
        assembly = self.compile_run("""
int64 Constant(int32 n) { if (n <= 0) { return 1; } return n*3; }
int32 main() {
    if (Constant(-2147483648) != 1 || Constant(0) != 1 || Constant(7) != 21) { return 1; }
    unsafe(using krt.mem;) {
        let f: fn(int32) -> int64 = &Constant;
        if (f(-7) != 1 || f(3) != 9) { return 2; }
    }
    return 0;
}
""")
        body = re.search(r"<_ZN8ConstantEi>:\n(.*?)(?=^[0-9a-f]+ <(?:_ZN|main)|\Z)",
                         assembly[3], re.M | re.S)
        self.assertIsNotNone(body, assembly[3])
        leaf = body.group(1).split("ret", 1)[0]
        self.assertNotRegex(leaf, r"\b(push|pop|call|rsp|rbp)\b|\[")

    def test_source_then_destination_normalization_all_widths(self):
        for source_unsigned in (False, True):
            definitions, checks = [], []
            for source_bits in range(2, 129, 2):
                width_checks = []
                source_kind = ("uint" if source_unsigned else "int") + str(source_bits)
                literal = (1 << source_bits) - 1
                normalized = wrap(literal, source_bits, source_unsigned)
                for target_bits, target_unsigned in ((2, False), (30, True), (32, False),
                                                      (62, False), (64, False), (64, True)):
                    target = ("uint" if target_unsigned else "int") + str(target_bits)
                    name = f"Convert{source_bits}{target}"
                    expected = wrap(normalized, target_bits, target_unsigned)
                    definitions.append(f"{target} {name}(int32 n) {{ "
                                       f"if (n <= 0) {{ return ({source_kind}){literal}; }} "
                                       f"return {name}(n-1)+({target})1; }}")
                    for n in (0, 1, 7):
                        answer = wrap(expected + n, target_bits, target_unsigned)
                        width_checks.append(f"if ({name}({n}) != ({target}){answer}) {{ return 1; }}")
                definitions.append(f"int32 Check{source_bits}() {{\n" +
                                   "\n".join(width_checks) + "\nreturn 0; }")
                checks.append(f"if (Check{source_bits}() != 0) {{ return 1; }}")
            with self.subTest(source_unsigned=source_unsigned):
                self.compile_run("\n".join(definitions) + "\nint32 main() {\n" +
                                 "\n".join(checks) + "\nreturn 0; }")

    def test_neutral_tail_return_uses_accumulator(self):
        assembly = self.compile_run("""
uint64 Combine(uint64 n) { if (n <= 1) { return 1; } return n*Combine(n-1); }
int32 main() {
    if (Combine(0) != 1 || Combine(1) != 1 || Combine(5) != 120 ||
        Combine(20) != (uint64)2432902008176640000) { return 1; }
    return 0;
}
""")
        bodies = {}
        for level in (2, 3):
            body = re.search(r"<_ZN7CombineEL>:\n(.*?)(?=^[0-9a-f]+ <(?:_ZN|main)|\Z)",
                             assembly[level], re.M | re.S)
            self.assertIsNotNone(body, assembly[level])
            bodies[level] = body.group(1)
        self.assertEqual(len(re.findall(r"\bimul\b", bodies[3])), 1)
        self.assertGreater(len(re.findall(r"\bimul\b", bodies[2])), 1)


if __name__ == "__main__":
    unittest.main()
