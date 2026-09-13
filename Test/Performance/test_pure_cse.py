"""Pure-call reuse preserves integer values, effects, control flow and traps."""
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()


def wrap(value, bits, unsigned=False):
    value %= 1 << bits
    return value if unsigned or value < 1 << (bits - 1) else value - (1 << bits)


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class PureCallCse(unittest.TestCase):
    def run_source(self, source, inspect_ir=False, outcome=0):
        results = {}
        with tempfile.TemporaryDirectory(prefix="kairote-pure-cse-") as directory:
            path = Path(directory)
            code = path / "Program.krt"
            code.write_text(source)
            for level in (2, 3):
                binary = path / f"ProgramO{level}"
                command = [str(COMPILER), f"-O{level}", str(code)]
                built = subprocess.run(command + ["-o", str(binary)], cwd=path,
                                       capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr + source)
                self.assertNotIn("AddressSanitizer", built.stderr)
                self.assertNotIn("runtime error:", built.stderr)
                if outcome == "timeout":
                    with self.assertRaises(subprocess.TimeoutExpired, msg=f"O{level}: {source}"):
                        subprocess.run([str(binary)], capture_output=True, timeout=0.5,
                                       preexec_fn=no_core)
                else:
                    executed = subprocess.run([str(binary)], capture_output=True, timeout=10,
                                              preexec_fn=no_core)
                    self.assertEqual(executed.returncode, outcome,
                                     f"O{level}: {executed.returncode}\n{executed.stderr!r}\n{source}")
                if inspect_ir:
                    output = path / f"ProgramO{level}.ir"
                    built = subprocess.run(command + ["target", "ir", "-o", str(output)],
                                           cwd=path, capture_output=True, text=True, timeout=30)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr + source)
                    self.assertNotIn("AddressSanitizer", built.stderr)
                    self.assertNotIn("runtime error:", built.stderr)
                    results[level] = output.read_text()
        return results

    def calls_in(self, ir, caller, callee):
        caller_pattern = rf"_ZN{len(caller)}{re.escape(caller)}E[^\s(]*"
        body = re.search(rf"^function {caller_pattern}\([^\n]*\)\n(.*?)(?=^function |\Z)",
                         ir, re.M | re.S)
        self.assertIsNotNone(body, ir)
        call_pattern = rf"\bcall _ZN{len(callee)}{re.escape(callee)}E[^\s(]*(?:\s|$)"
        return len(re.findall(call_pattern, body.group(1)))

    def test_equal_pure_calls_are_reused_in_ir(self):
        # The branch keeps the callee outside the straight-line helper inliner.
        ir = self.run_source("""
int64 Scramble(int64 n) {
    if (n == 0) { return 7; }
    return n*3+1;
}
int64 Repeat(int64 n) { return Scramble(n)+Scramble(n); }
int32 main() {
    if (Repeat(0) != 14 || Repeat(7) != 44 || Repeat(-9) != -52) { return 1; }
    return 0;
}
""", inspect_ir=True)
        self.assertEqual(self.calls_in(ir[2], "Repeat", "Scramble"), 2)
        self.assertLessEqual(self.calls_in(ir[3], "Repeat", "Scramble"), 1)

    def test_renamed_recursive_duplicates_and_pure_mutual_recursion(self):
        for name, step, base in (("Sprout", 1, 3), ("Expand", 2, 7), ("Scatter", 3, -5)):
            def reference(n):
                return base if n <= 0 else 2 * reference(n - step)
            checks = "\n".join(f"if ({name}({n}) != {reference(n)}) {{ return {n+4}; }}"
                               for n in range(-3, 12))
            with self.subTest(name=name, step=step, base=base):
                self.run_source(f"""
int64 {name}(int32 n) {{
    if (n <= 0) {{ return {base}; }}
    return {name}(n-{step})+{name}(n-{step});
}}
int32 main() {{ {checks} return 0; }}
""")
        ir = self.run_source("""
int64 CycleA(int32 n) { if (n <= 0) { return 3; } return CycleB(n-1)+1; }
int64 CycleB(int32 n) { if (n <= 0) { return 5; } return CycleA(n-1)+2; }
int64 TwiceCycle(int32 n) { return CycleA(n)+CycleA(n); }
int32 main() {
    if (TwiceCycle(0) != 6 || TwiceCycle(1) != 12 || TwiceCycle(6) != 24) { return 1; }
    return 0;
}
""", inspect_ir=True)
        self.assertEqual(self.calls_in(ir[2], "TwiceCycle", "CycleA"), 2)
        self.assertLessEqual(self.calls_in(ir[3], "TwiceCycle", "CycleA"), 1)

    def test_all_integer_widths_and_wrapping(self):
        for bits in range(2, 129, 2):
            for unsigned in (False, True):
                kind = ("uint" if unsigned else "int") + str(bits)
                values = {0, 1, wrap(-1, bits, unsigned),
                          wrap(1 << (bits - 1), bits, unsigned),
                          wrap((1 << (bits - 1)) - 1, bits, unsigned)}
                checks = []
                for value in sorted(values):
                    transformed = wrap(7 if value == 0 else value * 3 + 1, bits, unsigned)
                    expected = wrap(2 * transformed, bits, unsigned)
                    checks.append(f"if (Repeat(({kind}){value}) != ({kind}){expected}) {{ return 1; }}")
                with self.subTest(kind=kind):
                    self.run_source(f"""
{kind} Scramble({kind} n) {{
    if (n == 0) {{ return ({kind})7; }}
    return n*({kind})3+({kind})1;
}}
{kind} Repeat({kind} n) {{ return Scramble(n)+Scramble(n); }}
int32 main() {{ {" ".join(checks)} return 0; }}
""")

    def test_arguments_call_targets_and_return_paths_are_distinct(self):
        self.run_source("""
int64 Scramble(int64 n) { if (n == 0) { return 7; } return n*3+1; }
int64 Other(int64 n) { if (n == 0) { return 11; } return n*5+2; }
int64 Different(int64 a, int64 b) { return Scramble(a)+Scramble(b); }
int64 Targets(int64 n) { return Scramble(n)+Other(n); }
int64 Branches(int64 n, bool choose) {
    return choose ? Scramble(n)+Scramble(n) : Scramble(n+1)+Scramble(n+2);
}
int64 Join(int64 n, bool choose) {
    int64 saved=choose ? Scramble(n) : Scramble(n+3);
    return saved+Scramble(n+1);
}
int32 main() {
    if (Different(3,4) != 23 || Different(0,1) != 11 || Targets(2) != 19) { return 1; }
    if (Branches(3,true) != 20 || Branches(3,false) != 29) { return 2; }
    if (Join(3,true) != 23 || Join(3,false) != 32) { return 3; }
    return 0;
}
""")

    def test_full_128bit_constants_and_signed_conversions(self):
        high = (1 << 100) + 9
        other = (1 << 111) + 9
        self.run_source(f"""
int128 Wide(int128 n) {{ if (n == 0) {{ return 11; }} return n; }}
int64 Signed(int8 n) {{ if (n < 0) {{ return (int64)n-1000; }} return (int64)n+1000; }}
int64 Unsigned(uint8 n) {{ if (n == 0) {{ return 7; }} return (int64)n+1000; }}
int64 Scramble(int64 n) {{ if (n == 0) {{ return 7; }} return n*3+1; }}
int64 Casts(int64 n) {{ return Scramble((int8)n)+Scramble((uint8)n); }}
int128 Pair(int128 n) {{
    return Wide(n)+Wide((int128){high})+Wide((int128){other})+Wide((int128){high+3});
}}
int32 main() {{
    if (Pair(5) != (int128){2*high+other+8}) {{ return 1; }}
    if (Signed((int8)-1)+Unsigned((uint8)255) != 254) {{ return 2; }}
    if (Casts(255) != 764 || Casts(256) != 14) {{ return 3; }}
    return 0;
}}
""")

    def test_effects_propagate_through_wrappers_and_recursive_scc(self):
        definitions = ["int64 Bottom(int64 n) { counter++; return n+counter; }"]
        for index in range(8):
            callee = "Bottom" if index == 0 else f"Layer{index-1}"
            definitions.append(f"int64 Layer{index}(int64 n) {{ return {callee}(n); }}")
        self.run_source("static int64 counter=0;\n" + "\n".join(definitions) + """
int64 MutualA(int32 n) {
    if (n == 0) { return 1; }
    return MutualB(n-1)+MutualB(n-1);
}
int64 MutualB(int32 n) {
    counter++;
    if (n == 0) { return 2; }
    return MutualA(n-1)+MutualA(n-1);
}
int32 main() {
    if (Layer7(10)+Layer7(10) != 23 || counter != 2) { return 1; }
    counter=0;
    if (MutualA(3)+MutualA(3) != 32 || counter != 20) { return 2; }
    return 0;
}
""")

    def test_global_reads_writes_and_call_order(self):
        self.run_source("""
static int64 state=7;
static int64 trace=0;
int64 Read(int64 n) { if (n == 0) { return state; } return state+n; }
int64 ReadLayer(int64 n) { return Read(n); }
int64 ReadTop(int64 n) { return ReadLayer(n); }
int64 Change(int64 n) { state=n; return 0; }
int64 Visit(int64 n) { trace=trace*10+n; return n; }
int32 main() {
    if (ReadTop(2)+Change(19)+ReadTop(2) != 30) { return 1; }
    if (Visit(2)+Visit(3)+Visit(2) != 7 || trace != 232) { return 2; }
    state=4;
    int64 first=ReadTop(2);
    state=11;
    int64 second=ReadTop(2);
    return first == 6 && second == 13 ? 0 : 3;
}
""")

    def test_references_pointers_and_indirect_calls(self):
        self.run_source("""
static int64 effects=0;
int64 Increment(ref int64 n) { n++; return n; }
int64 Touch(int64 n) { effects++; return n+effects; }
int64 Second(int64 n) { effects+=10; return n+effects; }
int64 ReadPointer(int64* p) { unsafe(using krt.mem;) { return *p; } }
int64 WritePointer(int64* p) { unsafe(using krt.mem;) { *p=31; return 0; } }
int64 Apply(fn(int64) -> int64 f, int64 n) {
    unsafe(using krt.mem;) { return f(n)+f(n); }
}
int32 main() {
    int64 x=5;
    if (Increment(ref x)+Increment(ref x) != 13 || x != 7) { return 1; }
    unsafe(using krt.mem;) {
        let p=&x;
        if (ReadPointer(p)+WritePointer(p)+ReadPointer(p) != 38) { return 2; }
        let f: fn(int64) -> int64 = &Touch;
        if (Apply(f,4) != 11 || effects != 2) { return 3; }
        var chosen: fn(int64) -> int64 = &Touch;
        int64 first=chosen(4);
        chosen=&Second;
        int64 last=chosen(4);
        if (first != 7 || last != 17 || effects != 13) { return 4; }
    }
    return 0;
}
""")

    def test_addresses_and_stack_storage_keep_each_live_frame(self):
        self.run_source("""
int64 Frames(int32 n, int64* parent) {
    unsafe(using krt.mem;) {
        int64 local=n;
        var bytes=stackalloc int64[3];
        bytes[0]=n; bytes[1]=n+10; bytes[2]=n+20;
        if (&local == parent) { return 100000; }
        if (n <= 0) { return 1; }
        int64 total=Frames(n-1,&local)+Frames(n-1,&local);
        if (local != n || bytes[0] != n || bytes[1] != n+10 || bytes[2] != n+20) { return 200000; }
        return total;
    }
}
int32 main() {
    unsafe(using krt.mem;) { int64 anchor=0; return Frames(6,&anchor) == 64 ? 0 : 1; }
}
""")

    def test_noncanonical_bool_casts_and_memory_writes(self):
        self.run_source("""
bool Canonical(bool value) { return (bool)value; }
int32 Repeated(bool value) { return (int32)Canonical(value)+(int32)Canonical(value); }
int32 Casts(bool value) { return (int32)(bool)value+(int32)(bool)value; }
int32 CopyThenCast(bool value) { bool saved=value; return (int32)(bool)saved; }
bool Read(bool* p) { unsafe(using krt.mem;) { return (bool)*p; } }
int32 BeforeWrite(bool* p, byte* bytes) {
    unsafe(using krt.mem;) {
        int32 first=(int32)Read(p);
        *bytes=0;
        return first+10*(int32)Read(p);
    }
}
int32 main() {
    unsafe(using krt.mem;) {
        var bytes=stackalloc byte[3];
        bytes[0]=165; bytes[2]=90;
        let p=(bool*)(bytes+1);
        for (int32 value=0; value<256; value++) {
            bytes[1]=(byte)value;
            int32 expected=value == 0 ? 0 : 1;
            if (Repeated(*p) != expected*2 || Casts(*p) != expected*2 ||
                CopyThenCast(*p) != expected) { return 1; }
            *p=(bool)*p;
            if (bytes[1] != (byte)expected || bytes[0] != 165 || bytes[2] != 90) { return 2; }
            bytes[1]=(byte)value;
            if (BeforeWrite(p,bytes+1) != expected || bytes[1] != 0) { return 3; }
        }
    }
    return 0;
}
""")

    def test_mutation_redefinitions_and_loop_boundaries(self):
        def scramble(n):
            return 7 if n == 0 else n * 3 + 1
        expected = sum(scramble(i) + scramble(i + 1) for i in range(8))
        self.run_source(f"""
int64 Scramble(int64 n) {{ if (n == 0) {{ return 7; }} return n*3+1; }}
int64 Rewrite(int64 n) {{
    int64 a=Scramble(n++);
    int64 b=Scramble(n);
    int64 c=Scramble(++n);
    return a+10*b+100*c;
}}
int64 Loop(int32 count) {{
    int64 sum=0;
    int64 value=0;
    for (int32 i=0; i<count; i++) {{
        sum+=Scramble(value);
        value++;
        sum+=Scramble(value);
    }}
    return sum;
}}
int64 Conditional(int64 n, bool choose) {{
    int64 result=Scramble(n);
    if (choose) {{ n+=2; result=Scramble(n); }}
    else {{ n-=1; result=Scramble(n); }}
    return result+Scramble(n+1);
}}
int32 main() {{
    if (Rewrite(2) != 1407 || Rewrite(-1) != 468) {{ return 1; }}
    if (Loop(0) != 0 || Loop(1) != 11 || Loop(8) != {expected}) {{ return 2; }}
    if (Conditional(5,true) != 47 || Conditional(5,false) != 29) {{ return 3; }}
    return 0;
}}
""")

    def test_first_unused_call_still_traps(self):
        self.run_source("""
int64 Quotient(int64 n) { if (n == 123) { return n; } return 7/n; }
int32 main() { Quotient(0); return 0; }
""", outcome=-signal.SIGFPE)

    def test_first_unused_nonterminating_call_is_preserved(self):
        # One nontermination case, isolated and killed by subprocess.run.
        self.run_source("""
int64 Never(int64 n) { return Never(n); }
int32 main() { Never(7); return 0; }
""", outcome="timeout")

    def test_unexecuted_dangerous_paths_are_not_hoisted(self):
        self.run_source("""
int64 Quotient(int64 n) { if (n == 123) { return n; } return 7/n; }
int64 Never(int64 n) { return Never(n); }
int64 Safe(int64 n, bool choose) {
    if (choose) { return 19; }
    return Quotient(n)+Quotient(n);
}
int32 main() {
    if (Safe(0,true) != 19 || Safe(7,false) != 2) { return 1; }
    if (false && Never(7) == 1) { return 2; }
    if (true || Quotient(0) == 1) { return 0; }
    return 3;
}
""")


if __name__ == "__main__":
    unittest.main()
