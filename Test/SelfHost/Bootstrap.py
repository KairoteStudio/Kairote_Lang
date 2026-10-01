"""Build and execute three self-hosted generations; retain all evidence.

Only seed() invokes the C-written Stage 0. Subsequent compilations execute the
Kairote binary with an empty PATH and link its KRO using the absolute ArkLink
path. No C source or external compiler is used as an intermediate backend.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SEED_DEFAULT = ROOT / "build/Re.KrtC/KrtC"
LINKER_DEFAULT = ROOT / "build/ArkLink/ArkLink"
KRTC = Path(os.environ.get("KRTC", SEED_DEFAULT if SEED_DEFAULT.exists() else ROOT / "Re.KrtC/build/KrtC")).resolve()
ARKLINK = Path(os.environ.get("ARKLINK", LINKER_DEFAULT if LINKER_DEFAULT.exists() else ROOT / "ArkLink/build/ArkLink")).resolve()

PROBES = {
    "constant": ("int32 main() { return 7; }", 7),
    "logic": ("int32 main() { if (!true || !false != true) { return 1; } return 19; }", 19),
    "nested": ("int32 main() { int32 n = 0; while (n < 9) { int32 x = n + 1; "
               "if (x == 3) { n = x; continue; } else if (x == 7) { break; } "
               "else { n = x; } } return n; }", 6),
    "recursive": ("int32 fib(int32 n) { if (n < 2) { return n; } return fib(n-1)+fib(n-2); } "
                  "int32 main() { return fib(9); }", 34),
    "reference": ("void add(ref int32 n, int32 amount) { n = n + amount; } "
                  "int32 main() { int32 n = 3; add(ref n, 14); return n; }", 17),
    "arguments": ("int32 sum(int32 a,int32 b,int32 c,int32 d,int32 e,int32 f,int32 g,int32 h,int32 i) "
                  "{ return a+b+c+d+e+f+g+h+i; } int32 main() { return sum(1,2,3,4,5,6,7,8,9); }", 45),
    "arrays": ("int32 main() { byte[] b = new byte[3]; int32[] a = new int32[3]; "
               "b[0]=2; b[1]=5; b[2]=7; a[0]=100; a[1]=-30; a[2]=b[1]; return a[0]+a[1]+a[2]+b[2]; }", 82),
    "stack": ("int32 main() { unsafe(using krt.mem;) { byte* b = stackalloc byte[4]; "
              "b[0]=4; b[1]=8; b[2]=16; return b[0]+b[1]+b[2]; } }", 28),
    "ternary_argument": ("int32 sum(int32 x,int32 y) { return x+y; } int32 main() "
                         "{ int32 flag=0; return sum(flag ? 3 : 11, 5); }", 16),
    "object_fields": ((ROOT / "Test/SelfHost/test_object_fields.krt").read_text(), 0),
    "syscall": ('int32 main() { int64 fd=syscall(2,(int64)"program.krt",0,0,0,0,0); '
                'if(fd<0) { return 1; } syscall(3,fd,0,0,0,0,0); return 23; }', 23),
    'negative_ref': ('void f(ref int32 n) { n = 7; } int32 main() { int32 n=-1; f(ref n); if(n != 7) { return 1; } return 0; }', 0),
    'negative_field_ref': ('class Box { public int32 n; } void f(ref int32 n) { n=7; } int32 main() { Box b=new Box(); b.n=-1; f(ref b.n); if(b.n!=7) { return 1; } return 0; }', 0),
    'nested_loop': ('int32 main() { int32 outer=0; int32 total=0; while(outer<4) { outer=outer+1; int32 inner=0; while(inner<10) { inner=inner+1; if(inner==1) { continue; } if(inner==3) { continue; } if(inner==4) { break; } if(inner==9) { break; } total=total+inner; } if(outer==2) { continue; } total=total+10; } return total; }', 38),
    'typed_pointer': ('int32 main() { unsafe(using krt.mem;) { int32* p=stackalloc int32[3]; p[0]=11; p[1]=29; p[2]=37; int32* q=p+1; return q[0]; } }', 29),
    'pointer_deref': ('int32 main() { unsafe(using krt.mem;) { int32* p=stackalloc int32[2]; p[0]=-31; p[1]=88; return *p+100; } }', 69),
    'compound': ('int32 main() { int32 n=2; n+=3; n*=7; n-=5; n/=3; n%=6; return n; }', 4),
    'postfix': ('int32 main() { int32 n=6; int32 a=n++; int32 b=n--; return a+b+n; }', 19),
    'indexed_postfix': ('int32 main() { int32[] n=new int32[2]; n[0]=6; int32 a=n[0]++; int32 b=n[0]--; return a+b+n[0]; }', 19),
    'forloop': ('int32 main() { int32 n=0; for(int32 i=0;i<7;i++) { if(i==2) { continue; } if(i==6) { break; } n+=i; } return n; }', 13),
    'dowhile': ('int32 main() { int32 n=0; int32 s=0; do { n++; if(n==3) { continue; } if(n==6) { break; } s+=n; } while(n<9); return s; }', 12),
}


# Read the actual runtime sources: generated compilers receive one complete
# input module directly, so these generation checks never depend on Compile.py.
CONSOLE_SOURCE = "\n".join((ROOT / "libs/System" / name).read_text()
                             for name in ("Sys.krt", "Console.krt"))
PROBES.update({
    "methods": ('class Left { int32 value; public void Set(int32 n) { value=n; } '
                'public int32 Get() { return value; } } '
                'class Right { public int32 Get() { return 5; } } '
                'int32 main() { Left a=new Left(); Right b=new Right(); a.Set(12); '
                'int32 result=a.Get()+b.Get(); delete a; delete b; return result; }', 17),
    "overloads": ('static class MathTest { public static int32 Add(int32 a) { return a+1; } '
                  'public static int32 Add(int32 a,int32 b) { return a+b; } } '
                  'class Box { public int32 Get() { return 3; } public int32 Get(int32 x) { return x; } } '
                  'int32 main() { Box b=new Box(); int32 result=MathTest.Add(5)+MathTest.Add(2,3)+b.Get()+b.Get(3); '
                  'delete b; return result; }', 17),
    "hex_char": ("int32 main() { char letter='A'; char newline='\\n'; "
                 "if(letter!=65 || newline!=10) { return 1; } return 0x2a; }", 42),
    "global_class_constants": ('const int32 Base=17; static class Limits { public const int32 Extra=11; } '
                               'int32 main() { return Base+Limits.Extra; }', 28),
    "floating_values": ((ROOT / "Test/SelfHost/fixtures/numeric/arithmetic_calls_arrays_fields.krt").read_text(), 0),
    "floating_ieee_rounding": ((ROOT / "Test/SelfHost/fixtures/numeric/literal_rounding.krt").read_text(), 0),
    "integer_precision": ((ROOT / "Test/SelfHost/fixtures/numeric/nonstandard_widths.krt").read_text(), 0),
    "wide_integer_values": ((ROOT / "Test/SelfHost/fixtures/numeric/wide_arithmetic.krt").read_text(), 0),
    "integer_widths": ('int32 main() { int8 a=-7; int16 b=-30000; uint32 wide=(uint32)-1; '
                       'uint64 high=(uint64)-1; int8[] bytes=new int8[1]; bytes[0]=a; '
                       'if(a!=-7 || b!=-30000 || bytes[0]!=a || wide!=4294967295) { return 1; } '
                       'if(high<=1 || high/3!=6148914691236517205 || high%3!=0 || high>>63!=1) { return 2; } '
                       'int32 maximum=2147483647; if(maximum+1>=0) { return 3; } delete bytes; return 0; }', 0),
    "string_values": ('int32 main() { string a="ab"; string b="cd"; string same="ab"; '
                      'string combined=a+b; string nothing=null; string copied=nothing+a; '
                      'if(a!=same || a==b || combined!="abcd" || combined.Length!=4 || copied!=a) { return 1; } '
                      'if(nothing!=null || nothing.Length!=0) { return 2; } return 0; }', 0),
    "array_length_delete": ('int32 main() { byte[] data=new byte[4096]; int64[] empty=new int64[0]; '
                            'if(data==null || empty==null || data.Length!=4096 || empty.Length!=0) { return 1; } '
                            'unsafe(using krt.mem;) { byte* vector=stackalloc byte[1]; int64 address=(int64)data-16; '
                            'if(syscall(27,address,4096,(int64)vector,0,0,0)!=0) { return 2; } delete data; '
                            'if(syscall(27,address,4096,(int64)vector,0,0,0)!=-12) { return 3; } } '
                            'delete empty; delete null; return 0; }', 0),
    "console_library": (CONSOLE_SOURCE + '\nint32 main() { Console.WriteLine("hello"); '
                        'Console.WriteLine((int32)17); Console.WriteLine((uint64)-1); Console.WriteLine(true); return 0; }', 0),
})
PROBE_STDOUT = {"console_library": "hello\n17\n18446744073709551615\nTrue\n"}

# Exercise newly implemented syntax in every self-compiled generation.
PROBES.update({
    "generic_class_function": ('class Box<T>{public T value;public Box(T x){value=x;}public T Get(){return value;}} '
                               'T identity<T>(T value){return value;} int32 main(){Box<int32> b=new Box<int32>(42);'
                               'int32 result=identity(b.Get());delete b;return result;}',42),
    "generic_ref_array": ('void swap<T>(ref T a,ref T b){T c=a;a=b;b=c;} T first<T>(T[] values){return values[0];}'
                          'int32 main(){int32 a=17;int32 b=42;swap(ref a,ref b);string[] names=["ok"];'
                          'if(first<string>(names)!="ok"){return 1;}delete names;return a;}',42),
    "generic_static_state": ('static int32 sequence=0;int32 next(){sequence++;return sequence;}'
                              'class State<T>{public static T value=default(T);public static int32 id=next();}'
                              'int32 main(){State<int32>.value=17;State<string>.value="ok";'
                              'if(State<int32>.value!=17 || State<string>.value!="ok"){return 1;}'
                              'return sequence==2 && State<int32>.id==1 && State<string>.id==2 ? 0 : 2;}',0),
    "generic_constraints": ('class Box{public int32 n=17;} T create<T>() where T:new(){return new T();}'
                            'T add<T>(T a,T b) where T:struct{return a+b;}'
                            'int32 main(){Box b=create<Box>();int32 result=b.n+add<int32>(7,18);'
                            'delete b;return result;}',42),
    "constructors": ('class Box { public int32 x=7; public Box(int32 n){x+=n;} public int32 Get(){return x;} } '
                     'int32 main(){Box b=new Box(35);int32 n=b.Get();delete b;return n;}',42),
    "array_literals_foreach": ('byte[] values(){return [1,3,5,7];} int32 main(){byte[] a=values();int32 n=0;'
                              'foreach(var x in a){if(x==3){continue;}n+=x;}delete a;return n;}',13),
    "exception_unwind": ('class Failure {public int32 n;public Failure(int32 value){n=value;}} '
                         'void fail(){throw new Failure(42);} int32 main(){int32 n=0;'
                         'try{try{fail();}finally{n++;}}catch(Failure e){n+=e.n;delete e;}return n;}',43),
    "exception_finally_return": ('int32 value(ref int32 n){try{return 17;}finally{try{throw 9;}catch(int32 x){n=x;}}} '
                                 'int32 main(){int32 n=0;int32 result=value(ref n);return result+n;}',26),
    "inheritance_constructor": ('class Base{public int32 n=7;public function Base(int32 x){n+=x;}public int32 Get(){return n;}} '
                                'class Child:Base{public int32 extra=n+1;public function Child(int32 x):base(x){}} '
                                'int32 main(){Child child=new Child(13);Base root=child;int32 n=root.Get()+child.extra;delete root;return n;}',41),
    "inheritance_exception": ('class Base{public int32 n=42;}class Child:Base{}void raise(Base e){throw e;} '
                              'int32 main(){int32 n=0;try{try{Base e=new Child();raise(e);}catch(Base e){n++;throw;}}'
                              'catch(Child e){n+=e.n;delete e;}return n;}',43),
    "virtual_dispatch": ('abstract class Base{public abstract int32 Read();public virtual int32 Extra(){return 2;}'
                         'public int32 Total(){return Read()+Extra();}public virtual int32 Apply(Child value){return 1;}'
                         'public virtual int32 Apply(int32 value){return value;}}'
                         'class Child:Base{public override int32 Read(){return 37;}'
                         'public override int32 Extra(){return base.Extra()+3;}public override int32 Apply(Child value){return 3;}}'
                         'int32 main(){Child child=new Child();Base value=child;'
                         'if(value.Apply(child)!=3 || value.Apply(17)!=17){return 1;}'
                         'int32 n=value.Total();delete value;return n;}',42),
    "interface_dispatch": ('interface IValue{int32 Read();}interface IExtra:IValue{int32 Extra();}'
                           'class Item:IExtra{public int32 Read(){return 40;}public int32 Extra(){return 2;}}'
                           'class Child:Item{public int32 Read(){return 99;}}'
                           'int32 read<T>(T item) where T:IValue{return item.Read();}'
                           'int32 main(){IExtra item=new Child();IValue value=item;int32 n=read(value)+item.Extra();delete item;return n;}',42),
    "interface_exception": ('interface IValue{int32 Read();}class Failure:IValue{public int32 Read(){return 42;}}'
                            'int32 main(){int32 n=0;try{IValue value=new Failure();throw value;}'
                            'catch(IValue error){n=error.Read();delete error;}finally{n++;}return n;}',43),
})


class Bootstrap:
    def __init__(self, work):
        self.work = Path(work).resolve()
        self.work.mkdir(parents=True, exist_ok=True)
        self.commands = []
        self.report = {"complete": False, "commands": self.commands, "hashes": {}, "probes": {}}
        self.source = "\n".join(p.read_text() for p in sorted((ROOT / "SelfHost").rglob("*.krt")))
        self.report["source_sha256"] = hashlib.sha256(self.source.encode()).hexdigest()
        self.report["seed_compiler"] = str(KRTC)
        self.report["seed_sha256"] = hashlib.sha256(KRTC.read_bytes()).hexdigest()
        self.report["linker_sha256"] = hashlib.sha256(ARKLINK.read_bytes()).hexdigest()
        (self.work / "Compiler.krt").write_text(self.source)
        (self.work / "empty-path").mkdir(exist_ok=True)

    def save(self):
        (self.work / "report.json").write_text(json.dumps(self.report, indent=2) + "\n")

    def run(self, argv, cwd, expected=0, isolated=False, timeout=120):
        env = os.environ.copy()
        if isolated:
            env["PATH"] = str(self.work / "empty-path")
        result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env,
                                capture_output=True, text=True, timeout=timeout)
        record = {"argv": list(map(str, argv)), "cwd": str(cwd), "isolated_path": isolated,
                  "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
        self.commands.append(record)
        self.save()
        if result.returncode != expected:
            raise RuntimeError(f"{argv[0]} returned {result.returncode}, expected {expected}\n"
                               f"{result.stdout}{result.stderr}")
        return result

    def seed(self):
        binary = self.work / "stage1"
        self.run([KRTC, "-O2", self.work / "Compiler.krt", "output", binary], self.work)
        binary.chmod(0o755)
        return binary

    def compile(self, compiler, source, name):
        work = self.work / name
        work.mkdir(exist_ok=True)
        (work / "program.krt").write_text(source)
        output = work / "stage1-probe.kro"
        if output.exists():
            output.unlink()  # Never accept an artifact from an earlier invocation.
        self.run([compiler], work, isolated=True)
        if not output.is_file():
            raise RuntimeError(f"Compiler produced no KRO: {work}")
        digest = hashlib.sha256(output.read_bytes()).hexdigest()
        self.report["hashes"][name] = digest
        binary = work / "program"
        self.run([ARKLINK, output, "--target", "elf", "-o", binary], work, isolated=True)
        binary.chmod(0o755)
        self.save()
        return binary, digest

    def probes(self, compiler, label):
        hashes = {}
        for name, (source, expected) in PROBES.items():
            binary, digest = self.compile(compiler, source, f"{label}-{name}")
            result = self.run([binary], binary.parent, expected=expected, isolated=True, timeout=15)
            expected_stdout = PROBE_STDOUT.get(name, "")
            if result.stdout != expected_stdout:
                raise RuntimeError(f"{label}/{name} stdout {result.stdout!r}, expected {expected_stdout!r}")
            self.report.setdefault("probe_expectations", {})[name] = {
                "exit": expected, "stdout": expected_stdout,
                "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
            }
            hashes[name] = digest
            self.report["probes"][f"{label}-{name}"] = expected
            self.save()
            print(f"PASS {label}: {name}", flush=True)
        return hashes

    def errors(self, compiler, label):
        cases = {
            "syntax": "int32 main() { return (1 + ); }",
            "undefined": "int32 main() { return missing; }",
            "type": 'int32 main() { int32 value = "text"; return value; }',
            "arity": "int32 f(int32 x) { return x; } int32 main() { return f(); }",
            "reference": "void f(ref int32 x) { x=1; } int32 main() { f(ref 7); return 0; }",
            "duplicate": "int32 main() { int32 x=1; int32 x=2; return x; }",
            "return": "int32 main() { int32 x=1; }",
            "break": "int32 main() { break; return 0; }",
            "invalid_hex": "int32 main() { return 0x; }",
            "invalid_hex_digit": "int32 main() { return 0xGG; }",
            "literal_overflow": "int32 main() { uint64 x=18446744073709551616; return 0; }",
            "array_length_mutation": "int32 main() { int32[] x=new int32[2]; x.Length=7; return 0; }",
            "string_length_mutation": 'int32 main() { string x="abc"; x.Length=7; return 0; }',
            "string_arithmetic": 'int32 main() { string x="abc"; string y=x-1; return 0; }',
            "float_bitwise": "int32 main() { float64 x=3; return (int32)(x & 1); }",
            "float_reference_width": "void f(ref float32 x) { x=1; } int32 main() { float64 x=0; f(ref x); return 0; }",
            "float_array_index": "int32 main() { int32[] x=new int32[2]; return x[1.0]; }",
            "wide_literal_overflow": "int32 main() { uint128 x=340282366920938463463374607431768211456; return 0; }",
            "constructor_arity": "class Box {public Box(int32 x){}} int32 main(){Box b=new Box();return 0;}",
            "foreach_scope": "int32 main(){foreach(var x in [1,2]){}return x;}",
            "array_literal_mixed": 'int32 main(){int32[] x=[1,"wrong"];return 0;}',
            "rethrow_without_catch": "int32 main(){throw;}",
            "generic_instance_mismatch": "class Box<T>{T value;}int32 main(){Box<int32> b=new Box<string>();return 0;}",
            "generic_inference_mismatch": 'T pick<T>(T a,T b){return a;}int32 main(){return pick(1,"wrong");}',
            "generic_class_constraint": 'class H<T> where T:class{}int32 main(){H<int32> h=new H<int32>();return 0;}',
            "generic_constructor_constraint": 'class A{public A(int32 n){}}T create<T>() where T:new(){return new T();}int32 main(){create<A>();return 0;}',
            "inheritance_cycle": 'class A:B{}class B:A{}int32 main(){return 0;}',
            "unchecked_downcast": 'class A{}class B:A{}int32 main(){A a=new A();B b=(B)a;return 0;}',
            "abstract_instance": 'abstract class A{public abstract int32 Read();}int32 main(){A a=new A();return 0;}',
            "missing_abstract_override": 'abstract class A{public abstract int32 Read();}class B:A{}int32 main(){return 0;}',
            "missing_interface_method": 'interface I{int32 Read();}class A:I{}int32 main(){return 0;}',
            "interface_cycle": 'interface I:J{}interface J:I{}int32 main(){return 0;}',
            "unchecked_interface_cast": 'interface I{}class A{}int32 main(){A a=new A();I i=(I)a;return 0;}',

        }
        diagnostics = {}
        for name, source in cases.items():
            work = self.work / f"{label}-error-{name}"
            work.mkdir(exist_ok=True)
            (work / "program.krt").write_text(source)
            output = work / "stage1-probe.kro"
            if output.exists():
                output.unlink()
            first = self.run([compiler], work, expected=1, isolated=True, timeout=15)
            second = self.run([compiler], work, expected=1, isolated=True, timeout=15)
            if not first.stderr.startswith("E_") or first.stderr != second.stderr or output.exists():
                raise RuntimeError(f"Unstable diagnostic or artifact for {name}: {first.stderr!r}")
            diagnostics[name] = first.stderr
        self.report.setdefault("diagnostics", {})[label] = diagnostics
        self.save()
        return diagnostics

    def verify(self, probes_only=False):
        stage1 = self.seed()
        self.probes(stage1, "stage1")
        if probes_only:
            return
        stage2, hash2 = self.compile(stage1, self.source, "stage2")
        print(f"Stage 2 linked: {hash2}", flush=True)
        stage3, hash3 = self.compile(stage2, self.source, "stage3")
        print(f"Stage 3 linked: {hash3}", flush=True)
        _, hash4 = self.compile(stage3, self.source, "stage4-check")
        if len({hash2, hash3, hash4}) != 1:
            raise RuntimeError("Compiler KRO differs across generations")
        probes2 = self.probes(stage2, "stage2")
        probes3 = self.probes(stage3, "stage3")
        if probes2 != probes3:
            raise RuntimeError("Probe KRO differs between Stage 2 and Stage 3")
        if self.errors(stage2, "stage2") != self.errors(stage3, "stage3"):
            raise RuntimeError("Diagnostics differ between Stage 2 and Stage 3")
        self.report["complete"] = True
        self.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=ROOT / "build/selfhost")
    parser.add_argument("--probes-only", action="store_true", help="Develop native features without claiming bootstrap completion")
    args = parser.parse_args()
    build = Bootstrap(args.work)
    try:
        build.verify(args.probes_only)
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        build.report["failure"] = str(error)
        build.save()
        print(error, file=sys.stderr)
        return 1
    print(f"Evidence: {build.work / 'report.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
