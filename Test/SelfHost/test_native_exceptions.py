"""Native unwinding crosses calls and preserves catch/finally semantics."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.bootstrap import Bootstrap


class NativeExceptionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='krt-native-exceptions-')
        cls.build=Bootstrap(Path(cls.directory.name))
        cls.compiler=Path(os.environ['SELFHOST_COMPILER']).resolve() if 'SELFHOST_COMPILER' in os.environ else cls.build.seed()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self,name,source,expected=0):
        binary,_=self.build.compile(self.compiler,source,name)
        result=subprocess.run([binary],capture_output=True,timeout=10)
        self.assertEqual(result.returncode,expected,result.stderr.decode())

    def test_cross_function_typed_catch(self):
        self.check('typed-catch','''
class Failure { public int32 code; public Failure(int32 c){code=c;} }
void fail(int32 depth){if(depth==0){throw new Failure(42);} fail(depth-1);}
int32 main(){int32 answer=0;try{fail(5);}catch(string wrong){return 1;}catch(Failure e){answer=e.code;delete e;}if(answer!=42){return 2;}return 0;}
''')

    def test_catchall_rethrow_and_nested_handlers(self):
        self.check('rethrow','''
void raise(){throw "boom";}
int32 main(){int32 count=0;try{try{raise();}catch{count++;throw;}}catch(string text){if(text!="boom"){return 1;}count++;}if(count!=2){return 2;}return 0;}
''')

    def test_finally_normal_exception_unmatched_and_catch_throw(self):
        self.check('finally-paths','''
int32 main(){int32 n=0;try{n=1;}finally{n+=2;}if(n!=3){return 1;}
try{try{throw "first";}catch(string e){n+=10;throw 7;}finally{n+=100;}}catch(int32 e){n+=e;}if(n!=120){return 2;}
try{try{throw 5;}catch(string no){n=0;}finally{n+=1000;}}catch(int32 e){n+=e;}if(n!=1125){return 3;}return 0;}
''')

    def test_finally_return_preserves_value_and_overrides(self):
        self.check('finally-return','''
int32 first(ref int32 n){try{return n;}finally{n+=10;}}
int32 override_value(){try{return 5;}finally{return 7;}}
int32 nested(ref int32 n){try{try{return 9;}finally{n+=10;}}finally{n+=100;}}
int32 main(){int32 n=2;if(first(ref n)!=2 || n!=12){return 1;}if(override_value()!=7){return 2;}if(nested(ref n)!=9 || n!=122){return 3;}return 0;}
''')

    def test_finally_break_continue_and_loop_inside_try(self):
        self.check('finally-loops','''
int32 main(){int32 n=0;int32 i=0;while(i<5){i++;try{if(i<3){continue;}break;}finally{n+=10;}}if(n!=30 || i!=3){return 1;}
try{while(true){break;}n++;}finally{n+=100;}if(n!=131){return 2;}return 0;}
''')

    def test_exception_in_finally_and_return_finally_nested_try(self):
        self.check('nested-finally','''
int32 value(ref int32 n){try{return 41;}finally{try{throw 9;}catch(int32 x){n=x;}}}
int32 main(){int32 n=0;if(value(ref n)!=41 || n!=9){return 1;}try{try{throw "old";}finally{throw 17;}}catch(int32 x){n=x;}if(n!=17){return 2;}return 0;}
''')

    def test_standard_exception_message_code_and_cleanup(self):
        library=(Path(__file__).resolve().parents[2]/'libs/System/Exception.krt').read_text()
        self.check('standard-exception', library+'''
using System.Exception;
void fail(){throw new Exception("disk full",28);}
int32 main(){int32 caught=0;try{fail();}catch(Exception error){
    if(error.Message!="disk full" || error.ToString()!=error.Message || error.Code!=28){return 1;}
    caught=1;delete error;
}if(caught!=1){return 2;}return 0;}
''')

    def test_unhandled_exception_exit(self):
        self.check('unhandled','int32 main(){throw "unhandled";}',70)

    def test_invalid_exception_constructs(self):
        cases={'bare-rethrow':'int32 main(){throw;}',
               'try-without-handler':'int32 main(){try{return 0;}}',
               'catchall-not-last':'int32 main(){try{throw 1;}catch{}catch(int32 x){}return 0;}',
               'catch-scope':'int32 main(){try{throw 1;}catch(int32 x){}return x;}',
               'throw-null':'int32 main(){throw null;}'}
        for name,source in cases.items():
            with self.subTest(name=name):
                work=Path(self.directory.name)/name;work.mkdir();(work/'program.krt').write_text(source)
                result=subprocess.run([self.compiler],cwd=work,capture_output=True,text=True,timeout=10)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertFalse((work/'stage1-probe.kro').exists())


if __name__=='__main__':
    unittest.main()
