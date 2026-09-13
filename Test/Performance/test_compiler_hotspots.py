"""Build compiler component checks and inspect generated native code."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def build_harness(output, replacements=None):
    project = ROOT / "Re.KrtC"
    sources = re.findall(r'"([^"]+\.c)"', (project / "build.zig").read_text())
    sources = [str(project / source) for source in sources if source != "src/Main.c"]
    for original, replacement in (replacements or {}).items():
        sources = [str(replacement) if path.endswith(original) else path for path in sources]
    includes = ["src", "src/Core", "src/Tools", "src/Bytecode", "Shared", "stub_include", "vm",
                "src/compiler/Driver", "src/compiler/Frontend/Semantic"]
    command = [os.environ.get("CC", "cc"), "-std=gnu11", "-O2", "-w", "-ffunction-sections", "-fdata-sections"]
    command += ["-I" + str(project / path) for path in includes]
    command += [str(ROOT / "Test/Performance/test_compiler_hotspots.c"), *sources,
                "-L" + str(ROOT / "ArkLink/build"), "-larklink", "-lm", "-lpthread",
                "-Wl,--gc-sections", "-o", str(output)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


class CompilerHotspots(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_directory = tempfile.TemporaryDirectory(prefix="kairote-hotspot-build-")
        cls.harness = Path(cls.build_directory.name) / "checks"
        build_harness(cls.harness)

    @classmethod
    def tearDownClass(cls):
        cls.build_directory.cleanup()

    def check_mode(self, mode):
        with tempfile.TemporaryDirectory(prefix="kairote-hotspot-case-") as directory:
            path = Path(directory) / "library"
            path.mkdir()
            result = subprocess.run([str(self.harness), mode, str(path)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_preprocessor_boundaries_and_index_growth(self):
        self.check_mode("preprocessor")

    def test_symbol_shadowing_growth_and_nested_scopes(self):
        self.check_mode("symbols")

    def test_stdlib_cache_isolation_invalidation_and_threads(self):
        self.check_mode("cache")

    def test_small_files_reuse_pipeline_without_starting_threads(self):
        self.check_mode("parallel_small")

    def test_large_files_use_parallel_batches(self):
        self.check_mode("parallel_large")


@unittest.skipUnless(shutil.which("objdump"), "objdump is required for machine-code checks")
class NativeCodeSize(unittest.TestCase):
    def compile_run(self, source, expected_exit=0):
        with tempfile.TemporaryDirectory(prefix="kairote-native-size-") as directory:
            path = Path(directory)
            code, binary = path / "test.krt", path / "test"
            code.write_text(source)
            compiled = subprocess.run([str(COMPILER), str(code), "output", str(binary)],
                                      cwd=path, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, expected_exit, result.stderr)
            return subprocess.run(["objdump", "-d", "-Mintel", str(binary)],
                                  capture_output=True, text=True, check=True).stdout

    def test_single_epilogue_and_single_process_exit(self):
        assembly = self.compile_run("int32 main() { return 17; }", 17)
        self.assertEqual(len(re.findall(r"\bpop\s+rbp\b", assembly)), 1, assembly)
        self.assertEqual(len(re.findall(r"\bsyscall\b", assembly)), 1, assembly)
        self.assertIn("ret", assembly)

    def test_zero_uses_short_encoding(self):
        assembly = self.compile_run("int32 main() { return 0; }")
        self.assertRegex(assembly, r"31 c0\s+xor\s+eax,eax")
        self.assertNotRegex(assembly, r"movabs\s+rax,0x0\b")

    def test_multiple_returns_share_epilogue(self):
        assembly = self.compile_run("""
int32 Pick(int32 n) {
    if (n == 1) { return 3; }
    if (n == 2) { return 4; }
    return 5;
}
int32 main() {
    if (Pick(1) != 3 || Pick(2) != 4 || Pick(0) != 5) { return 1; }
    return 0;
}
""")
        self.assertEqual(len(re.findall(r"\bpop\s+rbp\b", assembly)), 2, assembly)

    def test_parameters_and_expression_temporaries_avoid_stack_roundtrips(self):
        assembly = self.compile_run("""
int64 Sum(int64 a, int64 b) { return (a+b)*(a-b)+(a^b); }
int32 main() { return Sum(9,4) == 78 ? 0 : 1; }
""")
        body = assembly.split("<_ZN3SumEll>:", 1)[1].split("<main>:", 1)[0]
        memory = [line for line in body.splitlines() if "[rbp-" in line]
        self.assertTrue(memory)
        for line in memory:
            self.assertRegex(line, r"(rbx|r12|r13|r14|r15)\b", line)
        self.assertRegex(body, r"mov\s+(rbx|r12|r13|r14|r15),rdi")
        self.assertRegex(body, r"mov\s+(rbx|r12|r13|r14|r15),rsi")

    def test_scalar_integers_avoid_unused_high_word_and_single_field_merges(self):
        assembly = self.compile_run("""
int32 Fib(int32 n) {
    if (n <= 1) { return n; }
    return Fib(n - 1) + Fib(n - 2);
}
int32 main() {
    if (Fib(20) != 6765 || Fib(-1) != -1) { return 1; }
    return 0;
}
""")
        body = assembly.split("<_ZN3FibEi>:", 1)[1].split("<main>:", 1)[0]
        self.assertNotRegex(body, r"\bsar\s+(rdx|r8),0x3f\b")
        self.assertNotRegex(body, r"\bmovabs\s+r11,0xffffffff00000000\b")


if __name__ == "__main__":
    unittest.main()
