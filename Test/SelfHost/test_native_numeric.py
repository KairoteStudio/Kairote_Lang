"""Execute typed integer programs so binding, lowering, and extension agree."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.bootstrap import Bootstrap


class NativeNumericTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="krt-native-numeric-")
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = cls.build.seed()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, f"{name}: branch {result.returncode}; {result.stderr!r}")

    def test_signed_narrow_locals_arrays_and_pointers(self):
        self.check("signed-storage", """
int32 main() {
    int8 a=-7; int16 b=-30000;
    if(a!=-7 || b!=-30000 || a>=0 || b>=0) { return 1; }
    int8[] bytes=new int8[2]; int16[] words=new int16[2];
    bytes[1]=-7; words[1]=-30000;
    if(bytes[1]!=-7 || words[1]!=-30000) { return 2; }
    unsafe(using krt.mem;) {
        int8* p=stackalloc int8[2]; int16* q=stackalloc int16[2];
        p[0]=-7; q[0]=-30000;
        if(*p!=-7 || q[0]!=-30000) { return 3; }
    }
    return 0;
}
""")

    def test_unsigned_dword_storage_and_references(self):
        self.check("unsigned-storage", """
class Box { public uint32 n; public int8 small; }
void bump(ref uint32 n) { n+=1; }
int32 main() {
    uint32 n=4000000000; uint32[] values=new uint32[2];
    Box box=new Box(); values[1]=n; box.n=n; box.small=-7;
    if(n!=4000000000 || n<0 || values[1]!=n || box.n!=n || box.small!=-7) { return 1; }
    bump(ref n); bump(ref values[1]); bump(ref box.n);
    if(n!=4000000001 || values[1]!=n || box.n!=n) { return 2; }
    return 0;
}
""")

    def test_casts_parameter_and_return_widths(self):
        self.check("cast-call-return", """
int8 narrow(int32 value) { return (int8)value; }
uint32 widen(uint32 value) { return value; }
int32 main() {
    if(narrow(255)!=-1 || narrow(249)!=-7) { return 1; }
    if(widen((uint32)-1)!=4294967295) { return 2; }
    int16 small=(int16)35536;
    if(small!=-30000) { return 3; }
    return 0;
}
""")

    def test_unsigned_64_division_shift_and_comparison(self):
        self.check("unsigned64", """
int32 main() {
    uint64 high=(uint64)-1;
    if(high<=1 || 1>=high) { return 1; }
    if(high/2!=9223372036854775807 || high%2!=1) { return 2; }
    if((high>>1)!=9223372036854775807) { return 3; }
    high/=2;
    if(high!=9223372036854775807) { return 4; }
    return 0;
}
""")

    def test_integer_promotions_and_expression_overflow(self):
        self.check("integer-promotions", """
class Box { public int32 n; }
int32 main() {
    int32 n=2147483647;
    if(n+1>=0) { return 1; }
    Box b=new Box(); b.n=n;
    if(b.n+1>=0) { return 2; }
    byte zero=0; byte one=1;
    if(zero-one!=-1 || -one!=-1) { return 3; }
    int32 a=1; uint32 large=4000000000;
    int64 first=a+large; int64 second=large+a;
    if(first!=4000000001 || second!=4000000001) { return 4; }
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
