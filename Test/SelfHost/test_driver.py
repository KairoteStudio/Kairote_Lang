"""Daily CLI and module-loading regressions, using the self-hosted compiler."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from SelfHost import Compile as driver

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program'))
LINKER = ROOT / 'build/ArkLink/ArkLink'
if not LINKER.exists():
    LINKER = ROOT / 'ArkLink/build/ArkLink'


class SourceLoadingTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='krt source loading ')
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)

    def write(self, name, text):
        path = self.work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def test_dependency_order_dedup_and_cycles(self):
        main = self.write('main.krt', 'using B; using A; int32 main() { return a()+b(); }')
        a = self.write('A.krt', 'using Shared; int32 a() { return shared(); }')
        b = self.write('B.krt', 'using Shared; int32 b() { return shared(); }')
        shared = self.write('Shared.krt', 'int32 shared() { return 7; }')
        bundle = driver.collect_sources([main, a])
        self.assertEqual([part.path for part in bundle.files], [shared, b, a, main])
        self.assertEqual(bundle.data, driver.collect_sources([main, a]).data)
        self.assertEqual(bundle.data.count(b'int32 shared()'), 1)
        shared.write_text('using A; int32 shared() { return 7; }')
        cyclic = driver.collect_sources([main])
        self.assertEqual([part.path for part in cyclic.files], [a, shared, b, main])
        self.assertEqual(cyclic.data.count(b'int32 shared()'), 1)
        self.assertEqual(cyclic.data, driver.collect_sources([main]).data)

    def test_directory_namespace_and_wildcard_order(self):
        main = self.write('main.krt', 'using Helpers.*; int32 main() { return 0; }')
        z = self.write('Helpers/Z.krt', 'int32 z() { return 1; }')
        a = self.write('Helpers/A.krt', 'int32 a() { return 2; }')
        self.write('Helpers/nested/Ignored.krt', 'int32 ignored() { return 3; }')
        self.assertEqual([part.path for part in driver.collect_sources([main]).files], [a, z, main])
        main.write_text('using Helpers; int32 main() { return 0; }')
        self.assertEqual([part.path for part in driver.collect_sources([main]).files], [a, z, main])

    def test_relative_quoted_import_and_search_precedence(self):
        main = self.write('src/main.krt', 'import "../shared/helper with spaces.krt"; using Choice; int32 main() { return 0; }')
        helper = self.write('shared/helper with spaces.krt', 'int32 f() { return 9; }')
        choice = self.write('include/Choice.krt', 'int32 choice() { return 1; }')
        self.write('later/Choice.krt', 'int32 choice() { return 2; }')
        bundle = driver.collect_sources([main], [self.work / 'include', self.work / 'later'])
        self.assertEqual([part.path for part in bundle.files], [helper, choice, main])
        local = self.write('src/Choice.krt', 'int32 choice() { return 3; }')
        self.assertEqual(driver.collect_sources([main], [self.work / 'include']).files[1].path, local)

    def test_comments_strings_and_unsafe_are_not_dependencies(self):
        text = ('// using Missing;\n/* import "also missing.krt"; */\n'
                'int32 main() { string s="using Nothing;"; '
                'unsafe(using krt.mem;) { return 7; } }')
        main = self.write('main.krt', text)
        bundle = driver.collect_sources([main])
        self.assertEqual(bundle.data, text.encode())
        self.assertEqual(len(bundle.files), 1)
        for text in ('/* using Missing;', '"using Missing;'):
            main.write_text(text)
            self.assertEqual(driver.collect_sources([main]).data, text.encode())

    def test_missing_import_and_alias_have_source_diagnostics(self):
        main = self.write('main.krt', '// 注释\nusing Missing;\nint32 main() { return 0; }')
        with self.assertRaisesRegex(driver.DriverError, r'main.krt:2:1: E_IMPORT: cannot find module'):
            driver.collect_sources([main])
        main.write_text('using Alias = Missing; int32 main() { return 0; }')
        with self.assertRaisesRegex(driver.DriverError, "E_IMPORT: cannot find module 'Missing'"):
            driver.collect_sources([main])
        missing = self.write('Missing.krt', 'namespace Missing; class Value {}')
        bundle = driver.collect_sources([main])
        self.assertEqual([source.path for source in bundle.files], [missing, main])
        self.assertIn(b'using Alias = Missing;', bundle.data)

    def test_utf8_multifile_diagnostics_and_preserved_offsets(self):
        first = self.write('first.krt', '// 中文\nint32 helper() { return 1; }')
        second_text = '// 第二个文件\nint32 main() { /* 中文 */ return missing; }'
        second = self.write('second.krt', second_text)
        bundle = driver.collect_sources([first, second])
        local_offset = second_text.encode().index(b'missing')
        offset = bundle.files[1].start + local_offset
        expected_column = len(second_text.splitlines()[1].split('missing')[0]) + 1
        diagnostic = bundle.diagnostic(f'E_LOWER at byte {offset}\n')
        self.assertIn(f'{second}:2:{expected_column}: E_LOWER:', diagnostic)
        dependency = self.write('dep.krt', 'int32 dep() { return 1; }')
        original = 'using /* 中文 */ dep;\nint32 main() { return dep(); }'
        second.write_text(original)
        mapped = driver.collect_sources([second]).files[-1]
        self.assertEqual(len(mapped.transformed), len(original.encode()))
        self.assertEqual(mapped.transformed.index(b'int32'), original.encode().index(b'int32'))
        self.assertEqual(driver.collect_sources([second]).files[0].path, dependency)

    def test_invalid_utf8_is_reported(self):
        main = self.work / 'invalid.krt'
        main.write_bytes(b'\xff')
        with self.assertRaisesRegex(driver.DriverError, 'E_ENCODING: invalid UTF-8 at byte 0'):
            driver.collect_sources([main])

    def test_source_size_budget_includes_dependencies(self):
        main = self.write('main.krt', 'using Helper; int32 main() { return helper(); }')
        self.write('Helper.krt', 'int32 helper() { return 1; }')
        with mock.patch.object(driver, 'MAX_SOURCE_BYTES', 50):
            with self.assertRaisesRegex(driver.DriverError, 'E_SIZE: combined source exceeds'):
                driver.collect_sources([main])

    def test_atomic_publish_failure_preserves_destination(self):
        old = self.write('output', 'previous successful output')
        artifact = self.write('artifact', 'new output')
        with mock.patch.object(driver.shutil, 'copyfileobj', side_effect=OSError('disk full')):
            with self.assertRaisesRegex(OSError, 'disk full'):
                driver.publish(artifact, old, True)
        self.assertEqual(old.read_text(), 'previous successful output')
        self.assertFalse(list(self.work.glob('.output.*')))


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), 'build self-hosted Stage 2 and ArkLink first')
class DriverIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='krt daily cli ')
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)

    def write(self, name, source):
        path = self.work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)
        return path

    def compile(self, *arguments):
        return subprocess.run([sys.executable, str(ROOT / 'SelfHost/Compile.py'), '--compiler', str(COMPILER),
                               '--linker', str(LINKER), *map(str, arguments)], cwd=self.work,
                              capture_output=True, text=True, timeout=30)

    def succeeds(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def executes(self, path, expected):
        self.assertEqual(subprocess.run([str(path)], timeout=10).returncode, expected)

    def test_default_output_and_positional_output_with_spaces(self):
        source = self.write('program with spaces.krt', 'int32 main() { return 29; }')
        self.succeeds(self.compile(source))
        self.executes(self.work / 'program with spaces', 29)
        binary = self.work / 'output with spaces'
        self.succeeds(self.compile(source, 'output', binary))
        self.executes(binary, 29)

    def test_object_then_link_does_not_require_compiler(self):
        source = self.write('program.krt', 'int32 main() { return 31; }')
        self.succeeds(self.compile(source, '-c'))
        kro = source.with_suffix('.kro')
        self.assertEqual(kro.read_bytes()[:4], b'KRO\0')
        self.assertEqual(kro.stat().st_mode & 0o111, 0)
        binary = self.work / 'linked'
        self.succeeds(self.compile(kro, '-o', binary, '--compiler', '/nonexistent/compiler'))
        self.executes(binary, 31)

    def test_imported_helper_executes_and_repeated_build_is_identical(self):
        main = self.write('src/main.krt', 'using Helper; int32 main() { return twice(19); }')
        self.write('modules/Helper.krt', 'int32 twice(int32 n) { return n*2; }')
        binary = self.work / 'program'
        self.succeeds(self.compile(main, '-I', self.work / 'modules', '-o', binary))
        self.executes(binary, 38)
        first = self.work / 'first.kro'
        second = self.work / 'second.kro'
        self.succeeds(self.compile(main, '-I', self.work / 'modules', '-c', '-o', first))
        self.succeeds(self.compile(main, '-I', self.work / 'modules', '-c', '-o', second))
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_diagnostics_map_second_file_and_preserve_output(self):
        first = self.write('helper.krt', '// 中文\nint32 helper() { return 1; }')
        second = self.write('bad file.krt', '// 第二个文件\nint32 main() { return missing; }')
        output = self.write('program', 'old successful binary')
        result = self.compile(first, second, '-o', output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f'{second}:2:', result.stderr)
        self.assertIn('E_LOWER:', result.stderr)
        self.assertNotIn('E_LOWER at byte', result.stderr)
        self.assertEqual(output.read_text(), 'old successful binary')

    def test_option_and_input_errors_preserve_output(self):
        source = self.write('main.krt', 'int32 main() { return 0; }')
        output = self.write('main', 'existing')
        result = self.compile(source, '-O2')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('native optimizer', result.stderr)
        self.assertEqual(output.read_text(), 'existing')
        result = self.compile(source, '-o', source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('overwrite an input file', result.stderr)
        result = self.compile(source, '--linker', '/nonexistent/linker')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_text(), 'existing')

    def test_parse_diagnostic_column_accounts_for_utf8(self):
        text = 'int32 main() { /* 中文 */ return (1 + ); }'
        source = self.write('unicode.krt', text)
        result = self.compile(source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f'{source}:1:{text.index(")", text.index("return")) + 1}: E_PARSE:', result.stderr)


if __name__ == '__main__':
    unittest.main()
