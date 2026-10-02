"""Heap lifetime, failure, metadata, and resident-memory runtime regressions."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import ARKLINK, KRTC, ROOT, Bootstrap


class NativeMemoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="krt-native-memory-")
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = cls.build.seed()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def compile(self, name, source):
        binary, _ = self.build.compile(self.compiler, source, name)
        return binary

    def test_empty_arrays_zero_fill_alignment_and_metadata(self):
        source = """
int32 main() {
    byte[] empty = new byte[0];
    int64[] values = new int64[17];
    if (empty == null || values == null) { return 1; }
    if (empty.Length != 0 || values.Length != 17) { return 6; }
    if (((int64)empty & 15) != 0 || ((int64)values & 15) != 0) { return 2; }
    unsafe(using krt.mem;) {
        // Payload[-16] retains the count; Payload[-8] matches Re.KrtC's total byte size.
        if (((int64*)empty)[-2] != 0 || ((int64*)values)[-2] != 17) { return 3; }
        if (((int64*)empty)[-1] != 16 || ((int64*)values)[-1] != 17 * 8 + 16) { return 7; }
    }
    int32 i = 0;
    while (i < 17) { if (values[i] != 0) { return 4; } values[i] = i + 1; i++; }
    if (values[16] != 17) { return 5; }
    delete empty; delete values;
    return 0;
}
"""
        binary = self.compile("empty-zero-aligned", source)
        self.assertEqual(subprocess.run([binary], timeout=10).returncode, 0)

    def test_delete_unmaps_arrays_and_objects_and_accepts_null(self):
        source = """
class Box { public int64 value; }
int32 main() {
    byte[] data = new byte[4096];
    Box box = new Box();
    if (data == null || box == null) { return 1; }
    box.value = 73;
    unsafe(using krt.mem;) {
        byte* vector = stackalloc byte[1];
        int64 address = (int64)data - 16;
        int64 object_address = (int64)box - 16;
        if (syscall(27, address, 4096, (int64)vector, 0, 0, 0) != 0) { return 2; }
        delete data; delete box;
        if (syscall(27, address, 4096, (int64)vector, 0, 0, 0) != -12) { return 3; }
        if (syscall(27, object_address, 4096, (int64)vector, 0, 0, 0) != -12) { return 4; }
    }
    byte[] nothing = null;
    Box no_box = null;
    delete nothing; delete no_box; delete null;
    return 0;
}
"""
        binary = self.compile("delete-unmaps", source)
        self.assertEqual(subprocess.run([binary], timeout=10).returncode, 0)

    def test_invalid_sizes_and_kernel_failure_return_null(self):
        source = """
int32 main() {
    int64 negative = -1;
    int64 huge = 4611686018427387904;
    int64 maximum = 9223372036854775807;
    int64 impossible = 281474976710656;
    byte[] a = new byte[negative];
    int64[] b = new int64[huge];
    byte[] c = new byte[maximum];
    byte[] d = new byte[impossible];
    if (a != null || b != null || c != null || d != null) { return 1; }
    delete a; delete b; delete c; delete d;
    return 0;
}
"""
        binary = self.compile("allocation-failures", source)
        self.assertEqual(subprocess.run([binary], timeout=10).returncode, 0)

    def test_repeated_touched_allocations_have_bounded_resident_memory(self):
        source = """
int32 main() {
    int32 round = 0;
    while (round < 256) {
        byte[] data = new byte[1048576];
        if (data == null) { return 1; }
        int32 page = 0;
        while (page < 1048576) { data[page] = 73; page += 4096; }
        if (data[1044480] != 73) { return 2; }
        delete data;
        round++;
    }
    unsafe(using krt.mem;) {
        byte* usage = stackalloc byte[144];
        if (syscall(98, 0, (int64)usage, 0, 0, 0, 0) != 0) { return 3; }
        if (syscall(1, 1, (int64)(usage + 32), 8, 0, 0, 0) != 8) { return 4; }
        byte* statm = stackalloc byte[128];
        int64 fd = syscall(2, (int64)"/proc/self/statm", 0, 0, 0, 0, 0);
        if (fd < 0) { return 5; }
        int64 count = syscall(0, fd, (int64)statm, 128, 0, 0, 0);
        if (count <= 0 || syscall(1, 1, (int64)statm, count, 0, 0, 0) != count) { return 6; }
        syscall(3, fd, 0, 0, 0, 0, 0);
    }
    return 0;
}
"""
        binary = self.compile("bounded-rss", source)
        result = subprocess.run([binary], capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(len(result.stdout), 8)
        maximum_rss_kib = int.from_bytes(result.stdout[:8], "little", signed=True)
        resident_pages = int(result.stdout[8:].split()[1])
        resident_bytes = resident_pages * os.sysconf("SC_PAGE_SIZE")
        # Current RSS avoids the process-launch high-water mark inherited from
        # Python. Dropping delete as a no-op retains all 256 MiB of touched data.
        self.assertLess(resident_bytes, 16777216)
        evidence = {"touched_bytes": 268435456, "maximum_rss_kib": maximum_rss_kib,
                    "final_resident_bytes": resident_bytes}
        (self.build.work / "memory-evidence.json").write_text(json.dumps(evidence) + "\n")

    def test_readonly_length_and_invalid_delete_are_diagnosed(self):
        cases = {
            "length-assignment": "int32[] a=new int32[3]; a.Length=1;",
            "length-increment": "int32[] a=new int32[3]; a.Length++;",
            "delete-literal": 'delete "literal";',
            "literal-array-alias": 'byte[] a="literal"; delete a;',
            "delete-stack": "unsafe(using krt.mem;) { byte* p=stackalloc byte[4]; delete p; }",
            "stack-array-alias": "unsafe(using krt.mem;) { byte* p=stackalloc byte[4]; byte[] a=p; delete a; }",
            "stack-array-ref-alias": "unsafe(using krt.mem;) { byte* p=stackalloc byte[4]; free(ref p); }",
        }
        for name, body in cases.items():
            with self.subTest(case=name):
                work = self.build.work / name
                work.mkdir()
                prefix = "void free(ref byte[] a) { delete a; } " if name == "stack-array-ref-alias" else ""
                (work / "program.krt").write_text(prefix + "int32 main() { " + body + " return 0; }")
                result = self.build.run([self.compiler], work, expected=1, isolated=True)
                self.assertTrue(result.stderr.startswith("E_"), result.stderr)
                self.assertFalse((work / "stage1-probe.kro").exists())

    def test_source_string_content_concat_and_length(self):
        source = """
int32 main() {
    string left = "same";
    string right = "same";
    string empty = "";
    string nothing = null;
    if (left != right || left == "different") { return 1; }
    if (nothing != null || nothing == empty) { return 2; }
    string joined = left + right;
    if (joined != "samesame" || joined.Length != 8) { return 3; }
    if (joined[7] != 101 || joined[8] != 0) { return 4; }
    if ((nothing + left) != left || (left + nothing) != left) { return 5; }
    if ((nothing + nothing) != empty || nothing.Length != 0) { return 6; }
    return 0;
}
"""
        binary = self.compile("string-source", source)
        self.assertEqual(subprocess.run([binary], timeout=10).returncode, 0)

    def test_string_runtime_primitives(self):
        cases = {
            "equal-distinct": ([('Data', 0, 4), ('Data', 4, 4), ('StringEqual', 0, 0)], 1),
            "unequal": ([('Data', 0, 4), ('Data', 8, 4), ('StringEqual', 0, 0)], 0),
            "equal-null": ([('Const', 0, 0), ('Const', 0, 0), ('StringEqual', 0, 0)], 1),
            "null-not-empty": ([('Const', 0, 0), ('Data', 3, 1), ('StringEqual', 0, 0)], 0),
            "length": ([('Data', 0, 4), ('StringLength', 0, 0)], 3),
            "null-length": ([('Const', 0, 0), ('StringLength', 0, 0)], 0),
            "concat": ([('Const', 11, 0), ('Data', 0, 4), ('Data', 8, 4), ('StringConcat', 0, 0), ('StringLength', 0, 0), ('Add', 0, 0)], 17),
            "concat-content": ([('Data', 0, 4), ('Data', 8, 4), ('StringConcat', 0, 0), ('Data', 12, 7), ('StringEqual', 0, 0)], 1),
            "concat-null": ([('Const', 0, 0), ('Data', 0, 4), ('StringConcat', 0, 0), ('Data', 0, 4), ('StringEqual', 0, 0)], 1),
            "concat-empty": ([('Const', 0, 0), ('Const', 0, 0), ('StringConcat', 0, 0), ('Data', 3, 1), ('StringEqual', 0, 0)], 1),
        }
        work = self.build.work / "string-runtime"
        work.mkdir()
        data = b"abc\0abc\0def\0abcdef\0"
        parts = [p.read_text() for p in sorted((ROOT / "SelfHost").rglob("*.krt"))
                 if p.name != "Main.krt"]
        body = ["int32 main() {", "KrtNativeModule m=new KrtNativeModule(); KrtKroObject o=new KrtKroObject();"]
        for name, (ops, _) in cases.items():
            body += ["if (!KrtNativeInit(m)) { return 1; }",
                     "m.function_count=1; m.entry=0; m.starts[0]=0; m.local_counts[0]=0; m.parameter_counts[0]=0;",
                     f"m.data_length={len(data)};"]
            body += [f"m.data_storage[{i}]={byte};" for i, byte in enumerate(data)]
            body += [f"KrtNativeEmit(m,(int32)KrtNativeOp.{op},{arg},{extra});" for op, arg, extra in ops]
            body += ["KrtNativeEmit(m,(int32)KrtNativeOp.Return,0,1);",
                     f'if (!KrtKroEmitNativeModule(m,o) || !KrtKroWriteObjectToPath(o,"{name}.kro")) {{ return 2; }}']
        source = work / "emitter.krt"
        source.write_text("\n".join(parts + body + ["return 0; }"]))
        emitter = work / "emitter"
        self.build.run([KRTC, "-O2", source, "output", emitter], work)
        self.build.run([emitter], work)
        for name, (_, expected) in cases.items():
            with self.subTest(case=name):
                binary = work / name
                self.build.run([ARKLINK, work / (name + ".kro"), "--target", "elf", "-o", binary], work)
                binary.chmod(0o755)
                self.assertEqual(subprocess.run([binary], timeout=10).returncode, expected)


if __name__ == "__main__":
    unittest.main()
