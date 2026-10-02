"""Virtual slots, abstract contracts and base calls across native compiler generations."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from Test.SelfHost.Bootstrap import Bootstrap


class NativeVirtualTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='krt-virtual-')
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = Path(os.environ['SELFHOST_COMPILER']).resolve() if 'SELFHOST_COMPILER' in os.environ else cls.build.seed()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source, expected=0):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, expected, result.stderr.decode())

    def test_slots_base_calls_and_hiding(self):
        self.check('slots', '''
class Base {public virtual int32 F(){return 1;}public int32 Via(){return F();}public virtual int32 H(){return 3;}}
class Middle:Base{public override int32 F(){return base.F()+10;}public int32 H(){return 50;}}
class Leaf:Middle{public override int32 F(){return base.F()+100;}public virtual int32 H(){return 70;}}
class Last:Leaf{public override int32 H(){return 80;}}
int32 main(){Last d=new Last();Base b=d;Middle m=d;Leaf l=d;
if(b.F()!=111 || b.Via()!=111 || b.H()!=3 || m.H()!=50 || l.H()!=80 || d.H()!=80){return 1;}delete b;return 0;}
''')

    def test_forward_derived_signature_preserves_override_slot(self):
        self.check('forward-derived', '''
class Base{public virtual int32 F(Derived d){return 1;}public virtual int32 F(int32 n){return 2;}}
class Derived:Base{public override int32 F(Derived d){return 3;}}
int32 main(){Derived d=new Derived();Base b=d;if(b.F(d)!=3 || b.F(4)!=2){return 1;}delete b;return 0;}
''')

    def test_abstract_contracts_and_reabstract(self):
        self.check('abstract', '''
abstract class A{public int32 n=3;public abstract int32 Read();public virtual int32 Sum(){return Read()+n;}}
class B:A{public override int32 Read(){return 5;}}
abstract class C:B{public abstract override int32 Read();}
class D:C{public override int32 Read(){return 9;}}
int32 main(){A a=new B();A b=new D();if(a.Sum()!=8 || b.Sum()!=12){return 1;}delete a;delete b;return 0;}
''')

    def test_generic_classes_overloads_and_independent_slots(self):
        self.check('generic-slots', '''
abstract class Box<T>{public abstract T Get(T value);public virtual int32 Tag(int32 n){return n+1;}public virtual int32 Tag(string n){return 4;}}
class Wrap<T>:Box<T>{public override T Get(T value){return value;}public override int32 Tag(int32 n){return base.Tag(n)+10;}}
int32 main(){Box<float64> a=new Wrap<float64>();Box<string> b=new Wrap<string>();
if(a.Get(2.5)!=2.5 || b.Get("yes")!="yes" || a.Tag(2)!=13 || b.Tag("x")!=4){return 1;}delete a;delete b;return 0;}
''')

    def test_receiver_and_arguments_evaluated_once_in_order(self):
        self.check('order', '''
int32 trace=0;
class Base{public virtual int32 F(int32 x,int32 y){return 0;}}
class Derived:Base{public override int32 F(int32 x,int32 y){return x*10+y;}}
Base value=new Derived();
Base receiver(){trace=trace*10+1;return value;}
int32 argument(int32 n){trace=trace*10+n;return n;}
int32 main(){int32 n=receiver().F(argument(2),argument(3));if(n!=23 || trace!=123){return 1;}delete value;return 0;}
''')

    def test_ref_wide_float_and_stack_arguments(self):
        self.check('abi', '''
class Base{public virtual uint128 Add(uint128 a,uint128 b){return 0;}public virtual float64 F(float64 x){return 0.0;}public virtual int64 Many(ref int32 x,int64 a,int64 b,int64 c,int64 d,int64 e,int64 f,int64 g){return 0;}}
class Derived:Base{public override uint128 Add(uint128 a,uint128 b){return a+b;}public override float64 F(float64 x){return x+0.5;}public override int64 Many(ref int32 x,int64 a,int64 b,int64 c,int64 d,int64 e,int64 f,int64 g){x+=7;return a+b+c+d+e+f+g;}}
int32 main(){Base b=new Derived();int32 n=2;uint128 large=(uint128)1<<90;
if(b.Add(large,large)!=large*2 || b.F(2.0)!=2.5 || b.Many(ref n,1,2,3,4,5,6,7)!=28 || n!=9){return 1;}delete b;return 0;}
''')

    def test_virtual_calls_during_construction(self):
        self.check('constructor', '''
class Base{public int32 n;public function Base(){n=Get();}public virtual int32 Get(){return 1;}}
class Derived:Base{public override int32 Get(){return 9;}}
int32 main(){Base b=new Derived();if(b.n!=9){return 1;}delete b;return 0;}
''')

    def test_null_receiver_terminates_without_memory_read(self):
        self.check('null', 'class A{public virtual int32 F(){return 1;}}int32 main(){A a=null;return a.F();}', 126)

    def test_invalid_virtual_contracts_are_diagnostics(self):
        cases = {
            'new-abstract': 'abstract class A{public abstract int32 F();}int32 main(){A a=new A();return 0;}',
            'missing-slot': 'abstract class A{public abstract int32 F();}class B:A{}int32 main(){return 0;}',
            'hiding-abstract': 'abstract class A{public abstract int32 F();}class B:A{public int32 F(){return 1;}}int32 main(){return 0;}',
            'abstract-concrete': 'class A{public abstract int32 F();}int32 main(){return 0;}',
            'abstract-body': 'abstract class A{public abstract int32 F(){return 1;}}int32 main(){return 0;}',
            'override-missing': 'class A{public override int32 F(){return 1;}}int32 main(){return 0;}',
            'override-nonvirtual': 'class A{public int32 F(){return 1;}}class B:A{public override int32 F(){return 2;}}int32 main(){return 0;}',
            'return-mismatch': 'class A{public virtual int64 F(){return 1;}}class B:A{public override int32 F(){return 2;}}int32 main(){return 0;}',
            'ref-mismatch': 'class A{public virtual int32 F(ref int32 n){return n;}}class B:A{public override int32 F(int32 n){return n;}}int32 main(){return 0;}',
            'static-virtual': 'class A{public static virtual int32 F(){return 1;}}int32 main(){return 0;}',
            'generic-virtual-method': 'class A{public virtual T F<T>(T x){return x;}}int32 main(){return 0;}',
            'generic-owner-virtual-method': 'class A<T>{public virtual U F<U>(U x){return x;}}int32 main(){A<int32> a=new A<int32>();return 0;}',
            'base-abstract': 'abstract class A{public abstract int32 F();}class B:A{public override int32 F(){return base.F();}}int32 main(){return 0;}',
            'bare-prototype': 'class A{public int32 F();}int32 main(){return 0;}',
            'global-virtual': 'virtual int32 f(){return 0;}int32 main(){return f();}',
            'duplicate-slot': 'class A{public virtual int32 F(){return 1;}public virtual int32 F(){return 2;}}int32 main(){return 0;}',
            'global-abstract': 'abstract int32 f();int32 main(){return 0;}',
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                work = Path(self.directory.name) / name
                work.mkdir()
                (work / 'program.krt').write_text(source)
                result = subprocess.run([self.compiler], cwd=work, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn('E_', result.stdout + result.stderr)
                self.assertFalse((work / 'stage1-probe.kro').exists())


if __name__ == '__main__':
    unittest.main()
