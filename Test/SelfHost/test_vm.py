"""Real EBC artifacts, seed interchange and execution by the native interpreter."""
import os
from pathlib import Path
import math
import random
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()
MAGIC = 0x45534243


@unittest.skipUnless(COMPILER.is_file(), 'build the native self-hosted compiler first')
class NativeVmTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='kairote native vm ')
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        self.environment = dict(os.environ, PATH='', KAIROTE_ROOT=str(ROOT))

    def command(self, *arguments, expected=0):
        result = subprocess.run([str(COMPILER), *map(str, arguments)], cwd=self.work,
                                env=self.environment, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def compile(self, source, name='program', level=2):
        path = self.work / (name + '.krt')
        path.write_text(source)
        output = self.work / name
        # A VM build must not need a linker, Python, a C compiler or PATH lookup.
        self.command(path, 'target', 'vm', '-O' + str(level), '--linker', self.work / 'absent-linker', '-o', output)
        self.assertGreater(output.stat().st_size, 10)
        self.assertEqual(output.read_bytes(), Path(str(output) + '.ebc').read_bytes())
        self.assertFalse(output.stat().st_mode & 0o111)
        self.assertEqual(struct.unpack_from('<I', output.read_bytes())[0], MAGIC)
        return output

    def test_seed_v1_constant_artifact_and_interchange(self):
        output = self.compile('int32 main(){return 17;}')
        expected = (struct.pack('<IHI', MAGIC, 1, 6) + bytes([25, 10, 0, 0, 24, 27])
                    + bytes(24) + struct.pack('<IId', 1, 2, 17.0))
        self.assertEqual(output.read_bytes(), expected)
        self.command('run-vm', output, expected=17)
        seed = Path(os.environ.get('KRTC', ROOT / 'build/Re.KrtC/KrtC')).resolve()
        if seed.is_file():
            source = self.work / 'program.krt'
            artifact = self.work / 'seed'
            result = subprocess.run([str(seed), str(source), 'target', 'vm', 'output', str(artifact)],
                                    cwd=self.work, capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.command('run-vm', self.work / 'seed.ebc', expected=17)

    def test_v1_scalar_chunk_operations_and_locals(self):
        # STK_ADJ; CONST0; SET_LOCAL0; POP; GET_LOCAL0; CONST1; ADD; RETURN; HALT.
        code = bytes([25, 10, 0, 0, 6, 0, 4, 5, 0, 0, 1, 13, 24, 27])
        data = (struct.pack('<IHI', MAGIC, 1, len(code)) + code + bytes(4 * len(code))
                + struct.pack('<IIdId', 2, 2, 7.0, 2, 12.0))
        path = self.work / 'legacy.ebc'; path.write_bytes(data)
        self.command('run-vm', path, expected=19)

    def test_v1_number_printing_rounding_and_ieee_extremes(self):
        rng = random.Random(71)
        values = [0.0, -0.0, 1.234565, 0.0009765625, 0.001953125, 1000005.0, 1000015.0,
                  float('inf'), -float('inf'), float('nan'), 5e-324, 1.7976931348623157e308]
        values += [struct.unpack('<d', struct.pack('<Q', rng.getrandbits(64)))[0] for _ in range(150)]
        code = b''.join(bytes([0, index, 19]) for index in range(len(values))) + bytes([1, 24, 27])
        data = (struct.pack('<IHI', MAGIC, 1, len(code)) + code + bytes(4 * len(code))
                + struct.pack('<I', len(values)) + b''.join(struct.pack('<Id', 2, value) for value in values))
        path = self.work / 'numbers.ebc'; path.write_bytes(data)
        expected = []
        for value in values:
            text = format(value, '.6g')
            if math.isnan(value) and struct.unpack('<Q', struct.pack('<d', value))[0] >> 63:
                text = '-nan'
            expected.append(text)
        self.assertEqual(self.command('run-vm', path).stdout.decode().splitlines(), expected)

    def test_v2_integer_loops_recursion_and_calls(self):
        output = self.compile('''
            int32 square(int32 n){return n*n;}
            int32 factorial(int32 n){if(n<2){return 1;} return n*factorial(n-1);}
            int32 main(){int32 n=0; int32 i=0;
                while(i<10){n=n+square(i); i=i+1;}
                return n-285+factorial(4);
            }
        ''')
        self.assertEqual(struct.unpack_from('<H', output.read_bytes(), 4)[0], 2)
        self.command('run-vm', output, expected=24)

    def test_exact_integer_widths_unsigned_and_memory(self):
        output = self.compile('''
            void bump(ref uint32 n){n=n+1;}
            int32 main(){uint64 high=(uint64)-1;
                if(high/2!=9223372036854775807 || high%2!=1 || (high>>1)!=9223372036854775807){return 1;}
                uint32[] values=new uint32[3]; values[2]=4000000000; bump(ref values[2]);
                if(values[2]!=4000000001 || values.Length!=3){return 2;}
                delete values;
                unsafe(using krt.mem;){int16* small=stackalloc int16[2]; small[1]=-30000;
                    if(small[1]!=-30000){return 3;}}
                return 0;
            }
        ''', level=0)
        self.command('run-vm', output)

    def test_strings_syscalls_and_global_initialization(self):
        output = self.compile('''
            static int32 number=7;
            int32 main(){string greeting="vm"+" hello\\n";
                if(greeting!="vm hello\\n" || greeting.Length!=9){return 2;}
                syscall(1,1,(int64)greeting,greeting.Length,0,0,0);
                number=number+8; return number;
            }
        ''')
        result = self.command('run-vm', output, expected=15)
        self.assertEqual(result.stdout, b'vm hello\n')

    def test_indirect_calls_and_stack_arguments(self):
        output = self.compile('''
            int64 sum(int64 a,int64 b,int64 c,int64 d,int64 e,int64 f,int64 g,int64 h){return a+b+c+d+e+f+g+h;}
            int32 main(){fn(int64,int64,int64,int64,int64,int64,int64,int64)->int64 call=&sum;
                return (int32)(7+call(1,2,3,4,5,6,7,8));}
        ''')
        self.command('run-vm', output, expected=43)

    def test_mmap_and_munmap_syscalls(self):
        output = self.compile('''
            int32 main(){int64 address=syscall(9,0,4096,3,34,-1,0);
                if(address<0){return 1;}
                unsafe(using krt.mem;){int64* values=(int64*)address; values[2]=17;
                    int32 result=(int32)values[2]; syscall(11,address,4096,0,0,0,0); return result;}
            }
        ''')
        self.command('run-vm', output, expected=17)

    def test_ieee_floats_and_exceptions_across_calls(self):
        output = self.compile('''
            float64 square(float64 n){return n*n;}
            void fail(){throw 17;}
            int32 main(){float32 n=1.25; n++;
                if(square((float64)n)!=5.0625){return 3;}
                float64 nan=0.0/0.0; if(nan==nan || !nan){return 4;}
                int32 value=0; try{fail();} catch(int32 n){value=n;} finally{value=value+1;}
                return value;
            }
        ''')
        self.command('run-vm', output, expected=18)

    def test_boxed_wide_integers_packed_arrays_and_references(self):
        output = self.compile('''
            void bump(ref uint128 n){n=n+1;}
            int32 main(){uint128 n=340282366920938463463374607431768211455;
                if(n/3!=113427455640312821154458202477256070485 || n%3!=0){return 1;}
                uint128[] values=new uint128[2]; values[1]=n; bump(ref values[1]);
                if(values[1]!=0){return 2;}
                int128 signed_value=-170141183460469231731687303715884105728;
                if(signed_value>>127!=-1){return 3;}
                float64 precise=(float64)(uint128)18446744073709551616;
                if((uint128)precise!=18446744073709551616){return 4;}
                delete values; return 0;
            }
        ''')
        self.command('run-vm', output)

    def test_malformed_bytecode_and_memory_addresses_report_errors(self):
        output = self.compile('int32 f(int32 n){return n+1;} int32 main(){return f(16);}', level=0)
        valid = output.read_bytes()
        count, functions = struct.unpack_from('<II', valid, 8)
        instructions = 32 + functions * 12
        corruptions = [valid[:5], valid[:-1], valid + b'x']
        unknown_op = bytearray(valid); struct.pack_into('<I', unknown_op, instructions, 255); corruptions.append(unknown_op)
        branch = bytearray(valid); struct.pack_into('<I', branch, instructions, 17); struct.pack_into('<q', branch, instructions + 8, count + 1); corruptions.append(branch)
        for index, data in enumerate(corruptions):
            path = self.work / f'corrupt{index}.ebc'; path.write_bytes(data)
            result = self.command('run-vm', path, expected=1)
            self.assertIn(b'E_VM', result.stderr)
        invalid = self.compile('int32 main(){unsafe(using krt.mem;){return (int32)*(int64*)1;}}', name='invalid')
        self.assertIn(b'E_VM', self.command('run-vm', invalid, expected=1).stderr)

    def test_v2_decoder_allocates_large_instruction_and_function_tables(self):
        # Exceed the previous fixed compiler buffers without requiring a large
        # source fixture or mirroring the serializer implementation.
        count = 300000
        data = (struct.pack('<IHHIIiIiI', MAGIC, 2, 0, count, 1, 0, 0, -1, 0)
                + struct.pack('<iii', 0, 0, 0)
                + struct.pack('<IIqq', 0, 0, 0, 0) * (count - 2)
                + struct.pack('<IIqq', 1, 0, 17, 0) + struct.pack('<IIqq', 20, 0, 0, 1))
        path = self.work / 'large.ebc'; path.write_bytes(data)
        self.command('run-vm', path, expected=17)
        functions = 2000
        metadata = b''.join(struct.pack('<iii', index * 2, 0, 0) for index in range(functions))
        pair = struct.pack('<IIqq', 1, 0, 17, 0) + struct.pack('<IIqq', 20, 0, 0, 1)
        data = (struct.pack('<IHHIIiIiI', MAGIC, 2, 0, functions * 2, functions, functions - 1, 0, -1, 0)
                + metadata + pair * functions)
        path.write_bytes(data)
        self.command('run-vm', path, expected=17)

    def test_project_sidecar_cache_restore_and_clean(self):
        (self.work / 'src').mkdir()
        (self.work / 'src/main.krt').write_text('int32 main(){return 17;}')
        project = self.work / 'project.krt'
        project.write_text('function Configure(Project p){p.Name("VmProject");p.Sources("src/*.krt");p.Target("vm");p.Output("program");}')
        self.command('build', project, '--linker', self.work / 'absent-linker')
        output = self.work / 'bin/linux/program'; sidecar = Path(str(output) + '.ebc')
        self.assertEqual(output.read_bytes(), sidecar.read_bytes())
        sidecar.unlink()
        self.assertIn(b'up-to-date', self.command('build', project, '--linker', self.work / 'absent-linker').stdout)
        self.command('run-vm', sidecar, expected=17)
        self.command('clean', project)
        self.assertFalse(output.exists()); self.assertFalse(sidecar.exists())


if __name__ == '__main__':
    unittest.main()
