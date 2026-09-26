import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
KRTC = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()

class LexerTests(unittest.TestCase):
    def run_parts(self, parts):
        with tempfile.TemporaryDirectory(prefix="krt-lexer-") as directory:
            work = Path(directory)
            source = work / "LexerTest.krt"
            source.write_text("\n".join((ROOT / path).read_text() for path in parts))
            for level in range(4):
                binary = work / f"LexerO{level}"
                build = subprocess.run([str(KRTC), f"-O{level}", str(source), "output", str(binary)], cwd=work, capture_output=True, text=True, timeout=60)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                self.assertEqual(subprocess.run([str(binary)], capture_output=True, timeout=10).returncode, 0, f"O{level}")

    def test_native_tokens(self):
        self.run_parts(("SelfHost/Frontend/Lexer/Token.krt", "SelfHost/Frontend/Lexer/Lexer.krt", "Test/SelfHost/test_lexer.krt"))

    def test_object_parameter_fields(self):
        self.run_parts(("Test/SelfHost/test_object_fields.krt",))

if __name__ == "__main__": unittest.main()
