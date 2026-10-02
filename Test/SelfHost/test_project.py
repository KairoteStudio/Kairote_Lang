"""Project lifecycle, cache invalidation and atomic outputs through the real CLI."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()
LINKER = ROOT / 'build/ArkLink/ArkLink'
if not LINKER.is_file():
    LINKER = ROOT / 'ArkLink/build/ArkLink'


@unittest.skipUnless(COMPILER.is_file(), 'build self-hosted compiler first')
class ProjectConfigurationTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='kairote project configuration ')
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        (self.work / 'src').mkdir()
        (self.work / 'src/main.krt').write_text('int32 main(){return 0;}')

    def config(self, text, filename='project.krt'):
        path = self.work / filename
        path.write_text(text)
        return path

    def load(self, path, configuration='release'):
        result = subprocess.run([str(COMPILER), 'project-info', str(path), '--config', configuration],
                                cwd=self.work, capture_output=True, text=True, timeout=30,
                                env=dict(os.environ, PATH=''))
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        data = json.loads(result.stdout)
        data['output'] = Path(data['output'])
        for key in ('sources', 'includes', 'objects'):
            data[key] = [Path(item) for item in data[key]]
        return SimpleNamespace(**data)

    def native(self, *arguments):
        return subprocess.run([str(COMPILER), *map(str, arguments)], cwd=self.work,
                              capture_output=True, timeout=30, env=dict(os.environ, PATH=''))

    def test_configure_variables_conditions_and_globs(self):
        path = self.config('''
            string base = "bin/";
            function Configure(Project settings) {
                settings.Name("My Project");
                settings.Type("console");
                settings.Sources("src/**/*.krt");
                if (os == "linux" && config != "debug") {
                    settings.Output(base + "${config}/${env:KRT_TEST_NAME}");
                } else { settings.Output("debug-program"); }
                settings.Includes("modules", "libs");
            }
        ''')
        with mock.patch.dict(os.environ, {'KRT_TEST_NAME': 'release app'}):
            result = self.load(path, 'release')
        self.assertEqual(result.output, self.work / 'bin/release/release app')
        self.assertEqual(result.sources, [self.work / 'src/main.krt'])
        self.assertEqual(result.includes, [self.work / 'modules', self.work / 'libs'])
        self.assertEqual(self.load(path, 'debug').output, self.work / f'bin/{sys.platform}/debug-program')

    def test_json_and_legacy_formats(self):
        path = self.config(json.dumps({'name': 'json app', 'sources': ['src/*.krt'],
                                       'output': 'bin/my program', 'optimization': 0}), 'project.json')
        self.assertEqual(self.load(path, 'debug').output, self.work / 'bin/my program')
        path = self.config('# comment\nproject: "legacy app"\ntype: console\nsources: src/*.krt\noutput: "my output"\n')
        self.assertEqual(self.load(path, 'release').output, self.work / f'bin/{sys.platform}/my output')

    def test_json_unicode_is_decoded_without_configuration_expansion(self):
        path = self.config('{"name":"\\u5f00\\u7f57","sources":["src/*.krt"],'
                           '"output":"bin/${config}/\\ud83d\\ude80"}', 'project.json')
        result = self.load(path, 'debug')
        self.assertEqual(result.name, '开罗')
        self.assertEqual(result.output, self.work / 'bin/${config}/🚀')

    def test_configuration_integer_values_are_not_truncated_to_machine_width(self):
        number = '12345678901234567890123456789012345678901234567890'
        path = self.config('function Configure(){Sources("src/*.krt");'
                           f'var version = {number}; if(version == {number} && true == 1 && true != version)'
                           '{Name(version);}else{Name("wrong");}}')
        self.assertEqual(self.load(path).name, number)

    def test_recursive_glob_is_sorted_and_deduplicated(self):
        (self.work / 'src/a').mkdir()
        (self.work / 'src/a/helper.krt').write_text('int32 helper(){return 7;}')
        path = self.config('function Configure(){Sources("src/**/*.krt", "src/main.krt");}')
        self.assertEqual(self.load(path).sources,
                         [self.work / 'src/a/helper.krt', self.work / 'src/main.krt'])

    def test_receiverless_rekrtc_configure_compatibility(self):
        path = self.config('function Configure(){ Name("App"); Output("Program"); Sources("src/*.krt"); }')
        self.assertEqual(self.load(path, 'release').output, self.work / f'bin/{sys.platform}/Program')

    def test_inactive_condition_can_use_its_local_configuration_variables(self):
        path = self.config('''function Configure(Project p) {
            p.Sources("src/*.krt"); string output = "release";
            if (config == "debug") {
                string prefix = "debug-"; output = prefix + "app"; p.Output(output);
            } else { p.Output(output); }
        }''')
        self.assertEqual(self.load(path, 'release').output.name, 'release')
        self.assertEqual(self.load(path, 'debug').output.name, 'debug-app')

    def test_errors_are_explicit_and_located(self):
        for source, expected in [
            ('function Configure(Project p){ p.Sources("missing/*.krt"); }', 'matched no files'),
            ('function Configure(Project p){ p.Sources("src/*.krt"); p.Optimization("4"); }', 'optimization level'),
            ('function Configure(Project p){ p.Sources("src/*.krt"); p.Output("${unknown}"); }', 'unknown variable'),
            ('function Configure(Project p){ p.Sorces("src/*.krt"); }', 'unknown project option'),
            ('function Configure(Project p){ while(true){} }', 'unsupported configuration statement'),
            ('function Configure(Project p){ p.Sources("src/*.krt") }', "expected ';'"),
            ('function Configure(Project p){ p.Sources("src/*.krt"); p.Target("pe"); }', 'project target'),
        ]:
            with self.subTest(expected=expected):
                path = self.config(source)
                with self.assertRaisesRegex(RuntimeError, expected):
                    self.load(path, 'release')

    def test_library_selection_prefers_source_and_deduplicates_glob(self):
        path = self.config('''function Configure(Project p) {
            p.Sources("src/*.krt", "src/main.krt"); p.Libraries("System.Console");
        }''')
        result = self.load(path, 'release')
        self.assertEqual(result.sources, [self.work / 'src/main.krt', ROOT / 'libs/System/Console.krt'])
        self.assertEqual(result.objects, [])

    def test_source_boundaries_preserve_native_namespace_scopes_and_diagnostics(self):
        first = self.work / 'src/first.krt'
        second = self.work / 'src/second.krt'
        first.write_text('namespace A; class Value {}')
        second.write_text('using A;\nint32 main(){ return missing; }')
        bundle = self.native(first, second, '--emit-source')
        self.assertEqual(bundle.returncode, 0, bundle.stderr)
        self.assertIn(b'namespace A; class Value {}\nnamespace;\nusing A;', bundle.stdout)
        checked = self.native(first, second, '--check')
        self.assertNotEqual(checked.returncode, 0)
        self.assertIn(f'{second}:2:22:', checked.stderr.decode())

    def test_type_alias_can_refer_to_another_explicit_source(self):
        first = self.work / 'src/first.krt'
        second = self.work / 'src/second.krt'
        first.write_text('namespace App.Model; class Value {}')
        second.write_text('using Item = App.Model.Value; int32 main(){return 0;}')
        bundle = self.native(second, first, '--emit-source')
        self.assertEqual(bundle.returncode, 0, bundle.stderr)
        self.assertIn(b'using Item = App.Model.Value;', bundle.stdout)
        listed = self.native(second, first, '--list-sources')
        self.assertEqual(listed.returncode, 0, listed.stderr)
        self.assertEqual(len(listed.stdout.decode().splitlines()), 2)


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), 'build self-hosted compiler and ArkLink first')
class ProjectIntegrationTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='kairote project integration ')
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        self.path = self.work / 'project.krt'
        self.path.write_text('''function Configure(Project p) {
            p.Name("demo"); p.Sources("main.krt"); p.Output("bin/demo");
        }''')
        self.source = self.work / 'main.krt'
        self.source.write_text('int32 main(){return 17;}')
        self.output = self.work / 'bin/demo'

    def command(self, action, *arguments, compiler=COMPILER, linker=LINKER):
        return subprocess.run([str(ROOT / 'SelfHost/krtc'), action,
                               str(self.path), '--compiler', str(compiler), '--linker', str(linker),
                               *map(str, arguments)], cwd=self.work, capture_output=True, text=True, timeout=60)

    def ok(self, result, message=None):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        if message:
            self.assertIn(message, result.stdout)

    def executes(self, status):
        self.assertEqual(subprocess.run([str(self.output)], timeout=10).returncode, status)

    def test_data_only_compilation_unit_has_real_data_and_symbols(self):
        (self.work / 'Data.krt').write_text('static int64 retained_data = 123456789; static uint128 retained_wide = (uint128)18446744073709551623;')
        self.path.write_text('function Configure(){Sources("main.krt", "Data.krt");Output("bin/demo");}')
        self.ok(self.command('build'))
        self.executes(17)
        path = self.work / 'obj/Data.kro'
        data = path.read_bytes()
        header = struct.unpack('<16I', data[:64])
        self.assertEqual((header[0], header[4], header[6], header[12]), (0x004f524b, 0, 24, 2))
        self.assertEqual(int.from_bytes(data[64:72], 'little'), 123456789)
        self.assertEqual(int.from_bytes(data[72:88], 'little'), 18446744073709551623)
        self.assertIn(b'retained_data\0', data)
        self.assertIn(b'retained_wide\0', data)
        path.unlink()
        self.ok(self.command('build'), 'up-to-date:')
        self.assertEqual(path.read_bytes(), data)
        self.ok(self.command('clean'))
        self.assertFalse(path.exists())

    def test_library_project_exports_callable_functions_without_main(self):
        self.source.write_text('int64 add(int64 a,int64 b){return a+b;} int64 twice(int64 a){return add(a,a);}')
        self.path.write_text('function Configure(){Type("library");Sources("main.krt");Output("bin/math");}')
        self.ok(self.command('check'))
        self.ok(self.command('build', linker=self.work / 'unused-linker'))
        data = (self.work / 'bin/math.kro').read_bytes()
        header = struct.unpack('<16I', data[:64])
        self.assertEqual(header[12], 2)
        self.assertEqual(data[64], 0x55)  # Function prologue, no executable startup.
        symbols = 64 + sum(header[4:7])
        names = data[symbols + header[12] * 32:]
        entries = {}
        for index in range(header[12]):
            symbol = struct.unpack_from('<8I', data, symbols + index * 32)
            name = names[symbol[0]:].split(b'\0', 1)[0].decode()
            entries[name] = symbol[1]
            self.assertGreater(symbol[2], 0)
            self.assertEqual(symbol[3:6], (1, 1, 1))
        self.assertEqual(set(entries), {'_KRT1$add$i64;i64;$i64', '_KRT1$twice$i64;$i64'})
        entries = {name.split('$')[1]: offset for name, offset in entries.items()}
        code = "import mmap,ctypes; b=bytes.fromhex(%r); m=mmap.mmap(-1,len(b),prot=7); m.write(b); p=ctypes.addressof(ctypes.c_char.from_buffer(m)); add=ctypes.CFUNCTYPE(ctypes.c_longlong,ctypes.c_longlong,ctypes.c_longlong)(p+%d); twice=ctypes.CFUNCTYPE(ctypes.c_longlong,ctypes.c_longlong)(p+%d); assert add(19,23)==42; assert twice(21)==42" % (data[64:64 + header[4]].hex(), entries['add'], entries['twice'])
        result = subprocess.run([sys.executable, '-c', code], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_library_data_cast_normalization_and_runtime_globals_rejection(self):
        self.source.write_text('static int64 x=(int8)255;')
        self.path.write_text('function Configure(){Type("library");Sources("main.krt");Output("bin/math");}')
        self.ok(self.command('build'))
        data = (self.work / 'bin/math.kro').read_bytes()
        self.assertEqual(int.from_bytes(data[64:72], 'little', signed=True), -1)
        self.source.write_text('int64 x=1; int64 read(){return x;}')
        self.assertNotEqual(self.command('build').returncode, 0)
        self.assertEqual((self.work / 'bin/math.kro').read_bytes(), data)

    def test_library_integer_constants_keep_expression_and_storage_types(self):
        self.path.write_text('function Configure(){Type("library");Sources("main.krt");Output("bin/math");}')
        for source, expected, signed in [
            ('int64 value=2147483647+1;', -2147483648, True),
            ('uint64 value=18446744073709551615/3;', 6148914691236517205, False),
            ('bool value=18446744073709551615>0;', 1, False),
            ('bool value=2;', 1, False),
            ('uint64 value=(uint40)1099511627775;', 1099511627775, False),
            ('int64 value=true ? 4294967296 : 8;', 4294967296, True),
            ('int64 value=false ? 8 : 4294967296;', 4294967296, True),
        ]:
            with self.subTest(source=source):
                self.source.write_text(source)
                self.ok(self.command('build'))
                data = (self.work / 'bin/math.kro').read_bytes()
                header = struct.unpack('<16I', data[:64])
                self.assertEqual(int.from_bytes(data[64:64 + header[6]], 'little', signed=signed), expected)
        self.source.write_text('int32 value=1; int32 value=2;')
        self.assertNotEqual(self.command('build').returncode, 0)

    def test_build_check_cache_restore_and_clean(self):
        self.ok(self.command('check'), 'checked:')
        self.assertFalse(self.output.exists())
        self.assertFalse((self.work / '.krtcache').exists())
        self.ok(self.command('build'), 'built:')
        self.executes(17)
        original = self.output.read_bytes()
        before = self.output.stat().st_mtime_ns
        self.ok(self.command('build'), 'up-to-date:')
        self.assertEqual(self.output.stat().st_mtime_ns, before)
        self.output.write_bytes(b'corrupted output')
        self.ok(self.command('build'), 'up-to-date:')
        self.assertEqual(self.output.read_bytes(), original)
        self.executes(17)
        self.ok(self.command('build', '--rebuild'), 'built:')
        self.ok(self.command('clean'), 'removed:')
        self.assertFalse(self.output.exists())
        self.assertTrue(self.source.is_file())

    def test_source_configuration_compiler_and_linker_invalidate_cache(self):
        compiler, linker = self.work / 'compiler', self.work / 'linker'
        shutil.copy2(COMPILER, compiler)
        shutil.copy2(LINKER, linker)
        self.ok(self.command('build', compiler=compiler, linker=linker), 'built:')
        self.source.write_text('int32 main(){return 23;}')
        self.ok(self.command('build', compiler=compiler, linker=linker), 'built:')
        self.executes(23)
        for tool in (compiler, linker):
            with tool.open('ab') as stream:
                stream.write(b'\0')
            self.ok(self.command('build', compiler=compiler, linker=linker), 'built:')
        self.path.write_text(self.path.read_text() + '\n// configuration changed\n')
        self.ok(self.command('build', compiler=compiler, linker=linker), 'built:')
        self.ok(self.command('build', compiler=compiler, linker=linker), 'up-to-date:')

    def test_import_changes_rebuild_and_failed_build_preserves_successful_output(self):
        helper = self.work / 'Helper.krt'
        helper.write_text('int32 answer(){return 19;}')
        self.source.write_text('using Helper; int32 main(){return answer();}')
        self.ok(self.command('build'), 'built:')
        self.executes(19)
        helper.write_text('int32 answer(){return 29;}')
        self.ok(self.command('build'), 'built:')
        self.executes(29)
        original = self.output.read_bytes()
        helper.write_text('int32 answer(){return missing;}')
        result = self.command('build')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(helper), result.stderr)
        self.assertEqual(self.output.read_bytes(), original)
        helper.write_text('int32 answer(){return 31;}')
        result = self.command('build', linker=Path('/bin/false'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('E_LINK:', result.stderr)
        self.assertEqual(self.output.read_bytes(), original)

    def test_clean_preserves_replaced_output_and_handles_deleted_sources(self):
        self.ok(self.command('build'))
        self.output.write_text('user replaced this file')
        self.source.unlink()
        self.path.write_text('configuration was edited while the sources were removed')
        self.ok(self.command('clean'), 'cleaned:')
        self.assertEqual(self.output.read_text(), 'user replaced this file')

    def test_output_cannot_overwrite_project_input(self):
        original = self.path.read_bytes()
        result = self.command('build', '-o', self.path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('overwrite an input', result.stderr)
        self.assertEqual(self.path.read_bytes(), original)

    def test_keep_temp_cache_restore_and_primary_kro_filename(self):
        output = self.work / 'main.kro'
        retained = self.work / 'main.kro.kro'
        self.ok(self.command('build', '--keep-temp', '-o', output))
        self.assertEqual(output.read_bytes()[:4], b'\x7fELF')
        self.assertNotEqual(retained.read_bytes()[:4], b'\x7fELF')
        expected = retained.read_bytes()
        retained.unlink()
        self.ok(self.command('build', '--keep-temp', '-o', output), 'up-to-date:')
        self.assertEqual(retained.read_bytes(), expected)
        self.ok(self.command('clean'))
        self.assertFalse(output.exists())
        self.assertFalse(retained.exists())

    def test_new_project_build_and_default_discovery(self):
        destination = self.work / 'My First App'
        result = subprocess.run([str(ROOT / 'SelfHost/krtc'), 'new', 'console', str(destination)],
                                capture_output=True, text=True, timeout=10)
        self.ok(result, 'created:')
        result = subprocess.run([str(ROOT / 'SelfHost/krtc'), 'build', '--compiler', str(COMPILER),
                                 '--linker', str(LINKER)], cwd=destination, capture_output=True, text=True, timeout=60)
        self.ok(result, 'built:')
        output = destination / 'bin/release/My First App'
        executed = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
        self.assertEqual(executed.returncode, 0)
        self.assertEqual(executed.stdout, 'Hello, Kairote!\n')
        result = subprocess.run([str(ROOT / 'SelfHost/krtc'), 'new', 'console', str(destination)],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not empty', result.stderr)


if __name__ == '__main__':
    unittest.main()
