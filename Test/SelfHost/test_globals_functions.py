"""Global initialization and typed indirect calls through the native compiler."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()


@unittest.skipUnless(COMPILER.is_file(), 'build the self-hosted compiler first')
class GlobalsAndFunctionPointers(unittest.TestCase):
    def run_source(self, source, expected=0, rejected=False):
        with tempfile.TemporaryDirectory(prefix='kairote-globals-functions-') as directory:
            work = Path(directory)
            path = work / 'main.krt'
            path.write_text(source)
            binary = work / 'program'
            compiled = subprocess.run([sys.executable, str(ROOT / 'SelfHost/compile.py'), str(path),
                                       '--compiler', str(COMPILER), '-o', str(binary)],
                                      capture_output=True, text=True, timeout=60)
            if rejected:
                self.assertNotEqual(compiled.returncode, 0, source)
                self.assertFalse(binary.exists())
                return
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr + source)
            executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(executed.returncode, expected, executed.stderr + source)

    def test_globals_initialize_once_in_order_and_are_shared_by_calls(self):
        self.run_source('''
            static int32 count = 2;
            static int32 answer = next() + next();
            static int32 empty;
            int32 next(){ count += 1; return count; }
            void set(ref int32 value){ value=19; }
            int32 main(){
                if(count!=4 || answer!=7 || empty!=0){return 1;}
                set(ref empty);
                if(next()!=5 || empty!=19){return 2;}
                return 0;
            }
        ''')

    def test_class_static_mutable_fields_and_initializers(self):
        self.run_source('''
            class State {
                public static int32 first=11;
                public static int32 second=first+3;
                public static int32 Tick(){ first++; return first+second; }
            }
            static int32 initial=State.second;
            int32 main(){
                if(State.Tick()!=26 || initial!=14){return 1;}
                State.second=7;
                if(State.Tick()!=20 || State.first!=13){return 2;}
                return 0;
            }
        ''')

    def test_global_arrays_and_objects_initialize_with_real_expressions(self):
        self.run_source('''
            class Box { int32 value; function Box(int32 n){ value=n; } int32 Get(){return value;} }
            static int32[] values={3,5,7};
            static Box box=new Box(values[1]);
            int32 main(){
                if(values.Length!=3 || box.Get()!=5){return 1;}
                values[0]=9;
                if(values[0]!=9){return 2;}
                delete box; delete values; return 0;
            }
        ''')

    def test_namespace_globals_do_not_collide(self):
        self.run_source('''
            namespace A { static int32 value=7; }
            namespace B { static int32 value=11; }
            int32 main(){ A.value+=1; return A.value+B.value; }
        ''', expected=19)

    def test_global_wide_storage_does_not_overlap_neighbor(self):
        self.run_source('''
            static uint128 large=(uint128)18446744073709551623;
            static int64 guard=17;
            int32 main(){
                if(large!=(uint128)18446744073709551623 || guard!=17){return 1;}
                large=large+9;
                if(large!=(uint128)18446744073709551632 || guard!=17){return 2;}
                return 0;
            }
        ''')

    def test_callback_pointer_and_ref_arguments(self):
        self.run_source('''
            int32 read_value(int32* value){return *value;}
            void change(ref int32 value){value=29;}
            class Calls {
                public static int32 Apply(fn(int32*) -> int32 callback){
                    int32 value=17; return callback(&value);
                }
            }
            int32 main(){
                if(Calls.Apply(&read_value)!=17){return 1;}
                fn(ref int32)->void set=&change;
                int32 value=1; set(ref value);
                return value==29 ? 0 : 2;
            }
        ''')

    def test_indirect_call_stack_arguments_and_expression_operands(self):
        self.run_source('''
            int64 sum(int64 a,int64 b,int64 c,int64 d,int64 e,int64 f,int64 g,int64 h){
                return a+b+c+d+e+f+g+h;
            }
            int32 main(){
                fn(int64,int64,int64,int64,int64,int64,int64,int64)->int64 call=&sum;
                if(7+call(1,2,3,4,5,6,7,8)!=43){return 1;}
                return 0;
            }
        ''')

    def test_static_method_address_and_global_function_pointer(self):
        self.run_source('''
            class Ops { public static int32 Add(int32 value){return value+7;} }
            static fn(int32)->int32 operation=&Ops.Add;
            int32 main(){ return operation(10); }
        ''', expected=17)

    def test_invalid_globals_and_function_pointer_signatures_are_rejected(self):
        for source in (
            'static int32 value=1; static int32 value=2; int32 main(){return value;}',
            'static int32 value="wrong"; int32 main(){return value;}',
            'int64 f(int64 n){return n;} int32 main(){fn(int32)->int32 call=&f;return call(1);}',
            'int32 f(int32 n){return n;} int32 main(){fn(int32)->int32 call=&f;return call();}',
            'int32 main(){fn(int32)->int32 call=17;return call(1);}',
            'void f(ref int32 n){n=1;} int32 main(){fn(int32)->void call=&f;return 0;}',
        ):
            with self.subTest(source=source):
                self.run_source(source, rejected=True)


if __name__ == '__main__':
    unittest.main()
