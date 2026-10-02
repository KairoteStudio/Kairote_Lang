"""Binary literals use the same signed and 128-bit parser as decimal/hex."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import Bootstrap


class BinaryLiteralTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="krt-binary-literals-")
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = (Path(os.environ["SELFHOST_COMPILER"]).resolve()
                        if "SELFHOST_COMPILER" in os.environ else cls.build.seed())

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_values_arithmetic_enums_and_wide_limbs(self):
        source = """
enum Bits{First=0b1,Second=0B10,Mask=0b1111}
int32 main(){if(0b10101!=21 || -0B10101!=-21 || 0b0000!=0){return 1;}
if((0b1010|0b0101)!=15 || 0b1000>>0b10!=2 || Bits.Mask!=15){return 2;}
uint64 low=0bLOW;uint128 high=0BONE;
uint128 maximum=0bMAX;
if(low!=18446744073709551615 || high!=18446744073709551616){return 3;}
if(maximum!=(uint128)-1 || high+0b1!=18446744073709551617){return 4;}
switch(0b10){case 0b1:return 5;case 0B10:break;default:return 6;}return 0;}
""".replace("LOW", "1" * 64).replace("ONE", "1" + "0" * 64).replace("MAX", "1" * 128)
        binary, _ = self.build.compile(self.compiler, source, "binary-values")
        result = subprocess.run([binary], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_missing_invalid_digits_suffix_and_overflow_are_rejected(self):
        for literal in ("0b", "0B", "0b2", "0b102", "0b10name", "0B11_01", "0b" + "1" * 129):
            with self.subTest(literal=literal):
                work = Path(self.directory.name) / str(len(list(Path(self.directory.name).iterdir())))
                work.mkdir()
                (work / "program.krt").write_text(f"int32 main(){{return {literal};}}")
                result = subprocess.run([self.compiler], cwd=work,
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse((work / "stage1-probe.kro").exists())


if __name__ == "__main__":
    unittest.main()
