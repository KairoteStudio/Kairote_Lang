"""Expired source scopes must retain distinct, correctly typed IR storage."""
import subprocess
import tempfile
from pathlib import Path
import unittest

from Test.SelfHost.Bootstrap import KRTC, ROOT


@unittest.skipUnless(KRTC.is_file(), 'build the C seed first')
class SeedScopedStorageTests(unittest.TestCase):
    def test_float_storage_is_distinct_after_branch_scope_exit(self):
        # The unmodified interpreter helper declares float32 result inside its
        # narrow branch and float64 result afterwards. Reusing their IR name
        # changed the exact 2.25 payload from 1074790400 to 1074790402.
        source = (ROOT / 'SelfHost/Backend/Vm/Floating.krt').read_text() + '''
float64 siblings(int32 branch) {
    if(branch==0) { float32 value=1.25; value+=1.0; return (float64)value; }
    if(branch==1) { int64 value=17; value+=9; return (float64)value; }
    float64 value=7.5; return value;
}
int32 main() {
    if(KrtVmFloatBinary(1067450368,1065353216,43,4)!=1074790400) { return 1; }
    if(KrtVmFloatBinary(4608308318706860032,4607182418800017408,43,8)!=4612248968380809216) { return 2; }
    if(siblings(0)!=2.25 || siblings(1)!=26.0 || siblings(2)!=7.5) { return 3; }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='kairote-seed-scoped-storage-') as directory:
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
