"""Expired source scopes must retain distinct, correctly typed IR storage."""
import subprocess
import tempfile
from pathlib import Path
import unittest

from Test.SelfHost.Bootstrap import KRTC, ROOT


@unittest.skipUnless(KRTC.is_file(), 'build the C seed first')
class SeedScopedStorageTests(unittest.TestCase):
    def test_float_storage_is_distinct_after_branch_scope_exit(self):
        # The narrow branch declares a float32 result and the later scope a
        # float64 result; reusing their IR name changed the exact 2.25 payload
        # from 1074790400 to 1074790402.
        source = '''
float64 KrtFloatValue(int64 bits, int32 width) {
    unsafe(using krt.mem;) {
        if (width == 4) { int32 narrow = (int32)bits; return (float64)*(float32*)&narrow; }
        return *(float64*)&bits;
    }
}
int64 KrtFloatBits(float64 value, int32 width) {
    unsafe(using krt.mem;) {
        if (width == 4) { float32 narrow = (float32)value; return (int64)*(uint32*)&narrow; }
        return *(int64*)&value;
    }
}
int64 KrtFloatBinary(int64 left, int64 right, int32 operation, int32 width) {
    float64 a = KrtFloatValue(left,width); float64 b = KrtFloatValue(right,width);
    if (width == 4) {
        float32 x = (float32)a; float32 y = (float32)b; float32 result = 0.0;
        if (operation == 43) { result = x + y; } else if (operation == 45) { result = x - y; }
        else if (operation == 42) { result = x * y; } else if (operation == 47) { result = x / y; } else { result = x % y; }
        return KrtFloatBits((float64)result,width);
    }
    float64 result = 0.0;
    if (operation == 43) { result = a + b; } else if (operation == 45) { result = a - b; }
    else if (operation == 42) { result = a * b; } else if (operation == 47) { result = a / b; } else { result = a % b; }
    return KrtFloatBits(result,width);
}
float64 siblings(int32 branch) {
    if(branch==0) { float32 value=1.25; value+=1.0; return (float64)value; }
    if(branch==1) { int64 value=17; value+=9; return (float64)value; }
    float64 value=7.5; return value;
}
int32 main() {
    if(KrtFloatBinary(1067450368,1065353216,43,4)!=1074790400) { return 1; }
    if(KrtFloatBinary(4608308318706860032,4607182418800017408,43,8)!=4612248968380809216) { return 2; }
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
