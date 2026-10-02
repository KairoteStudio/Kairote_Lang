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

    def object(self, name, source):
        path = self.work / (name + '.krt')
        path.write_text(source)
        output = path.with_suffix('.kro')
        result = self.invoke(path, '-c', '-o', output)
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

    def test_class_containing_definitions_remain_distinct_local_symbols(self):
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
        self.assertTrue(all(symbol[4] == 0 for _, symbol in readers + makers))
        exported = [name for name, symbol in symbols if symbol[3] != 0 and symbol[4] == 1]
        self.assertEqual(len(exported), len(set(exported)))
        caller = self.object('main', 'extern int32 answer();int32 main(){return answer();}')
        self.execute([caller, library])

    def test_named_types_cannot_hide_in_external_array_ref_or_callback_signatures(self):
        declarations = (
            'extern void consume(Box[] values);',
            'extern void consume(Box* values);',
            'extern void consume(ref Box value);',
            'extern Box[] produce();',
            'extern void consume(fn()->Box callback);',
            'extern void consume(fn(fn()->Box)->void callback);',
            'extern void consume(fn(ref Box)->void callback);',
            'extern void consume(Value value);',
            'extern void consume(Generic<int32> value);',
            'class Api{public extern int32 Read();}',
        )
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                self.rejected('class Box{} class Generic<T>{} enum Value{First=0} ' + declaration + ' int32 main(){return 0;}', 'E_ABI_UNSUPPORTED')

    def test_named_exception_guards_apply_after_generic_specialization(self):
        for source in (
            'class Failure{} void fail<T>(){throw new Failure();} void invoke(){fail<int32>();}',
            'class Failure{} void handle<T>(fn()->void callback){try{callback();}catch(T error){}} '
            'void invoke(fn()->void callback){handle<Failure>(callback);}',
            'enum Failure{Bad=1} void fail(){throw Failure.Bad;}',
        ):
            with self.subTest(source=source):
                self.rejected(source)

    def test_interface_and_virtual_receiver_external_signatures_remain_rejected(self):
        prefix = 'interface Readable{int32 Read();} class Base{public virtual int32 Read(){return 1;}} '
        for declaration in (
            'extern void consume(Readable value);',
            'extern void consume(Readable[] values);',
            'extern void consume(fn()->Readable callback);',
            'extern void consume(fn(Readable)->void callback);',
            'extern void consume(Base value);',
        ):
            with self.subTest(declaration=declaration):
                self.rejected(prefix + declaration + 'int32 main(){return 0;}', 'E_ABI_UNSUPPORTED')

    def test_interface_exception_guards_remain_active_in_libraries(self):
        for source in (
            'interface Failure{}class Problem:Failure{}void fail(){Failure value=new Problem();throw value;}',
            'interface Failure{}void invoke(fn()->void callback){try{callback();}catch(Failure failure){}}',
        ):
            with self.subTest(source=source):
                self.rejected(source)

    def test_library_internal_interface_and_virtual_dispatch_stays_local(self):
        library = self.object('dispatch', '''
            interface Readable{int32 Read();}
            class First:Readable{public virtual int32 Read(){return 17;}}
            class Second:First{public override int32 Read(){return 25;}}
            int32 answer(){Readable first=new First();Readable second=new Second();
                int32 result=first.Read()+second.Read();delete first;delete second;return result;}
        ''')
        symbols = read_object(library)[1]
        exported = [name for name, symbol in symbols if symbol[3] != 0 and symbol[4] == 1]
        self.assertEqual(exported, ['_KRT1$answer$$i32'])
        caller = self.object('main', 'extern int32 answer();int32 main(){return answer();}')
        self.execute([caller, library])


if __name__ == '__main__':
    unittest.main()
