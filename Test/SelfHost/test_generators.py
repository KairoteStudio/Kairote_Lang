"""Native lazy generators: suspension, persistent activation and cleanup."""
import re
import subprocess
import os
from pathlib import Path
import select
import time

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


PREFIX = 'using System.Collections.Generic;\n'

POSITIVE_CASES = {
    'custom-inherited-generic-sequence-and-cursor-contracts': '''
interface Advance{bool MoveNext();void Dispose();}
interface Read<T>{T Current();}
interface Cursor<T>:Advance,Read<T>{}
interface NamedCursor<T>:Cursor<T>{}
interface Sequence<T>{NamedCursor<T> GetEnumerator();}
interface NamedSequence<T>:Sequence<T>{}
struct Row{int32 id;uint128 wide;byte bytes[5];}
class Audit{public int32 entered;public int32 finished;}
NamedSequence<Row> Rows(Row seed,Audit audit){audit.entered++;
try{for(int32 i=0;i<3;i++){seed.id++;seed.wide+=1;seed.bytes[4]++;yield return seed;}}
finally{audit.finished++;}}
NamedCursor<int32> Direct(int32 value,Audit audit){audit.entered++;
try{yield return value;yield return value+1;}finally{audit.finished++;}}
int32 main(){Audit audit=new Audit();Row seed=default(Row);seed.id=7;
seed.wide=((uint128)1<<100)+19;seed.bytes[0]=31;seed.bytes[4]=37;
NamedSequence<Row> sequence=Rows(seed,audit);Sequence<Row> inherited=sequence;
if(audit.entered!=0||!(sequence is Sequence<Row>)){return 1;}
NamedCursor<Row> cursor=inherited.GetEnumerator();Cursor<Row> ancestor=cursor;Read<Row> reader=ancestor;Advance mover=ancestor;
delete sequence;if(audit.entered!=0||!(ancestor is NamedCursor<Row>)||(ancestor is Cursor<int32>)){return 2;}
if(!mover.MoveNext()){return 3;}Row first=reader.Current();Row independent=cursor.Current();first.bytes[4]=99;
if(independent.id!=8||independent.wide!=seed.wide+1||independent.bytes[0]!=31||independent.bytes[4]!=38){return 4;}
if(!ancestor.MoveNext()){return 5;}Row second=reader.Current();
if(second.id!=9||second.wide!=seed.wide+2||second.bytes[4]!=39||independent.bytes[4]!=38){return 6;}
mover.Dispose();if(audit.entered!=1||audit.finished!=1||seed.bytes[4]!=37){return 7;}
var replay=Rows(seed,audit);int32 sum=0;foreach(var row in replay){sum+=row.id;}delete replay;
if(sum!=27||audit.entered!=2||audit.finished!=2){return 8;}
NamedCursor<int32> direct=Direct(41,audit);Advance next=direct;Read<int32> current=direct;
if(audit.entered!=2||!next.MoveNext()||current.Current()!=41||!direct.MoveNext()||direct.Current()!=42||next.MoveNext()){return 9;}
next.Dispose();int32 result=audit.entered==3&&audit.finished==3?0:10;delete audit;return result;}
''',
    'lazy-independent-cursors': '''
class Audit{public int32 entered;public int32 yielded;public int32 finished;}
IEnumerable<int32> Numbers(int32 first,int32 count,Audit audit){audit.entered++;
try{for(int32 i=0;i<count;i++){audit.yielded++;yield return first+i;}}finally{audit.finished++;}}
int32 main(){Audit audit=new Audit();var sequence=Numbers(7,3,audit);
if(audit.entered!=0||audit.yielded!=0||audit.finished!=0){return 1;}
var a=sequence.GetEnumerator();var b=sequence.GetEnumerator();
if(audit.entered!=0||!a.MoveNext()||a.Current()!=7||audit.entered!=1||audit.yielded!=1){return 2;}
if(!b.MoveNext()||b.Current()!=7||audit.entered!=2||audit.yielded!=2){return 3;}
if(!a.MoveNext()||a.Current()!=8||b.Current()!=7){return 4;}a.Dispose();
if(audit.finished!=1){return 5;}int32 sum=b.Current();while(b.MoveNext()){sum+=b.Current();}
if(sum!=24||b.MoveNext()||audit.finished!=2){return 6;}b.Dispose();
int32 replay=0;foreach(var item in sequence){replay+=item;}
if(replay!=24||audit.entered!=3||audit.finished!=3||audit.yielded!=8){return 7;}
delete sequence;delete audit;return 0;}
''',
    'persistent-locals-ref-and-loop-branches': '''
void Add(ref int32 value,int32 amount){value+=amount;}
IEnumerable<int32> Values(int32 seed){int32 shared=seed;
for(int32 i=0;i<6;i++){if(i==1){continue;}Add(ref shared,i);yield return shared;if(i==4){break;}}
int32 turns=0;while(turns<2){yield return shared+turns;turns++;}
do{shared++;yield return shared;}while(shared<13);yield break;}
int32 main(){var sequence=Values(3);int32 expected[7];expected[0]=3;expected[1]=5;expected[2]=8;expected[3]=12;
expected[4]=12;expected[5]=13;expected[6]=13;int32 index=0;
foreach(var value in sequence){if(index>=7||value!=expected[index]){return 1;}index++;}
delete sequence;delete expected;return index==7?0:2;}
''',
    'nested-loops-switch-and-if-resume': '''
IEnumerable<int32> Grid(){for(int32 row=0;row<3;row++){for(int32 column=0;column<3;column++){
if(column==1){yield return row*10+column;continue;}switch(column){case 0:yield return row*10;break;
default:yield return row*10+column;break;}if(row==1&&column==2){break;}}}}
int32 main(){var sequence=Grid();int32 count=0;int32 sum=0;foreach(var value in sequence){count++;sum+=value;}
delete sequence;return count==9&&sum==99?0:1;}
''',
    'try-finally-dispose-and-throw-order': '''
class Audit{public int32 log;}
IEnumerable<int32> Values(Audit audit){try{try{yield return 42;yield return 43;}
finally{audit.log=audit.log*10+1;}}finally{audit.log=audit.log*10+2;}}
IEnumerable<int32> Raising(Audit audit){try{yield return 7;throw 99;}finally{audit.log=audit.log*10+3;}}
int32 main(){Audit audit=new Audit();var sequence=Values(audit);
foreach(var value in sequence){if(value!=42||audit.log!=0){return 1;}break;}
if(audit.log!=12){return 2;}delete sequence;var raising=Raising(audit);var cursor=raising.GetEnumerator();
if(!cursor.MoveNext()||cursor.Current()!=7){return 3;}try{cursor.MoveNext();return 4;}
catch(int32 error){if(error!=99||audit.log!=123){return 5;}}
if(cursor.MoveNext()){return 6;}cursor.Dispose();delete raising;delete audit;return 0;}
''',
    'dispose-finally-exception-runs-outer-finally': '''
class Audit{public int32 log;}
IEnumerable<int32> Values(Audit audit){try{try{yield return 42;}
finally{audit.log=audit.log*10+1;throw 77;}}finally{audit.log=audit.log*10+2;}}
int32 main(){Audit audit=new Audit();var sequence=Values(audit);var cursor=sequence.GetEnumerator();
if(!cursor.MoveNext()||cursor.Current()!=42){return 1;}try{cursor.Dispose();return 2;}
catch(int32 error){if(error!=77||audit.log!=12){return 3;}}delete sequence;delete audit;return 0;}
''',
    'escaping-closures-own-persistent-cells': '''
IEnumerable<fn()->int32> Callbacks(int32 initial){int32 shared=initial;
for(int32 i=0;i<3;i++){int32 local=i;yield return function()=>{shared++;local+=10;return shared*100+local;};}}
int32 main(){var sequence=Callbacks(5);fn()->int32 saved[3];int32 index=0;
foreach(var callback in sequence){saved[index]=callback;index++;}delete sequence;
if(index!=3||saved[0]()!=610||saved[1]()!=711||saved[0]()!=820||saved[2]()!=912){return 1;}
for(int32 i=0;i<3;i++){delete saved[i];}delete saved;return 0;}
''',
    'complete-struct-fixed-array-and-wide-copies': '''
struct Record{int32 id;int32 values[3];uint128 large;}
IEnumerable<Record> Records(Record original){Record local=original;for(int32 i=0;i<3;i++){
local.id++;local.values[0]+=3;local.values[2]+=7;local.large+=1;yield return local;}}
int32 main(){Record original=default(Record);original.id=5;original.values[0]=10;original.values[2]=20;original.large=(uint128)1<<100;
var sequence=Records(original);original.values[0]=99;var cursor=sequence.GetEnumerator();delete sequence;
if(!cursor.MoveNext()){return 1;}Record first=cursor.Current();Record alias=cursor.Current();first.values[0]=77;
if(alias.id!=6||alias.values[0]!=13||alias.values[2]!=27||alias.large!=((uint128)1<<100)+1){return 2;}
if(!cursor.MoveNext()){return 3;}Record second=cursor.Current();if(second.id!=7||second.values[0]!=16||second.values[2]!=34||second.large!=((uint128)1<<100)+2){return 4;}
if(!cursor.MoveNext()||cursor.MoveNext()){return 5;}cursor.Dispose();return first.values[0]==77&&alias.values[0]==13?0:6;}
''',
    'wide-and-floating-parameters-outlive-sequence': '''
IEnumerable<uint128> Wides(uint128 value,float64 step){for(int32 i=0;i<3;i++){value+=(uint128)step;yield return value;}}
int32 main(){var sequence=Wides(((uint128)1<<100)+5,3.0);var cursor=sequence.GetEnumerator();delete sequence;
for(int32 i=0;i<3;i++){if(!cursor.MoveNext()||cursor.Current()!=((uint128)1<<100)+8+(uint128)(i*3)){return 1;}}
if(cursor.MoveNext()){return 2;}cursor.Dispose();return 0;}
''',
    'generic-and-class-factory-contexts': '''
IEnumerable<T> Repeat<T>(T value,int32 times){for(int32 i=0;i<times;i++){yield return value;}}
class Owner{private int32 value=7;public IEnumerable<int32> Items(int32 count){for(int32 i=0;i<count;i++){yield return value+i;}}}
int32 main(){var repeated=Repeat(14,3);int32 sum=0;foreach(var value in repeated){sum+=value;}delete repeated;
Owner owner=new Owner();var items=owner.Items(3);foreach(var item in items){sum+=item;}delete items;delete owner;
return sum==66?0:1;}
''',
    'direct-enumerator-and-invalid-current': '''
IEnumerator<int32> Cursor(int32 start){yield return start;yield return start+1;}
int32 main(){var cursor=Cursor(41);try{cursor.Current();return 1;}catch(int32 error){if(error!=-2147483601){return 2;}}
if(!cursor.MoveNext()||cursor.Current()!=41||!cursor.MoveNext()||cursor.Current()!=42||cursor.MoveNext()){return 3;}
try{cursor.Current();return 4;}catch(int32 error){if(error!=-2147483601){return 5;}}
cursor.Dispose();return 0;}
''',
    'foreach-inside-generator-finally-and-fresh-cells': '''
struct Cursor{int32 Current;bool MoveNext(){Current++;return Current<=4;}}
struct Range{Cursor GetEnumerator(){return default(Cursor);}}
IEnumerable<int32> Values(){Range range=default(Range);foreach(var item in range){var callback=function()=>item*10;
try{yield return callback();}finally{delete callback;}}}
int32 main(){var sequence=Values();int32 sum=0;foreach(var value in sequence){sum+=value;}delete sequence;return sum==100?0:1;}
''',
    'real-paged-record-generator': '''
struct Record{int32 id;int32 cost;bool active;}
IEnumerable<Record> Page(Record[] records,int32 skip,int32 limit){int32 emitted=0;
foreach(var record in records){if(!record.active){continue;}if(skip>0){skip--;continue;}
yield return record;emitted++;if(emitted>=limit){yield break;}}}
int32 main(){Record[] records=new Record[8];for(int32 i=0;i<8;i++){records[i].id=i+1;records[i].cost=(i+1)*100;records[i].active=i%2==0;}
var page=Page(records,1,2);int32 ids=0;int32 total=0;foreach(var record in page){ids=ids*10+record.id;total+=record.cost;}
delete page;delete records;return ids==35&&total==800?0:1;}
''',
    'empty-before-first-and-first-call-exception': '''
class Audit{public int32 entered;public int32 finished;}
IEnumerable<int32> Values(Audit audit){audit.entered++;try{yield return 42;}finally{audit.finished++;}}
IEnumerator<int32> Empty(Audit audit){audit.entered++;yield break;}
IEnumerator<int32> Raising(Audit audit){audit.entered++;try{throw 42;yield return 0;}finally{audit.finished++;}}
int32 main(){Audit audit=new Audit();var sequence=Values(audit);var unused=sequence.GetEnumerator();unused.Dispose();
if(audit.entered!=0||audit.finished!=0){return 1;}delete sequence;var empty=Empty(audit);
if(empty.MoveNext()||empty.MoveNext()||audit.entered!=1||audit.finished!=0){return 2;}empty.Dispose();
var raising=Raising(audit);try{raising.MoveNext();return 3;}catch(int32 value){if(value!=42){return 4;}}
if(raising.MoveNext()||audit.entered!=2||audit.finished!=1){return 5;}raising.Dispose();delete audit;return 0;}
''',
    'dispose-cleanup-reentrancy-is-guarded': '''
class Audit{public IEnumerator<int32> cursor;public int32 rejected;}
IEnumerable<int32> Values(Audit audit){try{yield return 42;}finally{
try{audit.cursor.Dispose();}catch(int32 code){if(code==-2147483602){audit.rejected++;}else{throw code;}}
try{audit.cursor.MoveNext();}catch(int32 code){if(code==-2147483602){audit.rejected++;}else{throw code;}}}}
int32 main(){Audit audit=new Audit();var sequence=Values(audit);var cursor=sequence.GetEnumerator();audit.cursor=cursor;
if(!cursor.MoveNext()||cursor.Current()!=42){return 1;}cursor.Dispose();delete sequence;
int32 result=audit.rejected==2?0:2;delete audit;return result;}
''',
    'multiple-closed-generic-items-and-method-contexts': '''
struct Record{int32 id;int32 values[2];}
IEnumerable<T> Repeat<T>(T item,int32 count){for(int32 i=0;i<count;i++){yield return item;}}
class Owner<T>{public T item;public IEnumerable<T> Items(){yield return item;yield return item;}}
int32 main(){Record record=default(Record);record.id=7;record.values[0]=11;record.values[1]=13;
var records=Repeat(record,2);int32 sum=0;foreach(var row in records){sum+=row.id+row.values[0]+row.values[1];row.values[0]=0;}delete records;
var numbers=Repeat(5,3);foreach(var number in numbers){sum+=number;}delete numbers;
var texts=Repeat("ok",2);foreach(var text in texts){if(text[0]!='o'||text[1]!='k'){return 1;}}delete texts;
Owner<Record> owner=new Owner<Record>();owner.item=record;var owned=owner.Items();foreach(var row in owned){sum+=row.id;}delete owned;delete owner;
return sum==91&&record.values[0]==11?0:2;}
''',
    'yield-break-in-catch-and-overload-probe-rollback': '''
class Audit{public int32 finished;}
IEnumerable<T> Repeat<T>(T value,int32 count){for(int32 i=0;i<count;i++){yield return value;}}
IEnumerable<int32> Stopping(Audit audit){try{try{throw 42;}catch(int32 code){if(code==42){yield break;}throw code;}}
finally{audit.finished++;}yield return 7;}
int32 Select(fn(int32)->IEnumerable<int32> factory){var sequence=factory(21);int32 sum=0;foreach(var item in sequence){sum+=item;}delete sequence;return sum;}
int32 Select(fn(string)->IEnumerable<int32> factory){return -1;}
int32 main(){Audit audit=new Audit();var stopped=Stopping(audit);foreach(var item in stopped){return 1;}delete stopped;
if(audit.finished!=1){return 2;}delete audit;
return Select(function(value)=>Repeat(value,2))==42?0:3;}
''',
}

STRESS_SOURCE = PREFIX + '''
class Audit{public int32 finished;}
struct Record{int32 id;int32 values[2];}
IEnumerable<Record> Rows(int32 start,Audit audit){try{for(int32 i=0;i<3;i++){
Record record=default(Record);record.id=start+i;record.values[0]=start;record.values[1]=i;yield return record;}}
finally{audit.finished++;}}
IEnumerable<fn()->int32> Callbacks(int32 start,Audit audit){int32 shared=start;
try{for(int32 i=0;i<2;i++){int32 local=i;yield return function()=>{shared++;return shared+local;};}}
finally{audit.finished++;}}
IEnumerable<int32> Raising(int32 value,Audit audit){try{yield return value;throw value;}finally{audit.finished++;}}
int32 Check(int32 value){Audit audit=new Audit();var rows=Rows(value,audit);int32 count=0;
foreach(var row in rows){if(row.id!=value+count||row.values[0]!=value||row.values[1]!=count){return 1;}count++;}delete rows;
var early=Rows(value,audit);foreach(var row in early){if(row.id!=value){return 2;}break;}delete early;
var callbacks=Callbacks(value,audit);var cursor=callbacks.GetEnumerator();delete callbacks;
if(!cursor.MoveNext()){return 3;}var a=cursor.Current();if(!cursor.MoveNext()){return 4;}var b=cursor.Current();
cursor.Dispose();int32 first=a();int32 second=b();delete a;delete b;
if(first!=value+1||second!=value+3){return 5;}var raising=Raising(value,audit);
try{foreach(var number in raising){if(number!=value){return 6;}}return 7;}catch(int32 error){if(error!=value){return 8;}}
delete raising;int32 result=count==3&&audit.finished==4?0:9;delete audit;return result;}
void Mark(string message,int32 length){syscall(1,1,(int64)message,length,0,0,0);}
int32 Gate(){unsafe(using krt.mem;){byte token[1];int32 result=syscall(0,0,(int64)&token[0],1,0,0,0)==1?1:0;delete token;return result;}}
int32 main(){for(int32 warm=0;warm<16;warm++){int32 result=Check(warm);if(result!=0){return result;}}
Mark("READY\\n",6);if(Gate()!=1){return 201;}
for(int32 i=0;i<25000;i++){int32 result=Check(i);if(result!=0){return result;}}
Mark("DONE\\n",5);if(Gate()!=1){return 202;}return 0;}
'''


NEGATIVE_CASES = {
    'yield-outside-generator': ('int32 main(){yield return 42;return 0;}', 'int32 main', 'E_ITERATOR'),
    'incompatible-item': ('IEnumerable<int32> Values(){yield return "wrong";}int32 main(){return 0;}', 'yield return', 'E_ITERATOR'),
    'return-in-generator': ('IEnumerable<int32> Values(){yield return 42;return null;}int32 main(){return 0;}', 'return null', 'E_ITERATOR'),
    'yield-in-finally': ('IEnumerable<int32> Values(){try{yield return 42;}finally{yield return 43;}}int32 main(){return 0;}', 'yield return 43', 'E_ITERATOR'),
    'yield-in-catch': ('IEnumerable<int32> Values(){try{throw 42;}catch(int32 error){yield return error;}}int32 main(){return 0;}', 'yield return error', 'E_ITERATOR'),
    'yield-in-try-with-catch': ('IEnumerable<int32> Values(){try{yield return 42;}catch(int32 error){}}int32 main(){return 0;}', 'yield return 42', 'E_ITERATOR'),
    'ref-parameter': ('IEnumerable<int32> Values(ref int32 value){yield return value;}int32 main(){return 0;}', 'value){', 'E_ITERATOR'),
    'struct-this-borrow': ('struct Source{IEnumerable<int32> Values(){yield return 42;}}int32 main(){return 0;}', 'IEnumerable<int32> Values', 'E_ITERATOR'),
    'stack-allocation': ('IEnumerable<int32> Values(){unsafe(using krt.mem;){int32* storage=stackalloc int32[1];yield return storage[0];}}int32 main(){return 0;}', 'stackalloc', 'E_ITERATOR'),
    'yield-break-in-finally': ('IEnumerable<int32> Values(){try{yield return 42;}finally{yield break;}}int32 main(){return 0;}', 'yield break', 'E_ITERATOR'),
    'bare-yield': ('IEnumerable<int32> Values(){yield;}int32 main(){return 0;}', ';}int32', 'E_PARSE'),
}


class NativeGeneratorTests(NativeCompilerFixture):
    def check_case(self, name):
        path = self.work / (name + '.krt'); path.write_text(PREFIX + POSITIVE_CASES[name], encoding='utf-8')
        for target in ('native', 'vm'):
            for level in range(4):
                with self.subTest(fixture=name, target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    command = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(command, cwd=self.work, env=self.env, capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b''); self.assertEqual(result.stderr, b'')

    def test_lazy_factory_independent_cursors_replay_and_completion(self):
        self.check_case('lazy-independent-cursors')

    def test_custom_inherited_generic_sequence_and_direct_cursor_results(self):
        self.check_case('custom-inherited-generic-sequence-and-cursor-contracts')

    def test_persistent_locals_ref_writes_and_all_loop_forms(self):
        self.check_case('persistent-locals-ref-and-loop-branches')

    def test_resume_edges_cross_nested_loops_switch_and_conditionals(self):
        self.check_case('nested-loops-switch-and-if-resume')

    def test_try_finally_early_dispose_and_escaping_exception(self):
        self.check_case('try-finally-dispose-and-throw-order')

    def test_outer_finally_runs_when_disposal_inner_finally_throws(self):
        self.check_case('dispose-finally-exception-runs-outer-finally')

    def test_escaping_callbacks_retain_cells_after_cursor_and_sequence_release(self):
        self.check_case('escaping-closures-own-persistent-cells')

    def test_struct_fixed_arrays_and_uint128_current_copy(self):
        self.check_case('complete-struct-fixed-array-and-wide-copies')

    def test_wide_and_float_parameters_do_not_borrow_sequence_storage(self):
        self.check_case('wide-and-floating-parameters-outlive-sequence')

    def test_generic_and_class_factories_keep_source_context(self):
        self.check_case('generic-and-class-factory-contexts')

    def test_direct_cursor_and_invalid_current_diagnostics(self):
        self.check_case('direct-enumerator-and-invalid-current')

    def test_foreach_and_closure_cleanup_survive_suspension(self):
        self.check_case('foreach-inside-generator-finally-and-fresh-cells')

    def test_actual_struct_record_page_is_lazy(self):
        self.check_case('real-paged-record-generator')

    def test_dispose_before_first_empty_generator_and_first_call_exception(self):
        self.check_case('empty-before-first-and-first-call-exception')

    def test_cleanup_cannot_reenter_move_or_dispose(self):
        self.check_case('dispose-cleanup-reentrancy-is-guarded')

    def test_closed_generic_struct_string_scalar_and_owner_method_activations(self):
        self.check_case('multiple-closed-generic-items-and-method-contexts')

    def test_yield_break_in_catch_and_generic_overload_probe_restore(self):
        self.check_case('yield-break-in-catch-and-overload-probe-rollback')

    def test_twenty_five_thousand_complete_abandoned_throwing_and_closure_lifetimes(self):
        path = self.work / 'generator-lifetimes.krt'; path.write_text(STRESS_SOURCE, encoding='utf-8')

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
            self.assertEqual(result, expected, f'generator runner status={process.poll()} marker={result!r}')

        def memory(process):
            status = Path(f'/proc/{process.pid}/status').read_text(); result = {}
            for key in ('VmRSS', 'VmSize'):
                found = re.search(rf'^{key}:\s+(\d+) kB$', status, re.MULTILINE)
                self.assertIsNotNone(found, status); result[key] = int(found.group(1)) * 1024
            return result

        for target in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(target=target, optimization=level):
                    output = self.work / f'generator-lifetimes-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    process = subprocess.Popen(argv, cwd=self.work, env=self.env, stdin=subprocess.PIPE,
                                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    try:
                        marker(process, b'READY\n'); before = memory(process)
                        process.stdin.write(b'1'); process.stdin.flush(); marker(process, b'DONE\n'); after = memory(process)
                        print(f'generator memory target={target} O{level} before={before} after={after}')
                        self.assertLessEqual(after['VmRSS'] - before['VmRSS'], 16 * 1024 * 1024, (before, after))
                        self.assertLessEqual(after['VmSize'] - before['VmSize'], 32 * 1024 * 1024, (before, after))
                        process.stdin.write(b'2'); process.stdin.flush(); stdout, stderr = process.communicate(timeout=15)
                        self.assertEqual(process.returncode, 0, stdout + stderr)
                        self.assertEqual(stdout, b''); self.assertEqual(stderr, b'')
                    finally:
                        if process.poll() is None:
                            process.kill()
                        process.communicate(timeout=3)

    def test_invalid_generator_contracts_report_exact_locations_and_preserve_output(self):
        for name, (source, marker, code) in NEGATIVE_CASES.items():
            source = PREFIX + source; path = self.work / (name + '.krt'); path.write_text(source, encoding='utf-8')
            offset = source.index(marker); line = source.count('\n', 0, offset) + 1; column = offset - source.rfind('\n', 0, offset)
            for target in ('native', 'vm'):
                for level in range(4):
                    with self.subTest(fixture=name, target=target, optimization=level):
                        output = self.work / f'{name}-{target}-o{level}'; sentinel = b'existing generator artifact\x00'; output.write_bytes(sentinel)
                        flags = ['target', 'vm'] if target == 'vm' else []
                        command = [str(COMPILER), str(path), f'-O{level}', *flags, '-o', str(output)]
                        results = [subprocess.run(command, cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60) for _ in range(2)]
                        for result in results:
                            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                            self.assertEqual(re.findall(r':\d+:\d+: E_[A-Z_]+:', result.stderr), [f':{line}:{column}: {code}:'], result.stderr)
                        self.assertEqual(results[0].stderr, results[1].stderr); self.assertEqual(output.read_bytes(), sentinel)
