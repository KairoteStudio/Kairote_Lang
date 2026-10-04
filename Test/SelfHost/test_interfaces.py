"""Interface contracts and dispatch preserve reference and argument semantics."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import Bootstrap


class NativeInterfaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='krt-interfaces-')
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = (Path(os.environ['SELFHOST_COMPILER']).resolve()
                        if 'SELFHOST_COMPILER' in os.environ else cls.build.seed())

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def reject(self, name, source):
        work = Path(self.directory.name) / name
        work.mkdir()
        (work / 'program.krt').write_text(source)
        result = subprocess.run([self.compiler], cwd=work, capture_output=True,
                                text=True, timeout=10)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('E_', result.stdout + result.stderr)
        self.assertFalse((work / 'stage1-probe.kro').exists())

    def test_implicit_implementation_dispatch_upcast_and_null(self):
        self.check('implicit-dispatch', '''
interface Reader { int32 Read(); }
class A : Reader { public int32 value=7; public int32 Read(){return value;} }
class B : Reader { public int32 Read(){return 19;} }
Reader pass(Reader value){return value;}
int32 read(Reader value){return value.Read();}
int32 main(){A a=new A();Reader x=pass(a);Reader y=new B();Reader empty=null;
if(x.Read()!=7 || y.Read()!=19 || read(a)!=7 || empty!=null){return 1;}
Reader left=true?a:y;Reader right=false?y:a;
if(left.Read()!=7 || right.Read()!=7){return 3;}
if((int64)x!=(int64)a){return 2;}delete x;delete y;return 0;}
''')

    def test_multiple_interfaces_and_inherited_implementation(self):
        self.check('multiple-contracts', '''
interface Readable { int32 Read(); }
interface Writable { void Write(int32 value); }
class Storage {public int32 value=4;public int32 Read(){return value;}}
class Cell : Storage, Readable, Writable {public void Write(int32 next){value=next;}}
class Leaf : Cell {}
int32 main(){Leaf value=new Leaf();Readable reader=value;Writable writer=value;
if(reader.Read()!=4){return 1;}writer.Write(42);if(reader.Read()!=42 || value.Read()!=42){return 2;}
delete reader;return 0;}
''')

    def test_interface_inheritance_and_diamond_contracts(self):
        self.check('interface-diamond', '''
interface Root {int32 Read();}
interface Left : Root {int32 LeftValue();}
interface Right : Root {int32 RightValue();}
interface Both : Left, Right {int32 Sum();}
class Value : Both {public int32 Read(){return 3;}public int32 LeftValue(){return 5;}
public int32 RightValue(){return 7;}public int32 Sum(){return 15;}}
int32 main(){Value value=new Value();Both both=value;Left left=both;Right right=both;Root root=both;
if(both.Read()!=3 || both.LeftValue()!=5 || both.RightValue()!=7 || both.Sum()!=15 || left.Read()!=3 || right.Read()!=3 || root.Read()!=3){return 1;}
delete both;return 0;}
''')

    def test_duplicate_inherited_signatures_and_dense_interface_graph(self):
        interfaces = ['interface I0 {int32 Read();}',
                      'interface I1 {int32 Read();}']
        interfaces.extend(f'interface I{i}:I{i-1},I{i-2} {{}}'
                          for i in range(2, 32))
        self.check('dense-interface-graph', '\n'.join(interfaces) + '''
interface Unrelated {}
class Value:I31 {public int32 Read(){return 13;}}
int32 main(){I31 value=new Value();I0 first=value;I1 second=value;
if(value.Read()!=13 || first.Read()!=13 || second.Read()!=13){return 1;}
delete value;return 0;}
''')

    def test_overloads_ref_float_wide_and_stack_arguments(self):
        self.check('interface-signatures', '''
interface Math { int32 Read(int32 x); int64 Read(int64 x); void Change(ref int32 x);
float64 Add(float64 a,float64 b);uint128 Wide(uint128 x);int32 Sum(int32 a,int32 b,int32 c,int32 d,int32 e,int32 f,int32 g);}
class Calculator : Math {
public int32 Read(int32 x){return x+1;}public int64 Read(int64 x){return x+2;}
public void Change(ref int32 x){x+=3;}public float64 Add(float64 a,float64 b){return a+b;}
public uint128 Wide(uint128 x){return x+1;}
public int32 Sum(int32 a,int32 b,int32 c,int32 d,int32 e,int32 f,int32 g){return a+b+c+d+e+f+g;}}
int32 main(){Math math=new Calculator();int32 n=4;int64 wide=9;uint128 high=((uint128)1)<<100;
math.Change(ref n);if(n!=7 || math.Read(n)!=8 || math.Read(wide)!=11){return 1;}
if(math.Add(1.25,2.5)!=3.75 || math.Wide(high)!=high+1){return 2;}
if(math.Sum(1,2,3,4,5,6,7)!=28){return 3;}delete math;return 0;}
''')

    def test_generic_interfaces_named_constraints_and_factories(self):
        self.check('generic-interfaces', '''
namespace Contracts { interface Source<T> {T Read();} }
class Box<T> : Contracts.Source<T> {public T value;public function Box(T x){value=x;}public T Read(){return value;}}
T read<T,U>(U value) where U : Contracts.Source<T> {Contracts.Source<T> source=value;return source.Read();}
T identity<T>(T value) where T:class {return value;}
interface Marker {}
class Tag : Marker {}
T create<T>() where T:Marker,new() {return new T();}
int32 main(){Box<int32> box=new Box<int32>(17);Contracts.Source<int32> number=box;
Contracts.Source<string> text=new Box<string>("ready");
if(number.Read()!=17 || text.Read()!="ready" || read<int32,Box<int32>>(box)!=17){return 1;}
Contracts.Source<int32> same=identity<Contracts.Source<int32>>(number);if(same.Read()!=17){return 2;}
Marker tag=create<Tag>();delete tag;delete number;delete text;return 0;}
''')

    def test_abstract_class_and_explicit_override_implementation(self):
        self.check('abstract-implementation', '''
interface Readable {abstract int32 Read();}
abstract class Base : Readable {public abstract int32 Read();}
class Concrete : Base {public override int32 Read(){return 11;}}
class Direct : Readable {public override int32 Read(){return 23;}}
int32 main(){Readable first=new Concrete();Readable second=new Direct();
if(first.Read()!=11 || second.Read()!=23){return 1;}delete first;delete second;return 0;}
''')

    def test_function_keyword_interface_methods(self):
        self.check('function-interface', '''
int32 calls=0;
interface Shape {function Area();}
class Square:Shape {public override function Area(){calls++;}}
int32 main(){Square square=new Square();square.Area();Shape shape=square;shape.Area();
delete shape;return calls==2?0:1;}
''')

    def test_inherited_mapping_preserves_slots_until_reimplementation(self):
        self.check('interface-inherited-mapping', '''
interface I {int32 Read();}
interface J:I {}
class Base:I {public virtual int32 Read(){return 1;}}
class Hidden:Base {public int32 Read(){return 2;}}
class Overridden:Base {public override int32 Read(){return 3;}}
class Reimplemented:Base,I {public int32 Read(){return 4;}}
class Inherited:Base,I {}
class Plain:I {public int32 Read(){return 5;}}
class PlainHidden:Plain {public int32 Read(){return 6;}}
class PlainReimplemented:Plain,I {public int32 Read(){return 7;}}
class ViaDerivedInterface:Base,J {public int32 Read(){return 8;}}
abstract class Deferred:I {}
class Complete:Deferred {public int32 Read(){return 9;}}
class HiddenComplete:Complete {public int32 Read(){return 10;}}
int32 main(){Hidden hidden=new Hidden();I a=hidden;I b=new Overridden();I c=new Reimplemented();
I d=new Inherited();I e=new PlainHidden();I f=new PlainReimplemented();I g=new ViaDerivedInterface();I h=new HiddenComplete();
if(hidden.Read()!=2 || a.Read()!=1 || b.Read()!=3 || c.Read()!=4 || d.Read()!=1){return 1;}
if(e.Read()!=5 || f.Read()!=7 || g.Read()!=8 || h.Read()!=9){return 2;}
delete a;delete b;delete c;delete d;delete e;delete f;delete g;delete h;return 0;}
''')

    def test_interface_arrays_preserve_concrete_types(self):
        self.check('interface-array', '''
interface Item {int32 Get();}
class A:Item {public int32 Get(){return 3;}}
class B:Item {public int32 Get(){return 5;}}
int32 main(){Item[] items=[new A(),new B()];int32 sum=0;
foreach(Item item in items){sum+=item.Get();delete item;}delete items;return sum==8?0:1;}
''')

    def test_reject_invalid_contracts_and_unsafe_conversions(self):
        cases = {
            'missing': 'interface I{int32 f();}class A:I{}int32 main(){return 0;}',
            'return-mismatch': 'interface I{int32 f();}class A:I{public int64 f(){return 1;}}int32 main(){return 0;}',
            'parameter-mismatch': 'interface I{int32 f(int32 x);}class A:I{public int32 f(int64 x){return 1;}}int32 main(){return 0;}',
            'reference-mismatch': 'interface I{void f(ref int32 x);}class A:I{public void f(int32 x){}}int32 main(){return 0;}',
            'static-implementation': 'interface I{int32 f();}class A:I{public static int32 f(){return 1;}}int32 main(){return 0;}',
            'missing-inherited': 'interface I{int32 f();}interface J:I{}class A:J{}int32 main(){return 0;}',
            'unrelated-conversion': 'interface I{}class A{}int32 main(){A a=new A();I i=a;return 0;}',
            'ref-covariance': 'interface I{}class A:I{}void f(ref I i){}int32 main(){A a=new A();f(ref a);return 0;}',
            'array-covariance': 'interface I{}class A:I{}int32 main(){A[] a=new A[1];I[] i=a;return 0;}',
            'interface-new': 'interface I{}int32 main(){I i=new I();return 0;}',
            'interface-field': 'interface I{int32 value;}int32 main(){return 0;}',
            'default-body': 'interface I{int32 f(){return 1;}}int32 main(){return 0;}',
            'empty-default-body': 'interface I{void f(){}}int32 main(){return 0;}',
            'interface-constructor': 'interface I{public function I(){}}int32 main(){return 0;}',
            'interface-static': 'interface I{static int32 f();}int32 main(){return 0;}',
            'interface-generic-method': 'interface I{T f<T>(T x);}int32 main(){return 0;}',
            'duplicate-contract': 'interface I{int32 f();int32 f();}int32 main(){return 0;}',
            'conflicting-inherited-contract': 'interface I{int32 f();}interface J{int64 f();}interface Both:I,J{}int32 main(){return 0;}',
            'conflicting-class-contract': 'interface I{int32 f();}interface J{int64 f();}abstract class Both:I,J{}int32 main(){return 0;}',
            'interface-class-base': 'class A{}interface I:A{}int32 main(){return 0;}',
            'cycle': 'interface I:J{}interface J:I{}int32 main(){return 0;}',
            'duplicate-base': 'interface I{}class A:I,I{}int32 main(){return 0;}',
            'class-base-order': 'interface I{}class B{}class A:I,B{}int32 main(){return 0;}',
            'constraint-unrelated': 'interface I{}class A{}T f<T>(T x)where T:I{return x;}int32 main(){A a=f<A>(new A());return 0;}',
            'constraint-interface-new': 'interface I{}T f<T>()where T:new(){return new T();}int32 main(){I i=f<I>();return 0;}',
            'constraint-abstract-new': 'abstract class A{}T f<T>()where T:new(){return new T();}int32 main(){A a=f<A>();return 0;}',
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                self.reject(name, source)


if __name__ == '__main__':
    unittest.main()
