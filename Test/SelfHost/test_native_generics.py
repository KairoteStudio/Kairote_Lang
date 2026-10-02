"""Monomorphized classes and functions retain concrete types across call boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import Bootstrap


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

    def test_infers_nested_qualified_types_and_array_arguments(self):
        self.check('nested-inference', '''
namespace N { class Box<T>{public T value;public Box(T x){value=x;}} }
class Pair<T,U>{public T left;public U right;public Pair(T a,U b){left=a;right=b;}}
T unwrap<T>(N.Box<N.Box<T>> box){return box.value.value;}
T first<T>(N.Box<T[]> box){return box.value[0];}
U second<T,U>(Pair<N.Box<T>,U> pair){return pair.right;}
void replace<T>(ref N.Box<T> box,T value){box.value=value;}
int32 main(){N.Box<int32> inner=new N.Box<int32>(17);N.Box<N.Box<int32>> outer=new N.Box<N.Box<int32>>(inner);
string[] names=["first","second"];N.Box<string[]> array=new N.Box<string[]>(names);
Pair<N.Box<int32>,string> pair=new Pair<N.Box<int32>,string>(inner,"ok");
if(unwrap(outer)!=17 || first(array)!="first" || second(pair)!="ok"){return 1;}
replace(ref inner,42);if(unwrap(outer)!=42){return 2;}
delete pair;delete array;delete names;delete outer;delete inner;return 0;}
''')

    def test_infers_remaining_pointer_depth_and_ref_pointers(self):
        self.check('pointer-inference', '''
T read<T>(T** pointer){unsafe(using krt.mem;){return **pointer;}}
T load<T>(T* pointer){unsafe(using krt.mem;){return *pointer;}}
void replace<T>(ref T* pointer,T* next){pointer=next;}
int32 main(){unsafe(using krt.mem;){int32 value=17;int32 other=42;int32* pointer=&value;int32** twice=&pointer;
if(read(twice)!=17 || *load(twice)!=17){return 1;}
replace(ref pointer,&other);if(read(twice)!=42){return 2;}
uint8 byte_value=231;uint8* byte_pointer=&byte_value;uint8** byte_twice=&byte_pointer;
if(read(byte_twice)!=231 || *load(byte_twice)!=231){return 3;}return 0;}}
''')

    def test_infers_type_arguments_inside_function_pointer_shapes(self):
        self.check('callback-inference', '''
int32 increment(int32 value){return value+1;}
string echo(string value){return value;}
T invoke<T>(fn(T)->T callback,T value){return callback(value);}
int32 main(){if(invoke(&increment,41)!=42 || invoke(&echo,"ok")!="ok"){return 1;}return 0;}
''')

    def test_generic_type_comments_and_multi_parameter_callback_arguments(self):
        self.check('generic-type-syntax', '''
class Box<T>{public T value;public Box(T x){value=x;}}
T get<T>(Box</* fake delimiters < > , * [] */T> box){return box.value;}
F identity<F>(F callback){return callback;}
int32 total(int32 left,int32 right){return left+right;}
int32 main(){Box</* >, */int32> box=new Box</* < */int32>(42);
fn(int32,int32)->int32 callback=identity<fn(int32,int32)->int32>(&total);
if(get(box)!=42 || callback(19,23)!=42){return 1;}delete box;return 0;}
''')

    def test_callback_pointer_arguments_do_not_become_outer_pointers(self):
        self.check('callback-pointer-inference', '''
int32 read(int32* pointer){unsafe(using krt.mem;){return *pointer;}}
T invoke<T>(fn(T*)->T callback,T* pointer){return callback(pointer);}
F identity<F>(F callback){return callback;}
int32 main(){unsafe(using krt.mem;){int32 value=42;
fn(int32*)->int32 callback=identity<fn(int32*)->int32>(&read);
if(invoke(callback,&value)!=42 || invoke(&read,&value)!=42){return 1;}return 0;}}
''')

    def test_infers_generic_arguments_through_base_and_interface_views(self):
        self.check('generic-projection', '''
class Parent<T>{public T value=default(T);}
class Child<T>:Parent<T>{}
interface View<T>{T Get();}
class Box<T>:View<T>{public T value;public Box(T x){value=x;}public T Get(){return value;}}
T read_parent<T>(Parent<T> item){return item.value;}
T read_view<T>(View<T> item){return item.Get();}
int32 main(){Child<int32> child=new Child<int32>();child.value=17;Box<string> box=new Box<string>("ok");
if(read_parent(child)!=17 || read_view(box)!="ok"){return 1;}delete box;delete child;return 0;}
''')

    def test_extension_receiver_participates_in_nested_inference(self):
        self.check('extension-inference', '''
class Box<T>{public T value;public Box(T x){value=x;}}
T Extract<T>(Box<T> receiver){return receiver.value;}
T ExtractNested<T>(Box<Box<T>> receiver){return receiver.value.value;}
int32 main(){Box<int32> inner=new Box<int32>(42);Box<Box<int32>> outer=new Box<Box<int32>>(inner);
if(inner.Extract()!=42 || outer.ExtractNested()!=42){return 1;}
delete outer;delete inner;return 0;}
''')

    def test_registry_resizes_and_reuses_alias_and_repeated_instances(self):
        types = ([f'int{bits}' for bits in range(2, 65, 2)] + [f'uint{bits}' for bits in range(2, 65, 2)] +
                 ['int128', 'uint128'] + ['int32' + '*' * depth for depth in range(1, 49)] +
                 ['uint32' + '*' * depth for depth in range(1, 49)])
        calls = ''.join(f'mark<{type_name}>();' for type_name in types)
        self.check('registry-resize', '''
static int32 sequence=0;
int32 next(){sequence++;return sequence;}
class Stamp<T>{public static int32 id=next();}
int32 mark<T>(){return Stamp<T>.id;}
int32 main(){''' + calls + 'mark<int>();mark<int32>();mark<long>();mark<int64>();' +
                   'mark<uint8>();mark<byte>();return sequence==162 ? 0 : 1;}')

    def test_character_specializations_are_distinct_from_bytes(self):
        self.check('character-identity', '''
static int32 sequence=0;
int32 next(){sequence++;return sequence;}
class Stamp<T>{public static int32 id=next();}
int32 mark<T>(T value){return Stamp<T>.id;}
int32 main(){int32 character=mark<char>('A');int32 byte_id=mark<uint8>((uint8)65);
if(character==byte_id || mark('B')!=character || mark<byte>((byte)7)!=byte_id){return 1;}
char[] chars=['A'];uint8[] bytes=[(uint8)65];
if(mark(chars)==mark(bytes) || sequence!=4){return 2;}delete chars;delete bytes;return 0;}
''')

    def test_more_than_1024_functions_finish_semantic_binding(self):
        functions = ''.join(f'int32 helper_{index}(){{return {index};}}' for index in range(1100))
        self.check('many-functions', functions + 'int32 main(){return helper_1099()==1099 ? 0 : 1;}')

    def test_invalid_instantiation_and_reference_diagnostics(self):
        cases={
            'nested-inconsistent-inference':'class Box<T>{public T value;}T choose<T>(Box<T> a,Box<T> b){return a.value;}int32 main(){Box<int32> a=new Box<int32>();Box<string> b=new Box<string>();choose(a,b);return 0;}',
            'pointer-depth-inference':'T read<T>(T** value){unsafe(using krt.mem;){return **value;}}int32 main(){unsafe(using krt.mem;){int32 x=1;int32* p=&x;read(p);return 0;}}',
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
