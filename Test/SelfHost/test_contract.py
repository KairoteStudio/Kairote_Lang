import os
import json
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
KRTC = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()
ARKLINK = Path(os.environ.get("ARKLINK", ROOT / "ArkLink/build/ArkLink")).resolve()

class ContractTests(unittest.TestCase):
    def test_stage1_bootstrap_probe(self):
        parts = sorted((ROOT / "SelfHost").rglob("*.krt"))
        with tempfile.TemporaryDirectory(prefix="krt-stage1-") as directory:
            work = Path(directory)
            source = work / "Stage1.krt"
            source.write_text("\n".join(path.read_text() for path in parts))
            compiler = work / "stage1"
            build = subprocess.run([str(KRTC), "-O2", str(source), "output", str(compiler)],
                                   cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            (work / "program.krt").write_text("int32 helper() { return 7; } int32 main() { return helper(); }")
            compiler.chmod(0o755)
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            first = (work / "stage1-probe.kro").read_bytes()
            (work / "stage1-probe.kro").unlink()
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            second = (work / "stage1-probe.kro").read_bytes()
            self.assertEqual(hashlib.sha256(first).digest(), hashlib.sha256(second).digest())
            probe = work / "probe"
            link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(probe)],
                                  cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(link.returncode, 0, link.stdout + link.stderr)
            probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(probe)], cwd=work, timeout=10).returncode, 7)
            (work / "program.krt").write_text("int32 main() { return 7; }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            single_probe = work / "single-probe"
            single_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(single_probe)],
                                         cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(single_link.returncode, 0, single_link.stdout + single_link.stderr)
            single_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(single_probe)], cwd=work, timeout=10).returncode, 7)
            (work / "program.krt").write_text("int32 main() { int32 x = 5; return x; }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            local_probe = work / "local-probe"
            local_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(local_probe)],
                                        cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(local_link.returncode, 0, local_link.stdout + local_link.stderr)
            local_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(local_probe)], cwd=work, timeout=10).returncode, 5)
            (work / "program.krt").write_text("int32 main() { int32 x = 5; int32 y = 9; return y; }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            locals_probe = work / "locals-probe"
            locals_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(locals_probe)],
                                         cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(locals_link.returncode, 0, locals_link.stdout + locals_link.stderr)
            locals_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(locals_probe)], cwd=work, timeout=10).returncode, 9)
            (work / "program.krt").write_text("int32 main() { int32 x = 5; return x + 2; }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            expression_probe = work / "expression-probe"
            expression_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(expression_probe)],
                                             cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(expression_link.returncode, 0, expression_link.stdout + expression_link.stderr)
            expression_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(expression_probe)], cwd=work, timeout=10).returncode, 7)
            (work / "program.krt").write_text("int32 main() { int32 x = 1; x = 2; return x; }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            assignment_probe = work / "assignment-probe"
            assignment_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(assignment_probe)],
                                             cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(assignment_link.returncode, 0, assignment_link.stdout + assignment_link.stderr)
            assignment_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(assignment_probe)], cwd=work, timeout=10).returncode, 2)
            (work / "program.krt").write_text("int32 main() { if (1) { return 7; } else { return 9; } }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            if_probe = work / "if-probe"
            if_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(if_probe)],
                                     cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(if_link.returncode, 0, if_link.stdout + if_link.stderr)
            if_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(if_probe)], cwd=work, timeout=10).returncode, 7)
            (work / "program.krt").write_text("int32 main() { int32 flag = 1; if (flag) { return 7; } else { return 9; } }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            local_if_probe = work / "local-if-probe"
            local_if_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(local_if_probe)],
                                           cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(local_if_link.returncode, 0, local_if_link.stdout + local_if_link.stderr)
            local_if_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(local_if_probe)], cwd=work, timeout=10).returncode, 7)
            (work / "program.krt").write_text("int32 main() { int32 flag = 0; return flag ? 7 : 9; }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            local_ternary_probe = work / "local-ternary-probe"
            local_ternary_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(local_ternary_probe)],
                                                cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(local_ternary_link.returncode, 0, local_ternary_link.stdout + local_ternary_link.stderr)
            local_ternary_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(local_ternary_probe)], cwd=work, timeout=10).returncode, 9)
            (work / "program.krt").write_text("int32 main() { byte* p = stackalloc byte[4]; p[0] = 42; return p[0]; }")
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)
            memory_probe = work / "memory-probe"
            memory_link = subprocess.run([str(ARKLINK), "stage1-probe.kro", "--target", "elf", "-o", str(memory_probe)],
                                         cwd=work, capture_output=True, text=True, timeout=10)
            self.assertEqual(memory_link.returncode, 0, memory_link.stdout + memory_link.stderr)
            memory_probe.chmod(0o755)
            self.assertEqual(subprocess.run([str(memory_probe)], cwd=work, timeout=10).returncode, 42)
            padded = ("// padding for staged file reads\n" * 2500) + "int32 main() { return 7; }"
            (work / "program.krt").write_text(padded)
            self.assertEqual(len(padded), 82526)
            self.assertEqual(subprocess.run([str(compiler)], cwd=work, timeout=10).returncode, 0)

    def test_local_binding(self):
        parts = [
            'Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
            'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt',
            'Middle/Ir/Ir.krt', 'Frontend/Semantic/Semantic.krt',
        ]
        prelude = '\n'.join((ROOT / 'SelfHost' / part).read_text() for part in parts)
        cases = [
            ('int32 main() { int32 x = 5; int32 y = 9; return y; }', True, 1),
            ('int32 main() { int32 x = 5; int32 y = 9; return x; }', True, 0),
            ('int32 main() { int32 xx = 5; return xy; }', False, 0),
            ('int32 main() { return x; int32 x = 5; }', False, 0),
            ('int32 main() { int32 x = 5; int32 x = 9; return x; }', False, 0),
            ('int32 main() { int32 x = 5; }', False, 0),
        ]
        for text, accepted, slot in cases:
            with self.subTest(source=text), tempfile.TemporaryDirectory(prefix='krt-binding-') as directory:
                work = Path(directory)
                source = work / 'Binding.krt'
                main = f'''
int32 main() {{
    string input = {json.dumps(text)};
    KrtLexer lexer = new KrtLexer();
    KrtToken token = new KrtToken();
    KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(text)}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = KrtParserParseFunction(parser);
    if (parser.errors != 0) {{ return 1; }}
    bool accepted = KrtSemanticBindLocals(unit, lexer.source);
    if (accepted != {str(accepted).lower()}) {{ return 2; }}
    if (accepted) {{
        KrtIrFunction ir = new KrtIrFunction();
        if (!KrtIrLowerReturnFunction(unit, ir)) {{ return 3; }}
        if (ir.instruction_count != 6) {{ return 4; }}
        if (ir.rights[1] != 0 || ir.rights[3] != 1 || ir.lefts[4] != {slot}) {{ return 5; }}
        if (ir.immediates[0] != 5 || ir.immediates[2] != 9) {{ return 6; }}
    }}
    return 0;
}}
'''
                source.write_text(prelude + '\n' + main)
                binary = work / 'binding'
                build = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(binary)],
                                       cwd=work, capture_output=True, text=True, timeout=60)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_pointer_index_frontend_contract(self):
        parts = [
            'Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
            'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt',
            'Middle/Ir/Ir.krt', 'Frontend/Semantic/Semantic.krt',
        ]
        prelude = '\n'.join((ROOT / 'SelfHost' / part).read_text() for part in parts)
        text = 'int32 main() { byte* p = stackalloc byte[4]; p[0] = 1; return p[0]; }'
        with tempfile.TemporaryDirectory(prefix='krt-index-') as directory:
            work = Path(directory)
            source = work / 'Index.krt'
            main = f'''
int32 main() {{
    string input = {json.dumps(text)};
    KrtLexer lexer = new KrtLexer();
    KrtToken token = new KrtToken();
    KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(text)}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = KrtParserParseFunction(parser);
    if (parser.errors != 0) {{ return 1; }}
    if (!KrtSemanticBindLocals(unit, lexer.source)) {{ return 2; }}
    if (unit.left.left.next.kind != KrtAstKind.Assign) {{ return 3; }}
    if (unit.left.left.next.left.kind != KrtAstKind.Index) {{ return 4; }}
    KrtIrFunction ir = new KrtIrFunction();
    if (!KrtIrLowerReturnFunction(unit, ir)) {{ return 5; }}
    if (ir.instruction_count != 9 || !KrtIrValidateMemory(ir)) {{ return 6; }}
    if (ir.opcodes[0] != (int32)KrtIrOpcode.StackAlloc || ir.immediates[0] != 4) {{ return 7; }}
    if (ir.opcodes[4] != (int32)KrtIrOpcode.Store || ir.opcodes[7] != (int32)KrtIrOpcode.Load) {{ return 8; }}
    ir.lefts[7] = 8;
    if (KrtIrValidateMemory(ir)) {{ return 9; }}
    ir.lefts[7] = 6;
    ir.immediates[4] = 8;
    if (KrtIrValidateMemory(ir)) {{ return 10; }}
    return 0;
}}
'''
            source.write_text(prelude + '\n' + main)
            binary = work / 'index'
            build = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(binary)],
                                   cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_multi_function_main_ordering_contract(self):
        parts = [
            'Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
            'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt',
            'Frontend/Semantic/Semantic.krt',
        ]
        prelude = '\n'.join((ROOT / 'SelfHost' / part).read_text() for part in parts)
        text = 'using System; namespace Demo; int32 main() { return 0; } int32 alpha() { return 1; } int32 beta() { return 2; }'
        with tempfile.TemporaryDirectory(prefix='krt-order-') as directory:
            work = Path(directory)
            source = work / 'Order.krt'
            main = f'''
int32 main() {{
    string input = {json.dumps(text)};
    KrtLexer lexer = new KrtLexer(); KrtToken token = new KrtToken(); KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(text)}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = KrtParserParseCompilationUnit(parser);
    KrtSemanticOrderHelperFirst(unit, lexer.source);
    if (parser.errors != 0 || unit.child_count != 3) {{ return 1; }}
    KrtAstNode tail = unit.left;
    while (tail.next != null) {{ tail = tail.next; }}
    if (!KrtSemanticIsMain(tail, lexer.source)) {{ return 2; }}
    if (!KrtSemanticValidateFunctionNames(unit, lexer.source) || !KrtSemanticValidateEntryPoint(unit, lexer.source)) {{ return 3; }}
    return 0;
}}
'''
            source.write_text(prelude + '\n' + main)
            binary = work / 'order'
            build = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(binary)], cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_bitwise_and_shift_expression_contract(self):
        parts = [
            'Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
            'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt',
        ]
        prelude = '\n'.join((ROOT / 'SelfHost' / part).read_text() for part in parts)
        source_text = 'int32 main() { return 1 | 2 ^ 3 & 4 << 1; }'
        main = f'''
int32 main() {{
    string input = {json.dumps(source_text)};
    KrtLexer lexer = new KrtLexer(); KrtToken token = new KrtToken(); KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(source_text)}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = KrtParserParseFunction(parser);
    if (parser.errors != 0 || unit.left.left.kind != KrtAstKind.Return) {{ return 1; }}
    KrtAstNode expression = unit.left.left.left;
    if (expression.kind != KrtAstKind.Binary || expression.op != 124 ||
        expression.right.kind != KrtAstKind.Binary || expression.right.op != 94 ||
        expression.right.right.kind != KrtAstKind.Binary || expression.right.right.op != 38 ||
        expression.right.right.right.kind != KrtAstKind.Binary || expression.right.right.right.op != 106) {{ return 2; }}
    return 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix='krt-operators-') as directory:
            work = Path(directory); source = work / 'Operators.krt'; source.write_text(prelude + '\n' + main)
            binary = work / 'operators'
            build = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(binary)], cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_multiple_parameter_binding_contract(self):
        parts = [
            'Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
            'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt',
            'Frontend/Semantic/Semantic.krt',
        ]
        prelude = '\n'.join((ROOT / 'SelfHost' / part).read_text() for part in parts)
        text = 'int32 sum(int32 left, int32 right) { return right; } int32 main() { return sum(1, 2); }'
        with tempfile.TemporaryDirectory(prefix='krt-params-') as directory:
            work = Path(directory)
            source = work / 'Params.krt'
            main = f'''
int32 main() {{
    string input = {json.dumps(text)};
    KrtLexer lexer = new KrtLexer(); KrtToken token = new KrtToken(); KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(text)}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = KrtParserParseCompilationUnit(parser);
    if (parser.errors != 0 || unit.left.parameter_count != 2) {{ return 1; }}
    if (!KrtSemanticResolveModuleCalls(unit, lexer.source)) {{ return 2; }}
    if (unit.left.left.left.left.local_slot != -3) {{ return 3; }}
    return 0;
}}
'''
            source.write_text(prelude + '\n' + main)
            binary = work / 'params'
            build = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(binary)], cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_large_function_module_contract(self):
        parts = [
            'Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
            'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt',
            'Frontend/Semantic/Semantic.krt',
        ]
        prelude = '\n'.join((ROOT / 'SelfHost' / part).read_text() for part in parts)
        text = ' '.join(f'int32 f{i}() {{ return {i}; }}' for i in range(40)) + ' int32 main() { return 0; }'
        with tempfile.TemporaryDirectory(prefix='krt-large-module-') as directory:
            work = Path(directory)
            source = work / 'Large.krt'
            main = f'''
int32 main() {{
    string input = {json.dumps(text)};
    KrtLexer lexer = new KrtLexer(); KrtToken token = new KrtToken(); KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(text)}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = KrtParserParseCompilationUnit(parser);
    if (parser.errors != 0 || unit.child_count != 41) {{ return 1; }}
    if (!KrtSemanticValidateFunctionNames(unit, lexer.source) || !KrtSemanticValidateEntryPoint(unit, lexer.source)) {{ return 2; }}
    return 0;
}}
'''
            source.write_text(prelude + '\n' + main)
            binary = work / 'large'
            build = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(binary)], cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_void_unsafe_loop_control_contract(self):
        parts = [
            'Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
            'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt',
            'Frontend/Semantic/Semantic.krt',
        ]
        prelude = '\n'.join((ROOT / 'SelfHost' / part).read_text() for part in parts)
        text = 'void worker(int32 value) { unsafe(using krt.mem;) { while (true) { break; } } return; }'
        with tempfile.TemporaryDirectory(prefix='krt-void-control-') as directory:
            work = Path(directory)
            source = work / 'VoidControl.krt'
            main = f'''
int32 main() {{
    string input = {json.dumps(text)};
    KrtLexer lexer = new KrtLexer(); KrtToken token = new KrtToken(); KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(text)}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = KrtParserParseFunction(parser);
    if (parser.errors != 0 || !KrtSemanticValidateFunction(unit)) {{ return 1; }}
    if (!KrtSemanticBindLocals(unit, lexer.source)) {{ return 2; }}
    return 0;
}}
'''
            source.write_text(prelude + '\n' + main)
            binary = work / 'void-control'
            build = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(binary)], cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_frontend_ir_backend_contracts_compile_together(self):
        parts = [
            "SelfHost/Frontend/Lexer/Token.krt",
            "SelfHost/Frontend/Lexer/Lexer.krt",
            "SelfHost/Frontend/Parser/Ast.krt",
            "SelfHost/Frontend/Parser/Parser.krt",
            "SelfHost/Middle/Ir/Ir.krt",
            "SelfHost/Frontend/Semantic/Semantic.krt",
            "SelfHost/Backend/Kro/KroBackend.krt",
            "SelfHost/Driver/Compiler.krt",
            "Test/SelfHost/test_contract.krt",
        ]
        with tempfile.TemporaryDirectory(prefix="krt-contract-") as directory:
            work = Path(directory)
            source = work / "Contract.krt"
            source.write_text("\n".join((ROOT / part).read_text() for part in parts))
            binary = work / "contract"
            build = subprocess.run([str(KRTC), "-O2", str(source), "output", str(binary)], cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            self.assertEqual(subprocess.run([str(binary)], cwd=work, timeout=10).returncode, 0)
            payload = (work / 'contract.kro').read_bytes()
            self.assertEqual(payload[:8], b'KRO\x00\x02\x00\x00\x00')
            self.assertEqual(len(payload), 108)
            self.assertEqual(payload[64:70], b'\xb8\x00\x00\x00\x00\xc3')
            self.assertEqual(payload[70:74], b'\x00\x00\x00\x00')
            self.assertEqual(payload[74:78], b'\x00\x00\x00\x00')
            self.assertEqual(payload[78:82], b'\x06\x00\x00\x00')
            self.assertEqual(payload[82:86], b'\x01\x00\x00\x00')
            self.assertEqual(payload[86:90], b'\x01\x00\x00\x00')
            self.assertEqual(payload[90:94], b'\x01\x00\x00\x00')
            self.assertEqual(payload[102:], b'\x00main\x00')
            local = (work / 'local.kro').read_bytes()
            self.assertEqual(len(local), 80 + 38)
            self.assertEqual(local[64:80], bytes([0x55,0x48,0x89,0xe5,0xc7,0x45,0xf8,0x05,0x00,0x00,0x00,0x8b,0x45,0xf8,0x5d,0xc3]))
            locals_file = (work / 'locals.kro').read_bytes()
            self.assertEqual(len(locals_file), 64 + 23 + 38)
            self.assertEqual(payload[-6:], b'\x00main\x00')
            real_call = work / 'real-call-linked'
            real_call_link = subprocess.run([str(ARKLINK), 'real-call.kro', '--target', 'elf', '-o', str(real_call)], cwd=work,
                                             capture_output=True, text=True, timeout=10)
            self.assertEqual(real_call_link.returncode, 0, real_call_link.stdout + real_call_link.stderr)
            real_call.chmod(0o755)
            self.assertEqual(subprocess.run([str(real_call)], cwd=work, timeout=10).returncode, 7)
            argument_call = work / 'argument-call-linked'
            argument_link = subprocess.run([str(ARKLINK), 'argument-call.kro', '--target', 'elf', '-o', str(argument_call)], cwd=work,
                                           capture_output=True, text=True, timeout=10)
            self.assertEqual(argument_link.returncode, 0, argument_link.stdout + argument_link.stderr)
            argument_call.chmod(0o755)
            self.assertEqual(subprocess.run([str(argument_call)], cwd=work, timeout=10).returncode, 7)
            two_argument_call = work / 'two-argument-call-linked'
            two_argument_link = subprocess.run([str(ARKLINK), 'two-argument-call.kro', '--target', 'elf', '-o', str(two_argument_call)], cwd=work,
                                                capture_output=True, text=True, timeout=10)
            self.assertEqual(two_argument_link.returncode, 0, two_argument_link.stdout + two_argument_link.stderr)
            two_argument_call.chmod(0o755)
            self.assertEqual(subprocess.run([str(two_argument_call)], cwd=work, timeout=10).returncode, 9)

if __name__ == "__main__": unittest.main()
