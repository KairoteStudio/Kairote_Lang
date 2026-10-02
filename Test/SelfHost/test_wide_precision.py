"""Declared 66..126-bit integers normalize at arithmetic and storage boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import Bootstrap, ROOT


class WidePrecisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='krt-wide-precision-')
        cls.work = Path(cls.directory.name)
        cls.bootstrap = Bootstrap(cls.work)
        cls.compiler = (Path(os.environ['SELFHOST_COMPILER']).resolve()
                        if 'SELFHOST_COMPILER' in os.environ else cls.bootstrap.seed())
        cls.linker = ROOT / 'build/ArkLink/ArkLink'
        cls.seed = ROOT / 'build/Re.KrtC/KrtC'

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def execute(self, name, source, optimization=2, vm=False, seed=False):
        directory = self.work / name
        directory.mkdir()
        path = directory / 'program.krt'
        path.write_text(source)
        binary = directory / 'program'
        if seed:
            arguments = [str(self.seed), f'-O{optimization}', str(path), 'output', str(binary)]
        else:
            arguments = [str(self.compiler), str(path), f'-O{optimization}', '-o', str(binary),
                         '--linker', str(self.linker)]
            if vm:
                arguments += ['target', 'vm']
        result = subprocess.run(arguments, cwd=directory, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr + source)
        command = [str(self.compiler), 'run-vm', str(binary)] if vm else [str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, f'{name}: branch {result.returncode}\n' + result.stdout + result.stderr)

    @staticmethod
    def arithmetic(bits):
        signed_maximum = (1 << (bits - 1)) - 1
        signed_minimum = -(1 << (bits - 1))
        unsigned_maximum = (1 << bits) - 1
        return f'''
int32 main(){{
uint{bits} unsigned_value=(uint{bits})-1;
int{bits} signed_value=(int{bits}){signed_maximum};
if((uint128)unsigned_value!={unsigned_maximum}){{return 1;}}
if(unsigned_value+1!=0 || unsigned_value*2!={unsigned_maximum-1}){{return 2;}}
if(signed_value+1!={signed_minimum}){{return 3;}}
signed_value+=1;if((int128)signed_value!={signed_minimum} || signed_value>=0){{return 4;}}
if(-signed_value!={signed_minimum} || ~signed_value!={signed_maximum}){{return 5;}}
uint{bits} zero=0;uint{bits} one=1;
if((uint128)(~zero)!={unsigned_maximum} || (uint128)(-one)!={unsigned_maximum}){{return 6;}}
if((signed_value>>{bits-1})!=-1 || (unsigned_value>>{bits-1})!=1){{return 7;}}
if((uint{bits})((uint128)1<<{bits})!=0 || (int{bits})((uint128)1<<{bits-1})!={signed_minimum}){{return 8;}}
return 0;}}
'''

    def test_every_even_wide_precision_arithmetic_and_casts(self):
        for bits in range(66, 128, 2):
            with self.subTest(bits=bits):
                self.execute(f'arithmetic-{bits}', self.arithmetic(bits))

    def test_seed_and_native_execute_the_same_boundary_programs(self):
        if not self.seed.is_file():
            self.skipTest('build Re.KrtC for the side-by-side executable comparison')
        for bits in (66, 94, 126):
            for optimization in (0, 2, 3):
                source = self.arithmetic(bits)
                self.execute(f'seed-{bits}-O{optimization}', source, optimization, seed=True)
                self.execute(f'native-{bits}-O{optimization}', source, optimization)

    def test_calls_returns_ref_arrays_fields_and_post_prefix_updates(self):
        self.execute('storage-precision', '''
static int32 reads=0;
int32 index(){reads++;return 0;}
class Cell{public int66 value;public uint126 unsigned_value;}
int66 wrap(int66 value){return value+1;}
uint126 copy(uint128 value){return (uint126)value;}
void assign(ref int66 destination,int128 value){destination=(int66)value;}
void increase(ref uint126 value){value++;}
int32 main(){
int66 number=(int66)36893488147419103231;
int66 previous=number++;if(previous!=36893488147419103231 || number!=-36893488147419103232){return 1;}
int66 fresh=--number;if(fresh!=36893488147419103231 || number!=fresh){return 2;}
if(wrap(number)!=-36893488147419103232){return 3;}
int66[] values=[number];previous=values[index()]++;if(reads!=1 || previous!=number || values[0]>=0){return 4;}
fresh=--values[index()];if(reads!=2 || fresh!=number || values[0]!=number){return 5;}
assign(ref values[0],36893488147419103232);if(values[0]!=-36893488147419103232){return 6;}
Cell cell=new Cell();cell.value=number;previous=cell.value++;if(previous!=number || cell.value>=0){return 7;}
cell.unsigned_value=copy((uint128)-1);increase(ref cell.unsigned_value);if(cell.unsigned_value!=0){return 8;}
uint126[] unsigned_values=[copy((uint128)-1)];increase(ref unsigned_values[0]);if(unsigned_values[0]!=0){return 9;}
unsafe(using krt.mem;){int66* pointer=&number;previous=(*pointer)++;if(previous!=36893488147419103231 || number>=0){return 10;}}
delete cell;delete values;delete unsigned_values;return 0;}
''')

    def test_mixed_signed_wide_and_unsigned_narrow_promote_without_sign_loss(self):
        self.execute('mixed-domain', '''
int32 main(){int66 negative=-7;uint64 two=2;
if(negative+two!=-5 || negative*two!=-14 || negative/two!=-3 || negative%two!=-1){return 1;}
if(negative>=two || negative>=(uint64)0){return 2;}
if((negative>>(uint64)1)!=-4){return 3;}
uint66 maximum=(uint66)-1;int64 one=1;
if(maximum+one!=0 || maximum<=one){return 4;}return 0;}
''')

    def test_float_conversions_normalize_custom_wide_destinations(self):
        source = '''
int32 main(){uint66 zero=(uint66)73786976294838206464.0;
int66 minimum=(int66)36893488147419103232.0;
if(zero!=0 || minimum!=-36893488147419103232){return 1;}
if((uint66)21.75!=21 || (int66)-21.75!=-21){return 2;}
uint66 maximum=(uint66)-1;
if((float64)maximum!=73786976294838206464.0){return 3;}return 0;}
'''
        self.execute('float-boundaries', source)
        if self.seed.is_file():
            self.execute('seed-float-boundaries', source, seed=True)

    def test_typed_loads_normalize_raw_physical_storage(self):
        source = '''
int32 main(){int66[] values=new int66[1];uint66[] unsigned_values=new uint66[1];
unsafe(using krt.mem;){int128* pointer=(int128*)&values[0];*pointer=(int128)1<<66;
uint128* unsigned_pointer=(uint128*)&unsigned_values[0];*unsigned_pointer=((uint128)1<<67)|5;}
if((int128)values[0]!=0 || !(!values[0])){return 1;}
if((uint128)unsigned_values[0]!=5 || (unsigned_values[0]>>1)!=2){return 2;}
unsafe(using krt.mem;){int128* pointer=(int128*)&values[0];*pointer=(int128)1<<65;}
if((int128)values[0]!=-36893488147419103232){return 3;}
delete values;delete unsigned_values;return 0;}
'''
        self.execute('typed-load', source)
        if self.seed.is_file():
            self.execute('seed-typed-load', source, seed=True)

    def test_vm_uses_the_same_precision_normalization_operations(self):
        self.execute('vm-precision', self.arithmetic(66), vm=True)

    @staticmethod
    def integer_float_bits(number, precision):
        """Round an integer directly to IEEE bits using integer arithmetic."""
        fraction_bits, exponent_bits = (23, 8) if precision == 32 else (52, 11)
        sign = int(number < 0) << (precision - 1)
        magnitude = abs(number)
        if magnitude == 0:
            return sign
        exponent = magnitude.bit_length() - 1
        shift = exponent - fraction_bits
        if shift > 0:
            mantissa, remainder = divmod(magnitude, 1 << shift)
            halfway = 1 << (shift - 1)
            if remainder > halfway or (remainder == halfway and mantissa & 1):
                mantissa += 1
            if mantissa == 1 << (fraction_bits + 1):
                mantissa >>= 1
                exponent += 1
        else:
            mantissa = magnitude << -shift
        bias = (1 << (exponent_bits - 1)) - 1
        if exponent > bias:
            return sign | (((1 << exponent_bits) - 1) << fraction_bits)
        return sign | ((exponent + bias) << fraction_bits) | (mantissa - (1 << fraction_bits))

    def test_wide_to_float_rounds_once_at_half_ulp_boundaries(self):
        values = [0, 1, 42, -1, -42, 1 << 64, (1 << 64) + 1,
                  (1 << 64) + (1 << 11), (1 << 64) + (1 << 11) + 1,
                  (1 << 64) + (3 << 11), (1 << 117) + (1 << 64),
                  (1 << 117) + (1 << 64) + 1, (1 << 100) + (1 << 76),
                  (1 << 100) + (1 << 76) + 1, -((1 << 100) + (1 << 76) + 1),
                  (1 << 127) - 1, -(1 << 127), (1 << 128) - 1]
        functions = []
        for integer_type, suffix in [('uint128', 'u'), ('int128', 's')]:
            for precision in (32, 64):
                functions.append(f'''uint{precision} bits{precision}{suffix}({integer_type} value){{
float{precision} result=(float{precision})value;
unsafe(using krt.mem;){{return *(uint{precision}*)&result;}}}}''')
        checks = []
        branch = 0
        for number in values:
            for precision in (32, 64):
                branch += 1
                suffix = 's' if number < 0 else 'u'
                expected = self.integer_float_bits(number, precision)
                checks.append(f'if(bits{precision}{suffix}({number})!={expected}){{return {branch};}}')
        source = '\n'.join(functions) + '\nint32 main(){\n' + '\n'.join(checks) + '\nreturn 0;}'
        for optimization in (0, 2, 3):
            self.execute(f'rounding-native-O{optimization}', source, optimization)
            if self.seed.is_file():
                self.execute(f'rounding-seed-O{optimization}', source, optimization, seed=True)
        self.execute('rounding-vm', source, vm=True)


if __name__ == '__main__':
    unittest.main()
