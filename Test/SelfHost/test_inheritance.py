"""Single-class inheritance preserves layout, constructor order and runtime type."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import Bootstrap


class NativeInheritanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='krt-inheritance-')
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = Path(os.environ['SELFHOST_COMPILER']).resolve() if 'SELFHOST_COMPILER' in os.environ else cls.build.seed()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source, expected=0):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, expected, result.stderr.decode())

    def test_base_prefix_constructor_chain_and_upcasts(self):
        self.check('prefix-chain', '''
class Base {
    public int32 a=7;
    public function Base(int32 x){a+=x;}
    public int32 Get(){return a;}
}
class Middle : Base {
    public int32 b=a+1;
    public function Middle(int32 x):base(x){b+=2;}
}
class Leaf : Middle {
    public int32 c=b+1;
    public function Leaf(int32 x):base(x){c+=3;}
    public int32 ViaBase(){return base.Get();}
}
Base upcast(Leaf value){return value;}
int32 read(Base value){return value.a;}
int32 main(){Leaf value=new Leaf(5);Base root=upcast(value);Middle mid=value;
if(value.a!=12 || value.b!=15 || value.c!=19 || root.Get()!=12 || value.ViaBase()!=12){return 1;}
root.a=20;if(value.Get()!=20 || mid.a!=20 || read(value)!=20){return 2;}
if((int64)root!=(int64)value){return 3;}delete root;return 0;}
''')

    def test_implicit_default_base_constructor_and_hidden_members(self):
        self.check('implicit-base', '''
class Base { public int32 value=9; public function Base(){value++;} public int32 Read(){return value;} }
class Derived : Base { public int32 value=20; public int32 Sum(){return base.value+value;} public int32 Read(){return 99;} }
int32 main(){Derived d=new Derived();Base b=d;if(d.Sum()!=30 || d.Read()!=99 || b.Read()!=10){return 1;}delete b;return 0;}
''')

    def test_qualified_generic_base_layout_and_methods(self):
        self.check('generic-base', '''
namespace Models { class Box<T>{public T value;public function Box(T x){value=x;}public T Get(){return value;}} }
class Derived<U> : Models.Box<U> { public int32 marker=17; public function Derived(U x):base(x){} }
int32 main(){Derived<string> d=new Derived<string>("works");Models.Box<string> b=d;
if(b.Get()!="works" || d.value!="works" || d.marker!=17){return 1;}delete b;return 0;}
''')

    def test_actual_exception_type_survives_base_variables_and_rethrow(self):
        self.check('dynamic-exception', '''
class Base {public int32 code;public function Base(int32 n){code=n;}}
class Middle : Base {public function Middle(int32 n):base(n){}}
class Leaf : Middle {public function Leaf(int32 n):base(n){}}
void fail(Base error){throw error;}
int32 main(){int32 seen=0;Base error=new Leaf(42);
try{try{fail(error);}catch(Middle caught){if(caught.code!=42){return 1;}seen=1;throw;}finally{seen+=10;}}
catch(Leaf caught){if(caught.code!=42){return 2;}seen+=100;delete caught;}
if(seen!=111){return 3;}return 0;}
''')

    def test_base_exception_catch_and_standard_exception_constructor(self):
        library = (Path(__file__).resolve().parents[2] / 'libs/System/Exception.krt').read_text()
        self.check('system-exception', library + '''
namespace;
class DiskError : System.Exception {
    public int32 retry=3;
    public function DiskError(string message,int32 code):base(message,code){}
}
void fail(){System.Exception error=new DiskError("full",28);throw error;}
int32 main(){int32 caught=0;try{fail();}catch(System.Exception error){
if(error.Message!="full" || error.Code!=28 || error.ToString()!="full"){return 1;}caught++;delete error;}
if(caught!=1){return 2;}return 0;}
''')

    def test_catch_order_sibling_nonmatch_and_null_reference(self):
        self.check('catch-order', '''
class Base {} class A:Base{} class B:Base{}
int32 main(){int32 n=0;try{Base value=new A();throw value;}catch(B wrong){return 1;}catch(Base right){n=7;delete right;}catch(A later){return 2;}
if(n!=7){return 3;}try{Base empty=null;throw empty;}catch(Base wrong){return 4;}catch{n++;}if(n!=8){return 5;}return 0;}
''')

    def test_reject_invalid_inheritance_and_unsafe_reference_conversions(self):
        cases = {
            'cycle': 'class A:B{}class B:A{}int32 main(){return 0;}',
            'self-cycle': 'class A:A{}int32 main(){return 0;}',
            'scalar-base': 'class A:int32{}int32 main(){return 0;}',
            'array-base': 'class A{}class B:A[]{}int32 main(){return 0;}',
            'multiple-base': 'class A{}class B{}class C:A,B{}int32 main(){return 0;}',
            'missing-base-arguments': 'class A{public function A(int32 x){}}class B:A{}int32 main(){B b=new B();return 0;}',
            'base-on-ordinary-method': 'class A{public int32 f():base(){return 0;}}int32 main(){return 0;}',
            'base-on-root': 'class A{public function A():base(){}}int32 main(){return 0;}',
            'override': 'class A{public int32 f(){return 1;}}class B:A{public override int32 f(){return 2;}}int32 main(){return 0;}',
            'implicit-downcast': 'class A{}class B:A{}int32 main(){A a=new A();B b=a;return 0;}',
            'array-covariance': 'class A{}class B:A{}int32 main(){B[] b=new B[1];A[] a=b;return 0;}',
            'ref-covariance': 'class A{}class B:A{}void f(ref A a){}int32 main(){B b=new B();f(ref b);return 0;}',
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                work = Path(self.directory.name) / name
                work.mkdir()
                (work / 'program.krt').write_text(source)
                result = subprocess.run([self.compiler], cwd=work, capture_output=True, text=True, timeout=10)
                self.assertGreater(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse((work / 'stage1-probe.kro').exists())


if __name__ == '__main__':
    unittest.main()
