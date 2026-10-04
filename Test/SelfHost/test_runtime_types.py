"""Exact runtime identities, checked references and typed exception lifetimes."""
import subprocess
import os
import re
import select
import time
from pathlib import Path

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


PACKET_LIFETIMES = '''
struct Failure{uint128 id;byte payload[1024];}
Failure Make(int32 value){Failure result=default(Failure);result.id=((uint128)1<<110)+value;
 result.payload[1023]=(byte)value;return result;}
int32 ReturnFromFinally(int32 value){try{throw Make(value);}finally{return value+1;}}
int32 Check(int32 value){int32 checks=0;
 try{try{throw Make(value);}finally{throw 41;}}catch(int32 code){if(code!=41){return 1;}checks++;}
 try{try{throw Make(value);}catch(Failure first){throw Make(value+1);}
 finally{throw 42;}}catch(int32 code){if(code!=42){return 2;}checks++;}
 try{try{throw Make(value);}finally{throw Make(value+2);}}
 catch(Failure second){if(second.id!=((uint128)1<<110)+value+2||second.payload[1023]!=(byte)(value+2)){return 3;}checks++;}
 if(ReturnFromFinally(value)!=value+1){return 4;}checks++;
 object borrowed=Make(value);try{try{throw borrowed;}finally{throw 43;}}
 catch(int32 code){if(code!=43){return 5;}}Failure preserved=(Failure)borrowed;
 delete borrowed;if(preserved.id!=((uint128)1<<110)+value){return 6;}checks++;
 for(int32 turn=0;turn<2;turn++){try{throw Make(value);}finally{if(turn==0){continue;}break;}}
 checks++;return checks==6?0:7;}
void Mark(string message,int32 length){syscall(1,1,(int64)message,length,0,0,0);}
int32 Gate(){unsafe(using krt.mem;){byte token[1];int32 result=syscall(0,0,(int64)&token[0],1,0,0,0)==1?1:0;delete token;return result;}}
int32 main(){for(int32 warm=0;warm<16;warm++){int32 result=Check(warm);if(result!=0){return result;}}
 Mark("READY\\n",6);if(Gate()!=1){return 201;}
 for(int32 i=0;i<25000;i++){int32 result=Check(i);if(result!=0){return result;}}
 Mark("DONE\\n",5);if(Gate()!=1){return 202;}return 0;}
'''


class RuntimeTypeTests(NativeCompilerFixture):
    def verify(self, source):
        for level in range(4):
            with self.subTest(backend='native', optimization=level):
                binary, _ = self.compile(source, level)
                result = subprocess.run([str(binary)], cwd=self.work, env=self.env,
                                        capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with self.subTest(backend='vm', optimization=level):
                path = self.work / f'runtime-o{level}.krt'
                path.write_text(source)
                artifact = path.with_suffix('.ebc')
                self.command(path, f'-O{level}', '--target', 'vm', '-o', artifact)
                self.command('run-vm', artifact)

    def test_many_nullable_route_guards_preserve_reference_and_evaluation_rules(self):
        guards = '\n'.join(
            f'if(Lookup(route,{index})!=null&&face!=null&&empty==null&&zero!=null){{accepted++;}}'
            for index in range(256))
        self.verify('''
interface Route{int32 Handle();}
class Endpoint:Route{public int32 Handle(){return 42;}}
static int32 lookups=0;
Endpoint Lookup(Endpoint route,int32 slot){lookups++;return slot%2==0?route:null;}
int32 Validate(Endpoint route,Route face,object empty,object zero){int32 accepted=0;
''' + guards + '''
return accepted==128&&lookups==256&&face.Handle()==42?0:1;}
int32 main(){Endpoint route=new Endpoint();Route face=route;
object empty=default(object);object zero=0;
int32 result=Validate(route,face,empty,zero);delete zero;delete route;return result;}
''')

    def test_pending_packets_replaced_by_finally_release_owned_storage(self):
        path = self.work / 'pending-packet-lifetimes.krt'
        path.write_text(PACKET_LIFETIMES)

        def marker(process, expected):
            result = b''
            deadline = time.monotonic() + 120
            while b'\n' not in result and time.monotonic() < deadline:
                ready, _, _ = select.select([process.stdout], [], [], 0.1)
                if ready:
                    chunk = os.read(process.stdout.fileno(), 4096)
                    if not chunk:
                        break
                    result += chunk
                if process.poll() is not None:
                    break
            self.assertEqual(result, expected, f'packet runner status={process.poll()} marker={result!r}')

        def memory(process):
            status = Path(f'/proc/{process.pid}/status').read_text()
            result = {}
            for key in ('VmRSS', 'VmSize'):
                found = re.search(rf'^{key}:\s+(\d+) kB$', status, re.MULTILINE)
                self.assertIsNotNone(found, status)
                result[key] = int(found.group(1)) * 1024
            return result

        for target in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(target=target, optimization=level):
                    output = self.work / f'packet-lifetimes-{target}-o{level}'
                    flags = ['--target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    process = subprocess.Popen(argv, cwd=self.work, env=self.env, stdin=subprocess.PIPE,
                                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    try:
                        marker(process, b'READY\n')
                        before = memory(process)
                        process.stdin.write(b'1')
                        process.stdin.flush()
                        marker(process, b'DONE\n')
                        after = memory(process)
                        print(f'exception packet memory target={target} O{level} before={before} after={after}')
                        self.assertLessEqual(after['VmRSS'] - before['VmRSS'], 16 * 1024 * 1024, (before, after))
                        self.assertLessEqual(after['VmSize'] - before['VmSize'], 32 * 1024 * 1024, (before, after))
                        process.stdin.write(b'2')
                        process.stdin.flush()
                        stdout, stderr = process.communicate(timeout=15)
                        self.assertEqual(process.returncode, 0, stdout + stderr)
                        self.assertEqual(stdout, b'')
                        self.assertEqual(stderr, b'')
                    finally:
                        if process.poll() is None:
                            process.kill()
                        process.communicate(timeout=3)

    def test_checked_class_interface_and_null_references(self):
        self.verify('''
interface I{int32 Read();}class Base{public int32 value;}
class Derived:Base,I{public int32 Read(){return value;}}
class Other{}
int32 main(){Derived derived=new Derived();derived.value=42;
 Base parent=derived;Derived restored=(Derived)parent;I face=(I)parent;
 if(restored!=derived||face.Read()!=42){return 1;}
 Derived empty=(Derived)(Base)null;if(empty!=null){return 2;}
 Base plain=new Base();int32 failures=0;
 try{Derived invalid=(Derived)plain;}catch(int32 fault){failures++;}
 try{I invalid=(I)plain;}catch(int32 fault){failures++;}
 object erased=derived;try{Other invalid=(Other)erased;}catch(int32 fault){failures++;}
 delete plain;delete derived;return failures==3?0:3;}
''')

    def test_struct_and_wide_exception_values_survive_unwind_and_finally(self):
        self.verify('''
struct Failure{uint128 id;byte bytes[5];int64 code;}
void Raise(int32 count){Failure value=default(Failure);value.id=((uint128)1<<110)+count;
 value.bytes[4]=(byte)count;value.code=503;throw value;}
uint128 Wide(){throw ((uint128)1<<116)+19;}
int32 main(){int64 sum=0;int32 cleanups=0;int32 count=0;
 while(count<1000){try{try{Raise(count);}catch(Failure value){
  if(value.id!=((uint128)1<<110)+count||value.bytes[4]!=(byte)count){return 1;}
  sum+=value.code;throw;}}catch(Failure outer){sum+=outer.code;}finally{cleanups++;}count++;}
 try{Wide();}catch(uint128 value){if(value!=((uint128)1<<116)+19){return 2;}}
 object escaped=null;try{Raise(7);}catch(object value){escaped=value;}
 Failure copied=(Failure)escaped;delete escaped;
 return sum==1006000&&cleanups==1000&&copied.bytes[4]==7?0:3;}
''')

    def test_scalar_exception_to_object_preserves_all_scalar_types(self):
        self.verify('''
int32 main(){int32 count=0;
 try{throw (int8)-7;}catch(object value){if((int8)value!=-7){return 1;}delete value;count++;}
 try{throw (uint14)16001;}catch(object value){if((uint14)value!=16001){return 2;}delete value;count++;}
 try{throw true;}catch(object value){if(!(bool)value||value is uint8){return 3;}delete value;count++;}
 try{throw (char)65;}catch(object value){if((char)value!=65||value is uint8){return 4;}delete value;count++;}
 try{throw (float32)1.25;}catch(object value){if((float32)value!=1.25){return 5;}delete value;count++;}
 try{throw "failed";}catch(object value){if((string)value!="failed"){return 6;}delete value;count++;}
 return count==6?0:7;}
''')
