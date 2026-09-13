"""Transitive library import worklists, cycle termination, and error propagation."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from Support import COMPILER, LEVELS, ROOT, LibraryTestCase


class LibraryImports(LibraryTestCase):
    def rejected_source(self, source, extra_libraries, diagnostic):
        with tempfile.TemporaryDirectory(prefix="kairote-library-import-error-") as directory:
            work = Path(directory)
            shutil.copytree(ROOT / "libs", work / "libs", ignore=shutil.ignore_patterns("*.kro", "*.o"))
            for relative, contents in extra_libraries.items():
                path = work / "libs" / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
            source_path = work / "test_import.krt"
            source_path.write_text(source)
            for level in LEVELS:
                with self.subTest(optimization=level):
                    result = subprocess.run([str(COMPILER), f"-O{level}", str(source_path), "-o", str(work / "Program")],
                                            cwd=work, capture_output=True, text=True, timeout=30)
                    self.assertGreater(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(diagnostic, result.stdout + result.stderr)
                    self.assertNotIn("AddressSanitizer", result.stderr)
                    self.assertNotIn("runtime error:", result.stderr)

    def test_single_class_import_keeps_transitive_implementations(self):
        self.run_source('''
using System.Convert;
int32 main() {
    int64 value = 0;
    if (!Convert.TryToInt64("-9223372036854775808", ref value)) { return 1; }
    string text = Convert.ToString(value);
    if (!StringOps.Equals(text, "-9223372036854775808")) { return 2; }
    unsafe(using krt.mem;) {
        Memory.Free((void*)(int64)text, (int64)StringOps.Length(text) + 1);
    }
    return 0;
}
''')

    def test_compatibility_entry_points_share_one_runtime(self):
        self.run_source('''
using Stdlib;
using System.Runtime.Sys;
using System.Sys;
using System;
using System;
int32 main() {
    if (MathOps.Sqrt((int64)9223372036854775807) != (int64)3037000499) { return 1; }
    if (Sys.InternalStringLength("same runtime") != 12) { return 2; }
    Console.Write("ok");
    return 0;
}
''', expected_stdout="ok")

    def test_duplicate_and_self_imports_terminate(self):
        self.run_source('''
using Fixture.Self;
using Fixture.Self;
int32 main() { return SelfImport.Value() == 17 ? 0 : 1; }
''', extra_libraries={"Fixture/Self.krt": '''
using Fixture.Self;
using Fixture.Self;
public static class SelfImport {
    public static int32 Value() { return 17; }
}
'''})

    def test_string_and_null_ternary_selects_the_string_overload(self):
        self.run_source('''
using System.String;
public static class NullableString {
    public static int32 Read(string value) { return StringOps.Length(value); }
    public static int32 Read(int32 value) { return -1; }
}
int32 main() {
    bool choose = true;
    if (NullableString.Read(choose ? "value" : null) != 5) { return 1; }
    if (NullableString.Read(choose ? null : "value") != 0) { return 2; }
    choose = false;
    if (NullableString.Read(choose ? "value" : null) != 0) { return 3; }
    if (NullableString.Read(choose ? null : "value") != 5) { return 4; }
    return 0;
}
''')

    def test_numeric_and_null_arguments_preserve_string_overload_contracts(self):
        methods = [
            'public static int32 Select(char value) { return (int32)value; }',
            'public static int32 Select(string value) { return value == null ? -1 : -2; }',
        ]
        for reverse_order in (False, True):
            with self.subTest(reverse_order=reverse_order):
                ordered = reversed(methods) if reverse_order else methods
                declarations = "public static class CharOrString {\n" + "\n".join(ordered) + "\n}\n"
                self.run_source("using System.String;\n" + declarations + '''
int32 main() {
    if (StringOps.IndexOf("ab", 'a') != 0 || StringOps.IndexOf("ab", (byte)97) != 0) { return 1; }
    if (StringOps.IndexOf("ab", (int8)97) != 0 || StringOps.IndexOf("ab", (int16)97) != 0) { return 2; }
    if (StringOps.IndexOf("ab", (int32)97) != 0 || StringOps.LastIndexOf("aba", (byte)97) != 2) { return 3; }
    if (StringOps.IndexOf("ab", "b") != 1 || StringOps.IndexOf("ab", null) != -1) { return 4; }
    if (CharOrString.Select((byte)97) != 97 || CharOrString.Select((int16)97) != 97) { return 5; }
    if (CharOrString.Select((int8)97) != 97 || CharOrString.Select('a') != 97) { return 6; }
    if (CharOrString.Select(null) != -1 || CharOrString.Select("a") != -2) { return 7; }
    return 0;
}
''')
        self.rejected_source('''
public static class NumericOnly { public static int32 Select(char value) { return 0; } }
int32 main() { return NumericOnly.Select("a"); }
''', {}, "Undefined method")
        self.rejected_source('''
public static class StringOnly { public static int32 Select(string value) { return 0; } }
int32 main() { return StringOnly.Select((byte)97); }
''', {}, "Undefined method")

    def test_global_function_contracts_apply_inside_classes(self):
        self.run_source('''
int32 Change(ref int32 value) { value = 7; return value; }
public static class GlobalCaller {
    public static int32 Run() { int32 value = 1; Change(ref value); return value; }
}
public static class LocalCaller {
    public static int32 Change(int64 value) { return (int32)value + 1; }
    public static int32 Run() { return Change(8); }
}
int32 main() { return GlobalCaller.Run() == 7 && LocalCaller.Run() == 9 ? 0 : 1; }
''')
        cases = (
            ("int32 Change(ref int32 value) { value = 7; return value; }", "Change(0)", "writable"),
            ("int32 Change(int32* value) { return 0; }", "Change(null)", "nullable"),
            ("int32 Change(int32 value) { return value; }", "Change()", "argument count"),
        )
        for declaration, call, diagnostic in cases:
            with self.subTest(call=call, declaration=declaration):
                self.rejected_source(declaration + f'''
public static class InvalidCaller {{ public static int32 Run() {{ return {call}; }} }}
int32 main() {{ return InvalidCaller.Run(); }}
''', {}, diagnostic)

    def test_forward_only_and_empty_modules_need_no_linked_object(self):
        self.run_source('''
using Fixture.Forward;
using Fixture.Empty;
int32 main() { return ForwardLeaf.Value() == 23 ? 0 : 1; }
''', extra_libraries={
            "Fixture/Forward.krt": "using Fixture.Another;",
            "Fixture/Another.krt": "using Fixture.Leaf;",
            "Fixture/Empty.krt": "// This dependency deliberately has no definitions.\n",
            "Fixture/Leaf.krt": '''public static class ForwardLeaf {
    public static int32 Value() { return 23; }
}
''',
        })

    def test_forward_only_root_can_use_an_imported_entry_point(self):
        self.run_source("using Fixture.Program;", extra_libraries={
            "Fixture/Program.krt": "int32 main() { return 0; }",
        })

    def test_mutual_import_cycle_resolves_both_classes(self):
        self.run_source('''
using Fixture.Left;
int32 main() { return LeftImport.Value() == 42 && RightImport.Value() == 41 ? 0 : 1; }
''', extra_libraries={
            "Fixture/Left.krt": '''
using Fixture.Right;
public static class LeftImport {
    public static int32 Value() { return RightImport.Value() + 1; }
}
''',
            "Fixture/Right.krt": '''
using Fixture.Left;
public static class RightImport {
    public static int32 Value() { return 41; }
}
''',
        })

    def test_diamond_import_shares_dependency_without_duplicate_symbols(self):
        self.run_source('''
using Fixture.Left;
using Fixture.Right;
using Fixture.Leaf;
using Fixture.*;
int32 main() { return DiamondLeft.Value() + DiamondRight.Value() == 85 ? 0 : 1; }
''', extra_libraries={
            "Fixture/Left.krt": '''
using Fixture.Leaf;
public static class DiamondLeft {
    public static int32 Value() { return DiamondLeaf.Value() + 1; }
}
''',
            "Fixture/Right.krt": '''
using Fixture.Leaf;
public static class DiamondRight {
    public static int32 Value() { return DiamondLeaf.Value() + 2; }
}
''',
            "Fixture/Leaf.krt": '''
public static class DiamondLeaf {
    public static int32 Value() { return 41; }
}
''',
        })

    def test_transitive_chain_grows_the_using_worklist(self):
        libraries = {}
        depth = 24
        for index in range(depth):
            dependency = f"using Fixture.Level{index + 1};\n" if index + 1 < depth else ""
            value = f"ImportLevel{index + 1}.Value() + 1" if index + 1 < depth else "1"
            libraries[f"Fixture/Level{index}.krt"] = dependency + f'''public static class ImportLevel{index} {{
    public static int32 Value() {{ return {value}; }}
}}
'''
        self.run_source(f'''using Fixture.Level0;
int32 main() {{ return ImportLevel0.Value() == {depth} ? 0 : 1; }}
''', extra_libraries=libraries)

    def test_missing_dependency_in_a_cycle_is_reported(self):
        self.rejected_source("using Fixture.Left; int32 main() { return 0; }", {
            "Fixture/Left.krt": "using Fixture.Right;",
            "Fixture/Right.krt": "using Fixture.Left; using Fixture.Missing;",
        }, "Fixture/Missing")

    def test_project_link_skips_empty_and_forward_only_compilation_tasks(self):
        with tempfile.TemporaryDirectory(prefix="kairote-library-empty-project-") as directory:
            work = Path(directory)
            shutil.copytree(ROOT / "libs", work / "libs", ignore=shutil.ignore_patterns("*.kro", "*.o"))
            (work / "Main.krt").write_text("int32 main() { return 0; }")
            (work / "Empty.krt").write_text("// Empty compilation unit.\n" * 6000)
            (work / "Forward.krt").write_text("using System.Math;\n" + "// Forward-only compilation unit.\n" * 6000)
            (work / "project.krt").write_text('''function Configure() {
    Name("EmptyUnits");
    Output("Program");
    Sources("Main.krt", "Empty.krt", "Forward.krt");
}
''')
            for level in LEVELS:
                with self.subTest(optimization=level):
                    compiled = subprocess.run([str(COMPILER), f"-O{level}", "build", "project.krt"], cwd=work,
                                              capture_output=True, text=True, timeout=60)
                    self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                    self.assertNotIn("AddressSanitizer", compiled.stderr)
                    self.assertNotIn("runtime error:", compiled.stderr)
                    executed = subprocess.run([str(work / "bin/linux/Program")], capture_output=True, timeout=10)
                    self.assertEqual(executed.returncode, 0, executed.stderr)

    def test_invalid_transitive_source_is_not_silently_ignored(self):
        self.rejected_source("using Fixture.Forward; int32 main() { return 0; }", {
            "Fixture/Forward.krt": "using Fixture.Broken;",
            "Fixture/Broken.krt": "public static class Broken { public static int32 Value( { return 1; } }",
        }, "Broken.krt")

    def test_project_preserves_data_only_compilation_units(self):
        with tempfile.TemporaryDirectory(prefix="kairote-library-data-project-") as directory:
            work = Path(directory)
            (work / "Main.krt").write_text("int32 main() { return 0; }")
            (work / "Data.krt").write_text('''
static int64 retained_data = 123456789;
static uint128 retained_wide = (uint128)18446744073709551623;
''')
            (work / "project.krt").write_text('''function Configure() {
    Name("DataUnits");
    Output("Program");
    Sources("Main.krt", "Data.krt");
}
''')
            for level in LEVELS:
                with self.subTest(optimization=level):
                    compiled = subprocess.run([str(COMPILER), f"-O{level}", "build", "project.krt"], cwd=work,
                                              capture_output=True, text=True, timeout=60)
                    self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                    self.assertNotIn("AddressSanitizer", compiled.stderr)
                    self.assertNotIn("runtime error:", compiled.stderr)
                    object_data = (work / "obj/Data.kro").read_bytes()
                    self.assertGreaterEqual(len(object_data), 64)
                    header = struct.unpack_from("<16I", object_data)
                    self.assertEqual(header[0], 0x004F524B)
                    self.assertEqual(header[4], 0, "Data-only objects must have no executable entry sequence")
                    self.assertEqual(header[6], 24)
                    data_start = 64 + header[4] + header[5]
                    payload = object_data[data_start:data_start + header[6]]
                    self.assertEqual(payload, (123456789).to_bytes(8, "little") +
                                     (18446744073709551623).to_bytes(16, "little"))
                    self.assertIn(b"retained_data\0", object_data)
                    self.assertIn(b"retained_wide\0", object_data)
                    executed = subprocess.run([str(work / "bin/linux/Program")], capture_output=True, timeout=10)
                    self.assertEqual(executed.returncode, 0, executed.stderr)

    def test_overload_candidates_preserve_ref_and_array_storage(self):
        methods = [
            '''    public static int32 RefValue(ref int30 value, int64 extra) {
        value = 123;
        return extra == 4 ? 1 : 9;
    }
''',
            '''    public static int32 RefValue(ref int32 value, int32 extra) {
        value = 456;
        return 2;
    }
''',
            '''    public static int32 ArrayValue(byte[] values, int64 extra) {
        return values[1];
    }
''',
            '''    public static int32 ArrayValue(int16[] values, int32 extra) {
        return values[1];
    }
''',
            '''    public static int32 Select(byte[] values, int64 extra) {
        return 1;
    }
''',
            '''    public static int32 Select(int16[] values, int32 extra) {
        return 2;
    }
''',
        ]
        for reverse_order in (False, True):
            with self.subTest(reverse_order=reverse_order):
                ordered = reversed(methods) if reverse_order else methods
                self.run_source('''using Fixture.StorageOverloads;
int32 main() {
    int30 value = -7;
    int32 extra = 4;
    if (StorageOverloads.RefValue(ref value, extra) != 1 || value != 123) {
        return 1;
    }
    byte[] values = new byte[4];
    values[0] = 1;
    values[1] = 7;
    values[2] = 2;
    values[3] = 3;
    if (StorageOverloads.ArrayValue(values, extra) != 7) {
        return 2;
    }
    if (values[0] != 1 || values[1] != 7 || values[2] != 2 || values[3] != 3) {
        return 3;
    }
    if (StorageOverloads.Select(new byte[4], extra) != 1) {
        return 4;
    }
    if (StorageOverloads.ArrayValue(new byte[4], extra) != 0) {
        return 5;
    }
    byte[] alternate = new byte[4];
    alternate[1] = 19;
    for (int32 index = 0; index < 2; index++) {
        bool choose_values = index == 0;
        if (StorageOverloads.Select(choose_values ? values : alternate, extra) != 1) {
            return 6;
        }
        int32 expected = choose_values ? 7 : 19;
        if (StorageOverloads.ArrayValue(choose_values ? values : alternate, extra) != expected) {
            return 7;
        }
    }
    bool choose_values = true;
    if (StorageOverloads.ArrayValue(choose_values ? values : null, extra) != 7) {
        return 8;
    }
    choose_values = false;
    if (StorageOverloads.ArrayValue(choose_values ? null : values, extra) != 7) {
        return 9;
    }
    return 0;
}
''', extra_libraries={
                    "Fixture/StorageOverloads.krt":
                        "public static class StorageOverloads {\n" + "".join(ordered) + "}\n",
                })
        self.rejected_source('''int32 main() {
    byte[] narrow = new byte[2];
    int16[] wide = new int16[2];
    var values = true ? narrow : wide;
    return 0;
}
''', {}, "Conditional array operands")
        self.rejected_source('''int32 main() {
    byte[] values = new byte[2];
    var selected = true ? values : 1;
    return 0;
}
''', {}, "Conditional array operands")


if __name__ == "__main__":
    unittest.main()
