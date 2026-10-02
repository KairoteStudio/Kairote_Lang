"""Record executable frontend evidence, including accepted but incorrect seed code.

The C seed's token/AST inventory is deliberately not treated as a feature list.
Each fixture has an observable result or an expected rejection. JSON records
the separate compile, link and execution outcomes and remaining native gaps.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import ARKLINK, Bootstrap, KRTC, ROOT


CASES = {
    "switch_first": ("int32 f(int32 n){switch(n){case 1:return 11;case 2:return 23;default:return 31;}}int32 main(){return f(1);}", 11, True),
    "switch_later": ("int32 f(int32 n){switch(n){case 1:return 11;case 2:return 23;default:return 31;}}int32 main(){return f(2);}", 23, True),
    "switch_default": ("int32 f(int32 n){switch(n){case 1:return 11;case 2:return 23;default:return 31;}}int32 main(){return f(9);}", 31, True),
    "switch_fallthrough": ("int32 main(){int32 n=0;switch(1){case 1:n+=5;case 2:n+=7;break;default:n=99;}return n;}", 12, True),
    "switch_dynamic_label": ("int32 main(){int32 n=2;switch(n){case n:return 13;default:return 31;}}", 13, True),
    "switch_duplicate": ("int32 main(){switch(1){case 1:return 3;case 1:return 4;}return 0;}", None, True),
    "switch_string": ('int32 main(){string n="b";switch(n){case "a":return 11;case "b":return 23;default:return 31;}}', 23, True),
    "binary_literal": ("int32 main(){return 0b10101;}", 21, True),
    "binary_invalid": ("int32 main(){return 0b102;}", None, True),
    "octal_prefix": ("int32 main(){return 0o25;}", 21, False),
    "struct_default_local": ("struct Pair{public int32 x;public int32 y;}int32 main(){Pair a;a.x=7;a.y=11;return a.x+a.y;}", 18, False),
    "struct_copy_contents": ("struct Pair{public int32 x;public int32 y;}int32 main(){Pair a;a.x=7;a.y=11;Pair b=a;if(b.x!=7 || b.y!=11){return 1;}b.x=23;b.y=29;if(a.x!=7 || a.y!=11 || b.x!=23 || b.y!=29){return 2;}return 17;}", 17, False),
    "struct_parameter_contents": ("struct Pair{public int32 x;}int32 change(Pair a){if(a.x!=7){return 1;}a.x=23;return a.x;}int32 main(){Pair a;a.x=7;int32 r=change(a);if(r!=23 || a.x!=7){return 2;}return 17;}", 17, False),
    "struct_ref_contents": ("struct Pair{public int32 x;}int32 change(ref Pair a){if(a.x!=7){return 1;}a.x=23;return a.x;}int32 main(){Pair a;a.x=7;int32 r=change(ref a);if(r!=23 || a.x!=23){return 2;}return 17;}", 17, False),
    "struct_return_contents": ("struct Pair{public int32 x;}Pair make(){Pair a;a.x=7;return a;}int32 main(){Pair a=make();if(a.x!=7){return 1;}Pair b=a;if(b.x!=7){return 2;}b.x=23;if(a.x!=7){return 3;}return 17;}", 17, False),
    "lambda_call": ("int32 main(){var f=function(int32 x)=>x+5;return f(7);}", 12, False),
    "lambda_capture": ("int32 main(){int32 n=5;var f=function(int32 x)=>x+n;return f(7);}", 12, False),
    "private_field": ("class Box{private int32 x=7;}int32 main(){Box b=new Box();return b.x;}", None, False),
    "private_method": ("class Box{private int32 F(){return 7;}}int32 main(){Box b=new Box();return b.F();}", None, False),
    "nameof": ('int32 main(){string name=nameof(main);if(name!="main"){return 1;}return 17;}', 17, False),
    "sizeof_integer_storage": ("int32 main(){if(sizeof(int2)!=1 || sizeof(int16)!=2 || sizeof(int32)!=4 || sizeof(int64)!=8 || sizeof(int128)!=16){return 1;}return 17;}", 17, True),
    "sizeof_class": ("class Pair{public int32 x;public int32 y;}int32 main(){return sizeof(Pair);}", 8, True),
    "default_integer_assignment": ("int32 main(){int32 n=99;n=default(int32);return n+17;}", 17, True),
    "default_float_assignment": ("int32 main(){float64 n=99.0;n=default(float64);if(n!=0.0){return 1;}return 17;}", 17, True),
    "default_class_assignment": ("class Box{}int32 main(){Box b=new Box();b=default(Box);if(b!=null){return 1;}return 17;}", 17, True),
    "null_coalesce": ("class Box{public int32 n=17;}int32 main(){Box missing=null;Box b=missing??new Box();return b.n;}", 17, False),
    "null_coalesce_side_effects": ("class Box{public int32 n=17;}Box next(ref int32 n){n++;return new Box();}int32 main(){int32 n=0;Box b=new Box();Box c=b??next(ref n);return n+17;}", 17, False),
    "null_conditional": ("class Box{public int32 n=17;}int32 main(){Box b=null;int32 n=b?.n;return n+17;}", 17, False),
    "lock_body": ("class Box{}int32 main(){Box b=new Box();int32 n=3;lock(b){n+=4;}return n;}", 7, False),
    "lock_selector_side_effects": ("class Box{}Box next(ref int32 n){n++;return new Box();}int32 main(){int32 n=3;lock(next(ref n)){n+=4;}return n;}", 8, False),
    "fixed_pointer": ("int32 main(){unsafe(using krt.mem;){byte[] b=new byte[1];fixed(byte* p=b){p[0]=7;}return b[0];}}", 7, False),
    "yield_return_control": ("int32 main(){int32 n=3;yield return 7;n+=4;return n;}", 7, False),
    "yield_break_control": ("int32 main(){int32 n=3;yield break;n+=4;return n;}", 0, False),
    "await_value": ("int32 main(){int32 n=await 7;return n;}", 7, False),
    "character_bell": (r"int32 main(){return '\a';}", 7, False),
    "character_backspace": (r"int32 main(){return '\b';}", 8, False),
    "character_formfeed": (r"int32 main(){return '\f';}", 12, False),
}

SOURCE_REFERENCES = {
    "switch": ["Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:parser_parse_switch_statement", "Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:irgen_stmt_switch_statement", "SelfHost/Frontend/Semantic/Switch.krt", "SelfHost/Middle/Ir/Switch.krt"],
    "struct": ["Re.KrtC/src/Compiler/Frontend/Parser/ParserAdvanced.c:parser_parse_class_declaration", "SelfHost/Middle/Ir/Ir.krt:KrtNativeLowerModule"],
    "lambda": ["Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:irgen_expr_lambda_expression", "SelfHost/Frontend/Parser/Parser.krt:KrtParserParsePrimary"],
    "access": ["Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.c:AST_ACCESS_MODIFIER", "SelfHost/Frontend/Semantic/NativeBinding.krt:KrtNativeBindExpression"],
    "literals": ["Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c:lexer_read_number", "Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c:lexer_read_char", "SelfHost/Frontend/Lexer/Lexer.krt:KrtLexerNext", "SelfHost/Frontend/Parser/Parser.krt:KrtParserParsePrimary"],
    "meta_and_nullable": ["Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.c:parser_parse_primary", "Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:parser_parse_statement", "Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:AST_DEFAULT_EXPRESSION/AST_NULL_COALESCING", "SelfHost/Frontend/Semantic/NativeBinding.krt:KrtNativeBindExpression"],
    "lock_fixed_yield_await": ["Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:parser_parse_statement", "Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:irgen_stmt_lock_statement/irgen_stmt_fixed_statement", "SelfHost/Frontend/Parser/Parser.krt:KrtParserParseStatement"],
}


def run(argv, cwd, isolated=False):
    env = os.environ.copy()
    if isolated:
        env["PATH"] = ""
    try:
        result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env,
                                capture_output=True, text=True, timeout=30)
        return {"argv": list(map(str, argv)), "returncode": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr}
    except subprocess.TimeoutExpired:
        return {"argv": list(map(str, argv)), "timeout": True}


def outcome(result, expected):
    if any(part.get("timeout") for part in result.values()):
        return "timeout"
    if result["compile"]["returncode"] != 0:
        return "correct_rejection" if expected is None else "compile_rejected"
    if "link" in result and result["link"]["returncode"] != 0:
        return "link_failed"
    if expected is None:
        return "accepted_invalid"
    return "correct_execution" if result["run"]["returncode"] == expected else "wrong_execution"


class FrontendParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="krt-frontend-parity-")
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = (Path(os.environ["SELFHOST_COMPILER"]).resolve()
                        if "SELFHOST_COMPILER" in os.environ else cls.build.seed())
        cls.report = {"seed": str(KRTC), "native": str(cls.compiler),
                      "seed_sha256": hashlib.sha256(KRTC.read_bytes()).hexdigest(),
                      "native_sha256": hashlib.sha256(cls.compiler.read_bytes()).hexdigest(),
                      "source_references": SOURCE_REFERENCES, "cases": {}}
        for name, (source, expected, supported) in CASES.items():
            row = {"source": source, "expected_exit": expected, "native_supported": supported,
                   "source_sha256": hashlib.sha256(source.encode()).hexdigest()}
            for label, compiler in (("seed", KRTC), ("native", cls.compiler)):
                work = Path(cls.directory.name) / name / label
                work.mkdir(parents=True)
                path = work / "program.krt"
                path.write_text(source)
                binary = work / "program"
                if label == "seed":
                    result = {"compile": run([compiler, "-O0", path, "output", binary], work)}
                else:
                    object_path = work / "stage1-probe.kro"
                    result = {"compile": run([compiler, "-O0", path, "-c", "-o", object_path], work, True)}
                    if result["compile"].get("returncode") == 0:
                        result["link"] = run([ARKLINK, object_path, "--target", "elf", "-o", binary], work, True)
                if binary.exists() and result.get("link", result["compile"]).get("returncode") == 0:
                    binary.chmod(0o755)
                    result["run"] = run([binary], work, True)
                row[label] = result
                row[label + "_outcome"] = outcome(result, expected)
            row["actual_seed_feature_gap"] = (row["seed_outcome"] == "correct_execution" and
                                                row["native_outcome"] != "correct_execution")
            cls.report["cases"][name] = row
        target = Path(os.environ.get("FRONTEND_PARITY_REPORT", ROOT / "build/selfhost-frontend-parity.json"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(cls.report, indent=2) + "\n")

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_implemented_native_features(self):
        for name, row in self.report["cases"].items():
            if row["native_supported"]:
                with self.subTest(feature=name):
                    expected = "correct_rejection" if row["expected_exit"] is None else "correct_execution"
                    self.assertEqual(row["native_outcome"], expected, json.dumps(row["native"], indent=2))

    def test_reports_execution_and_rejection_separately(self):
        self.assertEqual(self.report["cases"]["binary_literal"]["seed_outcome"], "correct_execution")
        self.assertIn("run", self.report["cases"]["binary_literal"]["seed"])
        self.assertEqual(self.report["cases"]["binary_invalid"]["native_outcome"], "correct_rejection")


if __name__ == "__main__":
    unittest.main()
