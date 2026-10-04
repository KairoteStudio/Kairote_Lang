"""Independently compiled struct values require exact ABI2 layout contracts."""
import hashlib
import json
import struct
import subprocess

from Test.SelfHost.test_native_linking import read_object
from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


class StructLinkageTests(NativeCompilerFixture):
    def invoke(self, *arguments):
        return subprocess.run([str(COMPILER), '--linker', str(LINKER), *map(str, arguments)],
                              cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)

    def object(self, name, source, level, shared=None):
        folder = self.work / f'{name}-o{level}'
        folder.mkdir(exist_ok=True)
        if shared is not None:
            (folder / 'Shared.krt').write_text(shared)
            source = 'import "Shared.krt";\n' + source
        path = folder / 'Program.krt'
        path.write_text(source)
        output = folder / 'Program.kro'
        result = self.invoke(path, f'-O{level}', '-c', '-o', output)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(output.read_bytes()[:4], b'KRO\x00')
        return output

    def execute_objects(self, library, consumer, level):
        for reversed_order in (False, True):
            with self.subTest(optimization=level, reversed_order=reversed_order):
                output = self.work / f'program-o{level}-{reversed_order}'
                objects = (consumer, library) if reversed_order else (library, consumer)
                result = self.invoke(*objects, '-o', output)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(output.read_bytes()[:4], b'\x7fELF')
                run = subprocess.run([str(output)], cwd=self.work, env=self.env,
                                     capture_output=True, timeout=15)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def reject_objects(self, library, consumer, level, code):
        self.reject_object_set((library, consumer), level, code)

    def reject_object_set(self, objects, level, code):
        for reversed_order in (False, True):
            for previous in (None, b'previous executable\x00must survive'):
                with self.subTest(optimization=level, reversed_order=reversed_order,
                                  previous_artifact=previous is not None, code=code):
                    output = self.work / f'rejected-o{level}-{reversed_order}-{previous is not None}'
                    if output.exists():
                        output.unlink()
                    if previous is not None:
                        output.write_bytes(previous)
                    ordered = tuple(reversed(objects)) if reversed_order else objects
                    result = self.invoke(*ordered, '-o', output)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn(code, result.stderr)
                    if previous is None:
                        self.assertFalse(output.exists(), 'rejection created an executable')
                    else:
                        self.assertEqual(output.read_bytes(), previous)

    def test_shared_struct_value_result_ref_and_external_typed_callbacks(self):
        shared = 'namespace Data;public struct Pair{int64 first;int64 second;}\n'
        library_source = '''using Data;
Pair Change(Pair value){value.first+=10;value.second+=20;return value;}
void Touch(ref Pair value){value.second++;}
Pair Through(fn(Pair)->Pair callback,Pair value){return callback(value);}
'''
        consumer_source = '''using Data;
extern Pair Change(Pair value);extern void Touch(ref Pair value);
extern Pair Through(fn(Pair)->Pair callback,Pair value);
int32 main(){
    Pair source=default(Pair);source.first=7;source.second=11;
    Pair changed=Change(source);Touch(ref source);
    if(source.first!=7||source.second!=12||changed.first!=17||changed.second!=31){return 1;}
    fn(Pair)->Pair callback=&Change;Pair indirect=callback(changed);Pair roundtrip=Through(callback,source);
    if(indirect.first!=27||indirect.second!=51||roundtrip.first!=17||roundtrip.second!=32){return 2;}
    changed.first=99;if(source.first!=7||indirect.first!=27){return 3;}
    return 0;
}
'''
        for level in range(4):
            library = self.object('pairs-library', library_source, level, shared)
            consumer = self.object('pairs-consumer', consumer_source, level, shared)
            self.execute_objects(library, consumer, level)

    def test_nested_fixed_wide_generic_fields_and_pointer_cycles_cross_objects(self):
        shared = '''namespace Data;
public struct Box<T>{byte tag;T value;byte end;}
public struct Node{Node* next;int64 number;}
public struct Packet{uint128 wide;byte payload[5];Box<int64> box;Node* next;}
'''
        library_source = '''using Data;
Packet Create(uint128 wide,int64 number){Packet packet=default(Packet);packet.wide=wide;
packet.payload[0]=17;packet.payload[4]=25;packet.box.tag=3;packet.box.value=number;packet.box.end=5;return packet;}
Packet Copy(Packet value){value.payload[0]=31;value.box.value+=2;return value;}
void Update(ref Packet value){value.payload[4]=41;value.box.value+=3;}
int32 Check(Node* value){unsafe(using krt.mem;){return value.next==value&&value.number==42?0:1;}}
'''
        consumer_source = '''using Data;
extern Packet Create(uint128 wide,int64 number);extern Packet Copy(Packet value);
extern void Update(ref Packet value);extern int32 Check(Node* value);
int32 main(){unsafe(using krt.mem;){
    uint128 high=((uint128)1<<100)+((uint128)1<<70)+7;
    Packet original=Create(high,29);Node node=default(Node);node.number=42;node.next=&node;original.next=&node;
    Packet copied=Copy(original);Update(ref original);
    if(sizeof(Packet)!=64||sizeof(Box<int64>)!=24||sizeof(Node)!=16){return 1;}
    if(copied.wide!=high||copied.payload.Length!=5||copied.payload[0]!=31||copied.payload[4]!=25){return 2;}
    if(copied.box.tag!=3||copied.box.value!=31||copied.box.end!=5||copied.next!=&node||Check(copied.next)!=0){return 3;}
    if(original.payload[0]!=17||original.payload[4]!=41||original.box.value!=32){return 4;}
    copied.payload[4]=99;copied.box.value=101;
    if(original.payload[4]!=41||original.box.value!=32){return 5;}return 0;
}}
'''
        for level in range(4):
            library = self.object('nested-library', library_source, level, shared)
            consumer = self.object('nested-consumer', consumer_source, level, shared)
            self.execute_objects(library, consumer, level)

    def test_duplicate_abi2_strong_definitions_are_rejected_in_both_link_orders(self):
        shared = 'namespace Data;public struct Pair{int64 first;int64 second;}'
        consumer_source = '''using Data;extern Pair Echo(Pair value);
int32 main(){Pair value=default(Pair);value.first=7;return Echo(value).first==8?0:1;}'''
        for level in range(4):
            first = self.object('strong-first', 'using Data;Pair Echo(Pair value){value.first++;return value;}', level, shared)
            second = self.object('strong-second', 'using Data;Pair Echo(Pair value){value.first+=9;return value;}', level, shared)
            consumer = self.object('strong-consumer', consumer_source, level, shared)
            self.reject_object_set((first, second, consumer), level, 'E_ABI_SYMBOL')

    def test_valid_abi2_objects_link_and_execute_through_bare_arklink(self):
        shared = 'namespace Data;public struct Pair{int64 first;int64 second;}'
        library_source = 'using Data;Pair Echo(Pair value){value.first+=10;value.second+=20;return value;}'
        consumer_source = '''using Data;extern Pair Echo(Pair value);
int32 main(){Pair value=default(Pair);value.first=7;value.second=11;
Pair result=Echo(value);return value.first==7&&value.second==11&&result.first==17&&result.second==31?0:1;}'''
        for level in range(4):
            library = self.object('bare-library', library_source, level, shared)
            consumer = self.object('bare-consumer', consumer_source, level, shared)
            for reversed_order in (False, True):
                with self.subTest(optimization=level, reversed_order=reversed_order):
                    objects = (consumer, library) if reversed_order else (library, consumer)
                    output = self.work / f'bare-o{level}-{reversed_order}'
                    result = subprocess.run([str(LINKER), *map(str, objects), '--target', 'elf', '-o', str(output)],
                                            cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(output.read_bytes()[:4], b'\x7fELF')
                    output.chmod(0o755)
                    run = subprocess.run([str(output)], cwd=self.work, env=self.env, capture_output=True, timeout=15)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_callback_only_struct_contract_rejects_equal_size_different_layout(self):
        library_source = '''namespace Data;public struct Pair{int64 first;int64 second;}
int32 Invoke(fn()->Pair callback){Pair value=callback();return (int32)value.first;}'''
        consumer_source = '''namespace Data{public struct Pair{int64 first;int32 second;}
extern int32 Invoke(fn()->Pair callback);
Pair Make(){Pair value=default(Pair);value.first=7;value.second=19;return value;}}
int32 main(){return Data.Invoke(&Data.Make)==7?0:1;}'''
        for level in range(4):
            library = self.object('callback-layout-library', library_source, level)
            consumer = self.object('callback-layout-consumer', consumer_source, level)
            self.reject_objects(library, consumer, level, 'E_ABI_LAYOUT')

    def test_namespace_alias_and_fully_qualified_struct_callbacks_share_nominal_identity(self):
        shared = 'namespace Data;public struct Pair{int64 first;int64 second;}'
        library_source = '''using D=Data;
D.Pair Change(D.Pair value){value.first+=10;value.second+=20;return value;}
int32 Apply(fn(D.Pair)->D.Pair callback,D.Pair value){D.Pair changed=callback(value);return (int32)(changed.first+changed.second);}'''
        consumer_source = '''using Alias=Data;
extern Data.Pair Change(Data.Pair value);
extern int32 Apply(fn(Data.Pair)->Data.Pair callback,Data.Pair value);
int32 main(){Alias.Pair source=default(Alias.Pair);source.first=7;source.second=11;
fn(Alias.Pair)->Alias.Pair callback=&Change;Data.Pair changed=callback(source);
if(source.first!=7||source.second!=11||changed.first!=17||changed.second!=31){return 1;}
return Apply(callback,source)==48?0:2;}'''
        for level in range(4):
            library = self.object('alias-library', library_source, level, shared)
            consumer = self.object('alias-consumer', consumer_source, level, shared)
            self.execute_objects(library, consumer, level)

    def test_equal_layout_different_nominal_struct_callbacks_cannot_be_assigned(self):
        prefix = '''namespace First{public struct Pair{int64 first;int64 second;}}
namespace Second{public struct Pair{int64 first;int64 second;}}
extern First.Pair Change(First.Pair value);
'''
        source = prefix + '''int32 main(){
fn(Second.Pair)->Second.Pair callback=&Change;return 0;}'''
        path = self.work / 'different-nominal.krt'
        path.write_text(source)
        for level in range(4):
            for previous in (None, b'previous object\x00must survive'):
                with self.subTest(optimization=level, previous_artifact=previous is not None):
                    output = self.work / f'different-nominal-o{level}-{previous is not None}.kro'
                    if previous is not None:
                        output.write_bytes(previous)
                    result = self.invoke(path, f'-O{level}', '-c', '-o', output)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn('E_LOWER', result.stderr)
                    self.assertNotIn('E_PARSE', result.stderr)
                    if previous is None:
                        self.assertFalse(output.exists())
                    else:
                        self.assertEqual(output.read_bytes(), previous)

    def test_struct_owner_methods_and_value_results_link_against_extern_members(self):
        producer_type = '''public struct Pair{public int64 first;public int64 second;
public int64 Sum(){return first+second;}
public Pair Shift(int64 amount){first+=amount;return this;}
public readonly int64 Read(){return first+second;}
public int64 Touch(){first++;return first+second;}}'''
        consumer_type = '''public struct Pair{public int64 first;public int64 second;
public extern int64 Sum();public extern Pair Shift(int64 amount);
public readonly extern int64 Read();public extern int64 Touch();}'''
        holder = '''class Holder{public readonly Pair value;public Holder(Pair initial){value=initial;}}'''
        main = '''int32 main(){Pair value=default(Pair);value.first=7;value.second=11;
if(value.Sum()!=18){return 1;}Pair shifted=value.Shift(10);value.first=22;
if(value.Sum()!=33||shifted.Sum()!=28||shifted.first!=17||shifted.second!=11){return 2;}
fn(ref Pair)->int64 sum=&Pair.Sum;fn(ref Pair,int64)->Pair shift=&Pair.Shift;
if(sum(ref value)!=33){return 3;}Pair indirect=shift(ref value,1);value.first=30;
if(indirect.first!=23||indirect.second!=11||sum(ref value)!=41){return 4;}
Holder stored=new Holder(shifted);
if(stored.value.Read()!=28||stored.value.Touch()!=29||stored.value.Read()!=28){return 5;}
delete stored;return 0;}'''
        for level in range(4):
            library = self.object('owner-library', producer_type, level)
            consumer = self.object('owner-consumer', consumer_type + holder + main, level)
            self.execute_objects(library, consumer, level)
            duplicate = self.object('owner-body-consumer', producer_type + holder + main, level)
            self.reject_objects(library, duplicate, level, 'E_ABI_SYMBOL')
            readonly_declaration = consumer_type.replace('public extern int64 Touch();', 'public readonly extern int64 Touch();')
            mismatch = self.object('owner-readonly-mismatch', readonly_declaration +
                                   'int32 main(){Pair value=default(Pair);return (int32)value.Touch();}', level)
            self.reject_objects(library, mismatch, level, 'E_ABI_LAYOUT')

    def test_closed_generic_struct_owner_methods_link_against_extern_members(self):
        producer_type = '''public struct Box<T>{public T value;public int64 extra;
public int64 Sum(){return (int64)value+extra;}
public Box<T> Shift(int64 amount){extra+=amount;return this;}}'''
        consumer_type = '''public struct Box<T>{public T value;public int64 extra;
public extern int64 Sum();public extern Box<T> Shift(int64 amount);}'''
        instantiate = '''int64 ForceClosedOwner(){Box<int64> value=default(Box<int64>);
return value.Sum()+value.Shift(0).Sum();}'''
        main = '''int32 main(){Box<int64> value=default(Box<int64>);value.value=7;value.extra=11;
if(value.Sum()!=18){return 1;}Box<int64> shifted=value.Shift(10);value.value=22;
if(value.Sum()!=43||shifted.Sum()!=28||shifted.value!=7||shifted.extra!=21){return 2;}
fn(ref Box<int64>)->int64 sum=&Box<int64>.Sum;
fn(ref Box<int64>,int64)->Box<int64> shift=&Box<int64>.Shift;
if(sum(ref value)!=43){return 3;}Box<int64> indirect=shift(ref value,1);value.value=30;
if(indirect.value!=22||indirect.extra!=22||sum(ref value)!=52){return 4;}return 0;}'''
        for level in range(4):
            library = self.object('generic-owner-library', producer_type + instantiate, level)
            consumer = self.object('generic-owner-consumer', consumer_type + main, level)
            self.execute_objects(library, consumer, level)
            duplicate = self.object('generic-owner-body-consumer', producer_type + main, level)
            self.reject_objects(library, duplicate, level, 'E_ABI_SYMBOL')

    @staticmethod
    def pad_rodata_to_size(path, output, size):
        data = path.read_bytes()
        header = struct.unpack_from('<16I', data)
        padding = size - len(data)
        if padding < 0:
            raise AssertionError('requested object size is smaller than the input')
        boundary = 64 + header[4] + header[5]
        changed = bytearray(data[:boundary] + bytes(padding) + data[boundary:])
        struct.pack_into('<I', changed, 5 * 4, header[5] + padding)
        output.write_bytes(changed)
        if output.stat().st_size != size:
            raise AssertionError('large KRO fixture must have the exact requested size')

    @staticmethod
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()

    def test_large_scalar_and_abi2_objects_publish_and_restore_project_cache(self):
        cases = (
            ('scalar', 32 * 1024 * 1024, None,
             'int32 Answer(){return 42;}',
             'extern int32 Answer();int32 main(){return Answer()==42?0:1;}'),
            ('abi2', 33 * 1024 * 1024,
             'namespace Data;public struct Pair{int64 first;int64 second;}',
             'using Data;Pair Echo(Pair value){value.first+=10;value.second+=20;return value;}',
             '''using Data;extern Pair Echo(Pair value);int32 main(){Pair value=default(Pair);
value.first=7;value.second=11;Pair result=Echo(value);
return value.first==7&&value.second==11&&result.first==17&&result.second==31?0:1;}'''),
        )
        for label, size, shared, library_source, consumer_source in cases:
            with self.subTest(kind=label, object_size=size):
                library = self.object('large-' + label + '-library', library_source, 2, shared)
                consumer = self.object('large-' + label + '-consumer', consumer_source, 2, shared)
                large = self.work / f'large-{label}.kro'
                self.pad_rodata_to_size(library, large, size)
                if shared is not None:
                    original, offset, _ = self.manifest(library)
                    padded, padded_offset, _ = self.manifest(large)
                    self.assertEqual(padded_offset, offset)
                    _, symbols, _ = read_object(library)
                    marker = next(symbol for name, symbol in symbols if name == '_KRT_ABI2_MANIFEST')
                    self.assertEqual(padded[offset:offset + marker[2]], original[offset:offset + marker[2]])
                output = self.work / f'large-{label}-program'
                result = self.invoke(large, consumer, '-o', output)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertGreater(output.stat().st_size, 32 * 1024 * 1024)
                run = subprocess.run([str(output)], cwd=self.work, env=self.env, capture_output=True, timeout=15)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

                project_output = self.work / f'large-{label}-project-program'
                project = self.work / f'large-{label}-project.json'
                project.write_text(json.dumps({'name': 'large-' + label, 'sources': [str(consumer.with_suffix('.krt'))],
                                               'libraries': [str(large)], 'output': str(project_output), 'optimization': 2}))
                build = self.invoke('build', project)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                artifact_hash = self.digest(project_output)
                manifest_paths = tuple((self.work / '.krtcache').rglob('manifest.krtcache'))
                self.assertTrue(manifest_paths)
                cache_hashes = {str(path): self.digest(path) for path in (self.work / '.krtcache').rglob('*') if path.is_file()}
                cached = self.invoke('build', project)
                self.assertEqual(cached.returncode, 0, cached.stdout + cached.stderr)
                self.assertIn('up-to-date:', cached.stdout)
                self.assertEqual(self.digest(project_output), artifact_hash)
                project_output.unlink()
                restored = self.invoke('build', project)
                self.assertEqual(restored.returncode, 0, restored.stdout + restored.stderr)
                self.assertIn('up-to-date:', restored.stdout)
                self.assertEqual(self.digest(project_output), artifact_hash)
                self.assertEqual({str(path): self.digest(path) for path in (self.work / '.krtcache').rglob('*') if path.is_file()}, cache_hashes)
                run = subprocess.run([str(project_output)], cwd=self.work, env=self.env, capture_output=True, timeout=15)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_same_nominal_struct_requires_exact_size_field_and_fixed_count(self):
        shapes = (
            ('size', 'int64 first;int64 second;', 'int32 first;int32 second;'),
            ('field-type', 'uint32 first;uint32 second;', 'uint16 first;uint16 second;uint32 tail;'),
            ('field-name', 'uint32 first;uint32 second;', 'uint32 left;uint32 right;'),
            ('fixed-count', 'byte payload[4];int32 tag;', 'byte payload[3];int32 tag;'),
            ('void-depth', 'void* pointer;', 'void** pointer;'),
            ('callback-void-depth', 'fn()->void* callback;', 'fn()->void** callback;'),
        )
        for name, library_fields, consumer_fields in shapes:
            library_source = 'namespace Data;public struct Value{' + library_fields + '}Value Echo(Value value){return value;}'
            consumer_source = 'namespace Data{public struct Value{' + consumer_fields + '}extern Value Echo(Value value);}'
            consumer_source += 'int32 main(){Data.Value value=default(Data.Value);Data.Value copied=Data.Echo(value);return 0;}'
            for level in range(4):
                library = self.object(name + '-library', library_source, level)
                consumer = self.object(name + '-consumer', consumer_source, level)
                self.reject_objects(library, consumer, level, 'E_ABI_LAYOUT')

    @staticmethod
    def manifest(path):
        data = path.read_bytes()
        header, symbols, _ = read_object(path)
        matching = [(index, symbol) for index, (name, symbol) in enumerate(symbols) if name == '_KRT_ABI2_MANIFEST']
        if len(matching) != 1:
            raise AssertionError('one ABI2 manifest symbol is required')
        index, symbol = matching[0]
        if symbol[3] != 2 or symbol[4] != 0:
            raise AssertionError('manifest must be a local rodata symbol')
        offset, length = 64 + header[4] + symbol[1], symbol[2]
        magic, version, count, total = struct.unpack_from('<4I', data, offset)
        if (magic, version, total) != (0x32494241, 2, length) or count == 0:
            raise AssertionError('invalid ABI2 manifest header')
        cursor = offset + 16
        for _ in range(count):
            referenced, defined, nominal, contract = struct.unpack_from('<4I', data, cursor)
            if referenced >= len(symbols) or defined not in (0, 1) or nominal == 0 or contract == 0:
                raise AssertionError('invalid ABI2 manifest record')
            cursor += 16 + nominal + contract
        if cursor != offset + length:
            raise AssertionError('ABI2 records must consume the exact manifest length')
        strings_offset = 64 + sum(header[4:7]) + header[12] * 32 + header[14] * 16
        return data, offset, strings_offset + symbol[0]

    @classmethod
    def replace_manifest_payload(cls, path, payload):
        """Change the record set while keeping KRO/manifest lengths consistent."""
        original, offset, _ = cls.manifest(path)
        header, symbols, _ = read_object(path)
        index, marker = next((index, symbol) for index, (name, symbol) in enumerate(symbols)
                             if name == '_KRT_ABI2_MANIFEST')
        delta = len(payload) - marker[2]
        changed = bytearray(original[:offset] + payload + original[offset + marker[2]:])
        struct.pack_into('<I', changed, 5 * 4, header[5] + delta)
        symbol_table = 64 + sum(header[4:7]) + delta
        for position, (_, symbol) in enumerate(symbols):
            if position == index:
                struct.pack_into('<I', changed, symbol_table + position * 32 + 8, len(payload))
            elif symbol[3] == 2 and symbol[1] >= marker[1] + marker[2]:
                struct.pack_into('<I', changed, symbol_table + position * 32 + 4, symbol[1] + delta)
        return changed

    def test_missing_and_corrupt_manifests_are_rejected_before_linking(self):
        shared = 'public struct Pair{int64 first;int64 second;}'
        library_source = 'Pair Echo(Pair value){return value;}'
        consumer_source = 'extern Pair Echo(Pair value);int32 main(){Pair value=default(Pair);var copied=Echo(value);return 0;}'
        for level in range(4):
            library = self.object('manifest-library', library_source, level, shared)
            consumer = self.object('manifest-consumer', consumer_source, level, shared)
            self.execute_objects(library, consumer, level)
            original, offset, marker = self.manifest(library)
            self.manifest(consumer)
            header, symbols, _ = read_object(library)
            marker_index, marker_symbol = next((index, symbol) for index, (name, symbol) in enumerate(symbols)
                                               if name == '_KRT_ABI2_MANIFEST')
            symbol_table = 64 + sum(header[4:7])
            marker_entry = symbol_table + marker_index * 32
            record_count = struct.unpack_from('<I', original, offset + 8)[0]
            mutations = (
                ('missing', marker, b'X', 'E_ABI_MANIFEST'),
                ('magic', offset, struct.pack('<I', 0), 'E_ABI_MANIFEST'),
                ('version', offset + 4, struct.pack('<I', 3), 'E_ABI_VERSION'),
                ('length', offset + 16 + 12, struct.pack('<I', 0xffffffff), 'E_ABI_MANIFEST'),
                ('record-symbol', offset + 16, struct.pack('<I', header[12]), 'E_ABI_MANIFEST'),
                ('record-defined', offset + 20, struct.pack('<I', 0), 'E_ABI_MANIFEST'),
                ('record-count', offset + 8, struct.pack('<I', record_count + 1), 'E_ABI_MANIFEST'),
                ('marker-binding', marker_entry + 16, struct.pack('<I', 1), 'E_ABI_MANIFEST'),
                ('marker-section', marker_entry + 12, struct.pack('<I', 3), 'E_ABI_MANIFEST'),
                ('marker-range', marker_entry + 8, struct.pack('<I', header[5] + 1), 'E_ABI_MANIFEST'),
                ('marker-name', marker_entry, struct.pack('<I', header[13]), 'E_ABI_MANIFEST'),
                ('header-symbols', 12 * 4, struct.pack('<I', header[12] + 1), 'E_ABI_MANIFEST'),
                ('header-relocations', 14 * 4, struct.pack('<I', header[14] + 1), 'E_ABI_MANIFEST'),
                ('header-string-range', 13 * 4, struct.pack('<I', header[13] - 1), 'E_ABI_MANIFEST'),
            )
            for name, position, replacement, code in mutations:
                changed = bytearray(original)
                changed[position:position + len(replacement)] = replacement
                damaged = self.work / f'{name}-o{level}.kro'
                damaged.write_bytes(changed)
                self.reject_objects(damaged, consumer, level, code)
            if level == 2:
                output = self.work / 'existing-kept-program'
                retained = self.work / 'Program.kro'
                previous_output = b'previous executable must survive ABI rejection'
                previous_object = b'previous retained KRO must survive ABI rejection'
                output.write_bytes(previous_output)
                retained.write_bytes(previous_object)
                damaged = self.work / f'magic-o{level}.kro'
                result = self.invoke(consumer.with_suffix('.krt'), damaged, f'-O{level}',
                                     '--keep-temp', '-o', output)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn('E_ABI_MANIFEST', result.stderr)
                self.assertEqual(output.read_bytes(), previous_output)
                self.assertEqual(retained.read_bytes(), previous_object)
            payload = original[offset:offset + marker_symbol[2]]
            _, _, nominal, contract = struct.unpack_from('<4I', payload, 16)
            first_record_length = 16 + nominal + contract
            for name, records, count in (
                    ('duplicate-record', payload[16:] + payload[16:16 + first_record_length], record_count + 1),
                    ('missing-record', payload[16 + first_record_length:], record_count - 1)):
                replaced = struct.pack('<4I', 0x32494241, 2, count, 16 + len(records)) + records
                damaged = self.work / f'{name}-o{level}.kro'
                damaged.write_bytes(self.replace_manifest_payload(library, replaced))
                self.reject_objects(damaged, consumer, level, 'E_ABI_MANIFEST')

    def test_nested_named_reference_classes_and_interfaces_use_exact_abi3(self):
        reference = 'public class Reference{public int32 amount;public extern Reference(int32 value);}'
        cases = (
            (reference + 'public struct Value{public Reference item;public int32 tag;}',
             'item', 'new Reference(19)', 'value.item.amount+=7;', 'result.item.amount==26', ''),
            ('public interface Contract{int32 Read();}public struct Value{public Contract item;public int32 tag;}',
             'item', 'Create()', 'if(value.item.Read()!=19){throw 1;}', 'result.item.Read()==19',
             'private class Concrete:Contract{public int32 Read(){return 19;}}Contract Create(){return new Concrete();}'),
            (reference + 'public struct Inner{public Reference item;}public struct Value{public Inner nested;public int32 tag;}',
             'nested.item', 'new Reference(19)', 'value.nested.item.amount+=7;', 'result.nested.item.amount==26', ''),
            (reference + 'public struct Box<T>{public T item;}public struct Value{public Box<Reference> nested;public int32 tag;}',
             'nested.item', 'new Reference(19)', 'value.nested.item.amount+=7;', 'result.nested.item.amount==26', ''),
        )
        for index, (declarations, field, create, action, check, extra) in enumerate(cases):
            provider = declarations.replace('public extern Reference(int32 value);',
                                            'public Reference(int32 value){amount=value;}') + extra
            provider += 'Value Echo(Value value){' + action + 'value.tag=41;return value;}'
            consumer = declarations + ('extern Contract Create();' if extra else '')
            consumer += 'extern Value Echo(Value value);int32 main(){Value value=default(Value);value.tag=31;value.' + field + '=' + create + ';'
            consumer += 'Value result=Echo(value);bool correct=' + check + '&&result.tag==41&&value.tag==31;delete value.' + field + ';return correct?0:1;}'
            for level in range(4):
                with self.subTest(source=index, optimization=level):
                    library = self.object(f'named-nested-{index}-provider', provider, level)
                    client = self.object(f'named-nested-{index}-consumer', consumer, level)
                    names = {name for name, _ in read_object(library)[1]}
                    self.assertIn('_KRT_ABI3_MANIFEST', names)
                    self.assertTrue(any(name.startswith('_KRT3$Echo$') for name in names))
                    self.execute_objects(library, client, level)

    def test_scalar_libraries_keep_the_existing_krt1_symbol_contract(self):
        for level in range(4):
            library = self.object('scalar-library', 'int32 Add(int32 first,int32 second){return first+second;}', level)
            consumer = self.object('scalar-consumer', 'extern int32 Add(int32 first,int32 second);int32 main(){return Add(17,25)==42?0:1;}', level)
            exported = [name for name, symbol in read_object(library)[1] if symbol[3] != 0 and symbol[4] == 1]
            self.assertEqual(exported, ['_KRT1$Add$i32;i32;$i32'])
            self.execute_objects(library, consumer, level)

    def test_nullable_nested_fields_and_callback_parameters_refs_and_results(self):
        shared = '''namespace Data;public struct Packet{int64 marker;
int32*? first;int32*?* nested;int32*? slots[2];
fn(int32*?,ref int32*?)->int32*? callback;}'''
        library_source = '''using Data;
Packet Echo(Packet value){value.marker+=10;value.slots[1]=null;return value;}
int32*? Through(Packet value,fn(int32*?,ref int32*?)->int32*? callback,
int32*? first,ref int32*? second){return callback(first,ref second);}'''
        consumer_source = '''using Data;
extern Packet Echo(Packet value);
extern int32*? Through(Packet value,fn(int32*?,ref int32*?)->int32*? callback,
int32*? first,ref int32*? second);
int32*? Select(int32*? first,ref int32*? second){int32*? previous=second;second=first;return previous;}
int32 main(){unsafe(using krt.mem;){int32 number=42;int32*? second=(int32*?)&number;
Packet value=default(Packet);value.marker=7;value.first=null;value.nested=&second;
value.slots[0]=null;value.slots[1]=(int32*?)&number;value.callback=&Select;
Packet copied=Echo(value);
if(copied.marker!=17||value.marker!=7||value.slots[1]!=(int32*?)&number||copied.slots[1]!=null){return 1;}
int32*? result=Through(copied,&Select,null,ref second);
if(result!=(int32*?)&number||second!=null){return 2;}
second=(int32*?)&number;result=copied.callback(null,ref second);
if(result!=(int32*?)&number||second!=null||*copied.nested!=null){return 3;}return 0;}}'''
        for level in range(4):
            library = self.object('nullable-library', library_source, level, shared)
            consumer = self.object('nullable-consumer', consumer_source, level, shared)
            self.execute_objects(library, consumer, level)

    def test_nullable_layout_contract_distinguishes_each_pointer_and_callback_layer(self):
        high_nullable = 'int32*?' + '*' * 33
        high_nonnullable = 'int32' + '*' * 34
        shapes = (
            ('outer-null', 'int32*? pointer;', 'int32* pointer;'),
            ('pointee-null', 'int32*?* pointer;', 'int32** pointer;'),
            ('array-element-null', 'int32*?[] pointers;', 'int32*[]? pointers;'),
            ('callback-parameter-null', 'fn(int32*?)->int32 callback;', 'fn(int32*)->int32 callback;'),
            ('callback-ref-null', 'fn(ref int32*?)->int32 callback;', 'fn(ref int32*)->int32 callback;'),
            ('callback-result-null', 'fn()->int32*? callback;', 'fn()->int32* callback;'),
            ('high-pointee-null', high_nullable + ' pointer;', high_nonnullable + ' pointer;'),
        )
        for name, library_fields, consumer_fields in shapes:
            library_source = 'namespace Data;public struct Value{' + library_fields + '}Value Echo(Value value){return value;}'
            consumer_source = 'namespace Data{public struct Value{' + consumer_fields + '}extern Value Echo(Value value);}'
            consumer_source += 'int32 main(){Data.Value value=default(Data.Value);Data.Value copied=Data.Echo(value);return 0;}'
            for level in range(4):
                library = self.object(name + '-library', library_source, level)
                consumer = self.object(name + '-consumer', consumer_source, level)
                self.reject_objects(library, consumer, level, 'E_ABI_LAYOUT')

    def test_nullable_symbol_identity_preserves_all_64_bits_and_legacy_pointer_names(self):
        high_nullable = 'int32*?' + '*' * 33
        high_nonnullable = 'int32' + '*' * 34
        source = '''int32 Read(int32* value){unsafe(using krt.mem;){return *value;}}
int32 ReadNested(int32** value){unsafe(using krt.mem;){return **value;}}
int32 Marker<T>(){return 7;}
int32 Answer(){return Marker<''' + high_nullable + '>()+Marker<' + high_nonnullable + '>()+Marker<' + high_nullable + '>();}'
        consumer_source = 'extern int32 Answer();int32 main(){return Answer()==21?0:1;}'
        for level in range(4):
            library = self.object('nullable-symbol-library', source, level)
            symbols = read_object(library)[1]
            names = {name for name, _ in symbols}
            self.assertIn('_KRT1$Read$Pi32;$i32', names)
            self.assertIn('_KRT1$ReadNested$PPi32;$i32', names)
            markers = [name.split('$local', 1)[0] for name, _ in symbols if name.startswith('_KRT1$Marker$')]
            self.assertEqual(len(markers), 2, markers)
            self.assertEqual(len(set(markers)), 2, markers)
            self.assertTrue(any('M0000000100000000:' in name for name in markers), markers)
            self.assertTrue(any('M' not in name.split('G<', 1)[1] for name in markers), markers)
            consumer = self.object('nullable-symbol-consumer', consumer_source, level)
            self.execute_objects(library, consumer, level)

    def test_binary_generic_templates_have_an_explicit_diagnostic(self):
        path = self.work / 'generic-external.krt'
        path.write_text('extern T Echo<T>(T value);int32 main(){return Echo<int32>(17);}')
        for level in range(4):
            for previous in (None, b'previous object\x00must survive'):
                with self.subTest(optimization=level, previous_artifact=previous is not None):
                    output = self.work / f'generic-external-o{level}-{previous is not None}.kro'
                    if previous is not None:
                        output.write_bytes(previous)
                    result = self.invoke(path, f'-O{level}', '-c', '-o', output)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn('E_LINKAGE', result.stderr)
                    self.assertNotIn('E_PARSE', result.stderr)
                    if previous is None:
                        self.assertFalse(output.exists())
                    else:
                        self.assertEqual(output.read_bytes(), previous)
