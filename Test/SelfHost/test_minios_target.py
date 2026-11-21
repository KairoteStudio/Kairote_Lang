"""Executable target tests. Missing tools are a failed prerequisite, never a pass."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class MiniOSTargetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = Path(os.environ.get("SELFHOST_COMPILER", ROOT / "build/selfhost/stage2/program")).resolve()
        cls.linker = Path(os.environ.get("ARKLINK", ROOT / "build/ArkLink/ArkLink")).resolve()
        for tool in (cls.compiler, cls.linker):
            if not tool.is_file():
                raise RuntimeError(f"Build the native compiler and linker first: {tool}")

    def compile(self, source, platform="minios-x64", optimization=2):
        temporary = tempfile.TemporaryDirectory(prefix="krt-minios-target-")
        self.addCleanup(temporary.cleanup)
        work = Path(temporary.name)
        (work / "program.krt").write_text(source)
        result = subprocess.run([str(self.compiler), "--platform", platform,
            f"-O{optimization}", "-c", "program.krt", "-o", "program.kro"],
            cwd=work, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        obj = work / "program.kro"
        self.assertTrue(obj.is_file())
        binary = work / "program"
        linked = subprocess.run([str(self.linker), str(obj), "--target", "elf", "-o", str(binary)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(linked.returncode, 0, linked.stdout + linked.stderr)
        return binary

    def test_all_native_exit_allocation_and_syscall_paths(self):
        programs = {
            "constant": "int32 main(){return 42;}",
            "helper": "int32 answer(){return 42;} int32 main(){return answer();}",
            "arguments": "int32 add(int32 a,int32 b){return a+b;} int32 main(){return add(20,22);}",
            "allocation": "int32 main(){int32[] a=new int32[8];a[0]=42;int32 n=a[0];delete a;return n;}",
            "raw_syscall": "int32 main(){return (int32)syscall(49,0,4096,3,9,-1,0);}",
            "exception": "int32 main(){throw 42;}",
            "lazy_global": "int32 value(){return 42;}static int32 a=value();int32 main(){return a;}",
        }
        for optimization in (0, 2):
            for name, source in programs.items():
                with self.subTest(optimization=optimization, program=name):
                    binary = self.compile(source, optimization=optimization)
                    assembly = subprocess.check_output(["objdump", "-d", str(binary)], text=True)
                    self.assertNotRegex(assembly, r"\bsyscall\b")
                    self.assertRegex(assembly, r"int\s+\$0x80")

    def test_linux_stays_executable(self):
        binary = self.compile("int32 main(){return __krt_target_minios()?1:42;}", "linux-x64")
        binary.chmod(0o755)
        result = subprocess.run([str(binary)], timeout=10)
        self.assertEqual(result.returncode, 42)

    def test_minios_intrinsic_is_compile_time_boolean(self):
        binary = self.compile("int32 main(){return __krt_target_minios()?42:1;}")
        assembly = subprocess.check_output(["objdump", "-d", str(binary)], text=True)
        self.assertNotRegex(assembly, r"\bsyscall\b")
        self.assertRegex(assembly, r"int\s+\$0x80")

    def test_unknown_or_missing_platform_is_diagnosed(self):
        for arguments in (["--platform"], ["--platform", "unknown-x64"]):
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(self.compiler), *arguments], capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("E_OPTION", result.stdout + result.stderr)

if __name__ == "__main__":
    unittest.main()
