"""Daily CLI and module-loading regressions, using the self-hosted compiler."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program'))
LINKER = Path(os.environ.get('ARKLINK', ROOT / 'build/ArkLink/ArkLink')).resolve()
if 'ARKLINK' not in os.environ and not LINKER.exists():
    LINKER = ROOT / 'ArkLink/build/ArkLink'


@unittest.skipUnless(COMPILER.is_file(), 'build the native self-hosted compiler first')
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

    def command(self, *arguments, expected=0):
        result = subprocess.run([str(COMPILER), '--linker', str(LINKER), *map(str, arguments)],
                                cwd=self.work, capture_output=True, timeout=30,
                                env=dict(os.environ, PATH=''))
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def collect(self, paths, include_dirs=()):
        arguments = [*paths, '--list-sources']
        for directory in include_dirs:
            arguments.extend(['-I', directory])
        return [Path(line) for line in self.command(*arguments).stdout.decode('utf-8').splitlines()]

    def combined(self, paths, include_dirs=()):
        arguments = [*paths, '--emit-source']
        for directory in include_dirs:
            arguments.extend(['-I', directory])
        return self.command(*arguments).stdout

    def test_dependency_order_dedup_and_cycles(self):
        main = self.write('main.krt', 'using B; using A; int32 main() { return a()+b(); }')
        a = self.write('A.krt', 'using Shared; int32 a() { return shared(); }')
        b = self.write('B.krt', 'using Shared; int32 b() { return shared(); }')
        shared = self.write('Shared.krt', 'int32 shared() { return 7; }')
        self.assertEqual(self.collect([main, a]), [shared, b, a, main])
        bundle = self.combined([main, a])
        self.assertEqual(bundle, self.combined([main, a]))
        self.assertEqual(bundle.count(b'int32 shared()'), 1)
        shared.write_text('using A; int32 shared() { return 7; }')
        self.assertEqual(self.collect([main]), [a, shared, b, main])
        cyclic = self.combined([main])
        self.assertEqual(cyclic.count(b'int32 shared()'), 1)
        self.assertEqual(cyclic, self.combined([main]))

    def test_directory_namespace_and_wildcard_order(self):
        main = self.write('main.krt', 'using Helpers.*; int32 main() { return 0; }')
        z = self.write('Helpers/Z.krt', 'int32 z() { return 1; }')
        a = self.write('Helpers/A.krt', 'int32 a() { return 2; }')
        self.write('Helpers/nested/Ignored.krt', 'int32 ignored() { return 3; }')
        self.assertEqual(self.collect([main]), [a, z, main])
        main.write_text('using Helpers; int32 main() { return 0; }')
        self.assertEqual(self.collect([main]), [a, z, main])

    def test_symlink_aliases_are_deduplicated_in_cycles(self):
        shared = self.write('Shared.krt', 'import "linked.krt"; int32 shared(){return 7;}')
        (self.work / 'linked.krt').symlink_to(shared)
        main = self.write('main.krt', 'import "linked.krt"; import "Shared.krt"; '
                          'int32 main(){return shared();}')
        self.assertEqual(self.collect([main]), [shared, main])
        self.assertEqual(self.combined([main]).count(b'int32 shared()'), 1)

    def test_empty_units_preserve_file_boundaries(self):
        empty = self.write('empty.krt', '')
        main = self.write('main.krt', 'int32 main(){return 0;}')
        self.assertEqual(self.collect([empty, main]), [empty, main])
        self.assertEqual(self.combined([empty, main]), b'\nnamespace;\nint32 main(){return 0;}')

    def test_relative_quoted_import_and_search_precedence(self):
        main = self.write('src/main.krt', 'import "../shared/helper with spaces.krt"; using Choice; int32 main() { return 0; }')
        helper = self.write('shared/helper with spaces.krt', 'int32 f() { return 9; }')
        choice = self.write('include/Choice.krt', 'int32 choice() { return 1; }')
        self.write('later/Choice.krt', 'int32 choice() { return 2; }')
        bundle = self.collect([main], [self.work / 'include', self.work / 'later'])
        self.assertEqual(bundle, [helper, choice, main])
        local = self.write('src/Choice.krt', 'int32 choice() { return 3; }')
        self.assertEqual(self.collect([main], [self.work / 'include'])[1], local)

    def test_comments_strings_and_unsafe_are_not_dependencies(self):
        text = ('// using Missing;\n/* import "also missing.krt"; */\n'
                'int32 main() { string s="using Nothing;"; '
                'unsafe(using krt.mem;) { return 7; } }')
        main = self.write('main.krt', text)
        self.assertEqual(self.combined([main]), text.encode())
        self.assertEqual(self.collect([main]), [main])
        for text in ('/* using Missing;', '"using Missing;'):
            main.write_text(text)
            self.assertEqual(self.combined([main]), text.encode())

    def test_missing_import_and_alias_have_source_diagnostics(self):
        main = self.write('main.krt', '// 注释\nusing Missing;\nint32 main() { return 0; }')
        result = self.command(main, '--list-sources', expected=1)
        self.assertRegex(result.stderr.decode(), r'main.krt:2:1: E_IMPORT: cannot find module')
        main.write_text('using Alias = Missing; int32 main() { return 0; }')
        result = self.command(main, '--list-sources', expected=1)
        self.assertIn("E_IMPORT: cannot find module 'Missing'", result.stderr.decode())
        missing = self.write('Missing.krt', 'namespace Missing; class Value {}')
        self.assertEqual(self.collect([main]), [missing, main])
        self.assertIn(b'using Alias = Missing;', self.combined([main]))

    def test_utf8_multifile_diagnostics_and_preserved_offsets(self):
        first = self.write('first.krt', '// 中文\nint32 helper() { return 1; }')
        second_text = '// 第二个文件\nint32 main() { /* 中文 */ return missing; }'
        second = self.write('second.krt', second_text)
        expected_column = len(second_text.splitlines()[1].split('missing')[0]) + 1
        diagnostic = self.command(first, second, '--check', expected=1).stderr.decode()
        self.assertIn(f'{second}:2:{expected_column}: E_LOWER:', diagnostic)
        dependency = self.write('dep.krt', 'int32 dep() { return 1; }')
        original = 'using /* 中文 */ dep;\nint32 main() { return dep(); }'
        second.write_text(original)
        combined = self.combined([second])
        mapped = combined[combined.index(b'\nnamespace;\n') + 12:]
        self.assertEqual(len(mapped), len(original.encode()))
        self.assertEqual(mapped.index(b'int32'), original.encode().index(b'int32'))
        self.assertEqual(self.collect([second])[0], dependency)

    def test_invalid_utf8_is_reported(self):
        main = self.work / 'invalid.krt'
        main.write_bytes(b'\xff')
        result = self.command(main, '--list-sources', expected=1)
        self.assertIn('E_ENCODING: invalid UTF-8 at byte 0', result.stderr.decode())

    def test_source_size_budget_includes_dependencies(self):
        main = self.write('main.krt', 'using Helper; int32 main() { return helper(); }')
        prefix = 'int32 helper() { return 1; }'
        self.write('Helper.krt', prefix + ' ' * (16777216 - len(prefix)))
        result = self.command(main, '--list-sources', expected=1)
        self.assertIn('E_SIZE: combined source exceeds', result.stderr.decode())

    def test_namespace_scoped_imports_and_explicit_type_aliases(self):
        helper = self.write('helper.krt', 'int32 helper(){return 7;}')
        main = self.write('main.krt', 'namespace App { import "helper.krt"; class Marker{} } '
                          'using Alias=Model.Value; int32 main(){return helper();}')
        types = self.write('types.krt', 'namespace Model; class Value{}')
        self.assertEqual(self.collect([main, types]), [helper, main, types])
        self.command(main, types, '--check')

    def test_commented_qualified_alias_binds_native_type(self):
        types = self.write('types.krt', 'namespace App /* scope */ . Model; '
                           'class Value{public int32 result=42; public static int32 Read(){return 42;}}')
        main = self.write('main.krt', 'using Chosen = App /* alias */ . // path\nModel.Value; '
                          'using M = App /* namespace alias */ .Model; '
                          'int32 main(){Chosen item=new Chosen();'
                          'return (item.result+M /* qualified */ .Value.Read())/2;}')
        output = self.work / 'commented alias'
        self.command(main, types, '-o', output)
        self.assertEqual(subprocess.run([str(output)], cwd=self.work, timeout=10).returncode, 42)

    def test_unfinished_lexemes_cannot_close_in_a_later_file(self):
        first = self.write('first.krt', '/*')
        second = self.write('second.krt', '*/ int32 main(){return 0;}')
        # Introspection preserves source bytes, while compilation checks each
        # original unit before concatenation can repair the invalid comment.
        self.assertIn(b'/*\nnamespace;\n*/', self.combined([first, second]))
        result = self.command(first, second, '--check', expected=1)
        self.assertIn(f'{first}:1:1: E_PARSE:', result.stderr.decode())


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
        return subprocess.run([str(ROOT / 'SelfHost/krtc'), '--compiler', str(COMPILER),
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

    def test_object_then_native_link(self):
        source = self.write('program.krt', 'int32 main() { return 31; }')
        self.succeeds(self.compile(source, '-c'))
        kro = source.with_suffix('.kro')
        self.assertEqual(kro.read_bytes()[:4], b'KRO\0')
        self.assertEqual(kro.stat().st_mode & 0o111, 0)
        binary = self.work / 'linked'
        self.succeeds(self.compile(kro, '-o', binary))
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
        result = self.compile(source, '-O4')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('optimization level', result.stderr)
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
