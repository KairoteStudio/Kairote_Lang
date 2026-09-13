"""Native pointer behavior and compile-time rejection tests for Re.KrtC."""
import os
import resource
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('KRTC', ROOT / 'Re.KrtC/build/KrtC')).resolve()


class Pointers(unittest.TestCase):
    def compile(self, source, directory):
        source_path = Path(directory) / 'test.krt'
        binary = Path(directory) / 'test'
        source_path.write_text(source)
        result = subprocess.run([str(COMPILER), str(source_path), 'output', str(binary)],
                                cwd=directory, capture_output=True, text=True, timeout=30)
        self.assertNotIn('AddressSanitizer', result.stderr, result.stderr + source)
        self.assertNotIn('runtime error:', result.stderr, result.stderr + source)
        return result, binary

    def run_source(self, source):
        with tempfile.TemporaryDirectory(prefix='kairote-pointers-') as directory:
            result, binary = self.compile(source, directory)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr + source)
            def no_core():
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            result = subprocess.run([str(binary)], capture_output=True, timeout=5, preexec_fn=no_core)
            self.assertEqual(result.returncode, 0, f'exit {result.returncode}\n{source}\n{result.stderr!r}')

    def run_body(self, body, declarations=''):
        self.run_source(declarations + '\nint main() { unsafe(using krt.mem;) {\n' + body + '\n} return 0; }')

    def test_requested_example(self):
        self.run_source(Path(__file__).with_name('test_unsafe_pointers.krt').read_text())

    def test_all_integer_widths_and_aliases(self):
        for bits in range(2, 129, 2):
            for unsigned in (False, True):
                kind = ('uint' if unsigned else 'int') + str(bits)
                with self.subTest(type=kind):
                    maximum = (1 << (bits if unsigned else bits-1)) - 1
                    value = maximum if unsigned else -1
                    size = next(n for n in (1, 2, 4, 8, 16) if 8*n >= bits)
                    self.run_body(f'''
                        {kind} x = 0; int2 neighbor = 1;
                        let p = &x; *p = ({kind}){value};
                        if (x != ({kind}){value} || *p != x || neighbor != 1) {{ return 1; }}
                        if (({('uint128' if unsigned else 'int128')})x != {value}) {{ return 2; }}
                        var b = stackalloc {kind}[3];
                        b[0] = 0; b[1] = ({kind}){value}; b[2] = 0;
                        if (*(b + 1) != x || b[0] != 0 || b[2] != 0) {{ return 3; }}
                        if ((byte*)(b + 1) - (byte*)b != {size}) {{ return 4; }}
                        if (&b[2] - b != 2 || b - &b[2] != (usize)-2) {{ return 5; }}
                    ''')

    def test_byte_alias_normalizes_wide_signed_words(self):
        self.run_body("""
            int66 x=0;
            let p=(byte*)&x;
            p[8]=2;
            if ((int128)x != -(int128)36893488147419103232) { return 1; }
            if (mutateWide(0) != -(int128)36893488147419103232) { return 2; }
        """, """
            int128 mutateWide(int66 x) { unsafe(using krt.mem;) {
                let p=(byte*)&x; p[8]=2; return (int128)x;
            } }
        """)

    def test_stack_pages_calls_and_multiple_allocations(self):
        self.run_body('''
            var count = 4097;
            var a = stackalloc int[count];
            var b = stackalloc byte[4097];
            a[0] = 17; a[count - 1] = 29; b[0] = 33; b[4096] = 55;
            if (touch(11) != 12) { return 1; }
            if (a[0] != 17 || a[count - 1] != 29 || b[0] != 33 || b[4096] != 55) { return 2; }
            var marker = 0;
            if ((usize)&marker - (usize)a > 1048576) { return 3; }
            if (((usize)a % 16) != 0 || ((usize)b % 16) != 0) { return 4; }
        ''', 'int touch(int n) { return n + 1; }')

    def test_loop_pointer_comparison_and_increment(self):
        self.run_body('''
            var b = stackalloc int[16]; var p = b;
            while (p < b + 16) { *p = (int)(p - b); p++; }
            for (var i = 0; i < 16; i++) { if (b[i] != i) { return 1; } }
            p--; if (*p != 15 || p - b != 15) { return 2; }
            if (4 + b != b + 4 || (b + 4)[-2] != 2) { return 3; }
        ''')

    def test_nullable_pattern_scopes_and_single_evaluation(self):
        self.run_body('''
            var x = 3; var calls = 0;
            let maybe: int*? = null;
            if (maybe is int* hit) { return 1; } else { x = 4; }
            if (findOnce(&x, &calls) is int* hit) { *hit = 8; } else { return 2; }
            if (calls != 1 || x != 8) { return 3; }
            let other: int*? = &x;
            if (other is int* hit) { if (*hit != 8) { return 4; } }
        ''', 'int*? findOnce(int* x, int* calls) { unsafe(using krt.mem;) { *calls += 1; } return x; }')

    def test_function_pointer_signature_stack_and_wide_arguments(self):
        self.run_body('''
            let f: fn(int,int,int,int,int,int,int,int) -> int = &sum;
            if (f(1,2,3,4,5,6,7,8) != 36) { return 1; }
            let w: fn(int128, int128, int128, int128) -> int128 = &wide;
            if (w(1,2,3,(int128)18446744073709551616) != (int128)18446744073709551622) { return 2; }
            if (apply(&sum) != 36) { return 3; }
        ''', '''
            int sum(int a,int b,int c,int d,int e,int f,int g,int h) { return a+b+c+d+e+f+g+h; }
            int128 wide(int128 a,int128 b,int128 c,int128 d) { return a+b+c+d; }
            int apply(fn(int,int,int,int,int,int,int,int) -> int f) {
                unsafe(using krt.mem;) { return f(1,2,3,4,5,6,7,8); }
            }
        ''')

    def test_address_of_parameters_globals_and_pointer_slots(self):
        self.run_body('''
            *(&global) = -9;
            if ((int128)global != -9) { return 1; }
            if (mutate(1,2,3,4,5,6,7) != -11) { return 2; }
            var x = 1; var y = 2; let p = &x; let pp: int** = &p;
            *pp = &y; **pp = 9;
            if (x != 1 || y != 9 || p != &y) { return 3; }
        ''', '''
            static int global = 7;
            int128 mutate(int a,int b,int c,int d,int e,int f,int g) {
                unsafe(using krt.mem;) { *(&g) = -11; return (int128)g; }
            }
        ''')

    def test_function_address_before_definition(self):
        self.run_source('''
            int main() { unsafe(using krt.mem;) { let f: fn(int) -> int = &later;
                if (f(41) != 42) { return 1; }
            } return 0; }
            int later(int x) { return x + 1; }
        ''')

    def test_parameter_pointer_updates_and_compound_index(self):
        self.run_body("""
            var b=stackalloc int[4];
            b[0]=1; b[1]=2; b[2]=3; b[3]=4;
            b[1]+=5; b[1]*=2;
            if (b[1]!=14) { return 1; }
            if (walk(b)!=3) { return 2; }
            var p=b; p+=3; p-=1;
            if (*p!=3) { return 3; }
        """, """
            int walk(int* p) { unsafe(using krt.mem;) { p++; p+=1; return *p; } }
        """)

    def test_assignment_address_evaluated_before_rhs(self):
        self.run_body("""
            var x=1; var y=2; var p=&x;
            *p=redirect(&p, &y);
            if (x!=7 || y!=2 || p!=&y) { return 1; }
            p=&x; p[0]=redirect(&p, &y);
            if (x!=7 || y!=2) { return 2; }
        """, """
            int redirect(int** slot, int* next) { unsafe(using krt.mem;) { *slot=next; } return 7; }
        """)

    def test_class_pointer_return(self):
        self.run_body("""
            var x=42;
            let maybe: int*?=Lookup.Find(&x);
            if (maybe is int* hit) { *hit=9; }
            if (x!=9) { return 1; }
        """, """
            class Lookup { public static int*? Find(int* p) { return p; } }
        """)

    def test_narrow_binding_restores_shadowed_pointer(self):
        self.run_body("""
            var x=1; var y=2; let hit=&x;
            let maybe: int*?=&y;
            if (maybe is int* hit) { *hit=8; }
            *hit=9;
            if (x!=9 || y!=8) { return 1; }
            if (shadow(hit)!=7 || x!=9) { return 2; }
        """, """
            int shadow(int* p) { unsafe(using krt.mem;) {
                var x=7; let p=&x; return *p;
            } }
        """)

    def test_rejections(self):
        cases = [
            ('int main() { var x=1; let p=&x; return 0; }', 'requires unsafe'),
            ('int main() { unsafe(using Acme.Ffi.Native;) { var x=1; let p=&x; } return 0; }', 'requires unsafe'),
            ('let p: int*? = null; let x=*p;', 'Nullable'),
            ('let p: int*? = null; p[0]=1;', 'Nullable'),
            ('let p: int*? = null; let q=p+1;', 'Nullable'),
            ('let p: int*? = null; let q: int*=p;', 'Nullable'),
            ('let p: int*? = null; let q=(int*)p;', 'nullability'),
            ('let p: int*=null;', 'nullable'),
            ('var p: int*;', 'initializer'),
            ('var x=1; let p: byte*=&x;', 'Incompatible'),
            ('var x=1; let p=&x; let q=p+p;', 'Invalid pointer'),
            ('var x=1; let p=&x; let q=4-p;', 'arithmetic'),
            ('var x=1; let p: void*=&x; let v=*p;', 'void*'),
            ('var x=1; let p: void*=&x; let q=p+1;', 'void*'),
            ('let p=(int*)null;', 'nullability'),
            ('int main() { unsafe(using krt.mem;) { var x=1; } var y=2; let p=&y; return 0; }', 'requires unsafe'),
            ('let p=&42;', 'Address operand'),
            ('let p=stackalloc void[4];', 'sized type'),
            ('let p=stackalloc int[-1];', 'nonnegative'),
            ('let p: int*?=null; if (p is byte* hit) {}', 'same non-nullable'),
            ('let p: int*?=null; if (p is int* hit) {} *hit=1;', 'Undefined identifier'),
            ('let p: int*?=null; if (p is int* hit) {} else { *hit=1; }', 'Undefined identifier'),
            ('let f: fn(int) -> int = &add;', 'Incompatible'),
            ('let f: fn(int,int) -> int = &add; var x=f(1);', 'argument count'),
        ]
        for source, diagnostic in cases:
            with self.subTest(source=source):
                if not source.startswith('int main'):
                    source = 'int add(int a,int b) { return a+b; } int main() { unsafe(using krt.mem;) {' + source + '} return 0; }'
                with tempfile.TemporaryDirectory(prefix='kairote-pointers-reject-') as directory:
                    result, binary = self.compile(source, directory)
                    self.assertNotEqual(result.returncode, 0, source)
                    self.assertIn(diagnostic, result.stdout + result.stderr)
                    self.assertFalse(binary.exists())


if __name__ == '__main__':
    unittest.main()
