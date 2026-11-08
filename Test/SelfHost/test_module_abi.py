"""Actual native module boundaries, independent compilation, and KRO imports."""
import hashlib
import itertools
import json
import struct
import subprocess

from Test.SelfHost.test_native_linking import read_object
from Test.SelfHost.test_native_optimizer import NativeCompilerFixture
from Test.SelfHost.test_native_optimizer import LINKER, ROOT
from Test.SelfHost import test_struct_linkage as _struct_linkage


class ModuleAbiTests(NativeCompilerFixture):
    invoke = _struct_linkage.StructLinkageTests.invoke
    object = _struct_linkage.StructLinkageTests.object
    execute_objects = _struct_linkage.StructLinkageTests.execute_objects
    reject_objects = _struct_linkage.StructLinkageTests.reject_objects
    reject_object_set = _struct_linkage.StructLinkageTests.reject_object_set

    def execute_module_set(self, objects, level):
        for order, ordered in enumerate(itertools.permutations(objects)):
            with self.subTest(optimization=level, order=order):
                output = self.work / f'modules-o{level}-{order}'
                result = self.invoke(*ordered, '-o', output)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(output)], cwd=self.work, env=self.env,
                                        capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unknown_concrete_struct_boxes_callbacks_sret_and_typed_exceptions(self):
        declarations = '''namespace Boxes;
public struct Request{uint128 id;byte bytes[5];int64 amount;}
public struct Response{uint128 id;byte bytes[5];int64 total;}
public interface Counter{int32 Add(int32 amount);uint128 Wide();Response Process(Request request);}
'''
        provider = declarations + '''
private struct Hidden:Counter{int32 value;uint128 wide;
 public int32 Add(int32 amount){value+=amount;return value;}
 public uint128 Wide(){return wide;}
 public Response Process(Request request){Response response=default(Response);
  response.id=request.id+wide;response.total=request.amount+value;
  response.bytes[0]=request.bytes[0];response.bytes[4]=request.bytes[4]+1;return response;}}
object Create(int32 initial){Hidden value=default(Hidden);value.value=initial;
 value.wide=((uint128)1<<100)+7;return value;}
void Fail(int32 initial){throw Create(initial);}
object Narrow(){int14 value=-3077;return value;}
'''
        service = declarations + '''
Counter Advance(object value,int32 amount){Counter face=(Counter)value;
 if(face.Add(amount)!=12){throw 101;}return face;}
Response Route(Counter face,Request request){fn(Request)->Response callback=function(Request item)->Response{return face.Process(item);};
 Response response=callback(request);delete callback;return response;}
'''
        consumer = declarations + '''
extern object Create(int32 initial);extern void Fail(int32 initial);extern object Narrow();
extern Counter Advance(object value,int32 amount);extern Response Route(Counter face,Request request);
int32 main(){object original=Create(7);Counter face=Advance(original,5);
 if(face.Wide()!=((uint128)1<<100)+7||!(original is Counter)||face.Add(3)!=15){return 1;}
 Request request=default(Request);request.id=((uint128)1<<110)+19;request.amount=29;
 request.bytes[0]=31;request.bytes[4]=37;Response response=Route(face,request);
 if(response.id!=request.id+face.Wide()||response.total!=44||response.bytes[0]!=31||response.bytes[4]!=38){return 2;}
 if(request.bytes[4]!=37){return 3;}delete original;
 object narrow=Narrow();if((int14)narrow!=-3077||(narrow is int16)){return 4;}delete narrow;
 try{Fail(19);return 5;}catch(Counter failure){if(failure.Add(2)!=21||failure.Wide()!=((uint128)1<<100)+7){return 6;}delete failure;}
 return 0;}
'''
        for level in range(4):
            objects = (self.object('boxed-provider', provider, level),
                       self.object('boxed-service', service, level),
                       self.object('boxed-consumer', consumer, level))
            self.execute_module_set(objects, level)

    def test_unknown_provider_subclass_dispatch_type_tests_and_catches(self):
        declarations = '''namespace Models;
public interface Readable{int32 Read();}
public class Base:Readable{
 public int32 value;
 public extern Base(int32 initial);
 public virtual extern int32 Read();
}
'''
        provider = declarations.replace('public extern Base(int32 initial);',
                                        'public Base(int32 initial){value=initial;}').replace(
            'public virtual extern int32 Read();', 'public virtual int32 Read(){return value;}') + '''
public class HiddenDerived:Base{
 public HiddenDerived(int32 initial):base(initial){}
 public override int32 Read(){return value+100;}
}
Base Create(int32 initial){return new HiddenDerived(initial);}
void Fail(int32 initial){throw new HiddenDerived(initial);}
'''
        consumer = declarations + '''
extern Base Create(int32 initial);extern void Fail(int32 initial);
int32 main(){
 Base item=Create(7);Readable readable=item;
 if(item.value!=7||item.Read()!=107||readable.Read()!=107){return 1;}
 if(!(item is Base)||!(item is Readable)){return 2;}
 delete item;
 try{Fail(19);return 3;}catch(Base failure){
  if(failure.value!=19||failure.Read()!=119){return 4;}delete failure;
 }
 return 0;
}
'''
        for level in range(4):
            library = self.object('unknown-subclass-provider', provider, level)
            client = self.object('unknown-subclass-client', consumer, level)
            self.execute_objects(library, client, level)

    def test_independent_enum_nominals_fixed_wide_structs_and_closure_contracts(self):
        declarations = '''namespace Records;
public enum State{Ready=7,Done=19,Failed=-3}
public struct Packet{State state;uint128 wide;byte bytes[5];}
'''
        provider = declarations + '''
Packet Change(Packet packet){packet.state=State.Done;packet.wide+=17;packet.bytes[4]=41;return packet;}
fn(Packet)->Packet Make(int64 increment){int64 current=increment;
 return function(Packet packet)->Packet{current++;packet.wide+=(uint128)current;packet.state=State.Ready;return packet;};}
int32 Apply(fn(Packet)->Packet callback,Packet packet){Packet result=callback(packet);return result.state==State.Ready?0:1;}
'''
        consumer = declarations + '''
extern Packet Change(Packet packet);extern fn(Packet)->Packet Make(int64 increment);
extern int32 Apply(fn(Packet)->Packet callback,Packet packet);
int32 main(){Packet packet=default(Packet);packet.state=State.Failed;
 packet.wide=((uint128)1<<100)+7;packet.bytes[4]=5;
 Packet changed=Change(packet);
 if(changed.state!=State.Done||changed.wide!=packet.wide+17||changed.bytes[4]!=41){return 1;}
 if(packet.state!=State.Failed||packet.bytes[4]!=5){return 2;}
 fn(Packet)->Packet callback=Make(29);Packet first=callback(packet);Packet second=callback(packet);
 if(first.wide!=packet.wide+30||second.wide!=packet.wide+31||Apply(callback,packet)!=0){return 3;}
 delete callback;return 0;}
'''
        for level in range(4):
            library = self.object('enum-closure-provider', provider, level)
            client = self.object('enum-closure-client', consumer, level)
            self.execute_objects(library, client, level)

    def test_type_layout_contracts_are_independent_of_common_function_symbols(self):
        cases = (
            ('field-shape', 'public class Value{public int64 first;public int64 second;}',
             'public class Value{public int64 first;public int32 second;}'),
            ('enum-values', 'public enum Value{First=1,Second=7}',
             'public enum Value{First=1,Second=9}'),
            ('interface-result', 'public interface Value{int32 Read();}',
             'public interface Value{int64 Read();}'),
            ('static-shape', 'public class Value{public static int32 counter;}',
             'public class Value{public static int64 counter;}'),
            ('declaration-kind', 'public interface Value{int32 Read();}',
             'public struct Value{int32 Read(){return 7;}}'),
        )
        for name, provided, declared in cases:
            for level in range(4):
                with self.subTest(case=name, optimization=level):
                    library = self.object(name + '-provider', provided + 'int32 LibraryOnly(){return 0;}', level)
                    client = self.object(name + '-client', declared + 'int32 main(){return 0;}', level)
                    self.reject_objects(library, client, level, 'E_ABI_LAYOUT')

    def test_kro_metadata_supplies_types_functions_constructors_and_templates(self):
        provider = '''namespace Api;
public interface Readable{int32 Read();}
public class Counter:Readable{public int32 value;
 public Counter(int32 initial){value=initial;}
 public virtual int32 Read(){return value;}
 public int32 Add(int32 amount){value+=amount;return value;}}
public struct Pair{int64 first;int64 second;}
Counter Create(int32 initial){return new Counter(initial);}
Pair Echo(Pair value){value.second+=3;return value;}
T Identity<T>(T value){return value;}
'''
        consumer = '''using Api;
int32 main(){Counter item=Create(7);Readable readable=item;
 if(readable.Read()!=7||item.Add(5)!=12){return 1;}
 Counter constructed=new Counter(19);if(constructed.Read()!=19){return 2;}
 Pair pair=default(Pair);pair.first=29;pair.second=31;Pair changed=Echo(pair);
 if(changed.first!=29||changed.second!=34||pair.second!=31){return 3;}
 if(Identity<int32>(37)!=37||Identity<Pair>(changed).second!=34){return 4;}
 delete item;delete constructed;return 0;}
'''
        for level in range(4):
            library = self.object('metadata-provider', provider, level)
            (library.parent / 'Program.krt').unlink()
            folder = self.work / f'metadata-client-o{level}'
            folder.mkdir()
            path = folder / 'Consumer.krt'
            path.write_text(consumer)
            client = folder / 'Consumer.kro'
            result = self.invoke(path, library, f'-O{level}', '-c', '-o', client)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(client.is_file())
            self.execute_objects(library, client, level)

    def test_class_and_interface_symbols_have_abi3_exact_contracts(self):
        source = '''namespace Api;
public interface Readable{int32 Read();}
public class Counter:Readable{public int32 value;public int32 Read(){return value;}}
Readable Create(){return new Counter();}
int32 Read(Readable item){return item.Read();}
'''
        for level in range(4):
            library = self.object('named-symbols', source, level)
            names = {name for name, _ in read_object(library)[1]}
            self.assertIn('_KRT_ABI3_MANIFEST', names)
            self.assertIn('_KRT_ABI3_TYPES', names)
            self.assertIn('_KRT_MODULE3_METADATA', names)
            self.assertTrue(any(name.startswith('_KRT3$Api.Create$') for name in names))
            self.assertTrue(any(name.startswith('_KRTD3$') for name in names))

    def test_imported_static_state_and_closed_generic_storage_are_shared_once(self):
        provider = '''namespace Storage;
private static int32 initializations=0;
private int32 Seed(){initializations++;return 37;}
public class Stats{public static int32 count=Seed();public static int32 Read(){return count;}}
public class Counter<T>{public static int32 created=0;public T value;
 public Counter(T initial){value=initial;created++;}
 public T Read(){return value;}public static int32 Count(){return created;}}
int32 Initializations(){return initializations;}
Counter<int32> CreateInt(int32 initial){return new Counter<int32>(initial);}
'''
        service = '''using Storage;
int32 Advance(){Stats.count+=5;Counter<int32> value=new Counter<int32>(19);
 int32 result=value.Read();delete value;return result;}
'''
        consumer = '''using Storage;
extern int32 Advance();
int32 main(){if(Stats.count!=37||Stats.Read()!=37||Initializations()!=1){return 1;}
 Counter<int32> first=CreateInt(11);if(first.Read()!=11||Counter<int32>.Count()!=1){return 2;}
 if(Advance()!=19||Stats.count!=42||Counter<int32>.Count()!=2){return 3;}
 Counter<int64> wide=new Counter<int64>(1234567890123);
 if(wide.Read()!=1234567890123||Counter<int64>.Count()!=1||Counter<int32>.Count()!=2){return 4;}
 if(Initializations()!=1){return 5;}delete first;delete wide;return 0;}
'''
        for level in range(4):
            library=self.object('static-state-provider',provider,level)
            (library.parent/'Program.krt').unlink()
            modules=[library]
            for name,text in (('service',service),('consumer',consumer)):
                folder=self.work/f'static-state-{name}-o{level}';folder.mkdir()
                path=folder/'Program.krt';path.write_text(text);output=folder/'Program.kro'
                result=self.invoke(path,library,f'-O{level}','-c','-o',output)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr);modules.append(output)
            self.execute_module_set(tuple(modules),level)

    def test_explicit_external_statics_initialize_dependencies_and_retry_exceptions(self):
        declarations='''namespace State;
public static class Central{public static extern int32 calls;public static extern int32 value;
 public static extern uint128 wide;public static extern string text;public static extern int32 zero;}
'''
        provider='''namespace State;
private int32 Seed(){Central.calls++;if(Central.calls==1){throw 71;}return 37;}
public static class Central{public static int32 calls=0;public static uint128 wide=((uint128)1<<100)+19;
 public static int32 value=Seed();public static string text="ready";public static int32 zero;}
'''
        dependency=declarations+'''public static class Dependent{public static int32 value=Central.value+5;}
public static class Plain{public static int32 zero;}
'''
        consumer=declarations+'''public static class Dependent{public static extern int32 value;}
public static class Plain{public static extern int32 zero;}
int32 main(){try{int32 first=Dependent.value;return 1;}catch(int32 code){if(code!=71){return 2;}}
 if(Dependent.value!=42||Central.value!=37||Central.calls!=2||Central.zero!=0||Plain.zero!=0){return 3;}
 if(Central.wide!=((uint128)1<<100)+19||Central.text.Length!=5||Central.text[0]!=114){return 4;}
 int32 captured=5;fn(int32)->int32 read=function(int32 amount){return Central.value+Dependent.value+captured+amount;};
 if(read(7)!=91||Central.value+read(11)+Dependent.value!=174||Central.calls!=2){delete read;return 5;}
 delete read;unsafe(using krt.mem;){if(((int64)(&Central.wide)&15)!=0){return 6;}}return 0;}
'''
        for level in range(4):
            library=self.object('external-statics-provider',provider,level)
            service=self.object('external-statics-dependency',dependency,level)
            client=self.object('external-statics-client',consumer,level)
            self.execute_module_set((library,service,client),level)

    def test_strong_global_definitions_are_rejected_in_both_link_orders(self):
        for level in range(4):
            first=self.object('global-first','public static class State{public static int32 value=7;}',level)
            second=self.object('global-second','public static class State{public static int32 value=9;}',level)
            client=self.object('global-client','public static class State{public static extern int32 value;}int32 main(){return State.value==7?0:1;}',level)
            self.reject_object_set((first,second,client),level,'E_ABI_SYMBOL')
            for order,objects in enumerate(((first,second,client),(client,second,first))):
                output=self.work/f'bare-global-rejected-{level}-{order}';previous=b'previous global executable';output.write_bytes(previous)
                result=subprocess.run([str(LINKER),*map(str,objects),'--target','elf','-o',str(output)],cwd=self.work,env=self.env,capture_output=True,text=True,timeout=60)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr);self.assertIn('E_ABI_SYMBOL',result.stderr);self.assertEqual(output.read_bytes(),previous)

    def test_open_generic_method_signatures_normalize_parameter_names_and_constraints(self):
        provider='''namespace Templates;public interface Marker{}
public static class Operations{
 public static T Echo<T>(T value)where T:struct{return value;}
 public static U Transform<T,U>(T[] values,ref U result,fn(T)->U callback)where T:struct where U:struct{
  result=callback(values[0]);return result;}
 public static T Reference<T>(T value)where T:class,Marker,new(){return value;}}
'''
        consumer='''using Alias=Templates;namespace Templates;public interface Marker{}
public static class Operations{
 public static Element Echo<Element>(Element renamed)where Element:struct{return renamed;}
 public static Output Transform<Input,Output>(Input[] items,ref Output destination,fn(Input)->Output map)where Output:struct where Input:struct{
  destination=map(items[0]);return destination;}
 public static Item Reference<Item>(Item renamed)where Item:new(),Alias.Marker,class{return renamed;}}
int32 main(){if(Operations.Echo<int32>(37)!=37){return 1;}int32[] values=new int32[1];values[0]=7;
 uint128 result=0;fn(int32)->uint128 callback=function(int32 amount)->uint128{return ((uint128)amount<<100)+19;};
 uint128 actual=Operations.Transform<int32,uint128>(values,ref result,callback);
 delete callback;delete values;return actual==((uint128)7<<100)+19&&actual==result?0:2;}
'''
        for level in range(4):
            library=self.object('generic-method-alpha-provider',provider,level)
            client=self.object('generic-method-alpha-client',consumer,level)
            self.execute_objects(library,client,level)

    def test_metadata_body_edits_stop_at_lexical_braces_across_source_files(self):
        for level in range(4):
            for suffix in ('', '\n// trailing } { comment\n', '/* trailing } { comment */'):
                with self.subTest(optimization=level, suffix=suffix):
                    folder = self.work / f'body-boundary-o{level}-{len(suffix)}';folder.mkdir()
                    leaf = folder / 'Leaf.krt'
                    leaf.write_text('namespace Boundary;int32 Leaf(int32 amount){'
                                    'string marker="nested-body } {";/* inner } { */'
                                    'if(amount>0){return amount+7;}return 3;}' + suffix)
                    provider = folder / 'Provider.krt'
                    provider.write_text('import "Leaf.krt";namespace Boundary;'
                                        'int32 Relay(int32 value){return Leaf(value);}' + suffix)
                    library = folder / 'Provider.kro'
                    result = self.invoke(provider, f'-O{level}', '-c', '-o', library)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    header, symbols, _ = read_object(library)
                    record = next(record for name, record in symbols if name == '_KRT_MODULE3_METADATA')
                    data = library.read_bytes();position = 64 + header[4] + record[1]
                    count = struct.unpack_from('<I', data, position + 8)[0];position += 16
                    interfaces = {}
                    for _ in range(count):
                        path_length, length = struct.unpack_from('<II', data, position);position += 8
                        path = data[position:position + path_length].decode();position += path_length
                        interfaces[path] = data[position:position + length];position += length
                    for path in (leaf, provider):
                        interface = interfaces[str(path)]
                        self.assertIn(b'extern ', interface)
                        self.assertNotIn(b'nested-body', interface)
                        self.assertTrue(interface.endswith(suffix.encode()), interface)
                    leaf.unlink();provider.unlink()
                    consumer = folder / 'Consumer.krt'
                    consumer.write_text('using Boundary;int32 main(){return Relay(19)==26&&Leaf(0)==3?0:1;}')
                    client = folder / 'Consumer.kro'
                    result = self.invoke(consumer, library, f'-O{level}', '-c', '-o', client)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.execute_objects(library, client, level)

    def test_print_library_metadata_exports_a_program_at_end_of_file(self):
        for level in range(4):
            binary, _ = self.compile('int32 main(){print("READY");return 0;}', level)
            result = subprocess.run([str(binary)], cwd=self.work, env=self.env,
                                    capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout, b'READY\n')

    def test_open_generic_method_signature_conflicts_reject_independent_objects(self):
        cases=(
            ('parameter','T Echo<T>(T value){return value;}','U Echo<U>(int32 value){return default(U);}'),
            ('result','T Echo<T>(T value){return value;}','int32 Echo<U>(U value){return 7;}'),
            ('reference','T Echo<T>(ref T value){return value;}','U Echo<U>(U value){return value;}'),
            ('arity','T Echo<T>(T value){return value;}','U Echo<U,V>(U value){return value;}'),
            ('constraint','T Echo<T>(T value)where T:struct{return value;}','U Echo<U>(U value)where U:class{return value;}'),
            ('nested','T Echo<T>(fn(T)->T callback,T value){return callback(value);}','U Echo<U>(fn(int32)->U callback,U value){return value;}'),
        )
        for name,provided,declared in cases:
            for level in range(4):
                with self.subTest(case=name,optimization=level):
                    library=self.object('generic-'+name+'-provider','public static class Operations{public static '+provided+'}',level)
                    client=self.object('generic-'+name+'-client','public static class Operations{public static '+declared+'}int32 main(){return 0;}',level)
                    self.reject_objects(library,client,level,'E_ABI_LAYOUT')
                    for reverse,objects in enumerate(((library,client),(client,library))):
                        output=self.work/f'bare-generic-{name}-{level}-{reverse}';previous=b'previous generic executable';output.write_bytes(previous)
                        result=subprocess.run([str(LINKER),*map(str,objects),'--target','elf','-o',str(output)],cwd=self.work,env=self.env,capture_output=True,text=True,timeout=60)
                        self.assertNotEqual(result.returncode,0,result.stdout+result.stderr);self.assertIn('E_ABI_LAYOUT',result.stderr);self.assertEqual(output.read_bytes(),previous)

    def test_wide_function_reference_and_callback_boundaries_have_explicit_abi3(self):
        declarations='namespace Physical;public struct Tag{int32 value;}\n'
        provider=declarations+'''uint128 Echo(uint128 value,Tag tag){return value+(uint128)tag.value;}
void Touch(ref uint128 value,Tag tag){value+=(uint128)tag.value;}
uint128 Through(fn(uint128)->uint128 callback,uint128 value,Tag tag){return callback(value)+(uint128)tag.value;}
'''
        consumer=declarations+'''extern uint128 Echo(uint128 value,Tag tag);extern void Touch(ref uint128 value,Tag tag);
extern uint128 Through(fn(uint128)->uint128 callback,uint128 value,Tag tag);
int32 main(){Tag tag=default(Tag);tag.value=3;uint128 value=((uint128)1<<100)+19;
 if(Echo(value,tag)!=value+3){return 1;}Touch(ref value,tag);
 fn(uint128)->uint128 callback=function(uint128 amount)->uint128{return amount+7;};
 uint128 result=Through(callback,value,tag);delete callback;return result==((uint128)1<<100)+32&&value==((uint128)1<<100)+22?0:2;}
'''
        for level in range(4):
            library=self.object('wide-physical-provider',provider,level);client=self.object('wide-physical-client',consumer,level)
            names={name for name,_ in read_object(library)[1]}
            self.assertIn('_KRT_ABI3_MANIFEST',names);self.assertTrue(any(name.startswith('_KRT3$Physical.Echo$')for name in names))
            self.assertIn(b'KRTWIDE3',library.read_bytes());self.execute_objects(library,client,level)

    def test_legacy_wide_value_reference_and_callback_objects_are_rejected(self):
        fixture = ROOT / 'Test/SelfHost/fixtures/module-abi-wide'
        metadata = json.loads((fixture / 'provenance.txt').read_text())
        self.assertEqual(metadata['compiler_sha256'],
                         'ae82f9273739955093d460e4f4bc48924d0279925920ca070506105fbb307cab')
        self.assertEqual(hashlib.sha256((fixture / 'LegacyPhysical.krt').read_bytes()).hexdigest(),
                         metadata['source_sha256'])
        cases = (
            ('value', 'extern uint128 Echo(uint128 value,Tag tag);',
             'return Echo(value,tag)==value+3?0:1;', 'E_ABI_LAYOUT'),
            ('reference', 'extern void Touch(ref uint128 value,Tag tag);',
             'Touch(ref value,tag);return value==((uint128)1<<100)+22?0:1;', 'E_ABI_LAYOUT'),
            ('callback', 'extern uint128 Through(fn(uint128)->uint128 callback,uint128 value,Tag tag);',
             'fn(uint128)->uint128 callback=function(uint128 amount)->uint128{return amount+7;};'
             'uint128 result=Through(callback,value,tag);delete callback;return result==value+10?0:1;',
             'E_ABI_SYMBOL'),
        )
        for record in metadata['objects']:
            level = record['level']
            data = bytes.fromhex((fixture / record['hex_path']).read_text())
            self.assertEqual(hashlib.sha256(data).hexdigest(), record['object_sha256'])
            self.assertIn(b'_KRT_ABI2_MANIFEST', data)
            self.assertNotIn(b'KRTWIDE3', data)
            legacy = self.work / f'legacy-wide-o{level}.kro'
            legacy.write_bytes(data)
            for name, declaration, body, code in cases:
                with self.subTest(boundary=name, optimization=level):
                    source = ('namespace LegacyPhysical;public struct Tag{int32 value;}\n'
                              + declaration + 'int32 main(){Tag tag=default(Tag);tag.value=3;'
                              'uint128 value=((uint128)1<<100)+19;' + body + '}')
                    client = self.object('legacy-wide-' + name + '-client', source, level)
                    self.reject_objects(legacy, client, level, code)
                    for order, objects in enumerate(((legacy, client), (client, legacy))):
                        output = self.work / f'bare-legacy-wide-{name}-{level}-{order}'
                        previous = b'previous wide executable';output.write_bytes(previous)
                        result = subprocess.run([str(LINKER), *map(str, objects), '--target', 'elf', '-o', str(output)],
                                                cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn(code, result.stderr)
                        self.assertEqual(output.read_bytes(), previous)

    def test_repeated_public_interfaces_merge_from_distinct_kro_provenance(self):
        declarations='namespace Api;public struct Pair{int64 first;int64 second;}public interface Reader{Pair Read();}'
        provider=declarations+'''public class First:Reader{public Pair Read(){Pair value=default(Pair);value.first=7;return value;}}
Reader CreateFirst(){return new First();}'''
        service=declarations+'''public class Second:Reader{public Pair Read(){Pair value=default(Pair);value.second=11;return value;}}
Reader CreateSecond(){return new Second();}'''
        consumer='''using Api;int32 main(){Reader first=CreateFirst();Reader second=CreateSecond();
Pair a=first.Read();Pair b=second.Read();delete first;delete second;return a.first==7&&b.second==11?0:1;}'''
        for level in range(4):
            first=self.object('repeated-interface-first',provider,level);second=self.object('repeated-interface-second',service,level)
            (first.parent/'Program.krt').unlink();(second.parent/'Program.krt').unlink()
            folder=self.work/f'repeated-interface-consumer-o{level}';folder.mkdir();path=folder/'Program.krt';path.write_text(consumer)
            output=folder/'Program.kro';result=self.invoke(path,first,second,f'-O{level}','-c','-o',output)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.execute_module_set((first,second,output),level)

    def test_malformed_abi3_and_metadata_reject_without_replacing_outputs(self):
        provider='''namespace Api;public interface Reader{int32 Read();}
public class Value:Reader{public int32 Read(){return 7;}}Reader Create(){return new Value();}'''
        for level in range(4):
            library=self.object('malformed-provider',provider,level)
            original=library.read_bytes();header,symbols,_=read_object(library)
            for marker,code in (('_KRT_ABI3_MANIFEST','E_ABI_VERSION'),('_KRT_ABI3_TYPES','E_ABI_VERSION'),
                                ('_KRT_MODULE3_METADATA','E_MODULE_METADATA')):
                record=next(record for name,record in symbols if name==marker)
                corrupt=bytearray(original);struct.pack_into('<I',corrupt,64+header[4]+record[1]+4,255)
                damaged=library.parent/f'{marker}.kro';damaged.write_bytes(corrupt)
                folder=self.work/f'malformed-client-{marker}-o{level}';folder.mkdir()
                source=folder/'Program.krt';source.write_text('using Api;int32 main(){Reader value=Create();return value.Read()==7?0:1;}')
                output=folder/'Program.kro';previous=b'previous object\x00must survive';output.write_bytes(previous)
                result=self.invoke(source,damaged,f'-O{level}','-c','-o',output)
                self.assertEqual(result.returncode,1,result.stdout+result.stderr);self.assertIn(code,result.stderr);self.assertEqual(output.read_bytes(),previous)
                if marker!='_KRT_MODULE3_METADATA':
                    executable=folder/'program';executable.write_bytes(previous)
                    result=subprocess.run([str(LINKER),str(damaged),'--target','elf','-o',str(executable)],
                                          cwd=self.work,env=self.env,capture_output=True,text=True,timeout=15)
                    self.assertNotEqual(result.returncode,0,result.stdout+result.stderr);self.assertIn(code,result.stderr);self.assertEqual(executable.read_bytes(),previous)

    def test_weak_runtime_records_compare_payloads_and_complete_relocations(self):
        declarations = 'public struct Packet{public int64 value;}'
        provider = declarations + 'object Create(){Packet value=default(Packet);value.value=42;return value;}'
        consumer = declarations + 'extern object Create();int32 main(){object boxed=Create();Packet value=(Packet)boxed;delete boxed;return value.value==42?0:1;}'
        for level in range(4):
            library = self.object('weak-record-provider', provider, level)
            client = self.object('weak-record-consumer', consumer, level)
            self.execute_objects(library, client, level)
            original = library.read_bytes();header, symbols, _ = read_object(library)
            descriptor_index, descriptor = next((index, record) for index, (name, record) in enumerate(symbols)
                                                 if name.startswith('_KRTD3$') and '5061636b6574' in name)
            self.assertEqual(descriptor[4], 2)
            relocation_base = 64 + sum(header[4:7]) + header[12] * 32
            relocation_index = next(index for index in range(header[8], header[8] + header[9])
                                    if descriptor[1] <= struct.unpack_from('<I', original, relocation_base + index * 16)[0] < descriptor[1] + descriptor[2])
            relocation = relocation_base + relocation_index * 16
            for name in ('payload', 'relocation-target', 'relocation-kind', 'relocation-addend'):
                with self.subTest(optimization=level, difference=name):
                    damaged = bytearray(original)
                    if name == 'payload':
                        damaged[64 + header[4] + descriptor[1]] ^= 1
                    elif name == 'relocation-target':
                        struct.pack_into('<I', damaged, relocation + 4, descriptor_index)
                    elif name == 'relocation-kind':
                        struct.pack_into('<I', damaged, relocation + 8, 2)
                    else:
                        struct.pack_into('<i', damaged, relocation + 12, 1)
                    path = library.parent / f'weak-{name}.kro';path.write_bytes(damaged)
                    self.reject_objects(path, client, level, 'E_ABI_LAYOUT')
                    for order, objects in enumerate(((path, client), (client, path))):
                        output = self.work / f'bare-weak-{name}-{level}-{order}'
                        previous = b'previous weak executable';output.write_bytes(previous)
                        result = subprocess.run([str(LINKER), *map(str, objects), '--target', 'elf', '-o', str(output)],
                                                cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn('E_ABI_LAYOUT', result.stderr)
                        self.assertEqual(output.read_bytes(), previous)

    def test_imported_internal_and_private_members_remain_inaccessible(self):
        provider='''namespace Visibility;
internal class Hidden{public int32 value;}
public class Value{private int32 secret=7;internal int32 internalValue=11;public int32 Read(){return secret;}}
internal int32 Internal(){return 13;}Value Create(){return new Value();}'''
        for level in range(4):
            library=self.object('access-provider',provider,level);(library.parent/'Program.krt').unlink()
            for label,body in (('internal-type','Hidden value=new Hidden();return value.value;'),
                               ('private-field','Value value=Create();return value.secret;'),
                               ('internal-field','Value value=Create();return value.internalValue;'),
                               ('internal-function','return Internal();')):
                folder=self.work/f'access-{label}-o{level}';folder.mkdir();source=folder/'Program.krt'
                source.write_text('using Visibility;int32 main(){'+body+'}')
                output=folder/'Program.kro';previous=b'previous object';output.write_bytes(previous)
                result=self.invoke(source,library,f'-O{level}','-c','-o',output)
                self.assertEqual(result.returncode,1,result.stdout+result.stderr);self.assertIn('E_ACCESS',result.stderr);self.assertEqual(output.read_bytes(),previous)

    def test_internal_module_declarations_preserve_public_external_prototypes(self):
        provider = '''namespace Authorities;
internal int32 package(){return 9;}
internal int32 package(int32 value){return value+9;}
public int32 ReadInternal(){return package();}
public T Keep<T>(T value)where T:struct{if(package()!=9||package(7)!=16){throw 1;}return value;}
'''
        public = 'namespace Authorities;public int32 package(){return 42;}public int32 package(int32 value){return value+42;}'
        consumer = '''using Authorities;namespace Authorities;extern int32 package();extern int32 package(int32 value);
int32 main(){uint128 wide=((uint128)1<<100)+19;
return package()==42&&package(7)==49&&ReadInternal()==9&&Keep<int32>(7)==7&&Keep<uint128>(wide)==wide?0:1;}
'''
        for level in range(4):
            private_library = self.object('module-internal-provider', provider, level)
            public_library = self.object('module-public-provider', public, level)
            folder = self.work / f'module-authority-client-o{level}';folder.mkdir()
            source = folder / 'Program.krt';source.write_text(consumer)
            client = folder / 'Program.kro'
            result = self.invoke(source, private_library, public_library, f'-O{level}', '-c', '-o', client)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.execute_module_set((private_library, public_library, client), level)
            for objects in ((private_library, client), (client, private_library)):
                output = folder / 'unresolved-public';previous = b'previous authority executable';output.write_bytes(previous)
                result = self.invoke(*objects, '-o', output)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn('E_LINK', result.stderr)
                self.assertEqual(output.read_bytes(), previous)

    def test_internal_source_module_prototypes_still_bind_across_files(self):
        for level in range(4):
            folder = self.work / f'internal-same-module-o{level}';folder.mkdir()
            implementation = folder / 'Package.krt'
            implementation.write_text('''namespace LocalAssembly;internal int32 package(){return 9;}
internal int32 Choose(int64 value){return 64;}
internal int32 Reverse(int32 value){return 132;}''')
            template = folder / 'Templates.krt'
            template.write_text('''namespace LocalAssembly;
public int32 Choose(int32 value){return 32;}
public int32 Reverse(int64 value){return 164;}
public class Mixed{private int32 Pick(int64 value){return 64;}public int32 Pick(int32 value){return 32;}
 public int32 Check(){return Pick((int32)7)==32&&Pick((int64)7)==64?0:1;}}
public T Keep<T>(T value)where T:struct{if(package()!=9){throw 1;}return value;}''')
            source = folder / 'Main.krt'
            source.write_text('''namespace LocalAssembly;extern int32 package();int32 main(){
if(package()!=9||Keep<int64>(42)!=42||Choose((int32)7)!=32||Choose((int64)7)!=64||
 Reverse((int32)7)!=132||Reverse((int64)7)!=164){return 1;}
Mixed item=new Mixed();int32 result=item.Check();delete item;return result;}''')
            output = folder / 'program'
            result = self.invoke(source, implementation, template, f'-O{level}', '-o', output)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(output)], cwd=self.work, env=self.env, capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_constant_data_modules_export_initialized_aligned_storage_without_assignments(self):
        provider = '''namespace ConstantData;
public static int30 narrow=(int30)((-3077)*2+1);
public static bool predicate=(7<9)&&!false;
public static uint128 wide=(uint128)18446744073709551623;
public static uint72 masked=(uint72)1208925819614629174706183;
public static int72 sign=(int72)4722366482869645213695;
public static uint66 edge=(uint66)590295810358705651715;
public static int126 upper=(int126)85070591730234615865843651857942052863;
public static int64 folded=false?1:((8<<4)|3);
public static uint8 truncated=(uint8)300;
public static int64 zero;
'''
        consumer = '''using ConstantData;
int32 Verify(){return narrow==-7&&folded==19&&wide==(uint128)18446744073709551632&&
 masked==12&&sign==-2&&edge==4&&upper==-2?0:1;}
int32 main(){if(narrow!=-6153||!predicate||wide!=(uint128)18446744073709551623||
 masked!=7||sign!=-1||edge!=3||upper!=-1||folded!=131||truncated!=44||zero!=0){return 1;}
 uint128 dynamic=1208925819614629174706183;
 if((uint72)dynamic!=masked){return 3;}dynamic=4722366482869645213695;
 if((int72)dynamic!=sign){return 4;}dynamic=590295810358705651715;
 if((uint66)dynamic!=edge){return 5;}dynamic=85070591730234615865843651857942052863;
 if((int126)dynamic!=upper){return 6;}
 unsafe(using krt.mem;){if(((int64)(&wide)&15)!=0){return 2;}}
 narrow=-7;folded=19;wide+=9;masked+=5;sign--;edge++;upper--;return Verify();}
'''
        expected = {'narrow': (4, (-6153).to_bytes(4, 'little', signed=True)),
                    'predicate': (1, b'\x01'),
                    'wide': (16, (18446744073709551623).to_bytes(16, 'little')),
                    'masked': (16, (7).to_bytes(16, 'little')),
                    'sign': (16, (-1).to_bytes(16, 'little', signed=True)),
                    'edge': (16, (3).to_bytes(16, 'little')),
                    'upper': (16, (-1).to_bytes(16, 'little', signed=True)),
                    'folded': (8, (131).to_bytes(8, 'little')),
                    'truncated': (1, b'\x2c'), 'zero': (8, bytes(8))}
        for level in range(4):
            library = self.object('constant-data-provider', provider, level)
            header, symbols, relocations = read_object(library);raw = library.read_bytes()
            self.assertEqual(header[4], 3);self.assertEqual(raw[64:67], b'\x31\xc0\xc3')
            self.assertEqual(relocations, [])
            self.assertNotIn('_KRT_MODULE_INIT_GUARD', [name for name, _ in symbols])
            code = [(name, record) for name, record in symbols if record[3] == 1]
            self.assertEqual(len(code), len(expected))
            self.assertTrue(all(name.startswith('_KRTL3$') and record[1:3] == (0, 3)
                                for name, record in code))
            payload = raw[64 + header[4] + header[5]:64 + sum(header[4:7])]
            slots = []
            for field, (width, value) in expected.items():
                name, record = next((name, record) for name, record in symbols
                                    if name.startswith('_KRTG3$') and field.encode().hex() in name)
                self.assertEqual(record[2:4], (width, 3))
                self.assertEqual(record[1] % width, 0)
                self.assertEqual(payload[record[1]:record[1] + width], value)
                slots.append((record[1], record[1] + width))
                guard = next(record for guard_name, record in symbols
                             if guard_name == name.replace('_KRTG3$', '_KRTJ3$', 1))
                self.assertEqual(guard[2:4], (16, 3))
                self.assertEqual(payload[guard[1]:guard[1] + 16], (2).to_bytes(8, 'little') + bytes(8))
                slots.append((guard[1], guard[1] + 16))
            slots.sort()
            self.assertTrue(all(left[1] <= right[0] for left, right in zip(slots, slots[1:])))
            (library.parent / 'Program.krt').unlink()
            folder = self.work / f'constant-data-client-o{level}';folder.mkdir()
            source = folder / 'Program.krt';source.write_text(consumer);client = folder / 'Program.kro'
            result = self.invoke(source, library, f'-O{level}', '-c', '-o', client)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.execute_objects(library, client, level)
