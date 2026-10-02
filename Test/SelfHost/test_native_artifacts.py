"""Verify native compiler targets with no language preprocessing in the harness."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("SELFHOST_COMPILER", ROOT / "build/selfhost/stage2/program")).resolve()
LINKER = Path(os.environ.get("ARKLINK", ROOT / "build/ArkLink/ArkLink")).resolve()
ASSEMBLER = shutil.which("as") or ("/usr/bin/as" if Path("/usr/bin/as").is_file() else None)
SYSTEM_LINKER = shutil.which("ld") or ("/usr/bin/ld" if Path("/usr/bin/ld").is_file() else None)


@unittest.skipUnless(COMPILER.is_file(), "build the native compiler first")
class NativeArtifactTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="krt native targets ")
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        self.source = self.work / "main.krt"
        self.source.write_text("int32 helper(int32 x){return x+3;} int32 main(){return helper(39);}")
        self.env = dict(os.environ, PATH="", KAIROTE_ROOT=str(ROOT))

    def command(self, *arguments, expected=0):
        result = subprocess.run([str(COMPILER), *map(str, arguments)], cwd=self.work,
                                env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_ir_without_linker_and_default_output(self):
        self.command(self.source, "target", "ir", "--linker", self.work / "absent linker")
        text = (self.work / "output.ir").read_text()
        self.assertIn("function ", text)
        self.assertIn("parameter", text)
        self.assertIn(":i64 =", text)
        self.assertNotIn("load.local", text)
        self.assertIn("call", text)
        self.assertIn("return", text)
        self.assertFalse((self.work / "main").exists())

    def test_show_ir_and_check_never_publish(self):
        output = self.work / "existing output"
        output.write_text("previous output")
        result = self.command(self.source, "--show-ir", "--check", "-o", output)
        self.assertIn("typed SSA IR", result.stdout)
        self.assertEqual(output.read_text(), "previous output")
        self.assertFalse((self.work / "main.kro").exists())

    def test_ir_prints_minimum_signed_integer(self):
        self.source.write_text("int32 main(){int64 n=-9223372036854775808;return (int32)n;}")
        output = self.work / "minimum.ir"
        self.command(self.source, "target", "ir", "-O1", "-o", output)
        self.assertIn("-9223372036854775808", output.read_text())

    @unittest.skipUnless(ASSEMBLER and SYSTEM_LINKER, "GNU assembler and linker required")
    def test_assembly_round_trip_and_native_memory(self):
        self.source.write_text('int32 main(){int32[] a=new int32[2];a[1]=42;'
                               'string s="native";int32 n=a[1];delete a;return s.Length==6?n:1;}')
        self.command(self.source, "target", "asm", "--linker", self.work / "absent linker")
        assembly = self.work / "output.asm"
        self.assertIn(".global _start", assembly.read_text())
        object_path, binary = self.work / "assembled.o", self.work / "assembled app"
        subprocess.run([ASSEMBLER, str(assembly), "-o", str(object_path)], check=True, capture_output=True)
        subprocess.run([SYSTEM_LINKER, str(object_path), "-o", str(binary)], check=True, capture_output=True)
        result = subprocess.run([str(binary)], env=self.env, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 42, result.stderr)

    @unittest.skipUnless(LINKER.is_file(), "ArkLink required")
    def test_eo_and_keep_temp_preserve_real_objects(self):
        self.command(self.source, "target", "eo", "--linker", LINKER)
        output = self.work / "output.exe"
        self.assertEqual(output.read_bytes()[:4], b"\x7fELF")
        object_path = self.work / "main.kro"
        self.assertEqual(object_path.read_bytes()[:4], b"KRO\0")
        result = subprocess.run([str(output)], env=self.env, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 42)
        self.command(self.source, "--keep-temp", "--linker", LINKER, "-o", self.work / "other app")
        self.command(object_path, "--linker", LINKER, "-o", self.work / "linked object")
        result = subprocess.run([str(self.work / "linked object")], env=self.env, timeout=10)
        self.assertEqual(result.returncode, 42)

    def test_vm_target_is_nonempty_runnable_bytecode(self):
        output = self.work / "program.ebc"
        self.command(self.source, "target", "vm", "--linker", self.work / "absent linker", "-o", output)
        self.assertEqual(output.read_bytes()[:4], b"CBSE")
        self.assertEqual(output.read_bytes(), (self.work / "program.ebc.ebc").read_bytes())
        self.assertFalse(output.stat().st_mode & 0o111)
        self.command("run-vm", output, expected=42)

    def test_target_failures_preserve_destination(self):
        output = self.work / "output.ir"
        output.write_text("previous output")
        self.source.write_text("int32 main(){return missing;}")
        self.command(self.source, "target", "ir", "-o", output, expected=1)
        self.assertEqual(output.read_text(), "previous output")
        self.command(self.source, "target", "unknown", expected=1)


if __name__ == "__main__":
    unittest.main()
