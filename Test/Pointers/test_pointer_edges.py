"""Pointer edge cases: executable semantics, diagnostics, and isolated runtime traps."""
from pathlib import Path
import random
import itertools
import resource
import signal
import subprocess
import tempfile
import unittest

import test_pointers as support


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class PointerEdges(unittest.TestCase):
    compile = support.Pointers.compile
    run_source = support.Pointers.run_source
    run_body = support.Pointers.run_body

    def reject(self, body, declarations='', diagnostic=None):
        source = declarations + '\nint main() { unsafe(using krt.mem;) {' + body + '} return 0; }'
        with tempfile.TemporaryDirectory(prefix='kairote-pointer-invalid-') as directory:
            result, binary = self.compile(source, directory)
            self.assertGreater(result.returncode, 0, result.stdout + result.stderr + source)
            self.assertFalse(binary.exists(), source)
            if diagnostic:
                self.assertIn(diagnostic, result.stdout + result.stderr)

    def test_memory_lvalue_increment(self):
        self.run_body('''
            var x=4; let p=&x;
            let a=(*p)++; let b=++*p; let c=(*p)--; let d=--*p;
            if (a!=4 || b!=6 || c!=6 || d!=4 || x!=4) { return 1; }
            var buf=stackalloc int[3]; buf[0]=10; buf[1]=20; buf[2]=30;
            var i=0; let e=buf[i++]++; let f=++buf[i++];
            if (e!=10 || f!=21 || i!=2 || buf[0]!=11 || buf[1]!=21 || buf[2]!=30) { return 2; }
            var q=buf; let pp=&q; let before=(*pp)++; let after=++*pp;
            if (before!=buf || after!=buf+2 || q!=buf+2) { return 3; }
        ''')

    def test_memory_lvalue_compound_pointer(self):
        self.run_body('''
            var buf=stackalloc int[8]; var p=buf; let pp=&p;
            *pp+=3; if (p!=buf+3) { return 1; }
            *pp-=2; if (p!=buf+1) { return 2; }
            var slots=stackalloc int*[2]; slots[0]=buf; slots[1]=buf+7;
            slots[0]+=4; slots[1]-=3;
            if (slots[0]!=buf+4 || slots[1]!=buf+4) { return 3; }
        ''')

    def test_pointer_operand_evaluation_order(self):
        self.run_body('''
            var buf=stackalloc int[4]; buf[0]=10; buf[1]=20; buf[2]=30; buf[3]=40;
            var p=buf;
            let q=p+advance(&p);
            if (q!=buf+1 || p!=buf+2) { return 1; }
            p=buf; let x=p[advance(&p)]; if (x!=20) { return 2; }
            p=buf; let y=&p[advance(&p)]; if (y!=buf+1) { return 3; }
        ''', 'int advance(int** p) { unsafe(using krt.mem;) { *p=*p+2; } return 1; }')

    def test_call_argument_evaluation_order(self):
        self.run_body('''
            var x=1; var y=2; var p=&x;
            if (check(p, redirect(&p, &y))!=1) { return 1; }
            let f: fn(int*, int) -> int=&check;
            p=&x; if (f(p, redirect(&p, &y))!=1) { return 2; }
        ''', '''
            int redirect(int** p, int* next) { unsafe(using krt.mem;) { *p=next; } return 0; }
            int check(int* p, int ignored) { unsafe(using krt.mem;) { return *p; } }
        ''')

    def test_pointer_ternary_and_short_circuit(self):
        self.run_body('''
            var x=1; var y=2; var choose=true;
            let p=choose ? &x : &y; *p=7;
            if (x!=7 || y!=2) { return 1; }
            let maybe=choose ? &y : null;
            if (maybe is int* hit) { *hit=9; } else { return 2; }
            if (y!=9) { return 3; }
            let absent=choose ? null : &x;
            if (absent is int* hit) { return 4; }
            var calls=0;
            if (false && tick(&calls)>0) { return 5; }
            if (true || tick(&calls)>0) { if (calls!=0) { return 6; } }
            let branch=choose ? &x : tickPointer(&calls, &y);
            if (branch!=&x || calls!=0) { return 7; }
        ''', '''
            int tick(int* p) { unsafe(using krt.mem;) { *p+=1; return *p; } }
            int* tickPointer(int* p, int* result) { unsafe(using krt.mem;) { *p+=1; } return result; }
        ''')

    def test_nullable_slot_keeps_pointee_nullability(self):
        self.run_body('''
            var x=3; var maybe: int*?=null; let slot=&maybe;
            *slot=&x;
            let value=*slot;
            if (value is int* hit) { *hit=8; } else { return 1; }
            *slot=null;
            if (maybe!=null || x!=8) { return 2; }
        ''')
        self.reject('var maybe:int*?=null; let slot=&maybe; let value=**slot;')

    def test_multilevel_function_slot_and_indirect_expression(self):
        self.run_body('''
            var x=1; var p=&x; var pp=&p; let ppp=&pp;
            ***ppp=9; if (x!=9 || &***ppp!=p || &**ppp!=pp) { return 1; }
            var f: fn(int) -> int=&inc; let slot=&f;
            if ((*slot)(10)!=11 || (&inc)(20)!=21) { return 2; }
            *slot=&dec; if (f(10)!=9) { return 3; }
            if (recurse(&inc, 12)!=12) { return 4; }
        ''', '''
            int inc(int x) { return x+1; } int dec(int x) { return x-1; }
            int recurse(fn(int) -> int f, int n) { unsafe(using krt.mem;) {
                if (n==0) { return 0; } return f(recurse(f,n-1));
            } }
        ''')

    def test_noninteger_storage_and_canaries(self):
        for kind, first, second in (('bool','false','true'), ('char',"'a'","'z'"),
                                     ('float32','1.25','2.5'), ('float64','1.25','2.5')):
            with self.subTest(type=kind):
                self.run_body(f'''
                    {kind} x={first}; let p=&x; *p=({kind}){second};
                    if (x!=({kind}){second} || *p!=({kind}){second}) {{ return 1; }}
                    var buf=stackalloc {kind}[3];
                    buf[0]=({kind}){first}; buf[1]=({kind}){second}; buf[2]=({kind}){first};
                    if (buf[0]!=({kind}){first} || buf[1]!=({kind}){second} || buf[2]!=({kind}){first}) {{ return 2; }}
                ''')

    def test_float_pointer_operations_aliases_and_calls(self):
        self.run_body('''
            float32 x=1.25; let p=&x;
            *p+=1.25; if (*p!=2.5) { return 1; }
            (*p)++; if (x!=3.5) { return 2; }
            *p=2; if (x!=2.0) { return 3; }
            if (modify(1.25)!=2.5) { return 4; }
            let f: fn(float32) -> float32=&modify;
            if (f(1.25)!=2.5) { return 5; }
            if (global!=1.25) { return 6; } *(&global)=2.5;
            if (global!=2.5) { return 7; }
            let bits=(uint32*)p; *bits=1069547520;
            if (x!=1.5 || *p!=1.5) { return 8; }
            var calls=0;
            if (readOnce(p,&calls)+readOnce(p,&calls)!=3.0 || calls!=2) { return 9; }
            *bits=2147483648;
            if (*p!=0.0) { return 10; }
            *bits=2143289344;
            if (*p==*p || !(*p!=*p) || *p<0.0 || *p>=0.0) { return 11; }
        ''', '''
            static float32 global=1.25;
            float32 modify(float32 x) { unsafe(using krt.mem;) { let p=&x; *p=2.5; return x; } }
            float32 readOnce(float32* p, int* calls) { unsafe(using krt.mem;) { *calls+=1; return *p; } }
        ''')

    def test_compound_assignment_reads_before_rhs(self):
        self.run_body('''
            var x=3; let p=&x; *p+=overwrite(p);
            if (x!=5) { return 1; }
            x=3; p[0]+=overwrite(p); if (x!=5) { return 2; }
            x=3; x+=overwrite(p); if (x!=5) { return 3; }
        ''', 'int overwrite(int* p) { unsafe(using krt.mem;) { *p=10; } return 2; }')

    def test_large_index_load_matches_address(self):
        self.run_body('''
            var x=7; let origin=(int*)((usize)&x-(usize)4294967296);
            int32 index=1073741824;
            if (&origin[index]!=&x || origin[index]!=7) { return 1; }
            origin[index]=9; if (x!=9) { return 2; }
        ''')

    def test_nested_nullable_types_and_rejections(self):
        self.run_body('''
            var x=1; var p:int*?=&x; let pp:int*?*=&p;
            var slots=stackalloc int*?[2]; slots[0]=null; slots[1]=*pp;
            if (slots[0]!=null) { return 1; }
            if (slots[1] is int* hit) { *hit=7; } else { return 2; }
            if (x!=7) { return 3; }
        ''')
        for body in ('let q:int**=slot;', 'let q=(int**)slot;', 'let q:void*=slot;',
                     'let q=(usize)slot;', 'let q:int*=*slot;'):
            with self.subTest(body=body): self.reject('var p:int*?=null; let slot=&p;'+body)

    def test_wide_abi_register_and_stack_boundaries(self):
        for leading in range(8):
            types=['int']*leading+['int128','int*','uint66']
            params=[f'int a{i}' for i in range(leading)]+['int128 wide','int* p','uint66 tail']
            args=['1']*leading+['(int128)18446744073709551619','&x','(uint66)36893488147419103233']
            signature=','.join(types)
            with self.subTest(leading=leading):
                self.run_body(f'''
                    var x=0; let f:fn({signature})->int128=&mixed;
                    if (f({','.join(args)})!=(int128)18446744073709551626 || x!=7) {{ return 1; }}
                ''', f'''int128 mixed({','.join(params)}) {{ unsafe(using krt.mem;) {{
                    if (tail!=(uint66)36893488147419103233) {{ return -1; }}
                    *p=7; return wide+*p;
                }} }}''')

    def test_pointer_call_callee_is_captured_before_arguments(self):
        self.run_body('''
            var f:fn(int)->int=&first;
            if (f(change((void*)&f))!=11) { return 1; }
            if (f(1)!=21) { return 2; }
        ''', '''
            int first(int n) { return n+10; } int second(int n) { return n+20; }
            int change(void* slot) { unsafe(using krt.mem;) { *(usize*)slot=(usize)&second; } return 1; }
        ''')

    def test_unaligned_loads_stores_and_canaries(self):
        for bits in (8,16,30,32,62,64,66,96,126,128):
            with self.subTest(bits=bits):
                size=next(s for s in (1,2,4,8,16) if s*8>=bits)
                self.run_body(f'''
                    var bytes=stackalloc byte[32];
                    for (var i=0; i<32; i++) {{ bytes[i]=85; }}
                    let p=(uint{bits}*)(bytes+1); *p=(uint{bits}){(1<<bits)-1};
                    if (*p!=(uint{bits}){(1<<bits)-1} || bytes[0]!=85 || bytes[{size+1}]!=85) {{ return 1; }}
                ''')

    def test_guard_pages_exact_load_store_widths(self):
        declarations='public static extern int64 syscall(int64 n,int64 a,int64 b,int64 c,int64 d,int64 e,int64 f);'
        for bits in range(2,129,2):
            for unsigned in (False,True):
                kind=('uint' if unsigned else 'int')+str(bits)
                size=next(s for s in (1,2,4,8,16) if s*8>=bits)
                value=(1<<bits)-1 if unsigned else -1
                with self.subTest(type=kind):
                    self.run_body(f'''
                        usize region=(usize)syscall(9,0,12288,0,34,-1,0);
                        if (region>=(usize)-4095) {{ return 90; }}
                        if (syscall(10,region+4096,4096,3,0,0,0)!=0) {{ return 91; }}
                        let first=({kind}*)(region+4096);
                        let last=({kind}*)(region+8192-{size});
                        *first=({kind}){value}; *last=({kind}){value};
                        if (*first!=({kind}){value} || *last!=({kind}){value}) {{ return 1; }}
                        if (syscall(11,region,12288,0,0,0,0)!=0) {{ return 92; }}
                    ''',declarations)

    def test_invalid_raw_addresses_fault_in_child_process(self):
        declarations='public static extern int64 syscall(int64 n,int64 a,int64 b,int64 c,int64 d,int64 e,int64 f);'
        bodies=['let p=(int*)0; *p=7;', '''
            let region=(usize)syscall(9,0,8192,0,34,-1,0);
            if (region>=(usize)-4095) { return 90; }
            if (syscall(10,region,4096,3,0,0,0)!=0) { return 91; }
            let bytes=(byte*)region; bytes[4096]=7;
        ''']
        for body in bodies:
            with self.subTest(body=body),tempfile.TemporaryDirectory(prefix='kairote-pointer-fault-') as directory:
                result,binary=self.compile(declarations+'int main(){unsafe(using krt.mem;){'+body+'}return 0;}',directory)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                result=subprocess.run([str(binary)],capture_output=True,timeout=5,preexec_fn=no_core)
                self.assertEqual(result.returncode,-signal.SIGSEGV)

    def test_addressed_locals_under_register_pressure(self):
        lines=[]
        for i in range(100): lines += [f'int30 x{i}={i}; let p{i}=&x{i};']
        for i in range(100): lines += [f'*p{i}+={100-i};']
        for i in range(100): lines += [f'if(x{i}!=100 || *p{i}!=100) {{return {i+1};}}']
        self.run_body('\n'.join(lines))

    def test_maximum_function_pointer_arity(self):
        count=128
        params=','.join(f'int* p{i}' for i in range(count))
        signature=','.join(['int*']*count)
        args=','.join(['&x']*count)
        self.run_body(f'var x=7; let f:fn({signature})->int=&last; if(f({args})!=7){{return 1;}}',
                      f'int last({params}){{unsafe(using krt.mem;){{return *p127;}}}}')
        self.reject('',f'int tooMany({params},int* excess){{return 0;}}',diagnostic='128')

    def test_pointer_depth_limit(self):
        self.reject('let p:int'+'*'*65+'=(int*)0;',diagnostic='64')
        lines=['var x=1;']
        for i in range(64): lines += [f'let p{i}=&'+(f'p{i-1}' if i else 'x')+';']
        lines += ['*'*64+'p63=7;', 'if(x!=7){return 1;}']
        self.run_body('\n'.join(lines))
        self.reject('\n'.join(lines)+'let beyond=&p63;',diagnostic='64')

    def test_pointer_arrays_keep_element_nullability(self):
        self.run_body('''
            var x=1; let p=&x; let array=[p,p]; array[0][0]=7;
            if (x!=7) { return 1; }
            let nullable=[p,null];
            if (nullable[0] is int* hit) { *hit=9; } else { return 2; }
            if (nullable[1]!=null || x!=9) { return 3; }
        ''')
        self.reject('var p:int*?=null; let array=[p]; let q:int*=array[0];')
        self.reject('var p:int*?=null; let array=[p]; let q=(int*)array[0];')

    def test_multiple_source_files(self):
        sources={
            'Main.krt': '''int main(){unsafe(using krt.mem;){var x=1;
                let f:fn(int*)->int*=&pass;
                let maybe:int*?=find(f(&x));
                if(maybe is int* hit){*hit=7;}else{return 1;}
                if(x!=7){return 2;}
                if(wide((int128)18446744073709551616,&x)!=(int128)18446744073709551623){return 3;}
            }return 0;}''',
            'Pass.krt':'int* pass(int* p){return p;}',
            'Find.krt':'int*? find(int* p){return p;}',
            'Wide.krt':'int128 wide(int128 n,int* p){unsafe(using krt.mem;){return n+*p;}}',
        }
        with tempfile.TemporaryDirectory(prefix='kairote-pointer-multifile-') as directory:
            folder=Path(directory)
            for name,source in sources.items(): (folder/name).write_text(source)
            for order in itertools.permutations(sources):
                with self.subTest(order=order):
                    result=subprocess.run([str(support.COMPILER),*order,'output','program'],
                                          cwd=folder,capture_output=True,text=True,timeout=30)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    binary=folder/('program' if (folder/'program').exists() else 'program.exe')
                    result=subprocess.run([str(binary)],capture_output=True,timeout=5,preexec_fn=no_core)
                    self.assertEqual(result.returncode,0,result.stderr)

    def test_multi_file_failures_cancel_linking(self):
        failures=[
            'int bad(){var x=1; let p=&x; return 0;}',
            'int bad(){unsafe(using krt.mem;){let p:int*=null;}return 0;}',
            'int bad(){unsafe(using krt.mem;){var x=1; *(&x)=;}return 0;}',
        ]
        for bad in failures:
            with self.subTest(source=bad),tempfile.TemporaryDirectory(prefix='kairote-multifile-reject-') as directory:
                folder=Path(directory)
                (folder/'Main.krt').write_text('int main(){return 0;}')
                (folder/'Bad.krt').write_text(bad)
                result=subprocess.run([str(support.COMPILER),'Main.krt','Bad.krt','output','bad'],
                                      cwd=folder,capture_output=True,text=True,timeout=30)
                self.assertGreater(result.returncode,0,result.stdout+result.stderr)
                self.assertNotIn('BUILD SUCCESSFUL',result.stdout+result.stderr)
                self.assertFalse((folder/'bad.exe').exists())

    def test_nullable_equality_and_default_initialization(self):
        self.run_body('''
            var empty:int*?;
            let nullValue:int*?=null;
            var x=7; let p:int*?=&x; let q:int*?=&x;
            if(empty!=null || empty!=nullValue || p!=q || p==empty){return 1;}
            if(q is int* hit){if(*hit!=7){return 2;}}else{return 3;}
        ''')

    def test_reject_data_pointer_calls(self):
        for call in ('p(1)', '(p+0)(1)', '(*pp)(1)', 'pp(1)'):
            with self.subTest(call=call): self.reject('var x=1; let p=&x; let pp=&p;'+call+';')

    def test_reject_pointer_numeric_cast_misuse(self):
        for body in ('let p=(int*)1.25;', 'let x=(float64)p;', 'let x=(float32)p;',
                     'float32 x=3.0; let fp=&x; *fp%=2.0;'):
            with self.subTest(body=body): self.reject('var n=1; let p=&n;'+body)

    def test_compiler_storage_capacity_diagnostics(self):
        body=''.join(f'int x{i}={i}; let p{i}=&x{i};' for i in range(600))
        body+=''.join(f'*p{i}+=1;' for i in range(600))
        body+=''.join(f'if(x{i}!={i+1}){{return 1;}}' for i in range(600))
        self.reject(body,diagnostic='storage limit')

    def test_full_width_index_arithmetic_without_dereference(self):
        self.run_body('''
            let p=(int*)4096; int32 index=1073741824;
            if ((usize)&p[index] != (usize)4294971392) { return 1; }
            if ((usize)(p+index) != (usize)&p[index]) { return 2; }
            if ((usize)&p[-1] != 4092) { return 3; }
            uint128 wide=18446744073709551617;
            if ((usize)(p+wide)!=4100 || (usize)&p[wide]!=4100) { return 4; }
        ''')

    def test_stackalloc_zero_nested_calls_and_recursion(self):
        self.run_body('''
            var empty=stackalloc byte[0];
            if (((usize)empty % 16)!=0) { return 1; }
            if (nested(30)!=465) { return 2; }
            var sum=0;
            for (var i=0; i<100; i++) { var b=stackalloc int[17]; b[16]=i; sum+=b[16]; }
            if (sum!=4950) { return 3; }
        ''', '''
            int nested(int n) { unsafe(using krt.mem;) {
                var b=stackalloc int[100]; b[0]=n; b[99]=n;
                if (n==0) { return 0; }
                let value=nested(n-1); if (b[0]!=n || b[99]!=n) { return -10000; }
                return b[0]+value;
            } }
        ''')

    def test_stackalloc_overflow_traps(self):
        for kind, count in (('int','-1'), ('int','4611686018427387904'),
                             ('byte','18446744073709551615'),
                             ('int','(uint128)18446744073709551616')):
            with self.subTest(type=kind, count=count):
                source='int main() { unsafe(using krt.mem;) { var n='+count+'; var p=stackalloc '+kind+'[n]; } return 0; }'
                with tempfile.TemporaryDirectory(prefix='kairote-pointer-trap-') as directory:
                    result,binary=self.compile(source,directory)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    result=subprocess.run([str(binary)],capture_output=True,timeout=5,preexec_fn=no_core)
                    self.assertEqual(result.returncode,-signal.SIGILL,source)

    def test_fixed_seed_alias_differential(self):
        # Python is the independent oracle; all indices stay inside initialized storage.
        for seed in range(16):
            rng=random.Random(seed)
            values=[rng.randrange(-100,101) for _ in range(16)]
            lines=['var b=stackalloc int[16]; var p=b;']
            lines += [f'b[{i}]={v};' for i,v in enumerate(values)]
            for _ in range(80):
                index=rng.randrange(16); other=rng.randrange(16); delta=rng.randrange(-8,9)
                if rng.randrange(2):
                    lines.append(f'p=b+{index}; *p+=b[{other}]+({delta});')
                    values[index]=(values[index]+values[other]+delta+2**31)%2**32-2**31
                else:
                    lines.append(f'b[{index}]=*(b+{other})-({delta});')
                    values[index]=(values[other]-delta+2**31)%2**32-2**31
            lines += [f'if (b[{i}]!={v}) {{ return {i+1}; }}' for i,v in enumerate(values)]
            with self.subTest(seed=seed): self.run_body('\n'.join(lines))

    def test_reject_invalid_operators_and_lvalues(self):
        for expression in ('p*p','p/p','p%p','p|p','p&p','p^p','p<<1','p>>1',
                           '-p','~p','!p','p+1.5','p[1.5]','p[p]','p&&p'):
            with self.subTest(expression=expression):
                self.reject('var x=1; let p=&x; let y='+expression+';')
        for body in ('42++;','++42;','(&x)++;','(p+1)++;','*p=&x;', 'p*=2;',
                     'p[0]=&x;', 'let f:fn(int)->int=&inc; let g=f+1;'):
            with self.subTest(body=body):
                self.reject('var x=1; var p=&x;'+body, 'int inc(int x) { return x+1; }')

    def test_reject_pointer_type_escape_through_expressions(self):
        for body in ('let q:byte*=true ? p : p;', 'let q:int*=true ? p : null;',
                     'let q:int*=false ? null : p;', 'let q:int*=maybe ?? maybe;',
                     'let q:int=maybe;', 'let q:int*=maybe as int*;'):
            with self.subTest(body=body):
                self.reject('var x=1; let p=&x; let maybe:int*?=null;'+body)

    def test_permission_checks_in_nested_expressions(self):
        for expression in ('[&x]', '(&x, &x)', 'true ? &x : &x', 'stackalloc int[1]',
                           '[*p]', '[p+1]', '[stackalloc int[1]]'):
            source='int main() { var x=1; var p:int*; let hidden='+expression+'; return 0; }'
            # p is initialized inside a permitted block and used outside it.
            source=source.replace('var p:int*;', 'unsafe(using krt.mem;) { }') if 'p' not in expression else (
                'int leak(int* p) { let hidden='+expression+'; return 0; } int main() { return 0; }')
            with self.subTest(expression=expression), tempfile.TemporaryDirectory(prefix='kairote-pointer-permission-') as directory:
                result,binary=self.compile(source,directory)
                self.assertGreater(result.returncode,0,result.stdout+result.stderr+source)
                self.assertFalse(binary.exists())

    def test_reject_bad_function_signatures_and_returns(self):
        declarations='int* ptr(int* p) { return p; } int integer(int n) { return n; }'
        for body in ('let f:fn(byte*)->int*=&ptr;', 'let f:fn(int*)->byte*=&ptr;',
                     'let f:fn(int)->int=&integer; let x=f();',
                     'let f:fn(int)->int=&integer; let x=f(1,2);',
                     'let f:fn(int*)->int*=&ptr; let x=f(2);',
                     'let f:fn(int*)->int*=&ptr; let x=f(null);'):
            with self.subTest(body=body): self.reject(body,declarations)
        for declaration in ('int* bad() { return null; }', 'byte* bad(int* p) { return p; }'):
            with self.subTest(declaration=declaration): self.reject('',declaration)

    def test_reject_malformed_pointer_syntax(self):
        for body in ('let p:int*=;', 'let p:int**?=;', 'let p=stackalloc int[];',
                     'let p=stackalloc int[2;', 'let p=stackalloc void[1];',
                     'let p=stackalloc Unknown[1];', 'let p=stackalloc int[1.5];',
                     'let f:fn(int,)->int=&main;', 'let f:fn(int) int=&main;',
                     'var x=1; *(&x)=;', 'var x=1; let p=(int*?)&x; *p=7;'):
            with self.subTest(body=body): self.reject(body)


if __name__ == '__main__':
    unittest.main()
