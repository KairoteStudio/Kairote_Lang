"""The documented project combines language features across source files."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()


@unittest.skipUnless(COMPILER.is_file(), 'build the self-hosted compiler first')
class LanguageProjectTest(unittest.TestCase):
    def test_virtual_interface_generic_project(self):
        with tempfile.TemporaryDirectory(prefix='krt-dispatch-project-') as directory:
            project = Path(directory) / 'demo'
            shutil.copytree(ROOT / 'Test/SelfHost/examples/dispatch-project', project,
                            ignore=shutil.ignore_patterns('bin', 'obj', '.krtcache'))
            compiled = subprocess.run([
                sys.executable, str(ROOT / 'SelfHost/compile.py'), 'build', str(project),
                '--compiler', str(COMPILER)], capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(project / 'bin/release/DispatchDemo')],
                                      capture_output=True, text=True, timeout=10)
            self.assertEqual(executed.returncode, 0, executed.stderr)
            self.assertEqual(executed.stdout, 'reading verified\ninterface collection verified\n')

    def test_generic_float_collection_and_exception_project(self):
        with tempfile.TemporaryDirectory(prefix='krt-language-project-') as directory:
            project = Path(directory) / 'demo'
            shutil.copytree(ROOT / 'Test/SelfHost/examples/language-project', project,
                            ignore=shutil.ignore_patterns('bin', 'obj', '.krtcache'))
            compiled = subprocess.run([
                sys.executable, str(ROOT / 'SelfHost/compile.py'), 'build', str(project),
                '--compiler', str(COMPILER)], capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(project / 'bin/release/LanguageDemo')],
                                      capture_output=True, text=True, timeout=10)
            self.assertEqual(executed.returncode, 0, executed.stderr)
            self.assertEqual(executed.stdout, 'average verified\nempty input\n')


if __name__ == '__main__':
    unittest.main()
