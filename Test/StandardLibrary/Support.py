"""Build and execute the library through the user's normal using/import path."""
import os
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()
LEVELS = tuple(int(value) for value in os.environ.get("KRT_STDLIB_TEST_LEVELS", "0,1,2,3").split(","))


def disable_core_dump():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class LibraryTestCase(unittest.TestCase):
    def run_source(self, source, expected_stdout=None, levels=None, input_data=None, extra_libraries=None):
        results = {}
        with tempfile.TemporaryDirectory(prefix="kairote-stdlib-") as directory:
            work = Path(directory)
            shutil.copytree(ROOT / "libs", work / "libs", ignore=shutil.ignore_patterns("*.kro", "*.o"))
            for name, content in (extra_libraries or {}).items():
                target = work / "libs" / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content)
            source_path = work / "test_library.krt"
            source_path.write_text(source)
            for level in LEVELS if levels is None else levels:
                with self.subTest(optimization=level):
                    binary = work / f"ProgramO{level}"
                    built = subprocess.run([str(COMPILER), f"-O{level}", str(source_path), "-o", str(binary)],
                                           cwd=work, capture_output=True, text=True, timeout=60)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr + source)
                    self.assertNotIn("AddressSanitizer", built.stderr)
                    self.assertNotIn("runtime error:", built.stderr)
                    result = subprocess.run([str(binary)], input=input_data, capture_output=True,
                                            timeout=15, preexec_fn=disable_core_dump)
                    self.assertEqual(result.returncode, 0, f"O{level}: {result.returncode}\n{result.stderr!r}\n{source}")
                    if expected_stdout is not None:
                        expected = expected_stdout.encode() if isinstance(expected_stdout, str) else expected_stdout
                        self.assertEqual(result.stdout, expected)
                    results[level] = result
        return results
