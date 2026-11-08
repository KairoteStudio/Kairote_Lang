"""Native KRO exports, undefined symbols and relocations across separate builds."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()
LINKER = Path(os.environ.get('ARKLINK', ROOT / 'build/ArkLink/ArkLink')).resolve()
if 'ARKLINK' not in os.environ and not LINKER.is_file():
    LINKER = ROOT / 'ArkLink/build/ArkLink'


def read_object(path):
    data = path.read_bytes()
    header = struct.unpack_from('<16I', data)
    start = 64 + sum(header[4:7])
    strings = data[start + header[12] * 32 + header[14] * 16:]
    symbols = []
    for index in range(header[12]):
        symbol = struct.unpack_from('<8I', data, start + index * 32)
        name = strings[symbol[0]:].split(b'\0', 1)[0].decode()
        symbols.append((name, symbol))
    relocations = [struct.unpack_from('<IIIi', data, start + header[12] * 32 + index * 16)
                   for index in range(header[8])]
    return header, symbols, relocations


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), 'build a native compiler and ArkLink')
class NativeLinkingTests(unittest.TestCase):
    def setUp(self):
        work = tempfile.TemporaryDirectory(prefix='krt native linking ')
        self.addCleanup(work.cleanup)
        self.work = Path(work.name)

    def invoke(self, *args):
        return subprocess.run([str(ROOT / 'SelfHost/krtc'), *map(str, args),
                               '--compiler', str(COMPILER), '--linker', str(LINKER)],
                              cwd=self.work, capture_output=True, text=True, timeout=60)

    def ok(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def object(self, name, source, level=None, objects=()):
        path = self.work / (name + '.krt')
        path.write_text(source)
        target = path.with_suffix('.kro')
        options = () if level is None else (f'-O{level}',)
        self.ok(self.invoke(path, *objects, *options, '-c', '-o', target))
        return target

    def execute(self, objects, expected=42):
        target = self.work / 'program'
        self.ok(self.invoke(*objects, '-o', target))
        result = subprocess.run([str(target)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return target

    def test_separate_function_has_real_undefined_symbol_and_pc32_relocation(self):
        library = self.object('math', 'int32 add(int32 a,int32 b){return a+b;}')
        caller = self.object('main', 'extern int32 add(int32 x,int32 y); int32 main(){return add(19,23);}')
        header, symbols, relocations = read_object(caller)
        external = [(index, name) for index, (name, item) in enumerate(symbols) if item[3] == 0]
        self.assertEqual(external, [(1, '_KRT1$add$i32;i32;$i32')])
        self.assertEqual(len(relocations), 1)
        offset, symbol, kind, addend = relocations[0]
        self.assertLessEqual(offset + 4, header[4])
        self.assertEqual((symbol, kind, addend), (external[0][0], 3, 0))
        self.assertIn(external[0][1], [name for name, item in read_object(library)[1] if item[3] == 1])
        self.execute([caller, library])
        self.execute([library, caller])

    def test_namespace_overloads_are_stable_across_separate_libraries(self):
        first = self.object('first', 'namespace A; int32 choose(int32 x){return x+1;}')
        second = self.object('second', 'namespace A { int64 choose(int64 x){return x+2;} } namespace B { int32 choose(int32 x){return x+3;} }')
        caller = self.object('main', '''namespace A { extern int32 choose(int32 a); extern int64 choose(int64 b); }
            namespace B { extern int32 choose(int32 c); }
            int32 main(){if(A.choose(9)!=10 || A.choose((int64)18)!=20 || B.choose(9)!=12){return 1;} return 42;}''')
        self.execute([caller, first, second])

    def test_float_wide_ref_and_stack_arguments_share_native_abi(self):
        library = self.object('abi', '''float64 scale(float64 a,float32 b){return a*b;}
            uint128 wide(uint128 a,uint128 b){return a+b;}
            void bump(ref int64 value){value=value+5;}
            int64 total(int64 a,int64 b,int64 c,int64 d,int64 e,int64 f,int64 g,int64 h){return a+b+c+d+e+f+g+h;}''')
        caller = self.object('main', '''extern double scale(double x,float y);
            extern uint128 wide(uint128 x,uint128 y); extern void bump(ref long x);
            extern long total(long a,long b,long c,long d,long e,long f,long g,long h);
            int32 main(){int64 n=37;bump(ref n);if(n!=42 || scale(1.5,(float32)2.0)!=3.0){return 1;}
            uint128 n128=(uint128)18446744073709551623;
            if(wide(n128,(uint128)19)!=(uint128)18446744073709551642){return 2;}
            if(total(1,2,3,4,5,6,7,14)!=42){return 3;}return 42;}''')
        self.execute([caller, library])

    def test_external_function_address_and_callback_relocations(self):
        library = self.object('callback', '''int64 twice(int64 x){return x*2;}
            int64 apply(fn(int64)->int64 callback,int64 x){return callback(x);}''')
        caller = self.object('main', '''extern int64 twice(int64 x);
            extern int64 apply(fn(int64)->int64 callback,int64 x);
            int32 main(){fn(int64)->int64 f=&twice;if(f(21)!=42 || apply(f,21)!=42){return 1;}return 42;}''')
        self.assertEqual(len(read_object(caller)[2]), 2)
        self.execute([caller, library])

    def test_library_can_call_another_library_and_ignore_unused_prototypes(self):
        first = self.object('first', 'extern int32 absent(int32 x); extern int32 base(int32 x); int32 answer(){return base(40)+1;}')
        second = self.object('second', 'int32 base(int32 x){return x+1;}')
        caller = self.object('main', 'extern int32 answer(); int32 main(){return answer();}')
        self.assertFalse(any('absent' in name for name, _ in read_object(first)[1]))
        self.execute([caller, first, second])

    def test_matching_prototypes_merge_with_definition_and_conflicts_fail(self):
        caller = self.object('main', '''extern long value(int x); extern int64 value(int32 other);
            int64 value(int32 x){return x+2;} int32 main(){return (int32)value(40);}''')
        self.assertEqual(read_object(caller)[0][8], 0)
        self.execute([caller])
        source = self.work / 'bad.krt'
        source.write_text('extern int32 same(int32 x); int64 same(int32 x){return x;} int32 main(){return 0;}')
        result = self.invoke(source, '-c', '-o', self.work / 'bad.kro')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('E_LINKAGE', result.stderr)

    def test_static_class_methods_use_owner_identity(self):
        library = self.object('api', '''namespace Demo; class First{public static int32 value(int32 x){return x+1;}}
            class Second{public static int32 value(int32 x){return x+2;}}''')
        caller = self.object('main', '''namespace Demo { class First{public static extern int32 value(int32 x);}
            class Second{public static extern int32 value(int32 x);} }
            int32 main(){return Demo.First.value(19)+Demo.Second.value(20);}''')
        self.execute([caller, library])

    def test_unresolved_and_wrong_signature_fail_without_replacing_output(self):
        library = self.object('library', 'int64 answer(int64 x){return x;}')
        caller = self.object('main', 'extern int32 answer(int32 x); int32 main(){return answer(42);}')
        output = self.work / 'program'; output.write_bytes(b'previous output')
        result = self.invoke(caller, library, '-o', output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('E_LINK', result.stderr)
        self.assertEqual(output.read_bytes(), b'previous output')

    def test_class_values_and_callbacks_have_independent_native_contracts(self):
        declarations = 'namespace Contracts;public class Box{public int32 n;public extern Box(int32 initial);}'
        provider = declarations.replace('public extern Box(int32 initial);',
                                        'public Box(int32 initial){n=initial;}') + '''
Box consume(Box value){value.n++;return value;}
int32 apply(fn(Box)->int32 callback,Box value){return callback(value);}
'''
        consumer = declarations + '''extern Box consume(Box value);
extern int32 apply(fn(Box)->int32 callback,Box value);
int32 Read(Box value){return value.n+1;}
int32 main(){Box value=new Box(40);Box same=consume(value);
int32 result=apply(&Read,same);delete value;return result;}
'''
        for level in range(4):
            library = self.object(f'class-provider-o{level}', provider, level)
            caller = self.object(f'class-consumer-o{level}', consumer, level)
            symbols = read_object(library)[1]
            self.assertTrue(any(name.startswith('_KRT3$Contracts.consume$') and symbol[4] == 1
                                for name, symbol in symbols))
            for objects in ((caller, library), (library, caller)):
                self.execute(objects)

    def test_primitive_exceptions_unwind_across_objects_and_run_finally(self):
        library = self.object('throwing', '''void fail(){throw "cross object";}
            void fail_number(){try{throw 35;}finally{}}
            void nested(){try{fail();}catch(string e){throw 35;}}''')
        caller = self.object('main', '''extern void fail(); extern void fail_number(); extern void nested();
            int32 main(){int32 n=0;try{fail();}catch(string text){if(text!="cross object"){return 1;}n=1;}finally{n+=2;}
            try{fail_number();}catch(int32 x){n+=x;}finally{n+=4;}
            if(n!=42){return 2;}try{nested();}catch(int32 x){if(x!=35){return 3;}}return 42;}''')
        self.execute([caller, library])

    def test_bool_array_and_void_callback_signatures(self):
        library = self.object('arrays', '''bool first(bool[] values){return values[0];}
            void fill(float64[] values){values[1]=2.5;}
            void run(fn(ref int32)->void callback,ref int32 value){callback(ref value);}''')
        caller = self.object('main', '''extern bool first(bool[] values); extern void fill(double[] values);
            extern void run(fn(ref int32)->void callback,ref int32 value);
            void inc(ref int32 x){x+=2;}
            int32 main(){bool[] flags=[true,false];float64[] values=[1.0,0.0];fill(values);
            if(!first(flags) || values[1]!=2.5){return 1;}int32 n=40;run(&inc,ref n);return n;}''')
        self.execute([caller, library])

    def test_named_exceptions_cross_linked_calls_in_both_directions(self):
        declarations = 'namespace Failures;public class Failure{public int32 code;public extern Failure(int32 value);}'
        provider = declarations.replace('public extern Failure(int32 value);',
                                        'public Failure(int32 value){code=value;}') + '''
void fail(){throw new Failure(35);}
int32 check(fn()->void callback){try{callback();return 1;}catch(Failure value){
int32 code=value.code;delete value;return code;}}
'''
        consumer = declarations + '''extern void fail();extern int32 check(fn()->void callback);
void Raise(){throw new Failure(42);}
int32 main(){int32 score=0;try{fail();return 1;}catch(Failure value){score=value.code;delete value;}
return score==35&&check(&Raise)==42?42:2;}
'''
        for level in range(4):
            library = self.object(f'exception-provider-o{level}', provider, level)
            caller = self.object(f'exception-consumer-o{level}', consumer, level)
            for objects in ((caller, library), (library, caller)):
                self.execute(objects)

    def test_closed_generic_owner_methods_and_weak_storage_share_exact_identity(self):
        provider = '''namespace GenericState;public class Box<T>{public static int32 calls=0;
public static int32 value(){calls++;return calls+20;}}
int32 answer(){return Box<int32>.value()+Box<string>.value();}'''
        consumer = '''using GenericState;int32 main(){if(answer()!=42){return 1;}
if(Box<int32>.value()!=22||Box<string>.value()!=22||Box<int64>.value()!=21){return 2;}
return Box<int32>.calls==2&&Box<string>.calls==2&&Box<int64>.calls==1?42:3;}'''
        for level in range(4):
            library = self.object(f'generic-provider-o{level}', provider, level)
            methods = [(name, symbol) for name, symbol in read_object(library)[1] if '.value$' in name]
            self.assertEqual(len(methods), 2)
            self.assertEqual(len({name for name, _ in methods}), 2)
            self.assertTrue(all(name.startswith('_KRT3$') and symbol[4] == 1 for name, symbol in methods))
            caller = self.object(f'generic-consumer-o{level}', consumer, level, (library,))
            imported = [(name, symbol) for name, symbol in read_object(caller)[1] if '.value$' in name]
            self.assertEqual(len(imported), 3)
            self.assertTrue(all(name.startswith('_KRT3$') and symbol[4] == 2 for name, symbol in imported))
            for objects in ((caller, library), (library, caller)):
                self.execute(objects)

    def test_generic_function_arguments_and_owner_are_in_symbol_identity(self):
        library = self.object('generic-identities', '''
int32 marker<T>(){return 7;}
class Owner<T>{public static int32 marker<U>(){return 7;}}
int32 answer(){return marker<int32*>()+marker<int32**>()+marker<int32*>()+
Owner<int32>.marker<string>()+Owner<int32>.marker<int32>()+Owner<string>.marker<int32>();}
''')
        symbols = read_object(library)[1]
        markers = [name.split('$local', 1)[0] for name, _ in symbols if 'marker$' in name]
        self.assertEqual(len(markers), 5)
        self.assertEqual(len(set(markers)), 5)
        self.assertIn('_KRT1$marker$G<Pi32;>$$i32', markers)
        self.assertIn('_KRT1$marker$G<PPi32;>$$i32', markers)
        owner_markers = [name for name in markers if '.marker$' in name]
        self.assertEqual(len(owner_markers), 3)
        self.assertTrue(all('.G<' in name and '.marker$G<' in name for name in owner_markers), owner_markers)
        caller = self.object('main', 'extern int32 answer();int32 main(){return answer();}')
        self.execute([caller, library])

    def test_multiple_pointer_depths_have_separate_external_symbols(self):
        library = self.object('pointer-depths', '''
int32 read(int32* value){unsafe(using krt.mem;){return *value;}}
int32 read(int32** value){unsafe(using krt.mem;){return **value+1;}}
''')
        names = {name for name, _ in read_object(library)[1]}
        self.assertIn('_KRT1$read$Pi32;$i32', names)
        self.assertIn('_KRT1$read$PPi32;$i32', names)
        caller = self.object('main', '''
extern int32 read(int32* value);extern int32 read(int32** value);
int32 main(){unsafe(using krt.mem;){int32 value=20;int32* pointer=&value;
return read(pointer)+read(&pointer)+1;}}
''')
        self.execute([caller, library])

    def test_callback_shape_pointer_depth_is_encoded_inside_the_signature(self):
        library = self.object('pointer-callback', '''
int32 apply(fn(int32*)->int32 callback,int32* value){return callback(value);}
''')
        names = {name for name, _ in read_object(library)[1]}
        self.assertIn('_KRT1$apply$fn2(Pi32;)>i32;Pi32;$i32', names)
        caller = self.object('main', '''
extern int32 apply(fn(int32*)->int32 callback,int32* value);
int32 read(int32* value){unsafe(using krt.mem;){return *value;}}
int32 main(){unsafe(using krt.mem;){int32 value=42;return apply(&read,&value);}}
''')
        self.execute([caller, library])

    def test_character_abi_symbols_are_distinct_from_unsigned_bytes(self):
        library = self.object('characters', '''
char letter(char value){return value;}
uint8 byte_value(uint8 value){return value;}
char first(char[] values){return values[0];}
''')
        names = {name for name, _ in read_object(library)[1]}
        self.assertIn('_KRT1$letter$c;$c', names)
        self.assertIn('_KRT1$byte_value$u8;$u8', names)
        self.assertIn('_KRT1$first$Ac;$c', names)
        caller = self.object('main', '''
extern char letter(char value);extern uint8 byte_value(uint8 value);extern char first(char[] values);
int32 main(){char[] letters=['B'];if(letter('A')!=65 || byte_value((uint8)67)!=67 || first(letters)!=66){return 1;}
delete letters;return 42;}
''')
        self.execute([caller, library])

    def test_project_uses_native_library_object_and_rebuilds_on_library_change(self):
        library = self.object('library', 'int32 answer(){return 42;}')
        (self.work / 'main.krt').write_text('extern int32 answer(); int32 main(){return answer();}')
        config = self.work / 'project.json'
        config.write_text(json.dumps({'sources': ['main.krt'], 'libraries': ['library.kro'], 'output': 'bin/demo'}))
        self.ok(self.invoke('build', config))
        self.assertEqual(subprocess.run([str(self.work / 'bin/demo')], timeout=10).returncode, 42)
        self.object('library', 'int32 answer(){return 17;}')
        result = self.invoke('build', config); self.ok(result); self.assertIn('built:', result.stdout)
        self.assertEqual(subprocess.run([str(self.work / 'bin/demo')], timeout=10).returncode, 17)


if __name__ == '__main__':
    unittest.main()
