"""Execute real floating programs through a freshly built native compiler."""
import os
import random
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
KRTC = Path(os.environ.get('KRTC', ROOT / 'build/Re.KrtC/KrtC')).resolve()
ARKLINK = Path(os.environ.get('ARKLINK', ROOT / 'build/ArkLink/ArkLink')).resolve()

CASES = {path.stem: path.read_text() for path in sorted((ROOT / 'Test/SelfHost/fixtures/numeric').glob('*.krt'))}


def wide_differential_source():
    """Compare native two-limb arithmetic to Python's independent big integers."""
    rng = random.Random(217)
    mask = (1 << 128) - 1
    body = ['int32 main() {']
    for index in range(30):
        left, right = rng.getrandbits(128), rng.getrandbits(128) or 1
        body.append(f'uint128 a{index}={left}; uint128 b{index}={right};')
        results = [('+', (left + right) & mask), ('-', (left - right) & mask),
                   ('*', (left * right) & mask), ('/', left // right), ('%', left % right),
                   ('>>', left >> 71), ('<<', (left << 71) & mask)]
        for operator, expected in results:
            operand = '71' if operator in ('>>', '<<') else f'b{index}'
            body.append(f'if ((a{index}{operator}{operand}) != (uint128){expected}) {{return {index + 1};}}')
    for index in range(30, 50):
        left = rng.randrange(-(1 << 127), 1 << 127)
        right = rng.randrange(-(1 << 90), 1 << 90) or 1
        quotient = abs(left) // abs(right) * (1 if (left < 0) == (right < 0) else -1)
        remainder = left - quotient * right
        body.append(f'int128 a{index}=(int128){left}; int128 b{index}=(int128){right};')
        body.append(f'if (a{index}/b{index} != (int128){quotient} || '
                    f'a{index}%b{index} != (int128){remainder} || '
                    f'a{index}>>73 != (int128){left >> 73}) {{return {index + 1};}}')
    return '\n'.join(body + ['return 0; }'])


CASES['wide_differential'] = wide_differential_source()



class NativeFloatingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='krt-floating-')
        cls.work = Path(cls.directory.name)
        source = cls.work / 'compiler.krt'
        source.write_text('\n'.join(p.read_text() for p in sorted((ROOT / 'SelfHost').rglob('*.krt'))))
        cls.compiler = cls.work / 'compiler'
        result = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(cls.compiler)], cwd=cls.work, capture_output=True, text=True, timeout=120)
        if result.returncode:
            cls.directory.cleanup()
            raise AssertionError(result.stdout + result.stderr)
        cls.compiler.chmod(0o755)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def compile_native(self, name, source):
        work = self.work / name
        work.mkdir()
        (work / 'program.krt').write_text(source)
        env = os.environ.copy()
        env['PATH'] = ''
        result = subprocess.run([str(self.compiler)], cwd=work, env=env, capture_output=True, text=True, timeout=30)
        return work, result

    def test_execution(self):
        for name, source in CASES.items():
            with self.subTest(name=name):
                work, result = self.compile_native(name, source)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                executable = work / 'program'
                linked = subprocess.run([str(ARKLINK), str(work / 'stage1-probe.kro'), '--target', 'elf', '-o', str(executable)], cwd=work, capture_output=True, text=True, timeout=30)
                self.assertEqual(linked.returncode, 0, linked.stdout + linked.stderr)
                executable.chmod(0o755)
                ran = subprocess.run([str(executable)], cwd=work, capture_output=True, timeout=5)
                self.assertEqual(ran.returncode, 0, f'{name}: {ran.stdout!r} {ran.stderr!r}')

    def test_invalid_operations_rejected(self):
        sources = {
            'bitwise_float': 'int32 main() {float64 x=1.5; return (int32)(x & 1);}',
            'shift_float': 'int32 main() {float64 x=1.5; return (int32)(x << 1);}',
            'ref_float_width': 'void f(ref float32 x) {x=1;} int32 main() {float64 x=0; f(ref x); return 0;}',
            'array_float_width': 'int32 main() {float32[] x=new float64[2]; return 0;}',
            'missing_exponent': 'int32 main() {float64 x=1e+; return 0;}',
            'wide_implicit_truncation': 'int32 main() {uint64 x=18446744073709551616; return 0;}',
            'floating_index': 'int32 main(){int32[] a=new int32[2]; return a[1.0];}',
            'floating_pointer_math': 'int32 main(){unsafe(using krt.mem;){int32* p=stackalloc int32[1]; p=p+1.5;} return 0;}',
            'wide_literal_overflow': 'int32 main() {uint128 x=340282366920938463463374607431768211456; return 0;}',
        }
        for name, source in sources.items():
            with self.subTest(name=name):
                work, result = self.compile_native(name, source)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((work / 'stage1-probe.kro').exists())
