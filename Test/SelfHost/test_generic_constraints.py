"""Where clauses validate actual type arguments before native specialization."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.bootstrap import Bootstrap


class GenericConstraintTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='krt-generic-constraints-')
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = (Path(os.environ['SELFHOST_COMPILER']).resolve()
                        if 'SELFHOST_COMPILER' in os.environ else cls.build.seed())

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source, expected=0):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, expected, result.stderr.decode())

    def test_reference_constraint_class_string_and_array(self):
        self.check('reference', '''
class Box { public int32 n=17; }
class Holder<T> where T:class { public T value; public Holder(T x){value=x;} }
T identity<T>(T x) where T:class {return x;}
int32 main(){Box b=new Box();Holder<Box> h=new Holder<Box>(b);
if(identity(h.value).n!=17 || identity<string>("ok")!="ok"){return 1;}
int32[] values=[17];if(identity<int32[]>(values)[0]!=17){return 2;}
delete values;delete h;delete b;return 0;}
''')

    def test_value_constraints_preserve_float_and_integer_precision(self):
        self.check('value', '''
T sum<T>(T a,T b) where T:struct {return a+b;}
T copy<T>(T value) where T:unmanaged {return value;}
int32 main(){int30 a=536870911;int30 b=1;
if(sum<int30>(a,b)!=(int30)-536870912 || sum<float64>(1.5,2.25)!=3.75){return 1;}
uint128 wide=(uint128)18446744073709551623;if(copy(wide)!=wide){return 2;}return 0;}
''')

    def test_new_constraint_constructs_class_and_zero_values(self):
        self.check('factory', '''
class Box{public int32 n=17;}
T create<T>() where T:new(){return new T();}
int32 main(){Box b=create<Box>();if(b.n!=17){return 1;}
if(create<int32>()!=0 || create<float64>()!=0.0 || create<uint128>()!=(uint128)0){return 2;}
delete b;return 0;}
''')

    def test_named_base_constraint_and_parameter_dependency(self):
        self.check('base', '''
class Base{public int32 value=17;}
class Child:Base{}
T identity<T>(T value) where T:Base {return value;}
T first<T,U>(T value,U other) where T:U {return value;}
int32 main(){Child child=new Child();Base parent=child;
if(identity(child).value!=17 || first<Child,Base>(child,parent).value!=17){return 1;}
delete child;return 0;}
''')

    def test_constrained_static_initializer_constructs_each_concrete_type_once(self):
        self.check('static-factory', '''
static int32 calls=0;
class First{public int32 id;public First(){calls++;id=calls;}}
class Second{public int32 id;public Second(){calls++;id=calls;}}
class Factory<T> where T:class,new(){public static T value=new T();}
int32 main(){First first=Factory<First>.value;Second second=Factory<Second>.value;
if(first.id!=1 || second.id!=2 || calls!=2 || Factory<First>.value!=first){return 1;}
delete first;delete second;return 0;}
''')

    def test_method_constraints_can_reference_outer_type_argument(self):
        self.check('method', '''
class Base{public int32 n=17;}class Child:Base{}
class Holder<T> where T:class {public U Echo<U>(U value) where U:T{return value;}}
int32 main(){Holder<Base> h=new Holder<Base>();Child child=new Child();
if(h.Echo<Child>(child).n!=17){return 1;}delete child;delete h;return 0;}
''')

    def test_named_constraint_uses_declaration_namespace(self):
        self.check('namespace', '''
namespace A{class Base{public int32 n=17;}class Child:Base{}
T identity<T>(T value) where T:Base{return value;}}
namespace B{class Base{}}
int32 main(){A.Child value=new A.Child();if(A.identity<A.Child>(value).n!=17){return 1;}delete value;return 0;}
''')

    def test_invalid_arguments_and_constraint_declarations(self):
        cases = {
            'class-value': 'class H<T> where T:class{} int32 main(){H<int32> x=new H<int32>();return 0;}',
            'struct-reference': 'T id<T>(T x) where T:struct{return x;} int32 main(){id("bad");return 0;}',
            'unmanaged-reference': 'class A{} T id<T>(T x) where T:unmanaged{return x;} int32 main(){id(new A());return 0;}',
            'new-arity': 'class A{public A(int32 n){}} T make<T>() where T:new(){return new T();} int32 main(){make<A>();return 0;}',
            'new-array': 'T make<T>() where T:new(){return new T();} int32 main(){make<int32[]>();return 0;}',
            'unrelated-base': 'class A{} class B{} T id<T>(T x) where T:A{return x;} int32 main(){id(new B());return 0;}',
            'dependent-mismatch': 'class A{} class B{} T id<T,U>(T x) where T:U{return x;} int32 main(){id<A,B>(new A());return 0;}',
            'void': 'class H<T> where T:struct{} int32 main(){H<void> x=new H<void>();return 0;}',
            'unknown-parameter': 'class H<T> where U:class{} int32 main(){return 0;}',
            'duplicate-parameter': 'class H<T,T>{} int32 main(){return 0;}',
            'duplicate-clause': 'class H<T> where T:class where T:new(){} int32 main(){return 0;}',
            'contradictory': 'class H<T> where T:class,struct{} int32 main(){return 0;}',
            'duplicate-kind': 'class H<T> where T:class,class{} int32 main(){return 0;}',
            'missing-new-parens': 'class H<T> where T:new{} int32 main(){return 0;}',
            'recursive-constraint': 'class H<T> where T:H<T>{} int32 main(){H<int32> x=new H<int32>();return 0;}',
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                work = Path(self.directory.name) / name
                work.mkdir()
                (work / 'program.krt').write_text(source)
                result = subprocess.run([self.compiler], cwd=work, capture_output=True,
                                        text=True, timeout=10)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertTrue(result.stderr.startswith('E_'), result.stderr)
                self.assertFalse((work / 'stage1-probe.kro').exists())


if __name__ == '__main__':
    unittest.main()
