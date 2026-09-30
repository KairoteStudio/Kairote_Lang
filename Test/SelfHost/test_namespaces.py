"""Execute qualified namespace/type/function lookup and diagnose ambiguities."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SEED = Path(os.environ.get('KRTC', ROOT / 'build/Re.KrtC/KrtC')).resolve()
LINKER = ROOT / 'build/ArkLink/ArkLink'


class NamespaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='krt-namespaces-')
        cls.work = Path(cls.temporary.name)
        if 'SELFHOST_COMPILER' in os.environ:
            cls.compiler = Path(os.environ['SELFHOST_COMPILER']).resolve()
            if not cls.compiler.is_file():
                raise AssertionError(f'SELFHOST_COMPILER not found: {cls.compiler}')
            return
        source = cls.work / 'compiler.krt'
        source.write_text('\n'.join(p.read_text() for p in sorted((ROOT / 'SelfHost').rglob('*.krt'))))
        cls.compiler = cls.work / 'compiler'
        result = subprocess.run([str(SEED), '-O2', str(source), 'output', str(cls.compiler)], cwd=cls.work, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.compiler.chmod(0o755)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def check(self, source, expected=0, invalid=False):
        with tempfile.TemporaryDirectory(dir=self.work) as directory:
            work = Path(directory)
            (work / 'program.krt').write_text(source)
            result = subprocess.run([str(self.compiler)], cwd=work, capture_output=True, text=True, timeout=30)
            if invalid:
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((work / 'stage1-probe.kro').exists())
                return
            self.assertEqual(result.returncode, 0, result.stderr)
            executable = work / 'program'
            result = subprocess.run([str(LINKER), 'stage1-probe.kro', '--target', 'elf', '-o', str(executable)], cwd=work, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            executable.chmod(0o755)
            self.assertEqual(subprocess.run([str(executable)], timeout=5).returncode, expected)

    def test_distinct_qualified_types(self):
        self.check('namespace A {class Box{public static int32 Get(){return 7;}}} namespace B{class Box{public static int32 Get(){return 10;}}} int32 main(){return A.Box.Get()+B.Box.Get();}',17)

    def test_nested_namespace_and_qualified_local(self):
        self.check('namespace A{namespace B{class Box{public int32 value;}}} int32 main(){A.B.Box value=new A.B.Box();value.value=17;return value.value;}',17)

    def test_file_scope_boundary_and_import(self):
        self.check('namespace A; class Box{public int32 Value(){return 17;}} namespace; using A; int32 main(){Box value=new Box();return value.Value();}',17)

    def test_type_alias(self):
        self.check('namespace A{class Box{public int32 Value(){return 17;}}} using Chosen=A.Box; int32 main(){Chosen value=new Chosen();return value.Value();}',17)

    def test_namespace_alias(self):
        self.check('namespace A.B{class Box{public static int32 Value(){return 17;}}} using Chosen=A.B; int32 main(){return Chosen.Box.Value();}',17)

    def test_qualified_free_functions(self):
        self.check('namespace A{int32 value(){return 7;}} namespace B{int32 value(){return 10;}} int32 main(){return A.value()+B.value();}',17)

    def test_namespace_local_function_priority(self):
        self.check('int32 value(){return 99;} namespace A{int32 value(){return 17;} int32 run(){return value();}} int32 main(){return A.run();}',17)

    def test_alias_qualified_free_function(self):
        self.check('namespace A.B{int32 value(){return 17;}} using Chosen=A.B; int32 main(){return Chosen.value();}',17)

    def test_alias_qualified_global(self):
        self.check('namespace A.B{static int32 value=16;} using Chosen=A.B; int32 main(){Chosen.value++;return Chosen.value;}',17)

    def test_imported_global(self):
        self.check('namespace A{static int32 value=16;} using A; int32 main(){value++;return value;}',17)

    def test_import_selects_type_among_matching_short_names(self):
        self.check('namespace A{class Box{public static int32 Value(){return 17;}}} namespace B{class Box{public static int32 Value(){return 99;}}} using A; int32 main(){return Box.Value();}',17)

    def test_import_selects_function_among_matching_short_names(self):
        self.check('namespace A{int32 value(){return 17;}} namespace B{int32 value(){return 99;}} using A; int32 main(){return value();}',17)

    def test_namespace_relative_qualified_function(self):
        self.check('namespace A{namespace B{int32 value(){return 17;}} int32 run(){return B.value();}} int32 main(){return A.run();}',17)

    def test_ambiguities_and_duplicate_names(self):
        for source in (
            'namespace A{class Box{}} namespace B{class Box{}} using A; using B; int32 main(){Box value=new Box();return 0;}',
            'namespace A{class Box{}} namespace A{class Box{}} int32 main(){return 0;}',
            'namespace A{int32 f(){return 1;} int32 f(){return 2;}} int32 main(){return 0;}',
            'namespace A{class Box{}} int32 main(){Box value=new Box();return 0;}',
        ):
            with self.subTest(source=source):
                self.check(source, invalid=True)
