"""Foreach protocol: real calls, value copies, dispatch and deterministic cleanup."""
import re
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, ROOT, NativeCompilerFixture


POSITIVE_CASES = {
    'struct-state-current-field': '''
struct Cursor{int32 Current;int32 end;bool MoveNext(){Current++;return Current<=end;}}
struct Range{int32 first;int32 last;int32 factories;Cursor GetEnumerator(){factories++;Cursor c=default(Cursor);c.Current=first-1;c.end=last;return c;}}
int32 main(){Range range=default(Range);range.first=2;range.last=6;int32 sum=0;
foreach(var item in range){sum+=item;}if(sum!=20){return 1;}
sum=0;foreach(int64 item in range){sum+=(int32)item;}return sum==20&&range.first==2&&range.last==6&&range.factories==2?0:2;}
''',
    'class-dispose-break-continue-return': '''
class Audit{public int32 factory;public int32 moves;public int32 reads;public int32 disposed;}
class Cursor{public Audit audit;public int32 position;public bool MoveNext(){audit.moves++;position++;return position<=5;}
public int32 Current(){audit.reads++;return position;}public void Dispose(){audit.disposed++;delete this;}}
class Range{public Audit audit;public Cursor GetEnumerator(){audit.factory++;Cursor c=new Cursor();c.audit=audit;return c;}}
int32 First(Range range){foreach(var item in range){if(item==2){return item;}}return -1;}
int32 main(){Audit audit=new Audit();Range range=new Range();range.audit=audit;int32 sum=0;
foreach(var item in range){if(item==2){continue;}if(item==4){break;}sum+=item;}
if(sum!=4||audit.factory!=1||audit.moves!=4||audit.reads!=4||audit.disposed!=1){return 1;}
if(First(range)!=2||audit.factory!=2||audit.moves!=6||audit.reads!=6||audit.disposed!=2){return 2;}
delete range;delete audit;return 0;}
''',
    'interface-generic-and-inherited-dispatch': '''
interface IEnumerator<T>{bool MoveNext();T Current();void Dispose();}
interface IEnumerable<T>{IEnumerator<T> GetEnumerator();}
class Audit{public int32 disposed;}
class Cursor<T>:IEnumerator<T>{public T item;public bool used;public Audit audit;
public bool MoveNext(){if(used){return false;}used=true;return true;}
public T Current(){return item;}public void Dispose(){audit.disposed++;delete this;}}
class Sequence<T>:IEnumerable<T>{public T item;public Audit audit;
public IEnumerator<T> GetEnumerator(){Cursor<T> c=new Cursor<T>();c.item=item;c.audit=audit;return c;}}
int32 Sum<T>(IEnumerable<T> source,fn(T)->int32 convert){int32 sum=0;foreach(var item in source){sum+=convert(item);}return sum;}
int32 main(){Audit audit=new Audit();Sequence<int32> sequence=new Sequence<int32>();sequence.item=42;sequence.audit=audit;
IEnumerable<int32> source=sequence;int32 result=Sum(source,function(int32 item)=>item);
delete sequence;if(result!=42||audit.disposed!=1){return 1;}delete audit;return 0;}
''',
    'struct-current-full-copy-and-wide-precision': '''
struct Item{int32 values[3];uint128 large;int32 sequence;}
struct Cursor{Item Current;bool MoveNext(){Current.sequence++;Current.values[0]=Current.sequence;Current.values[2]=Current.sequence*7;
Current.large=((uint128)1<<100)+Current.sequence;return Current.sequence<=3;}}
struct Source{Cursor GetEnumerator(){return default(Cursor);}}
int32 main(){Source source=default(Source);Item saved=default(Item);int32 total=0;
foreach(var item in source){if(item.large!=((uint128)1<<100)+item.sequence){return 1;}
total+=item.values[0]+item.values[2];if(item.sequence==1){saved=item;}item.values[0]=99;}
return total==48&&saved.values[0]==1&&saved.values[2]==7&&saved.large==((uint128)1<<100)+1?0:2;}
''',
    'nested-loop-and-finally-cleanup-order': '''
class Audit{public int32 log;}
class Cursor{public int32 Current;public int32 id;public Audit audit;
public bool MoveNext(){Current++;return Current<=3;}public void Dispose(){audit.log=audit.log*10+id;delete this;}}
class Source{public int32 id;public Audit audit;public Cursor GetEnumerator(){Cursor c=new Cursor();c.id=id;c.audit=audit;return c;}}
int32 main(){Audit audit=new Audit();Source outer=new Source();outer.id=1;outer.audit=audit;
Source inner=new Source();inner.id=2;inner.audit=audit;
try{foreach(var a in outer){foreach(var b in inner){try{if(a==1&&b==1){throw 42;}}
finally{audit.log=audit.log*10+3;}}}}catch(int32 code){if(code!=42||audit.log!=321){return 1;}}
delete inner;delete outer;delete audit;return 0;}
''',
    'move-current-and-dispose-exceptions': '''
class Audit{public int32 disposed;}
class Cursor{public Audit audit;public int32 mode;public bool MoveNext(){if(mode==1){throw 11;}return true;}
public int32 Current(){if(mode==2){throw 22;}return 42;}
public void Dispose(){audit.disposed++;int32 copied=mode;delete this;if(copied==3){throw 33;}}}
class Source{public Audit audit;public int32 mode;public Cursor GetEnumerator(){Cursor c=new Cursor();c.audit=audit;c.mode=mode;return c;}}
int32 main(){Audit audit=new Audit();Source source=new Source();source.audit=audit;
for(int32 mode=1;mode<=3;mode++){source.mode=mode;try{foreach(var item in source){break;}return 1;}
catch(int32 code){if(code!=mode*11||audit.disposed!=mode){return 2;}}}
delete source;delete audit;return 0;}
''',
    'escaping-closure-fresh-iteration-cell': '''
struct Cursor{int32 Current;bool MoveNext(){Current++;return Current<=4;}}
struct Source{Cursor GetEnumerator(){return default(Cursor);}}
int32 main(){Source source=default(Source);fn()->int32 callbacks[4];int32 index=0;
foreach(var item in source){callbacks[index]=function()=>{item+=10;return item;};index++;}
int32 sum=0;for(int32 i=0;i<4;i++){if(callbacks[i]()!=i+11){return 1;}sum+=callbacks[i]();delete callbacks[i];}
return index==4&&sum==90?0:2;}
''',
    'nullable-collection-and-factory-evaluation-once': '''
class Cursor{public int32 Current;public bool MoveNext(){Current++;return Current<=2;}}
class Source{public int32 factories;public bool empty;public Cursor GetEnumerator(){factories++;return empty?null:new Cursor();}}
Source Select(Source source,ref int32 calls){calls++;return source;}
int32 main(){Source absent=null;int32 sum=0;foreach(var item in absent){sum+=item;}
Source source=new Source();source.empty=true;int32 calls=0;foreach(var item in Select(source,ref calls)){sum+=item;}
if(sum!=0||calls!=1||source.factories!=1){return 1;}delete source;return 0;}
''',
    'readonly-collection-defensive-struct-copy': '''
struct Cursor{int32 Current;bool MoveNext(){Current++;return Current<=2;}}
struct Source{int32 factories;Cursor GetEnumerator(){factories++;return default(Cursor);}}
int32 Read(Source original){let source=original;int32 sum=0;foreach(var item in source){sum+=item;}return sum*10+source.factories;}
int32 main(){Source source=default(Source);return Read(source)==30&&source.factories==0?0:1;}
''',
    'string-array-fast-path-and-user-protocol': '''
struct Cursor{int32 Current;bool MoveNext(){Current++;return Current<=3;}}
struct Source{Cursor GetEnumerator(){return default(Cursor);}}
int32 main(){int32 numbers[3];numbers[0]=4;numbers[1]=5;numbers[2]=6;int32 sum=0;
foreach(var item in numbers){sum+=item;}foreach(var character in "abc"){sum+=(int32)character;}
Source source=default(Source);foreach(var item in source){sum+=item;}
return sum==315?0:1;}
''',
    'paged-record-pipeline': '''
struct Record{int32 id;int32 cost;bool active;}
struct Cursor{Record Current;int32 next;int32 count;Record* records;
bool MoveNext(){unsafe(using krt.mem;){while(next<count){Record record=records[next];next++;if(record.active){Current=record;return true;}}return false;}}}
struct Catalog{Record* records;int32 count;Cursor GetEnumerator(){Cursor c=default(Cursor);c.records=records;c.count=count;return c;}}
int32 main(){unsafe(using krt.mem;){Record records[6];for(int32 i=0;i<6;i++){records[i].id=i+1;records[i].cost=(i+1)*100;records[i].active=i%2==0;}
Catalog catalog=default(Catalog);catalog.records=(Record*)records;catalog.count=6;int32 seen=0;int32 total=0;int32 last=0;
foreach(var record in catalog){if(seen==1){last=record.id;}seen++;total+=record.cost;}
return seen==3&&last==3&&total==900?0:1;}}
''',
    'extensions-overloads-and-optional-dispose': '''
class Cursor{public int32 value;public int32 last;}
class Source{public Cursor cursor;}
struct Other{}
int32 GetEnumerator(Other source){return 0;}
Cursor GetEnumerator(Source source){return source.cursor;}
bool MoveNext(Cursor cursor){cursor.value++;return cursor.value<=cursor.last;}
int32 Current(Cursor cursor){return cursor.value;}
int32 main(){Source source=new Source();Cursor cursor=new Cursor();cursor.last=4;source.cursor=cursor;
int32 sum=0;foreach(var item in source){sum+=item;}
int32 result=sum==10&&cursor.value==5?0:1;delete cursor;delete source;return result;}
''',
    'inherited-virtual-factory-and-enumerator-members': '''
class Audit{public int32 disposed;}
class BaseCursor{public int32 value;public Audit audit;public virtual bool MoveNext(){return false;}
public virtual int32 Current(){return -1;}public virtual void Dispose(){audit.disposed++;delete this;}}
class Cursor:BaseCursor{public override bool MoveNext(){value++;return value<=3;}public override int32 Current(){return value*7;}}
class BaseSource{public Audit audit;public virtual BaseCursor GetEnumerator(){return null;}}
class Source:BaseSource{public override BaseCursor GetEnumerator(){Cursor cursor=new Cursor();cursor.audit=audit;return cursor;}}
int32 main(){Audit audit=new Audit();Source actual=new Source();actual.audit=audit;BaseSource source=actual;
int32 sum=0;foreach(var item in source){sum+=item;}
int32 result=sum==42&&audit.disposed==1?0:1;delete actual;delete audit;return result;}
''',
    'using-scoped-protocol-extensions': '''
using Protocol;
class Cursor{public int32 value;}
class Source{public Cursor cursor;}
namespace Protocol{
public Cursor GetEnumerator(Source source){return source.cursor;}
public bool MoveNext(Cursor cursor){cursor.value++;return cursor.value<=3;}
public int32 Current(Cursor cursor){return cursor.value*7;}}
int32 main(){Source source=new Source();Cursor cursor=new Cursor();source.cursor=cursor;
int32 sum=0;foreach(var item in source){sum+=item;}
int32 result=sum==42?0:1;delete cursor;delete source;return result;}
''',
    'ref-extension-struct-collection-and-enumerator-state': '''
struct Cursor{int32 value;int32 end;}
struct Source{int32 factories;int32 count;}
Cursor GetEnumerator(ref Source source){source.factories++;Cursor cursor=default(Cursor);cursor.end=source.count;return cursor;}
bool MoveNext(ref Cursor cursor){cursor.value++;return cursor.value<=cursor.end;}
int32 Current(Cursor cursor){return cursor.value*7;}
int32 main(){Source source=default(Source);source.count=3;int32 sum=0;foreach(var item in source){sum+=item;}
Cursor direct=source.GetEnumerator();if(!direct.MoveNext()||direct.Current()!=7){return 1;}
return sum==42&&source.factories==2&&direct.value==1?0:2;}
''',
}


NEGATIVE_CASES = {
    'not-iterable': ('int32 main(){foreach(var value in 42){}return 0;}', '42', 'E_ITERATOR'),
    'missing-factory': ('struct Source{}int32 main(){Source s=default(Source);foreach(var x in s){}return 0;}', 's){}', 'E_ITERATOR'),
    'scalar-factory-result': ('struct Source{int32 GetEnumerator(){return 1;}}int32 main(){Source s=default(Source);foreach(var x in s){}return 0;}', 'foreach', 'E_ITERATOR'),
    'missing-move': ('struct Cursor{int32 Current;}struct Source{Cursor GetEnumerator(){return default(Cursor);}}int32 main(){Source s=default(Source);foreach(var x in s){}return 0;}', 'foreach', 'E_ITERATOR'),
    'non-boolean-move': ('struct Cursor{int32 Current;int32 MoveNext(){return 0;}}struct Source{Cursor GetEnumerator(){return default(Cursor);}}int32 main(){Source s=default(Source);foreach(var x in s){}return 0;}', 'foreach', 'E_ITERATOR'),
    'missing-current': ('struct Cursor{bool MoveNext(){return false;}}struct Source{Cursor GetEnumerator(){return default(Cursor);}}int32 main(){Source s=default(Source);foreach(var x in s){}return 0;}', 'foreach', 'E_ITERATOR'),
    'void-current': ('struct Cursor{bool MoveNext(){return false;}void Current(){}}struct Source{Cursor GetEnumerator(){return default(Cursor);}}int32 main(){Source s=default(Source);foreach(var x in s){}return 0;}', 'foreach', 'E_ITERATOR'),
    'non-void-dispose': ('struct Cursor{int32 Current;bool MoveNext(){return false;}int32 Dispose(){return 0;}}struct Source{Cursor GetEnumerator(){return default(Cursor);}}int32 main(){Source s=default(Source);foreach(var x in s){}return 0;}', 'foreach', 'E_ITERATOR'),
    'static-current': ('struct Cursor{public static int32 Current;bool MoveNext(){return false;}}struct Source{Cursor GetEnumerator(){return default(Cursor);}}int32 main(){Source s=default(Source);foreach(var x in s){}return 0;}', 'foreach', 'E_ITERATOR'),
}


class NativeIteratorTests(NativeCompilerFixture):
    def check_case(self, name):
        source = POSITIVE_CASES[name]
        path = self.work / (name + '.krt')
        path.write_text(source, encoding='utf-8')
        for target in ('native', 'vm'):
            for level in range(4):
                with self.subTest(fixture=name, target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    command = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(command, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b'')
                    self.assertEqual(result.stderr, b'')

    def test_mutable_struct_enumerator_current_field_and_value_copy(self):
        self.check_case('struct-state-current-field')

    def test_dispose_on_break_and_return_but_not_continue(self):
        self.check_case('class-dispose-break-continue-return')

    def test_generic_interface_factory_and_current_dispatch(self):
        self.check_case('interface-generic-and-inherited-dispatch')

    def test_current_struct_fixed_arrays_and_uint128_are_complete_copies(self):
        self.check_case('struct-current-full-copy-and-wide-precision')

    def test_nested_dispose_and_user_finally_order(self):
        self.check_case('nested-loop-and-finally-cleanup-order')

    def test_move_current_and_dispose_exceptions_propagate(self):
        self.check_case('move-current-and-dispose-exceptions')

    def test_escaping_closures_capture_a_fresh_loop_variable_each_round(self):
        self.check_case('escaping-closure-fresh-iteration-cell')

    def test_null_references_are_empty_and_factories_evaluate_once(self):
        self.check_case('nullable-collection-and-factory-evaluation-once')

    def test_readonly_collection_preserves_defensive_copy_semantics(self):
        self.check_case('readonly-collection-defensive-struct-copy')

    def test_existing_string_and_array_iteration(self):
        self.check_case('string-array-fast-path-and-user-protocol')

    def test_real_record_filter_pipeline_over_structs(self):
        self.check_case('paged-record-pipeline')

    def test_extension_overload_resolution_and_optional_dispose(self):
        self.check_case('extensions-overloads-and-optional-dispose')

    def test_inherited_virtual_factory_current_move_and_dispose(self):
        self.check_case('inherited-virtual-factory-and-enumerator-members')

    def test_using_scope_controls_extension_protocol_visibility(self):
        self.check_case('using-scoped-protocol-extensions')

    def test_ref_extension_receivers_keep_mutable_struct_state(self):
        self.check_case('ref-extension-struct-collection-and-enumerator-state')

    def test_protocol_errors_preserve_existing_output_and_repeat_diagnostics(self):
        for name, (source, marker, code) in NEGATIVE_CASES.items():
            path = self.work / (name + '.krt'); path.write_text(source, encoding='utf-8')
            offset = source.index(marker)
            line = source.count('\n', 0, offset) + 1
            column = offset - source.rfind('\n', 0, offset)
            for target in ('native', 'vm'):
                for level in range(4):
                    with self.subTest(fixture=name, target=target, optimization=level):
                        output = self.work / f'{name}-{target}-o{level}'
                        sentinel = b'existing iterator output\x00'; output.write_bytes(sentinel)
                        flags = ['target', 'vm'] if target == 'vm' else []
                        command = [str(COMPILER), str(path), f'-O{level}', *flags, '-o', str(output)]
                        results = [subprocess.run(command, cwd=self.work, env=self.env,
                                                  capture_output=True, text=True, timeout=60) for _ in range(2)]
                        for result in results:
                            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                            self.assertEqual(re.findall(r':\d+:\d+: E_[A-Z_]+:', result.stderr),
                                             [f':{line}:{column}: {code}:'], result.stderr)
                        self.assertEqual(results[0].stderr, results[1].stderr)
                        self.assertEqual(output.read_bytes(), sentinel)
