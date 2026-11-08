"""Wide scalar value storage, snapshots, sret and bounded temporary lifetime."""
import os
import select
import subprocess
import time

from Test.SelfHost import test_native_optimizer as native


CASES = {
    'loop-values-and-class-fields': '''
using System.Console;
class Carrier{public uint128 value;}
uint128 Make(int32 value){return ((uint128)1<<110)+(uint128)value;}
void Change(ref uint128 value){value+=7;}
int32 main(){Carrier carrier=new Carrier();uint128 previous=0;
for(int32 i=0;i<1000;i++){uint128 current=Make(i);if(i>0&&previous!=Make(i-1)){return 1;}
carrier.value=current;uint128 saved=carrier.value;Change(ref carrier.value);
if(saved!=current||carrier.value!=current+7){return 2;}previous=current;}
uint128 final=carrier.value;delete carrier;Console.WriteLine("wide value storage complete");
return previous==Make(999)&&final==Make(999)+7?0:3;}
''',
    'snapshots-preserve-left-to-right-evaluation': '''
int32 main(){uint128 value=((uint128)1<<100)+17;uint128 original=value;
uint128 sum=value+value++;if(sum!=original*2||value!=original+1){return 1;}
uint128 difference=value++-value++;if(difference!=(uint128)-1||value!=original+3){return 2;}
value+=value++;if(value!=(original+3)*2){return 3;}
uint128 old=value++;if(value!=old+1){return 4;}return 0;}
''',
    'wide-sret-functions-callbacks-interfaces-and-stack-arguments': '''
interface Readable{uint128 Read();}
class Counter:Readable{public uint128 value;public uint128 Read(){value++;return value;}}
uint128 Many(uint128 initial,int32 a,int32 b,int32 c,int32 d,int32 e,int32 f,int32 g){return initial+a+b+c+d+e+f+g;}
uint128 Repeat(uint128 value,int32 count){if(count==0){return value;}return Repeat(value+1,count-1);}
int32 main(){Counter counter=new Counter();counter.value=(uint128)1<<105;Readable reader=counter;
uint128 first=reader.Read();uint128 second=reader.Read();if(first!=((uint128)1<<105)+1||second!=first+1){return 1;}
fn(uint128)->uint128 callback=function(uint128 value)=>value+42;
uint128 result=callback(first);if(result!=first+42||Many(result,1,2,3,4,5,6,7)!=result+28||Repeat(result,100)!=result+100){return 2;}
delete callback;delete counter;return 0;}
''',
    'wide-captured-cells-boxes-generators-and-exceptions': '''
using System.Collections.Generic;
fn(uint128)->uint128 Counter(uint128 initial){return function(uint128 amount)=>{initial+=amount;return initial;};}
IEnumerable<uint128> Values(uint128 initial){for(int32 i=0;i<3;i++){yield return initial+i;}}
uint128 Raising(uint128 value){throw value+5;}
int32 main(){uint128 initial=(uint128)1<<112;var callback=Counter(initial);
uint128 first=callback(7);uint128 second=callback(11);if(first!=initial+7||second!=initial+18){return 1;}
object box=first;uint128 copied=(uint128)box;delete box;if(copied!=first){return 2;}
var sequence=Values(second);var cursor=sequence.GetEnumerator();delete sequence;
uint128 saved=0;for(int32 i=0;i<3;i++){if(!cursor.MoveNext()||cursor.Current()!=second+i){return 3;}
if(i==0){saved=cursor.Current();}}cursor.Dispose();if(saved!=second){return 4;}
try{Raising(first);}catch(uint128 fault){if(fault!=first+5){return 5;}}
delete callback;return 0;}
''',
}


PRESSURE = '''
class Carrier{public uint128 value;}
uint128 Step(uint128 value,int32 amount){return (value+(uint128)amount)*3/3;}
int32 Check(int32 round){uint128 original=((uint128)1<<110)+round;uint128 value=original;
Carrier carrier=new Carrier();carrier.value=value;
for(int32 i=0;i<8;i++){value=Step(value,i);uint128 before=carrier.value;carrier.value=value;
if(i>0&&before!=value-(uint128)i){delete carrier;return 1;}}
uint128 saved=carrier.value;delete carrier;return saved==original+28?0:2;}
void Mark(string message,int32 length){syscall(1,1,(int64)message,length,0,0,0);}
int32 Gate(){unsafe(using krt.mem;){byte token[1];int64 read=syscall(0,0,(int64)&token[0],1,0,0,0);delete token;return read==1?1:0;}}
int32 main(){for(int32 i=0;i<100;i++){if(Check(i)!=0){return 1;}}
Mark("READY\\n",6);if(Gate()!=1){return 2;}
for(int32 i=0;i<25000;i++){if(Check(i)!=0){return 3;}}
Mark("DONE\\n",5);if(Gate()!=1){return 4;}return 0;}
'''


class WideLifetimeTests(native.NativeCompilerFixture):
    def verify(self, source):
        for level in range(4):
            with self.subTest(backend='native', optimization=level):
                binary, _ = self.compile(source, level)
                result = subprocess.run([str(binary)], cwd=self.work, env=self.env,
                                        capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with self.subTest(backend='vm', optimization=level):
                path = self.work / f'wide-o{level}.krt'
                path.write_text(source)
                artifact = path.with_suffix('.ebc')
                self.command(path, f'-O{level}', '--target', 'vm', '-o', artifact)
                self.command('run-vm', artifact)

    def test_loop_wide_values_and_class_fields_hold_independent_values(self):
        self.verify(CASES['loop-values-and-class-fields'])

    def test_wide_reads_snapshot_before_later_operand_mutation(self):
        self.verify(CASES['snapshots-preserve-left-to-right-evaluation'])

    def test_wide_result_sret_uses_functions_callbacks_interfaces_and_stack_arguments(self):
        self.verify(CASES['wide-sret-functions-callbacks-interfaces-and-stack-arguments'])

    def test_wide_values_survive_closure_box_generator_and_exception_lifetimes(self):
        self.verify(CASES['wide-captured-cells-boxes-generators-and-exceptions'])

    def test_twenty_five_thousand_wide_operations_have_bounded_temporary_storage(self):
        def marker(process, expected):
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                self.assertIsNone(process.poll(), (process.returncode, process.stderr.read() if process.poll() is not None else b''))
                readable, _, _ = select.select([process.stdout], [], [], min(1, max(0, deadline-time.monotonic())))
                if readable:
                    self.assertEqual(process.stdout.readline().strip(), expected)
                    return
            self.fail(f'{expected!r} marker timeout')

        def memory(process):
            fields = {}
            with open(f'/proc/{process.pid}/status') as stream:
                for line in stream:
                    for name in ('VmRSS', 'VmSize'):
                        if line.startswith(name + ':'):
                            fields[name] = int(line.split()[1])*1024
            self.assertEqual(set(fields), {'VmRSS', 'VmSize'})
            return fields

        for backend in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(backend=backend, optimization=level):
                    if backend == 'native':
                        binary, _ = self.compile(PRESSURE, level)
                        argv = [str(binary)]
                    else:
                        path = self.work / f'wide-pressure-o{level}.krt'
                        path.write_text(PRESSURE)
                        artifact = path.with_suffix('.ebc')
                        self.command(path, f'-O{level}', '--target', 'vm', '-o', artifact)
                        argv = [str(native.COMPILER), '--linker', str(native.LINKER), 'run-vm', str(artifact)]
                    process = subprocess.Popen(argv, cwd=self.work, env=self.env,
                                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    try:
                        marker(process, b'READY'); before = memory(process)
                        process.stdin.write(b'x'); process.stdin.flush()
                        marker(process, b'DONE'); after = memory(process)
                        print(f'wide memory target={backend} O{level} before={before} after={after}')
                        self.assertLessEqual(after['VmRSS']-before['VmRSS'], 16*1024*1024, (before, after))
                        self.assertLessEqual(after['VmSize']-before['VmSize'], 16*1024*1024, (before, after))
                        stdout, stderr = process.communicate(b'x', timeout=15)
                        self.assertEqual(process.returncode, 0, stdout + stderr)
                    finally:
                        if process.poll() is None:
                            process.kill(); process.communicate(timeout=15)
