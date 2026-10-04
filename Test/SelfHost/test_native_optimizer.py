"""Check optimization semantics and generated code through the native CLI."""
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("SELFHOST_COMPILER", ROOT / "build/selfhost/stage2/program")).resolve()
LINKER = Path(os.environ.get("ARKLINK", ROOT / "build/ArkLink/ArkLink")).resolve()
if 'ARKLINK' not in os.environ and not LINKER.is_file():
    LINKER = ROOT / "ArkLink/build/ArkLink"


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), "build the native compiler and ArkLink first")
class NativeCompilerFixture(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="krt-native-optimizer-")
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        self.env = dict(os.environ, PATH="", KAIROTE_ROOT=str(ROOT))

    def command(self, *arguments):
        result = subprocess.run([str(COMPILER), "--linker", str(LINKER), *map(str, arguments)],
                                cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def compile(self, source, level):
        path = self.work / f"program-o{level}.krt"
        path.write_text(source, encoding="utf-8")
        object_path = path.with_suffix(".kro")
        self.command(path, f"-O{level}", "-c", "-o", object_path)
        binary = path.with_suffix("")
        self.command(object_path, "-o", binary)
        text_size = struct.unpack_from("<16I", object_path.read_bytes())[4]
        return binary, text_size

    def execute(self, source, levels=range(4), expected=0):
        sizes = {}
        for level in levels:
            with self.subTest(level=level):
                binary, sizes[level] = self.compile(source, level)
                result = subprocess.run([str(binary)], cwd=self.work, env=self.env,
                                        capture_output=True, timeout=15)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return sizes


class NativeOptimizerTests(NativeCompilerFixture):
    def test_constant_folding_preserves_value_and_reduces_machine_code(self):
        sizes = self.execute("""
int32 main(){
    if((7*9-5)==58){return ((3+4)*6)==42 ? 0 : 1;}
    return 2;
}
""")
        for level in (1, 2, 3):
            self.assertLess(sizes[level], sizes[0], f"O{level} did not reduce constant-expression code")

    def test_signed_width_unsigned_operations_and_float_values(self):
        self.execute("""
int32 main(){
    if((int8)255!=-1 || (int16)35536!=-30000){return 1;}
    if((int32)2147483647+1!=-2147483648){return 2;}
    if((int30)536870912!=-536870912){return 3;}
    if(((uint64)-1)/2!=9223372036854775807 || ((uint64)-1)%2!=1){return 4;}
    if(((uint64)-1>>3)!=2305843009213693951){return 5;}
    if((uint32)-1<=1 || (uint32)-1!=4294967295){return 6;}
    float64 a=1.25; float32 b=(float32)(a*2.0);
    if(b!=2.5 || (int32)(b*4.0)!=10){return 7;}
    return 0;
}
""")

    def test_direct_tail_recursion_preserves_parameter_cycles(self):
        source = """
int64 rotate(int32 depth,int64 a,int64 b,int64 c){
    if(depth==0){return a*100+b*10+c;}
    return rotate(depth-1,b,c,a);
}
int32 main(){return rotate(DEPTH,1,2,3)==312 ? 0 : 1;}
"""
        # Small calls check all levels. A million frames require the actual
        # O2/O3 loop transformation and simultaneous parameter replacement.
        self.execute(source.replace("DEPTH", "5"))
        self.execute(source.replace("DEPTH", "1000001"), levels=(2, 3))

    def test_tail_recursion_preserves_narrow_parameters_and_ref_writeback(self):
        self.execute("""
int8 narrow(int32 depth,int8 value){
    if(depth==0){return value;}
    return narrow(depth-1,(int8)(value+1));
}
int32 accumulate(int32 depth,ref int32 total){
    if(depth==0){return total;}
    total+=depth; return accumulate(depth-1,ref total);
}
int32 main(){
    int32 total=0;
    if(narrow(200,(int8)100)!=44){return 1;}
    if(accumulate(200,ref total)!=20100 || total!=20100){return 2;}
    return 0;
}
""")

    def test_local_ref_and_stack_addresses_retain_caller_frames(self):
        self.execute("""
int32 reference(int32 depth,ref int32 previous){
    int32 current=depth+100;
    if(depth==0){return previous;}
    return reference(depth-1,ref current);
}
int32 stack(int32 depth,int32* previous){
    unsafe(using krt.mem;){
        int32* current=stackalloc int32[1]; current[0]=depth+200;
        if(depth==0){return previous[0];}
        return stack(depth-1,current);
    }
}
int32 main(){
    int32 first=0;
    if(reference(16,ref first)!=101){return 1;}
    if(stack(16,(int32*)0)!=201){return 2;}
    return 0;
}
""")

    def test_recursive_exception_cleanup_runs_for_every_frame(self):
        self.execute("""
static int32 cleanup=0;
int32 chain(int32 depth){
    try{if(depth==0){throw 7;}return chain(depth-1);}
    finally{cleanup+=1;}
}
int32 main(){
    int32 caught=0;
    try{chain(32);}catch(int32 value){caught=value;}
    return caught==7 && cleanup==33 ? 0 : 1;
}
""")

    def test_division_by_zero_keeps_runtime_trap(self):
        self.execute("int32 main(){return 17/0;}", expected=-signal.SIGFPE)

    def test_o3_inlines_helpers_with_loops_and_multiple_returns(self):
        source = """
static int32 calls=0;
int32 argument(int32 value){calls=calls*10+value;return value;}
int32 choose(int32 value){if(value<0){return -value;}return value+1;}
int32 total(int32 count,int32 value){
    int32 result=0;
    while(count>0){result=result+value;count=count-1;}
    return result;
}
int32 main(){
    if(total(argument(3),argument(4))!=12 || calls!=34){return 1;}
    if(choose(-7)!=7 || choose(8)!=9){return 2;}
    return 0;
}
"""
        self.execute(source)
        path = self.work / "inline.krt"; path.write_text(source)
        outputs = []
        for level in (2, 3):
            output = self.work / f"inline-o{level}.ir"
            self.command(path, f"-O{level}", "target", "ir", "-o", output)
            outputs.append(output.read_text())
        self.assertLess(outputs[1].count(" = call "), outputs[0].count(" = call "))

    def test_inlined_helpers_preserve_dynamic_initialization_and_later_writes(self):
        source = """
static int32 initializations=0;
int32 choose(int32 value){if(value<0){return -value;}return value+1;}
int32 seed(){initializations+=1;return choose(40);}
static int32 retained=seed();
int32 main(){
    if(retained!=41 || initializations!=1){return 1;}
    retained=99;
    if(choose(-7)!=7 || choose(8)!=9){return 2;}
    return retained==99 && initializations==1 ? 0 : 3;
}
"""
        self.execute(source)
        path = self.work / "initialization.krt"; path.write_text(source)
        for level in (2, 3):
            with self.subTest(vm_level=level):
                output = self.work / f"initialization-o{level}.ebc"
                self.command(path, f"-O{level}", "target", "vm", "-o", output)
                result = subprocess.run([str(COMPILER), "run-vm", str(output)], cwd=self.work,
                                        env=self.env, capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
