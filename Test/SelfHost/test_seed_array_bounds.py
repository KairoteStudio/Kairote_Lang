"""The C-written compiler must check native managed-array accesses."""
import subprocess
import tempfile
from pathlib import Path
import unittest

from Test.SelfHost.Bootstrap import KRTC


@unittest.skipUnless(KRTC.is_file(), "build the C seed first")
class SeedArrayBoundsTests(unittest.TestCase):
    def compile_run(self, name, source, expected, level):
        with tempfile.TemporaryDirectory(prefix="kairote-seed-array-bounds-") as directory:
            work = Path(directory)
            code = work / f"{name}.krt"
            program = work / name
            code.write_text(source)
            compiled = subprocess.run(
                [str(KRTC), f"-O{level}", str(code), "output", str(program)],
                cwd=work, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(program)], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_valid_reads_writes_and_single_evaluation(self):
        source = """
int32 next(ref int32 count) { count++; return count-1; }
void increment(ref int32 value) { value++; }
int32 main() {
    int32[] values=[17,42]; int32 count=0;
    int32 first=values[next(ref count)];
    values[next(ref count)]=first;
    values[0] += 1;
    increment(ref values[1]);
    int32 result=count==2 && values[0]==18 && values[1]==18 ? 0 : 1;
    delete values; return result;
}
"""
        for level in (0, 2):
            with self.subTest(level=level):
                self.compile_run("array-bounds-valid", source, 0, level)

    def test_invalid_signed_and_unsigned_indices_exit_with_bounds_status(self):
        cases = {
            "negative_read": "int32 main() { int32[] a=[7]; int32 i=-1; return a[i]; }",
            "end_read": "int32 main() { int32[] a=[7]; return a[1]; }",
            "literal_end_read": "int32 main() { return [7][1]; }",
            "negative_write": "int32 main() { int32[] a=[7]; a[-1]=42; return 0; }",
            "end_write": "int32 main() { int32[] a=[7]; a[1]=42; return 0; }",
            "end_compound": "int32 main() { int32[] a=[7]; a[1]+=1; return 0; }",
            "end_ref": "void f(ref int32 x) { x=9; } int32 main() { int32[] a=[7]; f(ref a[1]); return 0; }",
            "end_address": "int32 main() { int32[] a=[7]; unsafe(using krt.mem;) { int32* p=&a[1]; return *p; } }",
            "wide_negative": "int32 main() { int32[] a=[7]; int128 i=-1; return a[i]; }",
            "wide_unsigned": "int32 main() { int32[] a=[7]; uint128 i=(uint128)1<<100; return a[i]; }",
        }
        for level in (0, 2):
            for name, source in cases.items():
                with self.subTest(level=level, case=name):
                    self.compile_run(name, source, 70, level)

    def test_noninteger_index_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="kairote-seed-array-index-type-") as directory:
            work = Path(directory)
            code = work / "program.krt"
            output = work / "program"
            code.write_text("int32 main() { int32[] a=[7]; return a[1.5]; }")
            result = subprocess.run([str(KRTC), "-O0", str(code), "output", str(output)],
                                    cwd=work, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(output.exists())

    def test_fixed_local_arrays_are_checked(self):
        valid = "int32 main() { int32 values[2]; values[0]=19; values[1]=23; return values[0]+values[1]==42 ? 0 : 1; }"
        invalid = "int32 main() { int32 values[2]; return values[2]; }"
        for level in (0, 2):
            with self.subTest(level=level, index="valid"):
                self.compile_run("fixed-array-valid", valid, 0, level)
            with self.subTest(level=level, index="past-end"):
                self.compile_run("fixed-array-past-end", invalid, 70, level)


if __name__ == "__main__":
    unittest.main()
