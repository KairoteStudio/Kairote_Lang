"""Iterator and generator composition with full struct boxes and ownership."""
import os
from pathlib import Path
import re
import select
import subprocess
import time

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


PREFIX = 'using System.Collections.Generic;\n'

POSITIVE_CASES = {
    'mutable-boxed-struct-enumerator-and-disposal-errors': '''
class Audit{public int32 disposed;}
struct Cursor:IEnumerator<int32>{int32 value;int32 mode;Audit audit;
public bool MoveNext(){value++;if(mode==1&&value==2){throw 11;}return value<=3;}
public int32 Current(){if(mode==2&&value==2){throw 22;}return value;}
public void Dispose(){audit.disposed++;if(mode==3){throw 33;}}}
class Source{public Audit audit;public int32 mode;
public IEnumerator<int32> GetEnumerator(){Cursor cursor=default(Cursor);cursor.audit=audit;cursor.mode=mode;return cursor;}}
int32 main(){Audit audit=new Audit();Source source=new Source();source.audit=audit;int32 sum=0;
foreach(var value in source){sum+=value;}if(sum!=6||audit.disposed!=1){return 1;}
foreach(var value in source){break;}if(audit.disposed!=2){return 2;}
for(int32 mode=1;mode<=3;mode++){source.mode=mode;
try{foreach(var value in source){if(mode==3){break;}}return 3;}
catch(int32 error){if(error!=mode*11||audit.disposed!=mode+2){return 4;}}}
delete source;delete audit;return 0;}
''',
    'array-protocol-and-generator-current-box-full-struct-values': '''
interface Cost{int32 Read();}
struct Row:Cost{int32 id;uint128 wide;byte bytes[5];public int32 Read(){return id+bytes[4];}}
struct Cursor{Row Current;bool MoveNext(){Current.id++;Current.wide=((uint128)1<<100)+Current.id;Current.bytes[4]=(byte)(Current.id*3);return Current.id<=2;}}
struct Source{Cursor GetEnumerator(){return default(Cursor);}}
IEnumerable<Row> Rows(Row seed){for(int32 i=0;i<2;i++){seed.id++;seed.bytes[4]++;yield return seed;}}
int32 main(){Source source=default(Source);int32 total=0;
foreach(Cost face in source){if(face is Row row){if(row.wide!=((uint128)1<<100)+row.id||row.bytes[4]!=row.id*3){return 1;}
row.bytes[4]=99;}else{return 1;}total+=face.Read();delete face;}
Row[] array=new Row[2];for(int32 i=0;i<2;i++){array[i].id=i+3;array[i].wide=((uint128)1<<110)+i;array[i].bytes[4]=(byte)(i+7);}
foreach(object boxed in array){Row row=(Row)boxed;if(row.wide!=((uint128)1<<110)+row.id-3||row.bytes[4]!=row.id+4){return 2;}total+=row.Read();delete boxed;}
Row seed=default(Row);seed.id=5;seed.wide=((uint128)1<<120)+19;seed.bytes[4]=11;var rows=Rows(seed);
foreach(object boxed in rows){Row row=(Row)boxed;if(row.wide!=seed.wide||row.bytes[4]!=row.id+6){return 3;}total+=row.Read();delete boxed;}
delete rows;delete array;return total==72&&seed.bytes[4]==11?0:4;}
''',
    'object-extension-protocol-owns-boxed-cursor-without-dispose': '''
interface State{bool Step();int32 Read();}
struct Cursor:State{int32 value;public bool Step(){value++;return value<=3;}public int32 Read(){return value;}}
class Source{}
object GetEnumerator(object source){return default(Cursor);}
bool MoveNext(object cursor){State state=(State)cursor;return state.Step();}
int32 Current(object cursor){State state=(State)cursor;return state.Read();}
int32 main(){object source=new Source();int32 sum=0;foreach(var item in source){sum+=item;}delete source;return sum==6?0:1;}
''',
}

STRESS_SOURCE = PREFIX + '''
class Audit{public int32 disposed;}
struct Cursor:IEnumerator<int32>{int32 value;int32 mode;Audit audit;
public bool MoveNext(){value++;if(mode==1&&value==2){throw 11;}return value<=3;}
public int32 Current(){if(mode==2&&value==2){throw 22;}return value;}
public void Dispose(){audit.disposed++;if(mode==3){throw 33;}}}
class Source{public Audit audit;public int32 mode;
public IEnumerator<int32> GetEnumerator(){Cursor cursor=default(Cursor);cursor.audit=audit;cursor.mode=mode;return cursor;}}
int32 Check(){Audit audit=new Audit();Source source=new Source();source.audit=audit;int32 sum=0;
foreach(var value in source){sum+=value;}if(sum!=6||audit.disposed!=1){return 1;}
foreach(var value in source){break;}if(audit.disposed!=2){return 2;}
for(int32 mode=1;mode<=3;mode++){source.mode=mode;
try{foreach(var value in source){if(mode==3){break;}}return 3;}
catch(int32 error){if(error!=mode*11||audit.disposed!=mode+2){return 4;}}}
delete source;delete audit;return 0;}
void Mark(string message,int32 length){syscall(1,1,(int64)message,length,0,0,0);}
int32 Gate(){unsafe(using krt.mem;){byte token[1];int32 result=syscall(0,0,(int64)&token[0],1,0,0,0)==1?1:0;delete token;return result;}}
int32 main(){for(int32 warm=0;warm<16;warm++){int32 result=Check();if(result!=0){return result;}}
Mark("READY\\n",6);if(Gate()!=1){return 201;}
for(int32 i=0;i<25000;i++){int32 result=Check();if(result!=0){return result;}}
Mark("DONE\\n",5);if(Gate()!=1){return 202;}return 0;}
'''


class NativeIteratorBoxingTests(NativeCompilerFixture):
    def check_case(self, name):
        path = self.work / (name + '.krt'); path.write_text(PREFIX + POSITIVE_CASES[name])
        for target in ('native', 'vm'):
            for level in range(4):
                with self.subTest(fixture=name, target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env, capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b''); self.assertEqual(result.stderr, b'')

    def test_boxed_struct_cursor_preserves_state_and_disposal_error_cleanup(self):
        self.check_case('mutable-boxed-struct-enumerator-and-disposal-errors')

    def test_struct_array_protocol_and_generator_elements_box_complete_values(self):
        self.check_case('array-protocol-and-generator-current-box-full-struct-values')

    def test_object_extension_protocol_releases_cursor_box_without_dispose(self):
        self.check_case('object-extension-protocol-owns-boxed-cursor-without-dispose')

    def test_twenty_five_thousand_boxed_cursor_complete_abandon_and_exception_lifetimes(self):
        path = self.work / 'boxed-cursor-lifetimes.krt'; path.write_text(STRESS_SOURCE)

        def marker(process, expected):
            result = b''; deadline = time.monotonic() + 120
            while b'\n' not in result and time.monotonic() < deadline:
                ready, _, _ = select.select([process.stdout], [], [], 0.1)
                if ready:
                    chunk = os.read(process.stdout.fileno(), 4096)
                    if not chunk:
                        break
                    result += chunk
                if process.poll() is not None:
                    break
            self.assertEqual(result, expected, f'boxed cursor status={process.poll()} marker={result!r}')

        def memory(process):
            status = Path(f'/proc/{process.pid}/status').read_text(); result = {}
            for key in ('VmRSS', 'VmSize'):
                found = re.search(rf'^{key}:\s+(\d+) kB$', status, re.MULTILINE)
                self.assertIsNotNone(found, status); result[key] = int(found.group(1)) * 1024
            return result

        for target in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(target=target, optimization=level):
                    output = self.work / f'boxed-cursor-lifetimes-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    process = subprocess.Popen(argv, cwd=self.work, env=self.env, stdin=subprocess.PIPE,
                                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    try:
                        marker(process, b'READY\n'); before = memory(process)
                        process.stdin.write(b'1'); process.stdin.flush(); marker(process, b'DONE\n'); after = memory(process)
                        print(f'boxed cursor memory target={target} O{level} before={before} after={after}')
                        self.assertLessEqual(after['VmRSS'] - before['VmRSS'], 16 * 1024 * 1024, (before, after))
                        self.assertLessEqual(after['VmSize'] - before['VmSize'], 32 * 1024 * 1024, (before, after))
                        process.stdin.write(b'2'); process.stdin.flush(); stdout, stderr = process.communicate(timeout=15)
                        self.assertEqual(process.returncode, 0, stdout + stderr)
                        self.assertEqual(stdout, b''); self.assertEqual(stderr, b'')
                    finally:
                        if process.poll() is None:
                            process.kill()
                        process.communicate(timeout=3)
