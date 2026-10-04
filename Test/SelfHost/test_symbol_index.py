"""Function-name indexing preserves native overload and scope contracts."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()
LINKER = Path(os.environ.get('ARKLINK', ROOT / 'build/ArkLink/ArkLink')).resolve()


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), 'build the native compiler and ArkLink first')
class SymbolIndexTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='krt-symbol-index-')
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.env = dict(os.environ, PATH='')
        self.counter = 0

    def check(self, sources, *, level, vm, code=None):
        self.counter += 1
        paths = []
        for name, source in sources.items():
            path = self.work / f'{self.counter}-{name}'
            path.write_text(source)
            paths.append(path)
        output = self.work / f'program-{self.counter}'
        sentinel = b'existing valid output must survive\x00'
        if code:
            output.write_bytes(sentinel)
        command = [str(COMPILER), '--linker', str(LINKER), *map(str, paths),
                   f'-O{level}', '-o', str(output)]
        if vm:
            command += ['target', 'vm']
        result = subprocess.run(command, cwd=self.work, env=self.env,
                                capture_output=True, text=True, timeout=60)
        detail = result.stdout + result.stderr + '\n' + '\n'.join(sources.values())
        if code:
            self.assertEqual(result.returncode, 1, detail)
            self.assertIn(code, result.stderr)
            self.assertNotIn('E_PARSE', result.stderr)
            self.assertEqual(output.read_bytes(), sentinel)
            output.unlink()
            missing = subprocess.run(command, cwd=self.work, env=self.env,
                                     capture_output=True, text=True, timeout=60)
            self.assertEqual(missing.returncode, 1, missing.stdout + missing.stderr)
            self.assertIn(code, missing.stderr)
            self.assertFalse(output.exists())
            return
        self.assertEqual(result.returncode, 0, detail)
        argv = [str(COMPILER), 'run-vm', str(output)] if vm else [str(output)]
        executed = subprocess.run(argv, cwd=self.work, env=self.env,
                                  capture_output=True, text=True, timeout=10)
        self.assertEqual(executed.returncode, 0, detail + executed.stdout + executed.stderr)

    def matrix(self, sources, *, code=None):
        for level in (0, 2):
            for vm in (False, True):
                with self.subTest(optimization=level, vm=vm, sources=tuple(sources), code=code):
                    self.check(sources, level=level, vm=vm, code=code)

    @staticmethod
    def colliding_names():
        # Make distinct valid identifiers collide in the low ten hash bits;
        # exact string comparison must remain authoritative after table growth.
        groups = {}
        number = 0
        while True:
            name = f'sample_{number}'
            value = 14695981039346656037
            for byte in name.encode():
                value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
            bucket = groups.setdefault(value & 1023, [])
            bucket.append(name)
            if len(bucket) == 4:
                return bucket
            number += 1

    def test_collisions_growth_overload_order_and_live_generic_methods(self):
        names = self.colliding_names()
        fillers = '\n'.join(f'int32 unused_{index}(){{return {index};}}' for index in range(140))
        collisions = '\n'.join(f'int32 {name}(){{return {17 + index};}}' for index, name in enumerate(names))
        checks = '\n'.join(f'if({name}()!={17 + index}){{return 1;}}' for index, name in enumerate(names))
        overloads = ('int32 Pick(int32 value){return value+1;}',
                     'int64 Pick(int64 value){return value+2;}')
        generic = '''
class Box<T>{public T value;public Box(T item){value=item;}public T Get(){return value;}}
T identity<T>(T value){return value;}
T descend<T>(T value,int32 depth){if(depth==0){return identity(value);}return descend<T>(value,depth-1);}
int32 pick(){return 9;}
'''
        body = '''
int32 main(){CHECKS
if(Pick((int32)16)!=17||Pick((int64)23)!=25||pick()!=9){return 2;}
Box<int32> first=new Box<int32>(17);Box<int64> second=new Box<int64>(25);
Box<string> third=new Box<string>("live");
if(first.Get()!=17||second.Get()!=25||third.Get()!="live"){return 3;}
if(identity(first.Get())+descend<int64>(second.Get(),3)!=42){return 4;}
delete first;delete second;delete third;return 0;}
'''.replace('CHECKS', checks)
        for declarations in (overloads, tuple(reversed(overloads))):
            self.matrix({'main.krt': fillers + collisions + '\n'.join(declarations) + generic + body})

    def test_original_file_priority_qualified_using_and_function_address(self):
        a = '''namespace Shared;private int32 local(){return 17;}
public int32 FromA(){fn()->int32 callback=&local;return callback();}
'''
        b = '''namespace Shared;private int32 local(){return 25;}
public int32 FromB(){return local();}
'''
        main = '''
namespace Left {int32 Value(){return 17;} class Ops{public static int32 Value(){return 17;}}}
namespace Right {int32 Value(){return 25;} class Ops{public static int32 Value(){return 25;}}}
namespace App {
using Left;
using Call=Left.Ops;
int32 Read(){return Value();}
namespace Nested {using Call=Right.Ops;int32 Read(){return Call.Value();}}
}
int32 main(){fn()->int32 callback=&Left.Value;
if(Shared.FromA()+Shared.FromB()!=42||App.Read()!=17||App.Nested.Read()!=25){return 1;}
if(callback()+Right.Value()!=42){return 2;}return 0;}
'''
        for order in (('a.krt', a, 'b.krt', b), ('b.krt', b, 'a.krt', a)):
            self.matrix({order[0]: order[1], order[2]: order[3], 'main.krt': main})

    def test_filtering_preserves_failure_codes_and_existing_output(self):
        fixtures = (
            ('E_ARGUMENT_COUNT', {'main.krt': 'int32 value(int32 item){return item;}int32 main(){return value();}'}),
            ('E_ARGUMENT_NAME', {'main.krt': 'int32 value(int32 item){return item;}int32 main(){return value(missing:42);}'}),
            ('E_ACCESS', {'hidden.krt': 'private int32 value(){return 42;}',
                          'main.krt': 'int32 main(){return value();}'}),
            ('E_ACCESS', {'main.krt': 'class Box{private static int32 value(){return 42;}}int32 main(){fn()->int32 callback=&Box.value;return callback();}'}),
            ('E_UNDEFINED_METHOD', {'main.krt': 'namespace One{int32 value(){return 42;}}int32 main(){return absent();}'}),
        )
        for code, sources in fixtures:
            self.matrix(sources, code=code)


if __name__ == '__main__':
    unittest.main()
