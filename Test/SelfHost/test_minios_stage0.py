"""Narrow Stage0 prerequisites used by the native MiniOS compiler bootstrap."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class MiniOSStage0Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.seed = Path(os.environ.get("KRTC", ROOT / "build/Re.KrtC/KrtC")).resolve()
        if not cls.seed.is_file():
            raise RuntimeError("Build Stage0 and set KRTC; missing tools are not passing tests")

    def compile(self, source):
        temporary = tempfile.TemporaryDirectory(prefix="krt-stage0-types-")
        self.addCleanup(temporary.cleanup)
        work = Path(temporary.name)
        path = work / "probe.krt"
        path.write_text(source)
        binary = work / "probe"
        result = subprocess.run([str(self.seed), "-O2", str(path), "output", str(binary)],
                                cwd=work, capture_output=True, text=True, timeout=30)
        return result, binary

    def test_class_null_returns_fields_and_comparison_are_references(self):
        source = ("class Link{public Link next;} Link empty(){return null;} "
                  "bool same(Link a,Link b){return a==b;} "
                  "int32 main(){Link a=null;Link b=empty();if(!same(a,b)){return 1;}"
                  "a=new Link();a.next=null;return a.next==b?42:2;}")
        result, binary = self.compile(source)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        binary.chmod(0o755)
        self.assertEqual(subprocess.run([str(binary)],timeout=10).returncode,42)

    def test_distinct_nonnull_class_references_do_not_compare_as_bool_bytes(self):
        source = ("class Link{public int64 value;} bool same(Link a,Link b){return a==b;} "
                  "int32 main(){Link a=new Link();Link b=new Link();"
                  "if(same(a,b)||same(a,null)||!same(a,a)||!same(null,null)){return 1;}return 42;}")
        result,binary=self.compile(source)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        binary.chmod(0o755)
        self.assertEqual(subprocess.run([str(binary)],timeout=10).returncode,42)

    def test_scalar_comparison_metadata_keeps_width_and_signedness(self):
        source = ("int32 main(){uint64 high=18446744073709551615;uint64 low=9223372036854775808;"
                  "if(!(high>low)||high<low){return 1;}int64 negative=-2147483649;"
                  "if(!(negative<0)){return 2;}int64 n=4;int64 start=3;int64 count=2;"
                  "if(!(count>n-start)){return 3;}return 42;}")
        result,binary=self.compile(source)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        binary.chmod(0o755)
        self.assertEqual(subprocess.run([str(binary)],timeout=10).returncode,42)

    def test_target_query_is_false_on_stage0_platforms(self):
        result,binary=self.compile("int32 main(){return __krt_target_minios()?1:42;}")
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        binary.chmod(0o755)
        self.assertEqual(subprocess.run([str(binary)],timeout=10).returncode,42)

    def test_target_query_rejects_arguments(self):
        result,_=self.compile("int32 main(){return __krt_target_minios(1)?1:0;}")
        self.assertNotEqual(result.returncode,0)

    def test_linux_runtime_sys_remains_stage0_compatible(self):
        source=(ROOT / "libs/System/Sys.krt").read_text()
        result,binary=self.compile(source + "\nint32 main(){return 42;}\n")
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        binary.chmod(0o755)
        self.assertEqual(subprocess.run([str(binary)],timeout=10).returncode,42)

    def test_unrelated_named_types_stay_rejected(self):
        result,_=self.compile("class A{}class B{}int32 main(){A a=new B();return 0;}")
        self.assertNotEqual(result.returncode,0)

    def test_nonnullable_raw_pointer_stays_rejected(self):
        result,_=self.compile("int32 main(){unsafe(using krt.mem;){byte* p=null;}return 0;}")
        self.assertNotEqual(result.returncode,0)

    def test_class_from_integer_stays_rejected(self):
        result,_=self.compile("class A{}int32 main(){A a=1;return 0;}")
        self.assertNotEqual(result.returncode,0)

    def test_string_from_integer_stays_rejected(self):
        result,_=self.compile("int32 main(){string s=1;return 0;}")
        self.assertNotEqual(result.returncode,0)

if __name__ == "__main__":
    unittest.main()
