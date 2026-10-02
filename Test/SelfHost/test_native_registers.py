"""Runtime and machine-code evidence for native local register allocation."""
import re
import shutil
import subprocess
import unittest
from pathlib import Path

from Test.SelfHost.test_native_optimizer import NativeCompilerFixture


class NativeRegisterTests(NativeCompilerFixture):
    def disassemble(self, binary):
        tool = shutil.which("objdump")
        if tool is None and Path("/usr/bin/objdump").is_file():
            tool = "/usr/bin/objdump"
        if tool is None:
            self.skipTest("objdump is needed to inspect generated machine code")
        result = subprocess.run([tool, "-d", str(binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_loop_memory_accesses_reduce_with_real_register_instructions(self):
        expected = sum((3+i)*(100-i)+(5+2*i) for i in range(100))
        source = """
int64 score(int32 count){
    int64 a=3; int64 b=5; int64 result=0;
    while(count>0){result=result+a*count+b;a=a+1;b=b+2;count=count-1;}
    return result;
}
int32 main(){return score(100)==EXPECTED ? 0 : 1;}
""".replace("EXPECTED", str(expected))
        self.execute(source)
        disassembly = []
        for level in (0, 1):
            binary, _ = self.compile(source, level)
            disassembly.append(self.disassemble(binary))
        accesses = [len(re.findall(r"-0x[0-9a-f]+\(%rbp\)", text)) for text in disassembly]
        self.assertLess(accesses[1], accesses[0], accesses)
        self.assertRegex(disassembly[1], r"%r1[23]")
        # Four simultaneous loop values exceed the two available registers.
        self.assertRegex(disassembly[1], r"mov\s+-0x[0-9a-f]+\(%rbp\),%rax")

    def test_loop_liveness_branch_joins_and_zero_initialization(self):
        self.execute("""
int64 choose(int32 count){
    int64 carry=7; int64 result;
    while(count>0){
        result=result+carry;
        int64 temporary=count*3;
        if(count%2==0){result=result+temporary;}else{result=result-temporary;}
        count=count-1;
    }
    return result;
}
int32 main(){return choose(20)==170 && choose(0)==0 ? 0 : 1;}
""")

    def test_expression_temporaries_reduce_stack_traffic_and_spill_in_order(self):
        source = """
int64 expression(int64 a,int64 b,int64 c,int64 d,int64 e){
    return (a+b)*(c+d)-(a-b)*(d+e)+((a&c)^(b|d));
}
int32 main(){return expression(13,7,11,5,3)==286 ? 0 : 1;}
"""
        self.execute(source)
        disassembly = []
        for level in (0, 1):
            binary, _ = self.compile(source, level)
            disassembly.append(self.disassemble(binary))
        traffic = [len(re.findall(r"\b(?:push|pop)\s", text)) for text in disassembly]
        self.assertLess(traffic[1], traffic[0], traffic)
        self.assertRegex(disassembly[1], r"%r1[23]")

    def test_temporaries_cross_short_circuit_branches_and_call_boundaries(self):
        self.execute("""
static int32 visits=0;
int64 value(int64 input){visits=visits+1;return input+2;}
int64 calculate(int64 input){
    int64 result=(input+1)*(value(input+2)+value(input+3));
    if((input<0 && value(99)>0) || (input>0 && value(input)==7)){result=result+10;}
    return result+(input>0 ? value(1)+value(2) : value(3));
}
int32 main(){return calculate(5)==131 && visits==5 ? 0 : 1;}
""")

    def test_stack_spills_many_arguments_calls_and_recursive_abi(self):
        self.execute("""
int64 sum(int64 a,int64 b,int64 c,int64 d,int64 e,int64 f,int64 g,int64 h,int64 i){
    return a+b+c+d+e+f+g+h+i;
}
int64 recursive(int32 count){if(count==0){return 1;}return count+recursive(count-1);}
int32 main(){
    int64 a=41;int64 b=73;int64 c=99;int64 d=123;
    if(sum(a,b,c,d,5,6,7,8,9)!=371){return 1;}
    if(recursive(20)!=211 || a!=41 || b!=73 || c!=99 || d!=123){return 2;}
    return 0;
}
""")

    def test_escaped_refs_keep_writable_stack_storage(self):
        self.execute("""
void change(ref int64 value){value=value+9;}
int64 pinned(int64 input){
    int64 value=input;int64 other=input+3;
    change(ref value);
    unsafe(using krt.mem;){int64* address=&value;address[0]=address[0]+4;}
    return value+other;
}
int32 main(){return pinned(10)==36 ? 0 : 1;}
""")

    def test_addressable_and_exception_locals_default_to_zero(self):
        self.execute("""
void update(ref int64 value){value=value+3;}
int64 handler(){int64 result;try{result=result+5;}finally{result=result+7;}return result;}
int32 main(){int64 local;update(ref local);return local==3 && handler()==12 ? 0 : 1;}
""")

    def test_float_narrow_and_wide_operations_preserve_live_registers(self):
        self.execute("""
uint128 quotient(uint128 a,uint128 b){return a/b;}
int32 main(){
    int64 first=123; int64 second=456;
    int8 narrow=(int8)249;uint32 unsigned_value=4000000000;
    float64 real=1.25;float32 small=(float32)2.0;
    uint128 wide=(uint128)18446744073709551642;
    if(quotient(wide,(uint128)2)!=(uint128)9223372036854775821){return 1;}
    if(wide/(uint128)3!=(uint128)6148914691236517214 || wide%(uint128)3!=0){return 2;}
    if(real*small!=2.5 || narrow!=-7 || unsigned_value!=4000000000){return 3;}
    if(first+second!=579 || first*second!=56088){return 4;}
    return 0;
}
""")

    def test_throwing_callee_and_handler_return_preserve_normal_caller(self):
        self.execute("""
static int32 cleanup=0;
int64 failure(int64 input){
    int64 first=input+17;int64 second=input+23;
    throw first+second;
}
int64 handler(){
    try{return failure(9);}catch(int64 value){return value;}
    finally{cleanup=cleanup+1;}
}
int32 main(){
    int64 first=101;int64 second=203;
    if(handler()!=58 || cleanup!=1){return 1;}
    if(first+second!=304 || first*second!=20503){return 2;}
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
