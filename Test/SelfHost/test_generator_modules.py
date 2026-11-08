"""Lazy iterators across actual independently compiled ABI3 modules."""
import itertools
import subprocess

from Test.SelfHost.test_native_optimizer import NativeCompilerFixture
from Test.SelfHost import test_struct_linkage as struct_linkage


class NativeGeneratorModuleTests(NativeCompilerFixture):
    invoke = struct_linkage.StructLinkageTests.invoke
    object = struct_linkage.StructLinkageTests.object
    execute_objects = struct_linkage.StructLinkageTests.execute_objects

    def execute_module_set(self, objects, level):
        for order, ordered in enumerate(itertools.permutations(objects)):
            with self.subTest(optimization=level, order=order):
                output = self.work / f'generator-modules-o{level}-{order}'
                result = self.invoke(*ordered, '-o', output)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(output)], cwd=self.work, env=self.env,
                                        capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(result.stdout, b''); self.assertEqual(result.stderr, b'')

    def test_unknown_generated_types_struct_current_closures_and_exception_cleanup(self):
        shared = '''namespace Packets;
public struct Record{int32 id;uint128 wide;byte bytes[5];}
public class Audit{public int32 finished;}
'''
        provider = '''using Packets;using System.Collections.Generic;
IEnumerable<Record> Rows(Record seed,Audit audit){try{for(int32 i=0;i<3;i++){
seed.id++;seed.wide+=1;seed.bytes[4]++;yield return seed;}}finally{audit.finished++;}}
IEnumerable<fn()->int32> Callbacks(int32 start,Audit audit){int32 shared=start;
try{for(int32 i=0;i<2;i++){int32 local=i;yield return function()=>{shared++;return shared*10+local;};}}
finally{audit.finished++;}}
IEnumerable<Record> Raising(Record seed,Audit audit){try{yield return seed;throw seed;}finally{audit.finished++;}}
'''
        service = '''using Packets;using System.Collections.Generic;
int32 Sum(IEnumerable<Record> rows){int32 result=0;foreach(var record in rows){result+=record.id+record.bytes[4];}return result;}
int32 First(IEnumerable<Record> rows){foreach(var record in rows){return record.id;}return -1;}
'''
        consumer = '''using Packets;using System.Collections.Generic;
extern IEnumerable<Record> Rows(Record seed,Audit audit);
extern IEnumerable<fn()->int32> Callbacks(int32 start,Audit audit);
extern IEnumerable<Record> Raising(Record seed,Audit audit);
extern int32 Sum(IEnumerable<Record> rows);extern int32 First(IEnumerable<Record> rows);
int32 main(){Audit audit=new Audit();Record seed=default(Record);seed.id=7;
seed.wide=((uint128)1<<100)+19;seed.bytes[0]=31;seed.bytes[4]=37;
var rows=Rows(seed,audit);if(audit.finished!=0||Sum(rows)!=144||audit.finished!=1){return 1;}delete rows;
var early=Rows(seed,audit);if(First(early)!=8||audit.finished!=2){return 2;}delete early;
var callbacks=Callbacks(5,audit);var cursor=callbacks.GetEnumerator();delete callbacks;
if(!cursor.MoveNext()){return 3;}var a=cursor.Current();if(!cursor.MoveNext()){return 4;}var b=cursor.Current();cursor.Dispose();
if(audit.finished!=3||a()!=60||b()!=71||a()!=80){return 5;}delete a;delete b;
var raising=Raising(seed,audit);try{foreach(var record in raising){
if(record.id!=7||record.wide!=seed.wide||record.bytes[0]!=31||record.bytes[4]!=37){return 6;}}return 7;}
catch(Record fault){if(fault.id!=7||fault.wide!=seed.wide||fault.bytes[0]!=31||fault.bytes[4]!=37||audit.finished!=4){return 8;}
fault.bytes[4]=99;if(seed.bytes[4]!=37){return 9;}}
delete raising;delete audit;return 0;}
'''
        for level in range(4):
            objects = (self.object('generator-provider', provider, level, shared),
                       self.object('generator-service', service, level, shared),
                       self.object('generator-consumer', consumer, level, shared))
            self.execute_module_set(objects, level)

    def test_metadata_only_provider_exports_generators_and_generic_source_templates(self):
        provider = '''using System.Collections.Generic;namespace StreamApi;
public struct Row{int32 id;uint128 wide;byte bytes[5];}
IEnumerable<Row> Rows(Row seed){for(int32 i=0;i<2;i++){seed.id++;seed.bytes[4]++;yield return seed;}}
IEnumerable<T> Repeat<T>(T value,int32 count){for(int32 i=0;i<count;i++){yield return value;}}
'''
        consumer = '''using StreamApi;
int32 main(){Row seed=default(Row);seed.id=7;seed.wide=((uint128)1<<100)+19;seed.bytes[4]=37;
var rows=Rows(seed);int32 count=0;foreach(var row in rows){count++;
if(row.id!=7+count||row.wide!=seed.wide||row.bytes[4]!=37+count){return 1;}}delete rows;
var copies=Repeat(seed,3);foreach(var row in copies){if(row.id!=7||row.bytes[4]!=37){return 2;}count++;}delete copies;
var numbers=Repeat(11,2);int32 sum=0;foreach(var number in numbers){sum+=number;}delete numbers;
return count==5&&sum==22&&seed.bytes[4]==37?0:3;}
'''
        for level in range(4):
            library = self.object('generator-metadata-provider', provider, level)
            (library.parent / 'Program.krt').unlink()
            folder = self.work / f'generator-metadata-client-o{level}'; folder.mkdir()
            path = folder / 'Consumer.krt'; path.write_text(consumer)
            client = folder / 'Consumer.kro'
            result = self.invoke(path, library, f'-O{level}', '-c', '-o', client)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(client.is_file()); self.execute_objects(library, client, level)

    def test_independent_generic_factories_have_distinct_struct_and_scalar_activations(self):
        declarations = '''using System.Collections.Generic;namespace SequenceApi;
public struct Record{int64 number;byte bytes[5];}
'''
        first = declarations + '''
IEnumerable<T> Repeat<T>(T value){yield return value;yield return value;}
IEnumerable<Record> Records(Record seed){return Repeat(seed);}
'''
        second = declarations + '''
IEnumerable<T> Repeat<T>(T value){yield return value;yield return value;}
IEnumerable<int32> Numbers(int32 value){return Repeat(value);}
'''
        consumer = declarations + '''
extern IEnumerable<Record> Records(Record seed);extern IEnumerable<int32> Numbers(int32 value);
int32 main(){Record seed=default(Record);seed.number=19;seed.bytes[4]=23;
var records=Records(seed);int32 count=0;foreach(var record in records){if(record.number!=19||record.bytes[4]!=23){return 1;}count++;}delete records;
var numbers=Numbers(7);int32 sum=0;foreach(var number in numbers){sum+=number;}delete numbers;return count==2&&sum==14?0:2;}
'''
        for level in range(4):
            objects = (self.object('record-generator-provider', first, level),
                       self.object('number-generator-provider', second, level),
                       self.object('generic-generator-client', consumer, level))
            self.execute_module_set(objects, level)

    def test_factory_initializes_provider_statics_before_lazy_body_and_only_once(self):
        provider = '''using System.Collections.Generic;namespace FactoryState;
int32 Seed(){return 41;}
public class State{public static int32 count=Seed();}
IEnumerable<int32> Values(){State.count++;yield return State.count;}
'''
        consumer = '''using System.Collections.Generic;namespace FactoryState;
public class State{public static extern int32 count;}
extern IEnumerable<int32> Values();
int32 main(){var first=Values();if(State.count!=41){return 1;}
State.count=51;var second=Values();if(State.count!=51){return 2;}
int32 sum=0;foreach(var value in first){sum+=value;}foreach(var value in second){sum+=value;}
delete first;delete second;return sum==105&&State.count==53?0:3;}
'''
        for level in range(4):
            library = self.object('generator-static-provider', provider, level)
            client = self.object('generator-static-consumer', consumer, level)
            self.execute_objects(library, client, level)

    def test_metadata_only_wide_current_sret_closed_generic_identity_and_early_dispose(self):
        provider = '''using System.Collections.Generic;namespace WideStreams;
public class Audit{public int32 entered;public int32 finished;}
IEnumerable<uint128> Values(uint128 seed,Audit audit){audit.entered++;
try{for(int32 i=0;i<3;i++){seed+=1;yield return seed;}}finally{audit.finished++;}}
IEnumerable<T> Repeat<T>(T value,Audit audit){audit.entered++;
try{yield return value;yield return value;}finally{audit.finished++;}}
'''
        consumer = '''using WideStreams;using System.Collections.Generic;
int32 main(){Audit audit=new Audit();uint128 seed=((uint128)1<<120)+((uint128)1<<70)+19;
var sequence=Values(seed,audit);IEnumerator<uint128> cursor=sequence.GetEnumerator();delete sequence;
object identity=cursor;
if(audit.entered!=0||!(identity is IEnumerator<uint128>)||(identity is IEnumerator<int32>)){return 1;}
if(!cursor.MoveNext()){return 2;}uint128 first=cursor.Current();uint128 independent=cursor.Current();first+=99;
if(independent!=seed+1||first!=seed+100){return 3;}
if(!cursor.MoveNext()){return 4;}uint128 second=cursor.Current();
if(second!=seed+2||independent!=seed+1||audit.entered!=1||audit.finished!=0){return 5;}
cursor.Dispose();if(audit.finished!=1){return 6;}
var replay=Values(seed,audit);int32 count=0;uint128 sum=0;
foreach(var value in replay){count++;sum+=value;if(value!=seed+(uint128)count){return 7;}}
delete replay;if(count!=3||sum!=seed*3+6||audit.entered!=2||audit.finished!=2){return 8;}
var wides=Repeat(seed,audit);var wideCursor=wides.GetEnumerator();object wideIdentity=wideCursor;delete wides;
var numbers=Repeat(41,audit);var numberCursor=numbers.GetEnumerator();object numberIdentity=numberCursor;delete numbers;
if(!(wideIdentity is IEnumerator<uint128>)||(wideIdentity is IEnumerator<int32>)||
!(numberIdentity is IEnumerator<int32>)||(numberIdentity is IEnumerator<uint128>)){return 9;}
if(!wideCursor.MoveNext()||wideCursor.Current()!=seed||!numberCursor.MoveNext()||numberCursor.Current()!=41){return 10;}
uint128 preserved=wideCursor.Current();if(!wideCursor.MoveNext()||wideCursor.Current()!=seed||preserved!=seed){return 11;}
wideCursor.Dispose();numberCursor.Dispose();
int32 result=audit.entered==4&&audit.finished==4&&seed==((uint128)1<<120)+((uint128)1<<70)+19?0:12;
delete audit;return result;}
'''
        for level in range(4):
            library = self.object('wide-generator-metadata-provider', provider, level)
            (library.parent / 'Program.krt').unlink()
            folder = self.work / f'wide-generator-client-o{level}'; folder.mkdir()
            path = folder / 'Consumer.krt'; path.write_text(consumer)
            client = folder / 'Consumer.kro'
            result = self.invoke(path, library, f'-O{level}', '-c', '-o', client)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(client.is_file()); self.execute_objects(library, client, level)
