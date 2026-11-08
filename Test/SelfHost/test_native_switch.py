"""Execute switch dispatch, fallthrough, loop transfers, and diagnostics."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import ARKLINK, Bootstrap


class NativeSwitchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="krt-native-switch-")
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = (Path(os.environ["SELFHOST_COMPILER"]).resolve()
                        if "SELFHOST_COMPILER" in os.environ else cls.build.seed())

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0,
                         f"{name}: branch {result.returncode}; {result.stderr!r}")

    def test_numeric_dispatch_default_and_missing_match(self):
        self.check("numeric-dispatch", """
int32 value(int32 n){switch(n){case 1:return 11;case 2:return 23;default:return 31;}}
int32 main(){if(value(1)!=11 || value(2)!=23 || value(9)!=31){return 1;}
int32 result=7;switch(9){case 1:result=99;break;case 2:result=88;break;}
if(result!=7){return 2;}return 0;}
""")

    def test_selector_once_and_label_evaluation_stops_at_match(self):
        self.check("single-selector", """
int32 selector(ref int32 count){count++;return 2;}
int32 label(ref int32 count,int32 n){count=count*10+n;return n;}
int32 main(){int32 calls=0;int32 labels=0;int32 result=0;
switch(selector(ref calls)){case label(ref labels,1):result=1;break;
case label(ref labels,2):result=2;break;case label(ref labels,3):result=3;break;default:result=9;}
if(calls!=1 || labels!=12 || result!=2){return 1;}
switch(selector(ref calls)){}if(calls!=2){return 2;}return 0;}
""")

    def test_grouped_labels_fallthrough_and_default_in_source_order(self):
        self.check("fallthrough", """
int32 value(int32 n){int32 result=0;switch(n){case 1:case 2:result+=5;
default:result+=7;case 3:result+=11;break;}return result;}
int32 grouped(int32 n){switch(n){case 1:case 2:return 17;default:return 29;}}
int32 changed(int32 n){switch(n){case 1:n+=5;case 2:return n;default:return 29;}}
int32 main(){if(value(1)!=23 || value(2)!=23 || value(3)!=11 || value(9)!=18){return 1;}
if(grouped(1)!=17 || grouped(2)!=17 || grouped(9)!=29){return 2;}
if(changed(1)!=6 || changed(2)!=2 || changed(9)!=29){return 3;}return 0;}
""")

    def test_string_char_float_and_wide_values(self):
        self.check("typed-dispatch", """
int32 text(string value){switch(value){case "a":return 1;case "b\\n":return 2;case null:return 3;default:return 4;}}
int32 main(){string b="b"+"\\n";if(text(b)!=2 || text("a")!=1 || text(null)!=3 || text("c")!=4){return 1;}
char letter='B';int32 result=0;switch(letter){case 'A':result=1;break;case 'B':result=2;break;default:result=3;}
if(result!=2){return 2;}float64 f=2.5;switch(f){case 1.5:return 3;case 2.5:break;default:return 4;}
uint128 wide=18446744073709551617;switch(wide){case 18446744073709551616:return 5;
case 18446744073709551617:break;default:return 6;}return 0;}
""")

    def test_wide_selector_snapshot_survives_mutating_labels_at_all_levels(self):
        source = """
static int32 calls=0;
static int32 labels=0;
uint96 Select(ref uint96 value){calls++;return value;}
uint96 Change(ref uint96 value){labels++;value+=7;return value;}
int126 SelectSigned(ref int126 value){calls++;return value;}
int126 ChangeSigned(ref int126 value){labels++;value-=9;return value;}
uint128 ChangeField(ref uint128 value){labels++;value+=11;return value;}
class Ledger{public uint128 value;}
int32 main(){
uint96 narrow=((uint96)1<<85)+3;uint96 expected=narrow;int32 matched=0;
switch(Select(ref narrow)){case Change(ref narrow):return 1;
case expected:matched++;break;default:return 2;}
if(narrow!=expected+7 || calls!=1 || labels!=1 || matched!=1){return 3;}
int126 signed_value=-((int126)1<<103)-5;int126 signed_expected=signed_value;
switch(SelectSigned(ref signed_value)){case ChangeSigned(ref signed_value):return 4;
case signed_expected:matched++;break;default:return 5;}
if(signed_value!=signed_expected-9 || calls!=2 || labels!=2 || matched!=2){return 6;}
Ledger ledger=new Ledger();ledger.value=((uint128)1<<127)+17;uint128 field_expected=ledger.value;
switch(ledger.value){case ChangeField(ref ledger.value):return 7;
case field_expected:matched++;break;default:return 8;}
if(ledger.value!=field_expected+11 || labels!=3 || matched!=3){return 9;}delete ledger;
int128 full=-((int128)1<<110)-19;
switch(full){case 0:return 10;case -((int128)1<<110)-19:break;default:return 11;}
float64 fraction=2.5;switch(fraction){case 1.5:return 12;case 2.5:break;default:return 13;}
string text="b";switch(text){case "a":return 14;case "b":break;default:return 15;}
char letter='B';switch(letter){case 'A':return 16;case 'B':break;default:return 17;}
return calls==2 && labels==3 && matched==3 ? 0 : 18;}
"""
        for target in ('native', 'vm'):
            for level in range(4):
                with self.subTest(target=target, level=level):
                    work = Path(self.directory.name) / f"snapshot-{target}-O{level}"
                    work.mkdir()
                    path = work / 'program.krt'; path.write_text(source)
                    output = work / 'program'
                    command = [str(self.compiler), str(path), f'-O{level}', '--linker', str(ARKLINK), '-o', str(output)]
                    if target == 'vm':
                        command += ['target', 'vm']
                    result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    command = [str(self.compiler), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_nested_switch_break_and_continue_enclosing_loops(self):
        self.check("loop-transfers", """
int32 main(){int32 result=0;for(int32 i=0;i<5;i++){
switch(i){case 0:continue;case 1:switch(2){case 2:result+=10;break;default:return 1;}break;
case 2:continue;case 3:result+=30;break;default:result+=40;break;}result++;}
if(result!=83){return 2;}int32 n=0;while(n<3){n++;switch(n){case 1:continue;default:break;}result+=n;}
if(result!=88){return 3;}return 0;}
""")

    def test_break_continue_return_and_throw_run_the_right_finally(self):
        self.check("finally-transfers", """
int32 value(ref int32 count){switch(1){case 1:try{return 17;}finally{count++;}default:return 0;}}
int32 main(){int32 result=0;for(int32 i=0;i<3;i++){
try{switch(i){case 0:continue;case 1:try{break;}finally{result+=10;}break;default:break;}result++;}
finally{result+=100;}}if(result!=312){return 1;}
if(value(ref result)!=17 || result!=313){return 2;}
try{switch(2){case 2:try{throw 7;}finally{result+=10;}default:result=0;}}
catch(int32 e){result+=e;}if(result!=330){return 3;}return 0;}
""")

    def test_invalid_labels_and_continue_without_loop(self):
        cases = {
            "duplicate-integer": "int32 main(){switch(1){case 1:return 1;case 1:return 2;}return 0;}",
            "duplicate-expression": "int32 main(){switch(2){case 1+1:return 1;case 2:return 2;}return 0;}",
            "duplicate-string": 'int32 main(){switch("a"){case "a":return 1;case "\\a":return 2;}return 0;}',
            "duplicate-default": "int32 main(){switch(1){default:return 1;default:return 2;}}",
            "incompatible-label": 'int32 main(){switch(1){case "a":return 1;default:return 0;}}',
            "continue-without-loop": "int32 main(){switch(1){case 1:continue;default:break;}return 0;}",
            "return-after-break": "int32 f(int32 n){switch(n){case 1:break;return 7;default:return 3;}}int32 main(){return f(1);}",
            "fallthrough-without-return": "int32 f(int32 n){switch(n){case 1:n++;default:n++;}}int32 main(){return f(1);}",
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                work = Path(self.directory.name) / name
                work.mkdir()
                (work / "program.krt").write_text(source)
                result = subprocess.run([self.compiler], cwd=work,
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse((work / "stage1-probe.kro").exists())


if __name__ == "__main__":
    unittest.main()
