"""The real C seed must support native compiler functions beyond old temp limits."""
import subprocess
import tempfile
from pathlib import Path
import unittest

from Test.SelfHost.Bootstrap import KRTC


@unittest.skipUnless(KRTC.is_file(), 'build the C seed first')
class SeedFunctionStorageTests(unittest.TestCase):
    def test_more_than_4096_temporaries_preserve_results(self):
        source = ('int64 calculate(int64 input){' + 'input=input+1;' * 5000 +
                  'return input;}int32 main(){return calculate(7)==5007 ? 0 : 1;}')
        with tempfile.TemporaryDirectory(prefix='kairote-seed-function-storage-') as directory:
            work = Path(directory)
            code, program = work / 'program.krt', work / 'program'
            code.write_text(source)
            for level in (0, 2, 3):
                with self.subTest(level=level):
                    result = subprocess.run([str(KRTC), f'-O{level}', str(code), 'output', str(program)],
                                            cwd=work, capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(program)], capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_long_string_literal_preserves_escapes_and_final_bytes(self):
        content = 'a' * 5000 + r'\n\t\\\"' + 'b' * 5000 + 'TAIL'
        source = 'int32 main(){string value="' + content + '";syscall(1,1,(int64)value,10008,0,0,0);return 0;}'
        expected = b'a' * 5000 + b'\n\t\\"' + b'b' * 5000 + b'TAIL'

        with tempfile.TemporaryDirectory(prefix='kairote-seed-long-string-') as directory:
            work = Path(directory)
            code, program = work / 'program.krt', work / 'program'
            code.write_text(source)
            for level in (0, 2, 3):
                with self.subTest(level=level):
                    result = subprocess.run([str(KRTC), f'-O{level}', str(code), 'output', str(program)],
                                            cwd=work, capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(program)], capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, expected)
