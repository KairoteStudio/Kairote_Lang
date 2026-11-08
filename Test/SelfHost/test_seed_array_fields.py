"""Stage 0 must use the declared element width for class array fields."""
import subprocess
import tempfile
from pathlib import Path
import unittest

from Test.SelfHost.Bootstrap import KRTC


@unittest.skipUnless(KRTC.is_file(), "build the seed compiler first")
class SeedArrayFieldTests(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory(prefix="kairote-seed-array-fields-") as directory:
            work = Path(directory)
            code, program = work / "program.krt", work / "program"
            code.write_text(source)
            for optimization in ("-O0", "-O2"):
                with self.subTest(optimization=optimization):
                    compiled = subprocess.run([str(KRTC), optimization, str(code), "output", str(program)],
                                              cwd=work, capture_output=True, text=True, timeout=30)
                    self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                    executed = subprocess.run([str(program)], capture_output=True, text=True, timeout=10)
                    self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def test_packed_member_arrays_and_scalar_member_arguments(self):
        self.compile_run("""
class Buffer {
    public byte[] bytes;
    public int16[] halves;
    public int32[] words;
    public float32[] samples;
    public int32 scalar;
}
class Holder { public Buffer data; }
int32 add(int32 value) { return value+1; }
int32 main() {
    Holder holder=new Holder(); holder.data=new Buffer();
    holder.data.bytes=new byte[3]; holder.data.halves=new int16[3];
    holder.data.words=new int32[3]; holder.data.samples=new float32[3];
    holder.data.scalar=41;
    holder.data.bytes[0]=7; holder.data.bytes[1]=19;
    holder.data.halves[0]=-17; holder.data.halves[1]=1234;
    holder.data.words[0]=-31; holder.data.words[1]=7654321;
    holder.data.samples[0]=1.5; holder.data.samples[1]=2.25;
    if(add(holder.data.scalar)!=42){return 1;}
    unsafe(using krt.mem;) {
        byte* bytes=(byte*)holder.data.bytes;
        int16* halves=(int16*)holder.data.halves;
        int32* words=(int32*)holder.data.words;
        if(bytes[0]!=7 || bytes[1]!=19){return 2;}
        if(halves[0]!=-17 || halves[1]!=1234){return 3;}
        if(words[0]!=-31 || words[1]!=7654321){return 4;}
        words[2]=42;
    }
    if(holder.data.words[2]!=42){return 6;}
    if(holder.data.samples[0]!=1.5 || holder.data.samples[1]!=2.25){return 5;}
    return 0;
}
""")

    def test_large_int32_member_array_uses_its_allocated_storage(self):
        self.compile_run("""
class Instructions { public int32[] ops; }
void fill(Instructions instructions) {
    int32 index=0;
    while(index<140000) { instructions.ops[index]=index+17; index++; }
}
int32 main() {
    Instructions instructions=new Instructions();
    instructions.ops=new int32[262144]; fill(instructions);
    unsafe(using krt.mem;) {
        int32* words=(int32*)instructions.ops;
        if(words[0]!=17 || words[131072]!=131089 || words[139999]!=140016){return 1;}
    }
    if(instructions.ops[131072]!=131089 || instructions.ops[139999]!=140016){return 2;}
    return 0;
}
""")

    def test_class_reference_member_arrays_preserve_full_pointer_values(self):
        self.compile_run("""
class Node { public int32 value; public Node next; }
class Registry { public Node[] nodes; public string[] names; }
Node Read(Registry registry,int32 index){return registry.nodes[index];}
int32 main(){
    Registry registry=new Registry();registry.nodes=new Node[3];registry.names=new string[3];
    Node node=new Node();node.value=42;registry.nodes[1]=node;registry.nodes[2]=Read(registry,1);
    registry.names[1]="class reference";
    if(registry.nodes[1]!=node || registry.nodes[2].value!=42){return 1;}
    registry.nodes[0]=registry.nodes[2];
    if(registry.nodes[0].value!=42){return 2;}
    if(registry.names[1][0]!='c'){return 3;}
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
