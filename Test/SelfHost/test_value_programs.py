"""Practical fixed buffers, packet snapshots, and deep reference conversions."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


class ValueProgramTests(NativeCompilerFixture):
    def execute_program(self, source):
        self.execute(source, levels=(0, 2))
        path = self.work / 'value-program.krt'
        path.write_text(source)
        output = self.work / 'value-program.vm'
        self.command(path, 'target', 'vm', '-O2', '-o', output)
        result = subprocess.run([str(COMPILER), 'run-vm', str(output)], cwd=self.work,
                                env=self.env, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_buffers_keep_generic_size_length_and_independent_copies(self):
        self.execute_program('''
struct Buffers{byte two[2];byte three[3];}
int32 Bytes<T>(T value){return sizeof(T);}
int32 Count<T>(T value){return (int32)value.Length;}
T Copy<T>(T value){return value;}
int32 main(){Buffers data=default(Buffers);data.two[0]=17;data.two[1]=19;data.three[0]=23;data.three[2]=25;
if(Bytes(data.two)!=2||Bytes(data.three)!=3||Count(data.two)!=2||Count(data.three)!=3){return 1;}
var short_snapshot=Copy(data.two);var long_snapshot=Copy(data.three);
data.two[0]=99;data.three[2]=101;
if(short_snapshot.Length!=2||long_snapshot.Length!=3||short_snapshot[0]!=17||short_snapshot[1]!=19){return 2;}
if(long_snapshot[0]!=23||long_snapshot[2]!=25){return 3;}
return 0;}
''')

    def test_nested_packet_snapshot_survives_later_ref_changes(self):
        self.execute_program('''
struct Header{uint16 tag;byte bytes[4];}
struct Packet{Header segments[2];int32 sequence;}
T Copy<T>(T value){return value;}
void Mutate(ref Packet packet){packet.segments[0].tag=99;packet.segments[1].bytes[3]=101;packet.sequence=103;}
int32 main(){Packet original=default(Packet);original.segments[0].tag=17;original.segments[1].bytes[3]=25;original.sequence=29;
Packet snapshot=Copy(original);Mutate(ref original);
if(snapshot.segments[0].tag!=17||snapshot.segments[1].bytes[3]!=25||snapshot.sequence!=29){return 1;}
if(original.segments[0].tag!=99||original.segments[1].bytes[3]!=101||original.sequence!=103){return 2;}
return 0;}
''')

    def test_fixed_arguments_capture_snapshots_in_source_evaluation_order(self):
        self.execute_program('''
struct Packet{byte bytes[4];}
int32 Later(ref Packet packet){packet.bytes[0]=99;return 3;}
int32 Consume<T>(T bytes,int32 marker){return bytes[0]*100+marker;}
int32 main(){Packet packet=default(Packet);packet.bytes[0]=17;
if(Consume(packet.bytes,Later(ref packet))!=1703||packet.bytes[0]!=99){return 1;}
packet.bytes[0]=17;if(Consume(marker:Later(ref packet),bytes:packet.bytes)!=9903){return 2;}
packet.bytes[0]=17;if(Consume(bytes:packet.bytes,marker:Later(ref packet))!=1703){return 3;}
return 0;}
''')

    @staticmethod
    def classes(depth):
        return 'class B0{}\n' + ''.join(f'class B{i}:B{i-1}{{}}\n' for i in range(1, depth + 1))

    def test_deep_conversions_preserve_nearest_overload_independently_of_order(self):
        self.execute_program(self.classes(41) + '''
int32 Take(B0 value){return value==null?1:0;}
int32 main(){B41 item=new B41();int32 result=Take(item);delete item;return result;}
''')
        overloads = ['int32 Pick(B0 value){return 1;}\n', 'int32 Pick(B1 value){return 0;}\n']
        for reversed_order in (False, True):
            with self.subTest(reversed_order=reversed_order):
                source = self.classes(42) + ''.join(reversed(overloads) if reversed_order else overloads)
                source += 'int32 main(){B42 item=new B42();int32 result=Pick(item);delete item;return result;}\n'
                self.execute_program(source)

    def test_fixed_shape_mismatches_reject_and_preserve_previous_artifact(self):
        cases = (
            ('E_UNDEFINED_METHOD', '''
struct Buffers{byte two[2];byte three[3];}
T Choose<T>(T first,T second){return first;}
int32 main(){Buffers data=default(Buffers);var invalid=Choose(data.two,data.three);return 0;}
'''),
            ('E_UNDEFINED_METHOD', '''
struct Packet{byte bytes[4];}
T Choose<T>(T first,T second){return first;}
int32 main(){Packet packet=default(Packet);byte[] dynamic=new byte[4];var invalid=Choose(packet.bytes,dynamic);delete dynamic;return 0;}
'''),
            ('E_LOWER', '''
struct Packet{byte bytes[4];}
byte[] Leak(){Packet local=default(Packet);return local.bytes;}
int32 main(){var invalid=Leak();return 0;}
'''))
        for index, (code, source) in enumerate(cases):
            for target in ('native', 'vm'):
                with self.subTest(index=index, target=target):
                    path = self.work / f'bad-{index}-{target}.krt'
                    path.write_text(source)
                    output = path.with_suffix('.previous')
                    previous = b'previous artifact\x00must survive'
                    output.write_bytes(previous)
                    argv = [str(COMPILER), '--linker', str(LINKER), str(path), '-O2', '-o', str(output)]
                    if target == 'vm':
                        argv += ['target', 'vm']
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn(code, result.stderr)
                    self.assertNotIn('E_PARSE', result.stderr)
                    self.assertRegex(result.stderr, r':\d+:\d+: ' + code + ':')
                    self.assertEqual(output.read_bytes(), previous)
