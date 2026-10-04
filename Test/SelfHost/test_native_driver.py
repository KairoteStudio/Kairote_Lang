"""Native CLI integration: each compiler invocation receives original files.

Python only creates fixtures and checks subprocess results. Compiler runs have
an empty PATH and call the generated executable directly.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get("SELFHOST_COMPILER", ROOT / "build/selfhost/stage2/program")).resolve()
LINKER = Path(os.environ.get("ARKLINK", ROOT / "build/ArkLink/ArkLink")).resolve()
if 'ARKLINK' not in os.environ and not LINKER.is_file():
    LINKER = ROOT / "ArkLink/build/ArkLink"


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), "build the native compiler and ArkLink first")
class NativeDriverTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="krt native cli ")
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        empty = self.work / "empty-path"
        empty.mkdir()
        self.env = dict(os.environ, PATH=str(empty), KRT_NATIVE_TEST_OUTPUT="environment app")

    def write(self, name, source):
        path = self.work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        return path

    def command(self, *arguments, expected=0):
        result = subprocess.run([str(COMPILER), "--linker", str(LINKER), *map(str, arguments)],
                                cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def execute(self, path, expected):
        result = subprocess.run([str(path)], cwd=self.work, env=self.env,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_native_multi_file_dependencies_and_repeatable_object(self):
        main = self.write("src/main.krt", "using Helpers.*; int32 main(){return first()+second();}")
        self.write("modules/Helpers/A.krt", "int32 first(){return 19;}")
        self.write("modules/Helpers/Z.krt", "int32 second(){return 23;}")
        output = self.work / "native app"
        self.command(main, "-I", self.work / "modules", "-o", output)
        self.execute(output, 42)
        first, second = self.work / "first.kro", self.work / "second.kro"
        self.command(main, "-I", self.work / "modules", "-c", "-o", first)
        self.command(main, "-I", self.work / "modules", "-c", "-o", second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.command(first, "-o", output)
        self.execute(output, 42)

    def test_quoted_import_spaces_cycles_and_comments(self):
        main = self.write("src/main.krt", '/* using Missing; */ import "../shared/helper with spaces.krt"; '
                          'int32 main(){string text="using Missing;"; return helper();}')
        helper = self.write("shared/helper with spaces.krt", 'import "../src/main.krt"; int32 helper(){return 37;}')
        output = self.work / "quoted app"
        self.command(main, helper, "output", output)
        self.execute(output, 37)

    def test_namespace_reset_and_explicit_type_alias(self):
        first = self.write("first.krt", "namespace App.Model; class Item{public int32 value=42;}")
        main = self.write("main.krt", "using Alias=App.Model.Item; int32 main(){Alias item=new Alias();return item.value;}")
        output = self.work / "namespaces"
        self.command(main, first, "-o", output)
        self.execute(output, 42)

    def test_native_library_object_and_source_linking(self):
        library = self.write("library.krt", "int64 add(int64 n){return n+1;}")
        object_path = self.work / "library.kro"
        self.command(library, "-c", "-o", object_path)
        self.assertEqual(object_path.read_bytes()[:4], b"KRO\0")
        main = self.write("main.krt", "extern int64 add(int64 n); int32 main(){return (int32)add(41);}")
        output = self.work / "linked app"
        self.command(main, object_path, "-o", output)
        self.execute(output, 42)

    def test_native_utf8_diagnostic_and_preserved_destination(self):
        first = self.write("helper.krt", "// 中文\nint32 helper(){return 1;}")
        second = self.write("bad file.krt", "// 第二个文件\nint32 main(){return missing;}")
        output = self.write("existing app", "previous successful output")
        result = self.command(first, second, "-o", output, expected=1)
        self.assertIn(f"{second}:2:21:", result.stderr)
        self.assertIn("E_LOWER", result.stderr)
        self.assertNotIn("E_LOWER at byte", result.stderr)
        self.assertEqual(output.read_text(), "previous successful output")
        syntax = "int32 main(){/* 中文 */return (1+);}"
        broken = self.write("unicode.krt", syntax)
        result = self.command(broken, expected=1)
        column = syntax.index(")", syntax.index("return")) + 1
        self.assertIn(f"{broken}:1:{column}:", result.stderr)
        self.assertIn("E_PARSE", result.stderr)

    def test_native_input_errors_and_check(self):
        main = self.write("main.krt", "int32 main(){return 17;}")
        output = self.work / "checked app"
        self.command(main, "--check", "-o", output)
        self.assertFalse(output.exists())
        self.assertIn("E_OPTION", self.command(main, "-O4", expected=1).stderr)
        self.assertIn("E_OUTPUT", self.command(main, "-o", main, expected=1).stderr)
        missing = self.write("missing.krt", "using Missing; int32 main(){return 0;}")
        result = self.command(missing, expected=1)
        self.assertIn("E_IMPORT", result.stderr)
        self.assertIn(f"{missing}:1:1:", result.stderr)
        invalid = self.work / "invalid.krt"
        invalid.write_bytes(b"\xff")
        self.assertIn("E_ENCODING", self.command(invalid, expected=1).stderr)

    def test_empty_translation_units_emit_objects_and_require_executable_entry(self):
        for index, text in enumerate(("", "// comment without a newline", "// comment\n" * 6000)):
            source = self.write(f"empty-{index}.krt", text)
            object_path = self.work / f"empty-{index}.kro"
            self.command(source, "-c", "-o", object_path)
            self.assertEqual(object_path.read_bytes()[:4], b"KRO\0")
            output = self.write(f"existing-{index}", "previous output")
            result = self.command(source, "-o", output, expected=1)
            self.assertIn("E_MODULE", result.stderr)
            self.assertEqual(output.read_text(), "previous output")
        main = self.write("main.krt", "int32 main(){return 42;}")
        output = self.work / "linked-empty"
        self.command(main, *sorted(self.work.glob("empty-*.kro")), "-o", output)
        self.execute(output, 42)

    def test_native_project_build_cache_check_and_clean(self):
        source = self.write("src/main.krt", "int32 main(){return helper();}")
        self.write("modules/Helper.krt", "int32 helper(){return 17;}")
        source.write_text("using Helper; int32 main(){return helper();}")
        project = self.write("project.krt", 'function Configure(Project p){p.Name("native");'
                             'p.Sources("src/**/*.krt");p.Includes("modules");'
                             'if(config=="release"){p.Output("bin/native app");}'
                             'else{p.Output("bin/debug app");}}')
        output = self.work / "bin/native app"
        self.command("check", project)
        self.assertFalse(output.exists())
        self.command("build", project)
        self.execute(output, 17)
        first = output.read_bytes()
        second = self.command("build", project)
        self.assertIn("up-to-date:", second.stdout)
        self.assertEqual(output.read_bytes(), first)
        source.write_text("using Helper; int32 main(){return helper()+6;}")
        self.command("build", project)
        self.execute(output, 23)
        self.command("clean", project)
        self.assertFalse(output.exists())
        self.assertTrue(source.is_file())

    def test_native_project_environment_configuration(self):
        self.write("main.krt", "int32 main(){return 29;}")
        project = self.write("project.krt", 'function Configure(Project p){p.Sources("main.krt");'
                             'p.Output("bin/${config}/${env:KRT_NATIVE_TEST_OUTPUT}");}')
        self.command("build", project)
        self.execute(self.work / "bin/release/environment app", 29)


if __name__ == "__main__":
    unittest.main()
