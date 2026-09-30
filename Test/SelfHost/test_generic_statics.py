"""Concrete generic classes own independent process-wide static storage."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.bootstrap import Bootstrap


class GenericStaticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='krt-generic-statics-')
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = Path(os.environ['SELFHOST_COMPILER']).resolve() if 'SELFHOST_COMPILER' in os.environ else cls.build.seed()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source, expected=0):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, expected, result.stderr.decode() + source)

    def test_unused_template_requires_no_concrete_storage(self):
        self.check('unused-template', '''
class Unused<T>{public static T value=default(T);public static T[] items=new T[3];}
int32 main(){return 0;}
''')

    def test_generic_constant_uses_its_declaring_context(self):
        self.check('constant-context', '''
class Limits<T>{public const int32 base_value=3;public const T value=base_value;
public static T current=value;}
int32 main(){if(Limits<int32>.value!=3 || Limits<float64>.value!=3.0){return 1;}
Limits<int32>.current=9;
if(Limits<int32>.current!=9 || Limits<float64>.current!=3.0){return 2;}return 0;}
''')

    def test_independent_concrete_storage_shared_by_objects_and_calls(self):
        self.check('separate-storage', '''
class Counter<T>{public static int32 count=3;public static T value=default(T);
public void Set(T x){value=x;count++;}public static T Read(){return value;}}
int32 main(){Counter<int32> a=new Counter<int32>();Counter<int32> b=new Counter<int32>();
Counter<string> c=new Counter<string>();a.Set(17);b.Set(42);c.Set("ok");
if(Counter<int32>.Read()!=42 || Counter<string>.Read()!="ok"){return 1;}
if(Counter<int32>.count!=5 || Counter<string>.count!=4){return 2;}
Counter<int32>.value=19;if(a.Read()!=19 || Counter<string>.value!="ok"){return 3;}
delete a;delete b;delete c;return 0;}
''')

    def test_initializer_runs_once_and_in_source_order(self):
        self.check('initialization-order', '''
static int32 sequence=0;
int32 next(){sequence++;return sequence;}
class State<T>{public static int32 first=next();public static int32 second=first+10;
public static T value=default(T);public static int32 Get(){return first+second;}}
static int32 captured=State<int32>.second;
int32 main(){int32 one=State<int32>.Get();int32 two=State<string>.Get();
if(sequence!=2 || captured!=11 || one!=12 || two!=14){return 1;}
State<int32> a=new State<int32>();State<int32> b=new State<int32>();
if(sequence!=2 || State<int32>.Get()!=12 || State<string>.Get()!=14){return 2;}
delete a;delete b;return 0;}
''')

    def test_instances_discovered_in_global_initializers_and_generated_methods(self):
        self.check('discovery-closure', '''
class Cell<T>{public static T value=default(T);public static int32 marker=7;}
class Factory<T>{public static Cell<T> item=new Cell<T>();
public static T Read(){return Cell<T>.value;}}
T read<T>(){return Factory<T>.Read();}
static int32 number=read<int32>();
static string text=read<string>();
int32 main(){if(number!=0 || text!=null || Cell<int32>.marker!=7 || Cell<string>.marker!=7){return 1;}
Cell<int32>.value=42;Cell<string>.value="hello";
if(read<int32>()!=42 || read<string>()!="hello"){return 2;}
delete Factory<int32>.item;delete Factory<string>.item;return 0;}
''')

    def test_generic_arrays_and_references(self):
        self.check('arrays-ref', '''
class Values<T>{public static T value=default(T);public static T[] items=[default(T),default(T)];
public static void Set(T x){value=x;items[0]=x;}}
void set<T>(ref T value,T next){value=next;}
int32 main(){Values<int32>.Set(17);Values<string>.Set("first");
set<int32>(ref Values<int32>.value,42);set<string>(ref Values<string>.items[1],"second");
if(Values<int32>.value!=42 || Values<int32>.items[0]!=17 || Values<int32>.items[1]!=0){return 1;}
if(Values<string>.value!="first" || Values<string>.items[0]!="first" || Values<string>.items[1]!="second"){return 2;}
delete Values<int32>.items;delete Values<string>.items;return 0;}
''')

    def test_specializations_created_only_by_initializers(self):
        self.check('initializer-only-specialization', '''
static int32 sequence=0;
int32 next(){sequence++;return sequence;}
class Hidden<T>{public static int32 marker=next();public static T value=default(T);
public static int32 Get(){return marker;}}
int32 read<T>(){return Hidden<T>.Get();}
static int32 first=read<int32>();
static int32 second=read<string>();
int32 main(){return first==1 && second==2 && sequence==2 ? 0 : 1;}
''')

    def test_all_static_fields_initialize_when_concrete_class_is_created(self):
        self.check('unreferenced-concrete-fields', '''
static int32 sequence=0;
int32 next(){sequence++;return sequence;}
class State<T>{public static int32 marker=next();public static T value=default(T);}
int32 main(){State<int32> first=new State<int32>();State<string> second=new State<string>();
State<int32> repeated=new State<int32>();if(sequence!=2){return 1;}
delete first;delete second;delete repeated;return 0;}
''')

    def test_float_and_wide_static_storage(self):
        self.check('float-wide', '''
class Values<T>{public static T value=default(T);public static T[] items=new T[2];
public static int32 guard=23;public static void Set(T x){value=x;items[1]=x;}}
void assign<T>(ref T target,T value){target=value;}
int32 main(){if(Values<uint128>.value!=0 || Values<float64>.value!=0.0){return 1;}
Values<float32>.Set(1.25f);Values<float64>.Set(2.5);Values<uint128>.Set((uint128)18446744073709551623);
if(Values<float32>.value!=1.25f || Values<float64>.items[1]!=2.5){return 2;}
uint128 copy=Values<uint128>.value;assign<uint128>(ref Values<uint128>.value,(uint128)340282366920938463463374607431768211450);
if(copy!=(uint128)18446744073709551623 || Values<uint128>.items[1]!=copy){return 3;}
if(Values<uint128>.value!=(uint128)340282366920938463463374607431768211450 || Values<uint128>.guard!=23){return 4;}
delete Values<float32>.items;delete Values<float64>.items;delete Values<uint128>.items;return 0;}
''')

    def test_namespace_and_nested_type_instance_identity(self):
        self.check('nested-identities', '''
namespace N{class Value<T>{public static T current=default(T);public static int32 counter=1;}}
class Box<T>{public T value;}
int32 main(){N.Value<Box<int32>>.counter=7;N.Value<Box<string>>.counter=11;
if(N.Value<Box<int32>>.counter!=7 || N.Value<Box<string>>.counter!=11){return 1;}
N.Value<int30>.current=(int30)536870911;N.Value<int32>.current=1073741823;
N.Value<int30>.current++;N.Value<int32>.current++;
if(N.Value<int30>.current!=-536870912 || N.Value<int32>.current!=1073741824){return 2;}return 0;}
''')

    def test_inherited_static_fields_keep_the_base_specialization_storage(self):
        self.check('inherited-storage', '''
static int32 initialized=0;
int32 next(){initialized++;return initialized;}
class Base<T>{public static T value=default(T);public static int32 marker=next();}
class Derived<T>:Base<T>{}
int32 main(){Derived<int32>.value=17;Derived<string>.value="value";
if(Base<int32>.value!=17 || Base<string>.value!="value" || initialized!=2){return 1;}
Base<int32>.value=42;if(Derived<int32>.value!=42 || Derived<string>.value!="value"){return 2;}
return 0;}
''')

    def test_invalid_concrete_initializers_and_reference_types(self):
        cases = {
            'concrete-initializer': 'class Bad<T>{public static T value="bad";}int32 main(){return Bad<int32>.value;}',
            'concrete-ref': 'class Value<T>{public static T value=default(T);}void set(ref int64 x){x=1;}int32 main(){set(ref Value<int32>.value);return 0;}',
            'concrete-array': 'class Value<T>{public static T[] values=["bad"];}int32 main(){return Value<int32>.values[0];}',
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                work = Path(self.directory.name) / name
                work.mkdir()
                (work / 'program.krt').write_text(source)
                result = subprocess.run([self.compiler], cwd=work, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertTrue(result.stderr.startswith('E_'), result.stderr)
                self.assertFalse((work / 'stage1-probe.kro').exists())


if __name__ == '__main__':
    unittest.main()
