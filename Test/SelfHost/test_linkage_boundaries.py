"""Native object boundaries preserve strong definitions and exact type identity."""
import subprocess
import tempfile
import unittest
from pathlib import Path

from Test.SelfHost.test_native_linking import COMPILER, LINKER, ROOT, read_object


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), 'build a native compiler and ArkLink')
class NativeLinkageBoundaries(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='krt linkage boundaries ')
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)

    def invoke(self, *args):
        return subprocess.run([str(ROOT / 'SelfHost/krtc'), *map(str, args),
                               '--compiler', str(COMPILER), '--linker', str(LINKER)],
                              cwd=self.work, capture_output=True, text=True, timeout=60)

    def object(self, name, source, level=2, objects=()):
        path = self.work / (name + '.krt')
        path.write_text(source)
        output = path.with_suffix('.kro')
        result = self.invoke(path, *objects, f'-O{level}', '-c', '-o', output)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return output

    def execute(self, objects, expected=42):
        output = self.work / 'program'
        result = self.invoke(*objects, '-o', output)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run = subprocess.run([output], capture_output=True, timeout=10)
        self.assertEqual(run.returncode, expected, run.stdout + run.stderr)

    def rejected(self, source, code="E_LOWER"):
        path = self.work / 'rejected.krt'
        path.write_text(source)
        output = self.work / 'rejected.kro'
        output.write_bytes(b'previous object')
        result = self.invoke(path, '-c', '-o', output)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(code, result.stderr)
        self.assertEqual(output.read_bytes(), b'previous object')

    def test_duplicate_native_strong_definitions_reject_both_orders(self):
        caller = self.object('main', 'extern int32 answer();int32 main(){return answer();}')
        first = self.object('first', 'int32 answer(){return 17;}')
        second = self.object('second', 'int32 answer(){return 42;}')
        same = self.object('same', 'int32 answer(){return 17;}')
        output = self.work / 'program'
        for objects in ((first, second), (second, first), (first, same), (same, first)):
            with self.subTest(objects=objects):
                output.write_bytes(b'previous executable')
                result = self.invoke(caller, *objects, '-o', output)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('E_LINK', result.stderr)
                self.assertEqual(output.read_bytes(), b'previous executable')

    def test_custom_integer_exception_precision_survives_object_boundary(self):
        library = self.object('throws', '''
            void fail30(){throw (int30)-7;}
            void fail_unsigned30(){throw (uint30)1073741823;}
            class Payload{public int32 number=17;}
            void fail_field(){Payload value=new Payload();int32 number=value.number;delete value;throw number;}
            void fail_direct_field(){Payload value=new Payload();throw value.number;}
        ''')
        caller = self.object('main', '''
            extern void fail30();extern void fail_unsigned30();extern void fail_field();extern void fail_direct_field();
            int32 main(){
                try{fail30();}catch(int32 n){return 1;}catch(int30 n){if(n!=-7){return 2;}}
                try{fail_unsigned30();}catch(uint32 n){return 3;}catch(int30 n){return 4;}
                    catch(uint30 n){if(n!=1073741823){return 5;}}
                try{fail_field();}catch(int64 n){return 6;}catch(int32 n){if(n!=17){return 7;}}
                try{fail_direct_field();}catch(int64 n){return 8;}catch(int32 n){if(n!=17){return 9;}}
                return 42;
            }
        ''')
        self.execute([caller, library])

    def test_class_containing_definitions_export_distinct_exact_symbols(self):
        library = self.object('classes', '''
            class Box<T>{}
            int32 read(Box<int32> value){return 17;}
            int32 read(Box<string> value){return 25;}
            Box<int32> make(){return new Box<int32>();}
            int32 answer(){Box<int32> first=make();Box<string> second=new Box<string>();
                int32 result=read(first)+read(second);delete first;delete second;return result;}
        ''')
        symbols = read_object(library)[1]
        readers = [(name, symbol) for name, symbol in symbols if '$read$' in name]
        makers = [(name, symbol) for name, symbol in symbols if '$make$' in name]
        self.assertEqual(len(readers), 2)
        self.assertEqual(len({name for name, _ in readers}), 2)
        self.assertTrue(all(name.startswith('_KRT3$') and symbol[4] == 1 for name, symbol in readers + makers))
        exported = [name for name, symbol in symbols if symbol[3] != 0 and symbol[4] == 1]
        self.assertEqual(len(exported), len(set(exported)))
        caller = self.object('main', 'extern int32 answer();int32 main(){return answer();}')
        self.execute([caller, library])

    def test_named_array_ref_pointer_and_nested_callback_boundaries_execute(self):
        declarations = '''namespace Shapes;
public class Box{public int32 n;public extern Box(int32 initial);}
public class Generic<T>{public T value;public extern Generic(T initial);}
public enum Value{First=7}
public class Api{public extern Api();public extern int32 Read();}
'''
        definitions = declarations.replace('public extern Box(int32 initial);', 'public Box(int32 initial){n=initial;}').replace(
            'public extern Generic(T initial);', 'public Generic(T initial){value=initial;}').replace(
            'public extern Api();', 'public Api(){}').replace('public extern int32 Read();', 'public int32 Read(){return 42;}')
        cases = (
            ('array', 'int32 consume(Box[] values)', '{return values[0].n+23;}',
             '', 'Box[] values=new Box[1];values[0]=new Box(19);int32 result=consume(values);delete values[0];delete values;return result;'),
            ('pointer', 'int32 consume(Box* value)', '{unsafe(using krt.mem;){return (*value).n+23;}}',
             '', 'unsafe(using krt.mem;){Box value=new Box(19);int32 result=consume(&value);delete value;return result;}'),
            ('reference', 'int32 consume(ref Box value)', '{value.n+=23;return value.n;}',
             '', 'Box value=new Box(19);int32 result=consume(ref value);delete value;return result;'),
            ('result-array', 'Box[] produce()', '{Box[] values=new Box[1];values[0]=new Box(42);return values;}',
             '', 'Box[] values=produce();int32 result=values[0].n;delete values[0];delete values;return result;'),
            ('callback-result', 'int32 consume(fn()->Box callback)', '{Box value=callback();int32 result=value.n+23;delete value;return result;}',
             'Box Make(){return new Box(19);}', 'return consume(&Make);'),
            ('nested-callback', 'int32 consume(fn(fn()->Box)->void callback)', '{callback(&Make);return 42;}',
             'void Accept(fn()->Box factory){Box value=factory();if(value.n!=19){throw 1;}delete value;}', 'return consume(&Accept);'),
            ('callback-ref', 'int32 consume(fn(ref Box)->void callback)', '{Box value=new Box(19);callback(ref value);int32 result=value.n;delete value;return result;}',
             'void Adjust(ref Box value){value.n+=23;}', 'return consume(&Adjust);'),
            ('enum', 'int32 consume(Value value)', '{return (int32)value+35;}', '', 'return consume(Value.First);'),
            ('closed-class', 'int32 consume(Generic<int32> value)', '{return value.value+23;}',
             '', 'Generic<int32> value=new Generic<int32>(19);int32 result=consume(value);delete value;return result;'),
            ('method', '', '', '', 'Api value=new Api();int32 result=value.Read();delete value;return result;'),
        )
        for name, signature, body, local, main in cases:
            for level in range(4):
                with self.subTest(boundary=name, optimization=level):
                    provider = definitions + ('Box Make(){return new Box(19);}' if name == 'nested-callback' else '')
                    provider += signature + body
                    consumer = declarations + ('extern ' + signature + ';' if signature else '') + local + 'int32 main(){' + main + '}'
                    library = self.object(f'shape-{name}-provider-o{level}', provider, level)
                    caller = self.object(f'shape-{name}-consumer-o{level}', consumer, level)
                    names = {item for item, _ in read_object(library)[1]}
                    self.assertIn('_KRT_ABI3_MANIFEST', names)
                    for objects in ((caller, library), (library, caller)):
                        self.execute(objects)

    def test_named_exception_identity_survives_generic_specialization(self):
        declaration = 'public class Failure{public int32 code;public extern Failure();}'
        definition = declaration.replace('public extern Failure();', 'public Failure(){code=42;}')
        cases = (
            (definition + 'void fail<T>(){throw new Failure();}void invoke(){fail<int32>();}',
             declaration + 'extern void invoke();int32 main(){try{invoke();return 1;}catch(Failure value){int32 result=value.code;delete value;return result;}}'),
            (definition + 'int32 handle<T>(fn()->void callback){try{callback();return 1;}catch(T error){delete error;return 42;}}int32 invoke(fn()->void callback){return handle<Failure>(callback);}',
             declaration + 'extern int32 invoke(fn()->void callback);void Raise(){throw new Failure();}int32 main(){return invoke(&Raise);}'),
            ('public enum Failure{Bad=1}void fail(){throw Failure.Bad;}',
             'public enum Failure{Bad=1}extern void fail();int32 main(){try{fail();return 1;}catch(Failure value){return value==Failure.Bad?42:2;}}'),
        )
        for index, (provider, consumer) in enumerate(cases):
            for level in range(4):
                with self.subTest(source=index, optimization=level):
                    library = self.object(f'generic-throw-{index}-provider-o{level}', provider, level)
                    caller = self.object(f'generic-throw-{index}-consumer-o{level}', consumer, level)
                    for objects in ((caller, library), (library, caller)):
                        self.execute(objects)

    def test_interface_and_virtual_receivers_cross_external_signatures(self):
        declarations = '''public interface Readable{int32 Read();}
public class Base:Readable{public extern Base();public virtual extern int32 Read();}'''
        definitions = declarations.replace('public extern Base();', 'public Base(){}').replace(
            'public virtual extern int32 Read();', 'public virtual int32 Read(){return 42;}')
        cases = (
            ('value', 'int32 consume(Readable value){return value.Read();}',
             'extern int32 consume(Readable value);', '', 'Base value=new Base();int32 result=consume(value);delete value;return result;'),
            ('array', 'int32 consume(Readable[] values){return values[0].Read();}',
             'extern int32 consume(Readable[] values);', '', 'Readable[] values=new Readable[1];values[0]=new Base();int32 result=consume(values);delete values[0];delete values;return result;'),
            ('callback-result', 'int32 consume(fn()->Readable callback){Readable value=callback();int32 result=value.Read();delete value;return result;}',
             'extern int32 consume(fn()->Readable callback);', 'Readable Make(){return new Base();}', 'return consume(&Make);'),
            ('callback-parameter', 'int32 consume(fn(Readable)->void callback){Readable value=new Base();callback(value);int32 result=value.Read();delete value;return result;}',
             'extern int32 consume(fn(Readable)->void callback);', 'void Check(Readable value){if(value.Read()!=42){throw 1;}}', 'return consume(&Check);'),
            ('virtual', 'int32 consume(Base value){return value.Read();}',
             'extern int32 consume(Base value);', '', 'Base value=new Base();int32 result=consume(value);delete value;return result;'),
        )
        for name, implementation, declaration, local, body in cases:
            for level in range(4):
                with self.subTest(boundary=name, optimization=level):
                    library = self.object(f'interface-{name}-provider-o{level}', definitions + implementation, level)
                    caller = self.object(f'interface-{name}-consumer-o{level}', declarations + declaration + local + 'int32 main(){' + body + '}', level)
                    for objects in ((caller, library), (library, caller)):
                        self.execute(objects)

    def test_unknown_interface_exceptions_match_across_both_modules(self):
        declarations = 'public interface Failure{int32 Read();}'
        provider = declarations + '''private class Problem:Failure{public int32 Read(){return 42;}}
void fail(){Failure value=new Problem();throw value;}
int32 invoke(fn()->void callback){try{callback();return 1;}catch(Failure value){int32 result=value.Read();delete value;return result;}}'''
        consumer = declarations + '''private class Local:Failure{public int32 Read(){return 42;}}
extern void fail();extern int32 invoke(fn()->void callback);
void Raise(){Failure value=new Local();throw value;}
int32 main(){try{fail();return 1;}catch(Failure value){int32 result=value.Read();delete value;if(result!=42){return 2;}}
return invoke(&Raise);}'''
        for level in range(4):
            library = self.object(f'interface-throw-provider-o{level}', provider, level)
            caller = self.object(f'interface-throw-consumer-o{level}', consumer, level)
            for objects in ((caller, library), (library, caller)):
                self.execute(objects)

    def test_library_interface_and_virtual_dispatch_exports_complete_members(self):
        library = self.object('dispatch', '''
            interface Readable{int32 Read();}
            class First:Readable{public virtual int32 Read(){return 17;}}
            class Second:First{public override int32 Read(){return 25;}}
            int32 answer(){Readable first=new First();Readable second=new Second();
                int32 result=first.Read()+second.Read();delete first;delete second;return result;}
        ''')
        symbols = read_object(library)[1]
        exported = [name for name, symbol in symbols if symbol[3] != 0 and symbol[4] == 1]
        self.assertIn('_KRT1$answer$$i32', exported)
        self.assertTrue(any(name.startswith('_KRT3$First.Read$') for name in exported))
        self.assertTrue(any(name.startswith('_KRT3$Second.Read$') for name in exported))
        caller = self.object('main', 'int32 main(){Second value=new Second();Readable face=value;int32 result=face.Read();delete value;return result==25&&answer()==42?42:1;}', objects=(library,))
        for objects in ((caller, library), (library, caller)):
            self.execute(objects)


if __name__ == '__main__':
    unittest.main()
