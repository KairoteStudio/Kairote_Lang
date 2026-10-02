"""Names select formals without changing the evaluation order of actuals."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import Bootstrap, ROOT


class NamedArgumentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="krt-named-arguments-")
        cls.work = Path(cls.directory.name)
        cls.bootstrap = Bootstrap(cls.work)
        cls.compiler = (Path(os.environ["SELFHOST_COMPILER"]).resolve()
                        if "SELFHOST_COMPILER" in os.environ else cls.bootstrap.seed())
        cls.linker = ROOT / "build/ArkLink/ArkLink"

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def execute(self, name, source, optimization=2, vm=False):
        directory = self.work / name
        directory.mkdir()
        path = directory / "program.krt"
        path.write_text(source)
        binary = directory / "program"
        arguments = [str(self.compiler), str(path), f"-O{optimization}", "-o", str(binary),
                     "--linker", str(self.linker)]
        if vm:
            arguments += ["target", "vm"]
        result = subprocess.run(arguments, cwd=directory, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr + source)
        command = [str(self.compiler), "run-vm", str(binary)] if vm else [str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, f"{name}: branch {result.returncode}\n" + result.stdout + result.stderr)

    def reject(self, name, source, label):
        path = self.work / (name + ".krt")
        path.write_text(source)
        result = subprocess.run([str(self.compiler), str(path), "--check"],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("E_ARGUMENT_NAME", result.stderr)
        prefix = source[:source.rindex(label)]
        line = prefix.count("\n") + 1
        column = len(prefix.rsplit("\n", 1)[-1]) + 1
        self.assertIn(f":{line}:{column}:", result.stderr)

    def test_out_of_order_names_and_mixed_in_position_arguments(self):
        source = '''
int32 pack(int32 a,int32 b,int32 c){return a*100+b*10+c;}
int32 main(){if(pack(c:3,a:1,b:2)!=123){return 1;}
if(pack(1,c:3,b:2)!=123 || pack(a:1,2,c:3)!=123){return 2;}
return 0;}
'''
        for optimization in (0, 2, 3):
            self.execute(f"named-O{optimization}", source, optimization)

    def test_side_effects_follow_source_order_and_nested_calls_keep_their_maps(self):
        self.execute("evaluation-order", '''
static int32 trace=0;
int32 step(int32 value){trace=trace*10+value;return value;}
int32 pack(int32 a,int32 b){return a*10+b;}
int32 main(){int32 result=pack(b:step(2),a:step(4));
if(result!=42 || trace!=24){return 1;}
trace=0;result=pack(b:pack(b:step(1),a:step(2)),a:step(3));
if(result!=51 || trace!=123){return 2;}return 0;}
''')

    def test_named_refs_evaluate_addresses_once_and_support_both_ref_forms(self):
        self.execute("named-ref", '''
static int32 trace=0;
int32 index(int32 value){trace=trace*10+value;return value;}
void assign(ref int32 left,ref int32 right){left=17;right=42;}
int32 main(){int32[] values=[0,0];
assign(right:ref values[index(1)],ref left:values[index(0)]);
if(trace!=10 || values[0]!=17 || values[1]!=42){return 1;}
delete values;return 0;}
''')

    def test_members_static_calls_receiver_order_and_member_actual_names(self):
        self.execute("named-members", '''
static int32 trace=0;
class Item{public int32 value;public int32 Pack(int32 a,int32 b){return value+a*10+b;}
public static int32 Sum(int32 a,int32 b){return a*10+b;}}
Item receiver(Item item){trace=trace*10+1;return item;}
int32 step(int32 value){trace=trace*10+value;return value;}
int32 main(){Item item=new Item();item.value=5;
if(receiver(item).Pack(b:step(2),a:step(4))!=47 || trace!=124){return 1;}
if(Item.Sum(b:item.value,a:3)!=35){return 2;}delete item;return 0;}
''')

    def test_virtual_and_interface_parameter_names_use_the_static_signature(self):
        self.execute("named-dispatch", '''
static int32 trace=0;
int32 step(int32 value){trace=trace*10+value;return value;}
interface IValue{int32 Pack(int32 left,int32 right);}
class Base{public virtual int32 Pack(int32 left,int32 right){return 1;}}
class Child:Base,IValue{public override int32 Pack(int32 x,int32 y){return x*10+y;}}
int32 main(){Child child=new Child();Base value=child;IValue view=child;
if(value.Pack(right:step(2),left:step(4))!=42 || trace!=24){return 1;}
if(view.Pack(right:3,left:5)!=53){return 2;}delete child;return 0;}
''')

    def test_generics_nested_types_refs_explicit_arguments_and_clone_metadata(self):
        self.execute("named-generics", '''
class Box<T>{public T value;public Box(T item){value=item;}}
T choose<T>(Box<T> box,T fallback){return box.value;}
void assign<T>(ref T destination,T value){destination=value;}
int32 pack(int32 a,int32 b){return a*10+b;}
T wrapper<T>(T value){if(pack(b:2,a:4)!=42){throw 99;}return value;}
int32 main(){Box<string> box=new Box<string>(item:"ok");
if(choose(fallback:"bad",box:box)!="ok" || wrapper(17)!=17 || wrapper("ok")!="ok"){return 1;}
uint66 number=0;assign<uint66>(value:(uint66)-1,destination:ref number);
if((uint128)number!=73786976294838206463){return 2;}delete box;return 0;}
''')

    def test_generic_extension_receiver_and_named_argument_inference(self):
        self.execute("named-extension", '''
class Box<T>{public T value;public Box(T item){value=item;}}
T Extract<T>(Box<T> receiver,T fallback,int32 marker){if(marker!=7){throw 99;}return receiver.value;}
int32 main(){Box<string> box=new Box<string>(item:"ok");
if(box.Extract(marker:7,fallback:"bad")!="ok"){return 1;}delete box;return 0;}
''')

    def test_constructors_and_base_initializers_preserve_argument_evaluation_order(self):
        self.execute("named-constructors", '''
static int32 trace=0;
int32 step(int32 value){trace=trace*10+value;return value;}
class Base{public int32 value;public Base(int32 left,int32 right){value=left*10+right;}}
class Child:Base{public function Child(int32 first,int32 second):base(right:step(second),left:step(first)){}}
int32 main(){Child item=new Child(second:step(2),first:step(4));
if(item.value!=42 || trace!=2424){return 1;}delete item;return 0;}
''')

    def test_overloads_and_expected_array_float_and_wide_conversions(self):
        self.execute("named-conversions", '''
int32 select(int32 left,int32 right){return left+right;}
int32 select(string text,int32 count){return text.Length+count;}
int32 calculate(byte[] bytes,float64 fraction,uint66 wide){
if(bytes[0]!=255 || fraction!=2.5 || (uint128)wide!=73786976294838206463){return 1;}return 0;}
int32 main(){if(select(count:4,text:"abc")!=7){return 1;}
return calculate(wide:(uint128)-1,bytes:[255],fraction:2.5);}
''')

    def test_known_function_address_names_and_anonymous_shapes_have_clear_contracts(self):
        self.execute("known-address", '''
int32 pack(int32 a,int32 b){return a*10+b;}
int32 main(){if((&pack)(b:2,a:4)!=42){return 1;}var pointer=&pack;
if(pointer(b:3,a:5)!=53){return 2;}return 0;}
''')
        self.reject("anonymous-pointer", '''
int32 pack(int32 a,int32 b){return a*10+b;}
int32 main(){fn(int32,int32)->int32 pointer=&pack;return pointer(b:2,a:4);}
''', "b:")

    def test_unknown_duplicate_and_intrinsic_names_are_diagnostics_at_the_label(self):
        self.reject("unknown", "int32 pack(int32 a,int32 b){return a+b;}\nint32 main(){return pack(missing:2,a:4);}", "missing:")
        self.reject("duplicate", "int32 pack(int32 a,int32 b){return a+b;}\nint32 main(){return pack(a:2,a:4);}", "a:")
        self.reject("syscall", "int32 main(){return (int32)syscall(number:39);}", "number:")
        self.reject("constructor-unknown", "class Item{public Item(int32 value){}}\nint32 main(){Item item=new Item(missing:2);return 0;}", "missing:")

    def test_128_named_parameters_cross_register_and_stack_boundaries(self):
        parameters = ",".join(f"int32 p{i}" for i in range(128))
        arguments = ",".join(f"p{i}:{i + 1}" for i in reversed(range(128)))
        method_parameters = ",".join(f"int32 p{i}" for i in range(127))
        method_arguments = ",".join(f"p{i}:{i + 1}" for i in reversed(range(127)))
        source = f"""
int32 check({parameters}){{if(p0!=1 || p6!=7 || p63!=64 || p127!=128){{return 1;}}return 0;}}
class Item{{public int32 Check({method_parameters}){{if(p0!=1 || p5!=6 || p63!=64 || p126!=127){{return 1;}}return 0;}}}}
int32 main(){{Item item=new Item();if(check({arguments})!=0 || item.Check({method_arguments})!=0){{return 1;}}delete item;return 0;}}
"""
        self.execute("named-128", source)
        self.execute("named-128-vm", source, vm=True)

    def test_named_calls_execute_as_real_vm_bytecode(self):
        self.execute("named-vm", '''
static int32 trace=0;
int32 step(int32 value){trace=trace*10+value;return value;}
T choose<T>(T first,T second){return first;}
class Base{public virtual int32 Pack(int32 a,int32 b){return 1;}}
class Child:Base{public override int32 Pack(int32 x,int32 y){return x*10+y;}}
void assign(ref uint66 destination,uint66 value){destination=value;}
int32 main(){Base item=new Child();
if(item.Pack(b:step(2),a:step(4))!=42 || trace!=24){return 1;}
if(choose(second:"bad",first:"ok")!="ok"){return 2;}
uint66 number=0;assign(value:(uint66)-1,destination:ref number);
if((uint128)number!=73786976294838206463){return 3;}
delete item;return 0;}
''', vm=True)
