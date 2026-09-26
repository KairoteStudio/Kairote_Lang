"""Parser-only contracts: assert preserved syntax, independently of codegen."""
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from Test.SelfHost.test_contract import ROOT, KRTC


class ParserTests(unittest.TestCase):
    def check_ast(self, source_text, assertions):
        parts = ('Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
                 'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt')
        prelude = '\n'.join((ROOT / 'SelfHost' / p).read_text() for p in parts)
        program = f'''
int32 main() {{
    string input = {json.dumps(source_text)};
    KrtLexer lexer = new KrtLexer(); KrtToken token = new KrtToken();
    KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(source_text.encode())}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = KrtParserParseFunction(parser);
    if (parser.errors != 0 || token.kind != KrtTokenKind.Eof) {{ return 1; }}
    {assertions}
    return 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix='krt-parser-') as directory:
            work = Path(directory)
            source = work / 'ParserTest.krt'
            source.write_text(prelude + program)
            binary = work / 'parser-test'
            result = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(binary)],
                                    cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], cwd=work, timeout=10)
            self.assertEqual(result.returncode, 0, source_text)

    def test_function_keyword_and_spans(self):
        for keyword in ('function', 'func'):
            with self.subTest(keyword=keyword):
                self.check_ast(f'{keyword} main() {{ return 7; }}', f'''
    if (unit.start != 0 || unit.name_start != {len(keyword)+1} || unit.name_length != 4) {{ return 2; }}
    if (unit.type_length != 0 || unit.left.left.left.value != 7) {{ return 3; }}
''')
        self.check_ast('int32 main() { return 7; }', '''
    if (unit.start != 0 || unit.type_start != 0 || unit.type_length != 5) { return 2; }
''')

    def test_loop_body_survives_following_statement(self):
        for loop, kind in (('for (int32 i = 0; i < 3; i++)', 'For'),
                           ('foreach (int32 i in items)', 'Foreach')):
            with self.subTest(loop=loop):
                self.check_ast(f'int32 main() {{ {loop} {{ break; }} return 0; }}', f'''
    KrtAstNode loop_node = unit.left.left;
    if (loop_node.kind != KrtAstKind.{kind} || loop_node.body == null) {{ return 2; }}
    if (loop_node.body.kind != KrtAstKind.Block || loop_node.body.left.kind != KrtAstKind.Break) {{ return 3; }}
    if (loop_node.next == null || loop_node.next.kind != KrtAstKind.Return) {{ return 4; }}
''')

    def test_lambda_retains_parameters(self):
        self.check_ast('int32 main() { var f = function (int32 first, ref int64 second) => first + second; return 0; }', '''
    KrtAstNode lambda_node = unit.left.left.left;
    if (lambda_node.kind != KrtAstKind.Lambda || lambda_node.parameter_count != 2) { return 2; }
    if (lambda_node.parameter_lengths[0] != 5 || lambda_node.parameter_lengths[1] != 6) { return 3; }
    if (lambda_node.parameter_type_lengths[0] != 6 || lambda_node.parameter_type_lengths[1] != 6) { return 4; }
    if (lambda_node.parameter_refs[0] || !lambda_node.parameter_refs[1]) { return 5; }
    if (lambda_node.left.kind != KrtAstKind.Binary || lambda_node.left.op != 43) { return 6; }
''')

    def test_type_meta_expressions(self):
        self.check_ast('int32 main() { return sizeof(int64) + default(int32); }', '''
    KrtAstNode expression = unit.left.left.left;
    if (expression.kind != KrtAstKind.Binary || expression.left.kind != KrtAstKind.Sizeof ||
        expression.right.kind != KrtAstKind.DefaultValue) { return 2; }
    if (expression.left.type_length != 5 || expression.right.type_length != 5) { return 3; }
''')
