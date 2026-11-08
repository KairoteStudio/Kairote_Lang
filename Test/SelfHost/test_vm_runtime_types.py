"""Execute EBC6 records, retain EBC5 wide ABI and reject malformed metadata."""
import struct
import unittest

from Test.SelfHost import test_vm


PROGRAM = '''
public interface Reader{int32 Read();}
public class Base:Reader{public int32 value;public Base(int32 n){value=n;}public virtual int32 Read(){return value;}}
public class Derived:Base{public Derived(int32 n):base(n){}public override int32 Read(){return value+7;}}
struct Row:Reader{int32 value;public int32 Read(){return value;}}
int32 main(){
    object number=(int14)-3077;if((int14)number!=-3077||(number is int16)){return 1;}delete number;
    Row row=default(Row);row.value=41;Reader first=row;
    if(first.Read()!=41){return 2;}if(first is Row copy){if(copy.value!=41){return 2;}}else{return 2;}delete first;
    Base parent=new Derived(19);object erased=parent;
    if(erased is Reader readable){if(readable.Read()!=26){return 3;}}else{return 3;}
    if(!((Base)erased is Derived)){return 3;}
    delete erased;return 0;
}
'''


@unittest.skipUnless(test_vm.COMPILER.is_file(), 'build the native self-hosted compiler first')
class RuntimeVmTests(unittest.TestCase):
    setUp = test_vm.NativeVmTests.setUp
    compile = test_vm.NativeVmTests.compile
    command = test_vm.NativeVmTests.command
    def test_runtime_types_dispatch_and_unboxing_at_all_optimization_levels(self):
        for level in range(4):
            with self.subTest(optimization=level):
                output = self.compile(PROGRAM, name=f'runtime-o{level}', level=level)
                self.assertEqual(struct.unpack_from('<H', output.read_bytes(), 4)[0], 6)
                self.command('run-vm', output)

    def test_corrupted_descriptors_relocations_and_instructions_are_rejected(self):
        output = self.compile(PROGRAM, name='runtime-corruption', level=0)
        valid = output.read_bytes()
        version, = struct.unpack_from('<H', valid, 4)
        self.assertEqual(version, 6)
        count, functions = struct.unpack_from('<II', valid, 8)
        data_length, = struct.unpack_from('<I', valid, 28)
        instructions = 32 + functions * 28
        footer = instructions + count * 24 + data_length
        magic, length, types, relocations = struct.unpack_from('<4I', valid, footer)
        self.assertEqual(magic, 0x3554524B)
        self.assertGreater(types, 2)
        self.assertGreater(relocations, 2)
        offsets_start = footer + 16
        relocation_start = offsets_start + types * 4
        blob = relocation_start + relocations * 12
        offsets = struct.unpack_from('<' + 'I' * types, valid, offsets_start)
        records = [struct.unpack_from('<iii', valid, relocation_start + i * 12)
                   for i in range(relocations)]
        descriptor = offsets[0]
        key_relocation = next(i for i, (offset, kind, _) in enumerate(records)
                              if offset == descriptor + 16 and kind == 0)
        base_relocation = next(i for i, (offset, kind, _) in enumerate(records)
                               if kind == 1 and offset in [value + 48 for value in offsets])
        method_relocation = next(i for i, (_, kind, _) in enumerate(records) if kind == 2)
        runtime_instruction = next(i for i in range(count)
                                   if struct.unpack_from('<I', valid, instructions + i * 24)[0] == 74)
        corruptions = {'truncated-footer': valid[:footer + 7], 'truncated-blob': valid[:-1],
                       'trailing-bytes': valid + b'x'}

        def change(name, position, fmt, value):
            data = bytearray(valid)
            struct.pack_into(fmt, data, position, value)
            corruptions[name] = data

        change('footer-magic', footer, '<I', 0)
        change('oversized-blob', footer + 4, '<I', 16777217)
        change('oversized-type-count', footer + 8, '<I', 65537)
        change('oversized-relocation-count', footer + 12, '<I', length // 8 + 1)
        change('negative-descriptor-offset', offsets_start, '<i', -1)
        change('unaligned-descriptor', offsets_start, '<I', 1)
        change('out-of-range-descriptor', offsets_start, '<I', length)
        change('overlapping-descriptors', offsets_start + 4, '<I', offsets[0])
        change('unaligned-relocation', relocation_start, '<i', 1)
        change('outside-relocation', relocation_start, '<i', length)
        change('unknown-relocation-kind', relocation_start + 4, '<i', 3)
        change('negative-relocation-target', relocation_start + 8, '<i', -1)
        data = bytearray(valid)
        data[relocation_start + 12:relocation_start + 24] = data[relocation_start:relocation_start + 12]
        corruptions['duplicate-relocation-site'] = data
        change('key-pointing-into-descriptor', relocation_start + key_relocation * 12 + 8, '<i', descriptor)
        change('unrelocated-pointer-value', blob + descriptor + 16, '<Q', 1)
        change('incorrect-key-hash', blob + descriptor, '<Q', 0)
        change('empty-type-key', blob + descriptor + 8, '<Q', 0)
        change('oversized-type-key', blob + descriptor + 8, '<Q', length + 1)
        change('unknown-type-kind', blob + descriptor + 24, '<Q', 10)
        change('zero-alignment', blob + descriptor + 40, '<Q', 0)
        change('non-power-of-two-alignment', blob + descriptor + 40, '<Q', 3)
        change('unknown-descriptor-flags', blob + descriptor + 88, '<Q', 4)
        data = bytearray(valid)
        struct.pack_into('<Q', data, blob + descriptor + 88, 1)
        struct.pack_into('<Q', data, blob + descriptor + 32, 16)
        corruptions['reference-wrapper-with-nonpointer-storage'] = data
        change('oversized-interface-count', blob + descriptor + 56, '<Q', types + 1)
        change('oversized-method-count', blob + descriptor + 72, '<Q', length + 1)
        change('function-out-of-range', relocation_start + method_relocation * 12 + 8, '<i', functions)
        change('external-function', relocation_start + method_relocation * 12 + 8, '<i', -1)
        base_site, _, _ = records[base_relocation]
        source_class = offsets.index(base_site - 48)
        change('cyclic-base', relocation_start + base_relocation * 12 + 8, '<i', source_class)
        method_site, _, _ = records[method_relocation]
        change('wrong-receiver-adjustment', blob + method_site + 8, '<Q', 99)
        change('descriptor-op-type-index', instructions + runtime_instruction * 24 + 8, '<q', types)
        change('descriptor-op-extra', instructions + runtime_instruction * 24 + 16, '<q', 1)
        change('old-version-with-runtime-records', 4, '<H', 4)
        for name, data in corruptions.items():
            with self.subTest(corruption=name):
                path = self.work / (name + '.ebc')
                path.write_bytes(data)
                first = self.command('run-vm', path, expected=1)
                second = self.command('run-vm', path, expected=1)
                self.assertEqual(first.stdout, b'')
                self.assertEqual(first.stderr, second.stderr)
                self.assertIn(b'E_VM', first.stderr)

    def test_runtime_descriptor_memory_is_immutable_during_execution(self):
        output = self.compile(PROGRAM, name='immutable-runtime', level=0)
        data = bytearray(output.read_bytes())
        count, functions, entry = struct.unpack_from('<IIi', data, 8)
        starts = [struct.unpack_from('<i', data, 32 + index * 28)[0] for index in range(functions)]
        first = starts[entry]
        last = starts[entry + 1] if entry + 1 < functions else count
        self.assertGreaterEqual(last - first, 5)
        instructions = 32 + functions * 28
        for index, (opcode, argument, extra) in enumerate(((74, 0, 0), (1, 0, 0), (23, 0, 8),
                                                         (1, 0, 0), (20, 0, 1))):
            struct.pack_into('<IIqq', data, instructions + (first + index) * 24,
                             opcode, 0, argument, extra)
        path = self.work / 'descriptor-write.ebc'
        path.write_bytes(data)
        result = self.command('run-vm', path, expected=1)
        self.assertIn(b'E_VM', result.stderr)

    def test_old_ebc5_pointer_wide_locals_keep_immutable_value_aliases(self):
        # This is old-format bytecode, with pointer-valued wide local slots.
        # Saving the first loop result must survive later executions of the
        # same producing opcode. EBC6 uses inline language storage instead.
        operations = []
        def emit(op, argument=0, extra=0):
            operations.append((op, argument, extra)); return len(operations) - 1
        emit(66, 1, 0); emit(3, 0); emit(1, 0); emit(3, 2)
        loop = len(operations)
        emit(2, 0); emit(66, 1); emit(61, 43); emit(3, 0)
        emit(2, 2); emit(1, 0); emit(16, 61)
        choose = emit(18)
        save = len(operations); emit(2, 0); emit(3, 1)
        next_iteration = len(operations)
        operations[choose] = (18, save, next_iteration)
        emit(2, 2); emit(1, 1); emit(4); emit(3, 2)
        emit(2, 2); emit(1, 3); emit(16, 60)
        repeat = emit(18)
        finished = len(operations); operations[repeat] = (18, loop, finished)
        emit(2, 1); emit(66, 2); emit(62, 61)
        matched = emit(18)
        good = len(operations); emit(1, 0); emit(20, 0, 1)
        bad = len(operations); emit(1, 1); emit(20, 0, 1)
        operations[matched] = (18, good, bad)
        header = struct.pack('<4sHH6I', b'CBSE', 5, 0, len(operations), 1, 0, 0, 0xFFFFFFFF, 0)
        function = struct.pack('<7i', 0, 3, 0, 0, 0, 0, -1)
        instructions = b''.join(struct.pack('<IIqq', op, 0, argument, extra)
                                for op, argument, extra in operations)
        footer = struct.pack('<4I', 0x3554524B, 0, 0, 0)
        path = self.work / 'legacy-v5-wide-locals.ebc'
        path.write_bytes(header + function + instructions + footer)
        self.command('run-vm', path)

    def test_tagged_exception_descriptors_survive_every_blob_alignment(self):
        alignments = set()
        for length in range(16):
            source = '''
struct Failure{int32 code;byte data[9];}
void Raise(){Failure value=default(Failure);value.code=503;value.data[8]=37;throw value;}
int32 main(){string padding="''' + 'x' * length + '''";
    if(padding=="impossible"){return 1;}
    try{Raise();return 2;}catch(Failure value){return value.code==503&&value.data[8]==37?0:3;}}
'''
            for level in (0, 2):
                with self.subTest(literal_length=length, optimization=level):
                    output = self.compile(source, name=f'aligned-{length}-o{level}', level=level)
                    data = output.read_bytes()
                    count, functions = struct.unpack_from('<II', data, 8)
                    data_length, = struct.unpack_from('<I', data, 28)
                    footer = 32 + functions * 28 + count * 24 + data_length
                    _, _, types, relocations = struct.unpack_from('<4I', data, footer)
                    blob = footer + 16 + types * 4 + relocations * 12
                    alignments.add(blob % 16)
                    self.command('run-vm', output)
        self.assertEqual(alignments, set(range(16)))
