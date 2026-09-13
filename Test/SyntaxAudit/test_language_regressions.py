"""Executable and diagnostic regressions for enums, generics, namespaces and ref."""
import os
from pathlib import Path
import resource
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('KRTC', ROOT / 'Re.KrtC/build/KrtC')).resolve()


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class LanguageRegressions(unittest.TestCase):
    def check(self, source, error=None):
        with tempfile.TemporaryDirectory(prefix='kairote-language-') as directory:
            src, binary = Path(directory) / 'Main.krt', Path(directory) / 'Main'
            src.write_text(source)
            result = subprocess.run([str(COMPILER), str(src), 'output', str(binary)],
                                    cwd=directory, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            self.assertNotIn('AddressSanitizer', output, output)
            self.assertNotIn('runtime error:', output, output)
            if error:
                self.assertGreater(result.returncode, 0, source + output)
                self.assertIn(error, output, output)
                self.assertFalse(binary.exists(), output)
            else:
                self.assertEqual(result.returncode, 0, output + source)
                result = subprocess.run([str(binary)], capture_output=True, timeout=5, preexec_fn=no_core)
                self.assertEqual(result.returncode, 0, f'exit={result.returncode}\n{source}')

    def test_enum_values_and_constants(self):
        self.check('''
            enum Code { Zero, One, Negative=-7, Next, Flags=1<<4, Both=Flags|One, Max=2147483647, Min=-2147483648, }
            int32 echo(Code code) { return code; }
            int main() {
                Code a=Code.Next;
                if (a!=-6 || Code.Zero!=0 || Code.One!=1 || Code.Both!=17) return 1;
                if (echo(Code.Min)!=-2147483648 || echo(Code.Max)!=2147483647) return 2;
                switch (a) { case Code.Next: return 0; default: return 3; }
            }
        ''')

    def test_enum_invalid_definitions(self):
        for body in ('A, A', 'A=2147483648', 'A=-2147483649', 'A=2147483647, B',
                     'A=1/0', 'A=1<<32', 'A=Missing', 'A=1.5', 'A=F()', 'A=1 B=2'):
            with self.subTest(body=body):
                self.check('int F(){return 1;} enum E {' + body + '} int main(){return 0;}', 'Invalid enum')

    def test_enum_missing_member_and_immutable(self):
        self.check('enum E { A } int main(){return E.Missing;}', 'Unknown enum member')
        self.check('enum E { A } int main(){E.A++; return 0;}', 'lvalue')
        self.check('enum E { A } int main(){E.A=2; return 0;}', 'lvalue')

    def test_namespace_paths_and_collisions(self):
        self.check('''
            namespace Left { class Helper { public static int Value(){return 3;} } }
            namespace Right { class Helper { public static int Value(){return 7;} } }
            namespace Outer.Inner { class Helper { public static int Value(){return 11;} }
                int Call(){return Helper.Value();}
            }
            namespace Outer { namespace Inner { int Other(){return Helper.Value()+1;} } }
            int main(){
                if(Left.Helper.Value()!=3 || Right.Helper.Value()!=7) return 1;
                if(Outer.Inner.Helper.Value()!=11 || Outer.Inner.Call()!=11 || Outer.Inner.Other()!=12) return 2;
                return 0;
            }
        ''')

    def test_namespace_instances(self):
        self.check('''
            namespace A.B { class Counter { int x; public void Set(int v){x=v;} public int Get(){return x;} } }
            int main(){A.B.Counter c=new A.B.Counter(); c.Set(37); if(c.Get()!=37)return 1; return 0;}
        ''')

    def test_namespace_invalid_path(self):
        self.check('namespace A {class H {public static int Get(){return 1;}}} int main(){return B.H.Get();}', 'Undefined')

    def test_generic_distinct_storage_and_instances(self):
        self.check('''
            class Box<T> { T value; public void Set(T v){value=v;} public T Get(){return value;} }
            int main(){
                Box<int32> a=new Box<int32>(); Box<uint128> b=new Box<uint128>();
                Box<int32> c=new Box<int32>(); Box<int30> d=new Box<int30>();
                a.Set(5); b.Set((uint128)1267650600228229401496703205379); c.Set(9); d.Set((int30)-13);
                if(a.Get()!=5 || c.Get()!=9 || d.Get()!=-13)return 1;
                if(b.Get()!=(uint128)1267650600228229401496703205379)return 2;
                return 0;
            }
        ''')

    def test_generic_multiple_type_parameters(self):
        self.check('''
            class Pair<A,B> { A first; B second;
                public void Set(A a,B b){first=a; second=b;}
                public A First(){return first;} public B Second(){return second;}
            }
            int main(){Pair<uint128,int32> p=new Pair<uint128,int32>();
                p.Set((uint128)18446744073709551623,-3);
                if(p.First()!=(uint128)18446744073709551623 || p.Second()!=-3)return 1;
                return 0;
            }
        ''')

    def test_generic_float_storage(self):
        self.check('''
            class Box<T>{T value; public void Set(T v){value=v;} public T Get(){return value;}}
            int main(){Box<float32> a=new Box<float32>(); Box<float64> b=new Box<float64>();
                a.Set((float32)1.25); b.Set(3.5); if(a.Get()!=1.25 || b.Get()!=3.5)return 1; return 0;}
        ''')

    def test_generic_in_namespace_and_forward_declaration(self):
        self.check('''
            int main(){A.Box<int32> p=new A.Box<int32>(); p.Set(41); if(p.Get()!=41)return 1; return 0;}
            namespace A {class Box<T>{T value; public void Set(T v){value=v;} public T Get(){return value;}}}
        ''')

    def test_generic_invalid_arguments(self):
        for kind in ('Box<>', 'Box<int32,int64>', 'Box<void>', 'Box<Missing>'):
            with self.subTest(kind=kind):
                self.check('class Box<T>{T value;} int main(){'+kind+' p=new '+kind+'(); return 0;}', 'generic')
        self.check('class Box<T,T>{T value;} int main(){return 0;}', 'Duplicate generic parameter')

    def test_ref_alias_and_forwarding(self):
        self.check('''
            function Bump(ref int x){x=x+1;}
            void Pair(ref int a,ref int b){a=3; b+=a; ++a; b--;}
            void Forward(ref int x){Bump(x); Pair(x,x);}
            int main(){int x=9; Bump(x); if(x!=10)return 1; Forward(x); if(x!=6)return 2;
                {int x=40; Bump(x); if(x!=41)return 3;} if(x!=6)return 4; return 0;}
        ''')

    def test_ref_integer_widths(self):
        for kind in ('int2','int30','int64','int66','int126','int128','uint2','uint30','uint64','uint66','uint128'):
            with self.subTest(kind=kind):
                self.check(f'''
                    void Set(ref {kind} x){{x=({kind})-1;}}
                    int main(){{{kind} x=0; int neighbor=99; Set(x);
                        if(x!=({kind})-1 || neighbor!=99)return 1; return 0;}}
                ''')

    def test_ref_float_and_global(self):
        self.check('''
            static float32 g=1.0;
            void Add(ref float32 v){v+=1.25; v++;}
            void Set(ref float64 v){v=7.5;}
            int main(){Add(g); float64 x=0.0; Set(x); if(g!=3.25 || x!=7.5)return 1; return 0;}
        ''')

    def test_ref_pointer_and_callback(self):
        self.check('''
            void Replace(ref int* p,int* q){unsafe(using krt.mem;){p=q;}}
            void Bump(ref int x){x++;}
            int main(){unsafe(using krt.mem;){
                int a=2; int b=7; var p=&a; Replace(p,&b); if(*p!=7)return 1;
                let f:fn(ref int)->void=&Bump; f(b); if(b!=8)return 2;
                Bump(*p); if(b!=9)return 3;
            }return 0;}
        ''')

    def test_ref_array_evaluation_once(self):
        self.check('''
            void Pair(ref int x,ref int y){x+=10; y+=20;}
            int main(){int[] a=[1,2,3]; int i=0; Pair(a[i++],a[i++]);
                if(i!=2 || a[0]!=11 || a[1]!=22 || a[2]!=3)return 1; return 0;}
        ''')

    def test_ref_many_arguments(self):
        parameters=','.join(f'ref int p{i}' for i in range(10))
        assignments=''.join(f'p{i}={i+1};' for i in range(10))
        variables=''.join(f'int x{i}=0;' for i in range(10))
        arguments=','.join(f'x{i}' for i in range(10))
        checks=' || '.join(f'x{i}!={i+1}' for i in range(10))
        self.check(f'void Set({parameters}){{{assignments}}} int main(){{{variables}Set({arguments});if({checks})return 1;return 0;}}')

    def test_ref_invalid_arguments(self):
        for argument in ('1', 'x+1', '(int)x', 'F()'):
            with self.subTest(argument=argument):
                self.check('int F(){return 0;} void Set(ref int v){v=1;} int main(){int x=0; Set('+argument+');return 0;}', 'lvalue')
        self.check('void Set(ref int32 v){v=1;} int main(){int64 x=0; Set(x);return 0;}', 'match exactly')
        self.check('void Set(ref int32 v){v=1;} int main(){int[] x=[0]; Set(x);return 0;}', 'scalar slot')


    def test_ref_explicit_modifier_and_signature(self):
        self.check("void Bump(ref int x){x++;} int main(){int x=1; Bump(ref x); if(x!=2)return 1; return 0;}")
        self.check("void Value(int x){} int main(){int x=1; Value(ref x); return 0;}", "value parameter")
        self.check("void Bump(ref int x){x++;} int main(){unsafe(using krt.mem;){let f:fn(int)->void=&Bump;} return 0;}", "Incompatible")

    def test_ref_array_wide_and_nullable_slot(self):
        self.check('''
            void Set(ref uint128 x){x=(uint128)18446744073709551625;}
            void Clear(ref int*? p){unsafe(using krt.mem;){p=null;}}
            int main(){uint128[] a=[0,0]; Set(a[1]);
                if(a[0]!=0 || a[1]!=(uint128)18446744073709551625)return 1;
                unsafe(using krt.mem;){int x=1; var p:int*?=&x; Clear(p); if(p!=null)return 2;}
                return 0;
            }
        ''')
        self.check("void Clear(ref int*? p){} int main(){unsafe(using krt.mem;){int x=1; var p=&x; Clear(p);}return 0;}", "match exactly")

    def test_ref_cross_file_all_orders(self):
        from itertools import permutations
        sources = {
            "Main.krt": "int main(){int66 x=0; Forward(x); if(x!=(int66)18446744073709551625)return 1; return 0;}",
            "Set.krt": "void Set(ref int66 x){x=(int66)18446744073709551625;}",
            "Forward.krt": "void Forward(ref int66 x){Set(x);}",
        }
        for order in permutations(sources):
            with self.subTest(order=order), tempfile.TemporaryDirectory(prefix='kairote-ref-files-') as directory:
                paths=[]
                for name in order:
                    path=Path(directory)/name
                    path.write_text(sources[name])
                    paths.append(str(path))
                result=subprocess.run([str(COMPILER),*paths,'output','Program'],cwd=directory,capture_output=True,text=True,timeout=30)
                output=result.stdout+result.stderr
                self.assertEqual(result.returncode,0,output)
                self.assertNotIn('AddressSanitizer',output,output)
                self.assertNotIn('runtime error:',output,output)
                result=subprocess.run([str(Path(directory)/'Program.exe')],capture_output=True,timeout=5,preexec_fn=no_core)
                self.assertEqual(result.returncode,0,result.stderr)

    def test_generic_nested_and_small_wide_argument(self):
        self.check('''
            class Box<T>{T value; public void Set(T v){value=v;} public T Get(){return value;}}
            int main(){Box<uint128> inner=new Box<uint128>(); inner.Set(5);
                Box<Box<uint128>> outer=new Box<Box<uint128>>(); outer.Set(inner);
                Box<uint128> copy=outer.Get(); if(copy.Get()!=5)return 1; return 0;}
        ''')


    def test_generic_class_function_parameters_and_returns(self):
        self.check('''
            class Box<T>{T value; public void Set(T v){value=v;} public T Get(){return value;}}
            Box<int32> Use(Box<int32> b){b.Set(11); return b;}
            int main(){Box<int32> b=new Box<int32>(); Box<int32> c=Use(b);
                if(c.Get()!=11 || b.Get()!=11)return 1; return 0;}
        ''')
        self.check("class Box<T>{T value;} int main(){Box<int32> x=new Box<uint128>(); return 0;}", "Incompatible generic")

    def test_ref_methods_and_recursion(self):
        self.check('''
            class Box<T>{public void Set(ref T x,T value){x=value;}}
            class Helper{public static void Bump(ref int x){x++;}}
            void Walk(ref int x,int n){if(n==0)return; x++; Walk(x,n-1);}
            int main(){Box<uint128> b=new Box<uint128>(); uint128 wide=0;
                b.Set(wide,(uint128)18446744073709551627);
                int x=0; Helper.Bump(ref x); Walk(x,7);
                if(x!=8 || wide!=(uint128)18446744073709551627)return 1; return 0;}
        ''')


    def test_generic_constructors_and_inferred_instances(self):
        self.check('''
            class Box<T>{T value; function Box(T v){this.value=v;} public T Get(){return this.value;}}
            int main(){var a=new Box<int32>(17); var b=new Box<uint128>(5);
                if(a.Get()!=17 || b.Get()!=5)return 1; return 0;}
        ''')
        fields = ''.join(f'T field{i};' for i in range(300))
        self.check('class Large<T>{'+fields+'''
            function Large(T v){field0=1; field299=v;}
            public T Get(){return field299+field0;}}
            int main(){var box=new Large<uint128>((uint128)18446744073709551625);
                if(box.Get()!=(uint128)18446744073709551626)return 1; return 0;}
        ''')


if __name__ == '__main__':
    unittest.main()
