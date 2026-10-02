"""Execute restored scopes, type semantics, diagnostics and parser statements."""
import unittest
from Test.SelfHost import test_native_driver as driver


@unittest.skipUnless(driver.COMPILER.is_file() and driver.LINKER.is_file(), "build native compiler first")
class SemanticRestorationTests(unittest.TestCase):
    setUp = driver.NativeDriverTests.setUp
    write = driver.NativeDriverTests.write
    command = driver.NativeDriverTests.command
    execute = driver.NativeDriverTests.execute

    def run_source(self, source, expected=0):
        source = self.write("main.krt", source)
        output = self.work / "program"
        self.command(source, "-o", output)
        self.execute(output, expected)
        return output

    def test_using_frames_inherit_and_shadow_aliases(self):
        self.run_source("""namespace One {class Item{public int32 n=17;}}
namespace Two {class Item{public int32 n=42;}}
namespace App {using Alias=One.Item; namespace Child {
using Alias=Two.Item; int32 Read(){Alias item=new Alias();return item.n;}}}
int32 main(){return App.Child.Read();}
""", 42)

    def test_body_using_loads_module_and_does_not_leak(self):
        self.write("Helper.krt", "namespace Imported; class Item{public int32 n=42;}")
        self.run_source("int32 main(){{using Helper;Item item=new Item();return item.n;}}", 42)
        source = self.write("bad.krt", "int32 main(){{using Helper;Item item=new Item();}Item other=new Item();return 0;}")
        self.assertIn("E_LOWER", self.command(source, "--check", expected=1).stderr)

    def test_using_does_not_expose_unrelated_loaded_file(self):
        self.write("A.krt", "namespace HiddenA;class First{}")
        unrelated = self.write("B.krt", "namespace HiddenB;class Second{}")
        source = self.write("bad.krt", "using A;int32 main(){Second value=new Second();return 0;}")
        self.assertIn("E_LOWER", self.command(source, unrelated, "--check", expected=1).stderr)

    def test_module_alias_follows_its_loaded_file(self):
        self.write("Module.krt", "namespace Different.Name;class Item{public int32 n=42;}")
        self.run_source("using Lib=Module;int32 main(){Lib.Item item=new Lib.Item();return item.n;}",42)

    def test_inheritance_cycle_reports_actual_path(self):
        source = self.write("cycle.krt", "class A:B{}class B:C{}class C:A{}int32 main(){return 0;}")
        result = self.command(source, "--check", expected=1)
        self.assertIn("E_INHERITANCE", result.stderr)
        self.assertIn("A -> B -> C -> A", result.stderr)

    def test_diagnostics_collect_independent_functions(self):
        source = self.write("bad.krt", "int32 first(){return missingOne;}\nint32 second(){return missingTwo;}\nint32 main(){return 0;}")
        result = self.command(source, "--check", expected=1)
        self.assertEqual(result.stderr.count("E_LOWER"), 2, result.stderr)
        self.assertIn(f"{source}:1:", result.stderr)
        self.assertIn(f"{source}:2:", result.stderr)

    def test_named_array_declarators_inference_and_type_comments(self):
        self.run_source("""
int32 choose(char value){return 17;}int32 choose(uint8 value){return 42;}
int32 main(){int32 /* misleading * [] */ values[3];values[1]=17;
int32 other[]=[25];auto total=values[1]+other[0];var n:int32=total;
usize size=3;char c='A';uint8 b=65;point{n=n+choose(b)-42;}
if(choose(c)!=17||size!=3){return 1;}return n;}
""", 42)
        source = self.write("readonly.krt", "int32 main(){let n=17;n=42;return n;}")
        self.assertIn("E_LOWER", self.command(source, "--check", expected=1).stderr)

    def test_type_tests_runtime_identity_pattern_and_null(self):
        self.run_source("""
interface IValue {int32 Read();}class Root{public int32 n=17;}
class Child:Root,IValue{public int32 Read(){return 42;}}class Other{}
int32 main(){Root value=new Child();if(!(value is Root)||!(value is IValue)||value is Other){return 1;}
Root empty=null;if(empty is Child){return 2;}
if(!(value is Child typed && typed.Read()==42)){return 4;}
if(value is Child child){return child.Read();}return 3;}
""", 42)

    def test_prefix_updates_once_and_returns_stored_precision(self):
        self.run_source("""
int32 main(){int32[] values=[17];int32 index=0;int32 n=++values[index++];
int8 narrow=127;int8 wrapped=++narrow;float32 f=1.5;float32 updated=--f;
uint128 wide=41;uint128 big=++wide;
unsafe(using krt.mem;){int32* p=stackalloc int32[2];p[1]=42;int32* q=++p;if(q[0]!=42){return 1;}}
if(n!=18||values[0]!=18||index!=1||narrow!=-128||wrapped!=-128||f!=0.5||updated!=0.5||big!=42||wide!=42){return 2;}
return (int32)big;}
""", 42)

    def test_function_style_casts_preserve_precision_and_evaluate_once(self):
        self.run_source("""
int32 main(){int32 n=41;int32 old=int32(n++);
if(int8(258)!=2||old!=41||n!=42||int32(float32(42.75))!=42){return 1;}
uint66 wide=uint66((uint128)1<<66);if(wide!=0){return 2;}
return int32(float64(n));}
""", 42)

    def test_stackalloc_retains_complete_pointer_element_type(self):
        self.run_source("""
int32 main(){unsafe(using krt.mem;){int32 value=42;
int32** values=stackalloc int32*[2];values[1]=&value;
int32*** indirect=stackalloc int32**[1];indirect[0]=values;
return *indirect[0][1];}}
""", 42)

    def test_print_scalars_wide_floats_and_order(self):
        output = self.run_source("""
int32 next(ref int32 n){n++;return n;}
int32 main(){int32 n=0;print("text",'A',true,false,(int64)-9223372036854775808," ",
(uint64)-1," ",(uint128)-1," ",-1.25," ",(float32)1.5," ",'A'+1," ",next(ref n),next(ref n));return n;}
""", 2)
        result = driver.subprocess.run([str(output)], cwd=self.work, env=self.env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.stdout, "textATrueFalse-9223372036854775808 18446744073709551615 340282366920938463463374607431768211455 -1.25 1.5 66 12\n")

    def test_print_helpers_stay_local_in_library_objects(self):
        first = self.write("one.krt", "void first(){print(17);}")
        second = self.write("two.krt", "void second(){print(42);}")
        one, two = self.work / "one.kro", self.work / "two.kro"
        self.command(first, "-c", "-o", one)
        self.command(second, "-c", "-o", two)
        source = self.write("main.krt", "extern void first();extern void second();int32 main(){first();second();return 0;}")
        output = self.work / "linked"
        self.command(source, one, two, "-o", output)
        result = driver.subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "17\n42\n")


if __name__ == "__main__":
    unittest.main()
