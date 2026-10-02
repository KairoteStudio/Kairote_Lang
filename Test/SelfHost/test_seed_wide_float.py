"""The C seed must use both integer limbs for IEEE conversion and round trips."""
import subprocess
import tempfile
from pathlib import Path
import unittest

from Test.SelfHost.Bootstrap import KRTC


@unittest.skipUnless(KRTC.is_file(), 'build the C seed first')
class SeedWideFloatTests(unittest.TestCase):
    def test_signed_unsigned_wide_and_uint64_high_float_conversions(self):
        source = '''
float64 signed_double(int128 value) { return (float64)value; }
float32 unsigned_single(uint128 value) { return (float32)value; }
int128 signed_integer(float64 value) { return (int128)value; }
uint128 unsigned_integer(float32 value) { return (uint128)value; }
int32 main() {
    uint128 large=(uint128)1<<64;
    if((float64)large!=18446744073709551616.0 || (uint128)(float64)large!=large) { return 1; }
    large=(uint128)1<<100;
    if((uint128)unsigned_single(large)!=large || unsigned_integer((float32)large)!=large) { return 2; }
    int128 negative=-((int128)1<<90);
    if(signed_integer(signed_double(negative))!=negative) { return 3; }
    if((float64)(uint64)-1!=18446744073709551616.0 || (float32)(uint64)-1!=18446744073709551616.0) { return 4; }
    if((uint128)(float64)(uint128)-1!=0) { return 5; }
    float64 infinity=1.0/0.0; float64 nan=0.0/0.0;
    if((int128)infinity!=0 || (uint128)nan!=0 || (int128)0.5!=0) { return 6; }
    if((int128)-3.75!=-3 || (uint128)-3.75!=(uint128)-3) { return 7; }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='kairote-seed-wide-float-') as directory:
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
