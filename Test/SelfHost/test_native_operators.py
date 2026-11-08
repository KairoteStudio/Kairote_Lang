"""Longest-match operators preserve storage widths and target evaluation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import Bootstrap


class NativeOperatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="krt-native-operators-")
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = (Path(os.environ["SELFHOST_COMPILER"]).resolve()
                        if "SELFHOST_COMPILER" in os.environ else cls.build.seed())

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_bitwise_shift_compound_and_signed_unsigned_storage(self):
        self.check("compound-values", """
int32 main(){int32 n=5;n|=10;n&=6;n^=3;n<<=2;n>>=1;if(n!=10){return 1;}
int32 negative=-16;negative>>=2;if(negative!=-4){return 2;}
uint64 high=(uint64)-1;high>>=63;if(high!=1){return 3;}
byte small=128;small<<=1;if(small!=0){return 4;}
uint128 wide=1;wide<<=100;wide>>=98;wide|=3;wide&=6;wide^=2;
if(wide!=4){return 5;}return 0;}
""")

    def test_compound_target_and_subscript_are_evaluated_once(self):
        self.check("compound-targets", """
class Box{public int32 n=7;}Box target(Box value,ref int32 calls){calls++;return value;}
int32 index(ref int32 calls){calls++;return 0;}
int32 main(){int32 calls=0;int32[] values=[3];Box b=new Box();
values[index(ref calls)]<<=3;target(b,ref calls).n|=8;
if(calls!=2 || values[0]!=24 || b.n!=15){return 1;}delete values;delete b;return 0;}
""")

    def test_assignment_and_compound_for_increments_continue_correctly(self):
        self.check("for-increments", """
int32 main(){int32 total=0;for(int32 i=0;i<7;i+=2){if(i==2){continue;}total+=i;}
if(total!=10){return 1;}total=0;for(int32 i=0;i<7;i=i+3){total+=i;}
if(total!=9){return 2;}total=0;for(int32 i=1;i<=8;i<<=1){total++;}
if(total!=4){return 3;}return 0;}
""")

    def test_logical_operators_bind_below_bitwise_operators(self):
        self.check("logical-precedence", """
int32 main(){if((1|2&&0)!=0 || (1&&0|2)!=1 || (0||1&2)!=0){return 1;}
if((1^3&&1)!=1 || (1|2==2)!=1){return 2;}return 0;}
""")

    def test_pointer_depth_dereference_and_compound_stride(self):
        self.check("pointer-depth", """
int32 main(){unsafe(using krt.mem;){int32* values=stackalloc int32[2];values[0]=17;values[1]=29;
int32* p=values;int32** pp=&p;int32*** ppp=&pp;
if(**pp!=17 || ***ppp!=17){return 1;}**pp=42;if(values[0]!=42){return 2;}
p+=1;if(**pp!=29 || p!=values+1){return 3;}p-=1;if(***ppp!=42){return 4;}
return 0;}}
""")


if __name__ == "__main__":
    unittest.main()
