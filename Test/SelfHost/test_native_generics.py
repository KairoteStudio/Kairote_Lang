"""Monomorphized classes and functions retain concrete types across call boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.bootstrap import Bootstrap


class NativeGenericTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='krt-native-generics-')
        cls.build=Bootstrap(Path(cls.directory.name))
        cls.compiler=Path(os.environ['SELFHOST_COMPILER']).resolve() if 'SELFHOST_COMPILER' in os.environ else cls.build.seed()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self,name,source,expected=0):
        binary,_=self.build.compile(self.compiler,source,name)
        result=subprocess.run([binary],capture_output=True,timeout=10)
        self.assertEqual(result.returncode,expected,result.stderr.decode())

    def test_class_multiple_instantiations_and_constructor(self):
        self.check('class-instances','''
class Box<T>{public T value;public Box(T x){value=x;}public T Get(){return value;}public void Set(T x){value=x;}}
int32 main(){Box<int32> a=new Box<int32>(17);Box<string> b=new Box<string>("hello");Box<float64> c=new Box<float64>(2.5);
if(a.Get()!=17 || b.Get()!="hello" || c.Get()!=2.5){return 1;}
a.Set(42);if(a.Get()!=42 || b.Get()!="hello"){return 2;}delete a;delete b;delete c;return 0;}
''')

    def test_function_explicit_inferred_recursive_and_return_type(self):
        self.check('generic-functions','''
T identity<T>(T value){return value;}
T descend<T>(T value,int32 depth){if(depth==0){return identity<T>(value);}return descend<T>(value,depth-1);}
int32 main(){if(identity<int32>(17)!=17 || identity("text")!="text" || descend<float64>(2.5,3)!=2.5){return 1;}return 0;}
''')

    def test_nested_class_type_identity(self):
        self.check('nested-class','''
class Box<T>{T value;public Box(T x){value=x;}public T Get(){return value;}}
int32 main(){Box<int32> inner=new Box<int32>(42);Box<Box<int32>> outer=new Box<Box<int32>>(inner);
Box<int32> returned=outer.Get();if(returned.Get()!=42){return 1;}delete outer;delete inner;return 0;}
''')

    def test_generic_references(self):
        self.check('generic-ref','''
void swap<T>(ref T a,ref T b){T temporary=a;a=b;b=temporary;}
int32 main(){int32 a=17;int32 b=42;swap(ref a,ref b);if(a!=42 || b!=17){return 1;}
string left="one";string right="two";swap<string>(ref left,ref right);if(left!="two" || right!="one"){return 2;}return 0;}
''')

    def test_generic_array_parameters_returns_and_foreach(self):
        self.check('generic-arrays','''
T first<T>(T[] values){return values[0];}
T[] pair<T>(T left,T right){return [left,right];}
int32 main(){int32[] values=pair<int32>(17,42);string[] names=pair("one","two");
if(first(values)!=17 || first<string>(names)!="one"){return 1;}
int32 total=0;foreach(var value in values){total+=value;}if(total!=59){return 2;}delete values;delete names;return 0;}
''')

    def test_null_arguments_do_not_invent_an_inferred_type(self):
        self.check('generic-null-inference', '''
T first<T>(T left,T right){return left;}
T second<T>(T left,T right){return right;}
T identity<T>(T value){return value;}
int32 main(){if(first(null,"value")!=null || second(null,"value")!="value"){return 1;}
if(first("value",null)!="value" || second("value",null)!=null){return 2;}
if(identity<string>(null)!=null){return 3;}return 0;}
''')

    def test_generic_class_field_initializers(self):
        self.check('generic-field-initializers','''
class Box<T>{public T value=default(T);public int32 marker=7;public Box(T x){value=x;}public T Get(){return value;}}
int32 main(){Box<int32> n=new Box<int32>(42);Box<string> s=new Box<string>("ok");if(n.Get()!=42 || s.Get()!="ok" || n.marker!=7){return 1;}delete n;delete s;return 0;}
''')

    def test_generic_method_on_generic_class(self):
        self.check('generic-method','''
class Box<T>{public T value;public Box(T x){value=x;}public U Echo<U>(U x){return x;}public T Get(){return value;}}
int32 main(){Box<int32> b=new Box<int32>(17);if(b.Echo<string>("ok")!="ok" || b.Get()!=17){return 1;}delete b;return 0;}
''')

    def test_static_generic_class_calls(self):
        self.check('generic-static', '''
class Box<T>{public static T Identity(T value){return value;}}
int32 main(){if(Box<int32>.Identity(17)!=17 || Box<string>.Identity("ok")!="ok"){return 1;}return 0;}
''')

    def test_qualified_static_generic_class_calls(self):
        self.check('qualified-generic-static', '''
namespace N {class Box<T>{public static T Identity(T value){return value;}}}
int32 main(){if(N.Box<int32>.Identity(42)!=42 || N.Box<float64>.Identity(2.5)!=2.5){return 1;}return 0;}
''')

    def test_generic_wide_integer_value_and_ref(self):
        self.check('generic-wide','''
class Box<T>{public T value;public Box(T x){value=x;}public T Get(){return value;}}
T identity<T>(T x){return x;}
void assign<T>(ref T destination,T value){destination=value;}
int32 main(){uint128 number=(uint128)18446744073709551625;Box<uint128> b=new Box<uint128>(number);
uint128 result=identity(b.Get());if(result!=number){return 1;}assign<uint128>(ref result,(uint128)7);if(result!=7 || b.Get()!=number){return 2;}delete b;return 0;}
''')

    def test_nonstandard_integer_precision_and_reference_identity(self):
        self.check('generic-integer-precision', '''
class Box<T>{public T value;public Box(T x){value=x;}public T Get(){return value;}public void Bump(){value++;}}
T copy<T>(T value){return value;}
void swap<T>(ref T left,ref T right){T temporary=left;left=right;right=temporary;}
void bump<T>(ref T value){value++;}
T first<T>(T[] values){return values[0];}
int32 main(){
Box<int30> small=new Box<int30>((int30)536870911);Box<int32> large=new Box<int32>(1073741823);
Box<uint30> usmall=new Box<uint30>((uint30)1073741823);Box<uint32> ularge=new Box<uint32>((uint32)1073741823);
small.Bump();large.Bump();usmall.Bump();ularge.Bump();
if(small.Get()!=-536870912 || large.Get()!=1073741824 || usmall.Get()!=0 || ularge.Get()!=1073741824){return 1;}
int30 a=copy((int30)536870911);int32 b=copy((int32)1073741823);if(a!=536870911 || b!=1073741823){return 2;}
int30 other=-7;swap<int30>(ref a,ref other);if(a!=-7 || other!=536870911){return 3;}
uint30 unsigned_other=copy<uint30>((uint30)1073741823);bump(ref unsigned_other);if(unsigned_other!=0){return 4;}
int30[] numbers=[(int30)536870911];bump<int30>(ref numbers[0]);if(first(numbers)!=-536870912){return 5;}
uint30[] unsigned_numbers=[(uint30)1073741823];if(first(unsigned_numbers)!=1073741823){return 6;}
delete small;delete large;delete usmall;delete ularge;delete numbers;delete unsigned_numbers;return 0;}
''')

    def test_invalid_instantiation_and_reference_diagnostics(self):
        cases={
            'arity':'class Box<T>{T value;}int32 main(){Box<int32,string> b=new Box<int32,string>();return 0;}',
            'incompatible-instances':'class Box<T>{T value;}int32 main(){Box<int32> b=new Box<string>();return 0;}',
            'inconsistent-inference':'T select<T>(T a,T b){return a;}int32 main(){return select(1,"bad");}',
            'null-inference':'T identity<T>(T value){return value;}int32 main(){identity(null);return 0;}',
            'reference-precision-mismatch':'void set<T>(ref T x,T v){x=v;}int32 main(){int32 x=0;set<int30>(ref x,(int30)1);return 0;}',
            'inferred-precision-mismatch':'T pick<T>(T a,T b){return a;}int32 main(){int30 a=1;int32 b=2;return (int32)pick(a,b);}',
            'reference-mismatch':'void set<T>(ref T x,T v){x=v;}int32 main(){int32 x=0;set<int64>(ref x,1);return 0;}',
        }
        for name,source in cases.items():
            with self.subTest(name=name):
                work=Path(self.directory.name)/name;work.mkdir();(work/'program.krt').write_text(source)
                result=subprocess.run([self.compiler],cwd=work,capture_output=True,text=True,timeout=10)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertFalse((work/'stage1-probe.kro').exists())


if __name__=='__main__':unittest.main()
