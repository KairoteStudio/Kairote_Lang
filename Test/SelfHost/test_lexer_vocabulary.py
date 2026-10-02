"""Validate the native lexer against the actual C seed vocabulary and values."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import Bootstrap, KRTC, ROOT


def vocabulary():
    widths = list(map(int, re.findall(r"KRT_INTEGER_WIDTH\((\d+)\)",
                                     (ROOT / "Re.KrtC/src/Core/Utils/IntegerWidths.def").read_text())))
    header = (ROOT / "Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.h").read_text()
    body = header.split("typedef enum {", 1)[1].split("} KrtTokenType;", 1)[0]
    for prefix in ("INT", "UINT"):
        expression = (r"#define KRT_INTEGER_WIDTH\(bits\) TOKEN_" + prefix +
                      r"##bits,\s*#include[^\n]+\s*#undef KRT_INTEGER_WIDTH")
        body = re.sub(expression, ",".join(f"TOKEN_{prefix}{width}" for width in widths) + ",", body)
    kinds = {name: index for index, name in enumerate(re.findall(r"TOKEN_[A-Z0-9_]+", body))}
    lexer = (ROOT / "Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c").read_text()
    table = lexer.split("static Keyword keywords[] =", 1)[1].split("{NULL, 0}", 1)[0]
    keywords = [(word, kinds[name]) for word, name in
                re.findall(r'\{"([^"]+)",\s*(TOKEN_[A-Z0-9_]+)\}', table)]
    keywords += [(f"{prefix}{width}", kinds[f"TOKEN_{prefix.upper()}{width}"])
                 for width in widths for prefix in ("int", "uint")]
    return kinds, keywords


OPERATORS = {
    "+": "PLUS", "-": "MINUS", "*": "MULTIPLY", "/": "DIVIDE", "%": "MODULO",
    "=": "ASSIGN", "==": "EQUAL", "!=": "NOT_EQUAL", "<": "LESS", ">": "GREATER",
    "<=": "LESS_EQUAL", ">=": "GREATER_EQUAL", "(": "LEFT_PAREN", ")": "RIGHT_PAREN",
    "{": "LEFT_BRACE", "}": "RIGHT_BRACE", "[": "LEFT_BRACKET", "]": "RIGHT_BRACKET",
    ",": "COMMA", ";": "SEMICOLON", ":": "COLON", "::": "DOUBLE_COLON", ".": "DOT",
    "->": "ARROW", "|": "PIPE", "$": "DOLLAR", "&&": "AND", "||": "OR", "!": "NOT",
    "++": "INCREMENT", "--": "DECREMENT", "+=": "PLUS_ASSIGN", "-=": "MINUS_ASSIGN",
    "*=": "MUL_ASSIGN", "/=": "DIV_ASSIGN", "%=": "MOD_ASSIGN", "~": "TILDE",
    "&": "BITWISE_AND", "^": "BITWISE_XOR", "<<": "LSHIFT", ">>": "RSHIFT", "**": "POWER",
    "?": "QUESTION", "?:": "QUESTION_COLON", "??": "NULL_COALESCING", "?.": "QUESTION_DOT",
    "=>": "LAMBDA", "&=": "AND_ASSIGN", "|=": "OR_ASSIGN", "^=": "XOR_ASSIGN",
    "<<=": "LSHIFT_ASSIGN", ">>=": "RSHIFT_ASSIGN",
}


def harness(items, category):
    text = " ".join(word for word, _ in items)
    statements = []
    offset = 0
    for index, (word, expected) in enumerate(items):
        statements.append(f"if(!expect(lexer,token,{expected},{category},{offset},{len(word)})){{return {index % 250 + 1};}}")
        offset += len(word) + 1
    # This helper checks kind and category separately: keyword spellings retain
    # a word category, while the public kind must report the exact seed enum.
    return "\n".join((ROOT / path).read_text() for path in
                     ("SelfHost/Frontend/Lexer/Token.krt", "SelfHost/Frontend/Lexer/Lexer.krt")) + f"""
bool expect(KrtLexer lexer,KrtToken token,int32 kind,int32 category,int64 start,int64 length){{
KrtLexerNext(lexer,token);int32 bits=KrtTokenIntegerBits(token.kind);if(bits!=0 && !KrtLexerWordEqual(lexer.source,token.start,token.length,"int",3) && !KrtLexerWordEqual(lexer.source,token.start,token.length,"byte",4) && !KrtLexerWordEqual(lexer.source,token.start,token.length,"usize",5) && !KrtLexerWordEqual(lexer.source,token.start,token.length,"long",4) && lexer_integer_type_bits(lexer.source,token.start,token.length)!=bits){{return false;}}return (int32)token.kind==kind && (int32)token.category==category && token.start==start && token.length==length;}}
int32 main(){{KrtLexer lexer=new KrtLexer();KrtToken token=new KrtToken();
unsafe(using krt.mem;){{KrtLexerInit(lexer,(byte*)(int64)"{text}",{len(text)});}}
{''.join(statements)}
if(!expect(lexer,token,0,0,{len(text)},0)){{return 251;}}
int32 bits=2;while(bits<=128){{
KrtTokenKind signed_kind=KrtTokenIntegerType(bits,false);KrtTokenKind unsigned_kind=KrtTokenIntegerType(bits,true);
if(KrtTokenIntegerBits(signed_kind)!=bits || KrtTokenIntegerBits(unsigned_kind)!=bits ||
KrtTokenIsUnsigned(signed_kind) || !KrtTokenIsUnsigned(unsigned_kind)){{return 252;}}bits=bits+2;}}
if(KrtTokenIntegerType(1,false)!=KrtTokenKind.Eof || KrtTokenIntegerType(130,true)!=KrtTokenKind.Eof){{return 253;}}
return 0;}}
"""


class LexerVocabularyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="krt-token-vocabulary-")
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = (Path(os.environ["SELFHOST_COMPILER"]).resolve()
                        if "SELFHOST_COMPILER" in os.environ else cls.build.seed())

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def execute(self, source, name):
        # Run the lexer code after both C seed compilation and native generation.
        work = Path(self.directory.name) / (name + "-seed")
        work.mkdir()
        path = work / "program.krt"
        path.write_text(source)
        binary = work / "program"
        result = subprocess.run([KRTC, "-O0", path, "output", binary], cwd=work,
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(subprocess.run([binary], capture_output=True, timeout=5).returncode, 0)
        binary, _ = self.build.compile(self.compiler, source, name + "-native")
        result = subprocess.run([binary], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_every_seed_keyword_and_integer_width(self):
        kinds, keywords = vocabulary()
        self.assertEqual(len(kinds), 299)
        self.assertEqual(len(keywords), 244)
        self.execute(harness(keywords, 1), "keywords")

    def test_every_operator_and_punctuation_uses_longest_match(self):
        kinds, _ = vocabulary()
        self.execute(harness([(word, kinds["TOKEN_" + token]) for word, token in OPERATORS.items()], 5), "operators")

    def test_keyword_prefixes_case_and_noncanonical_widths_remain_identifiers(self):
        words = ("int1", "int3", "int130", "int02", "uint0002", "int32value", "returning", "Class", "Console", "auto", "print")
        self.execute(harness([(word, 1) for word in words], 1), "identifiers")

    def test_literals_unknown_bytes_and_recovery(self):
        source = "\n".join((ROOT / path).read_text() for path in
                           ("SelfHost/Frontend/Lexer/Token.krt", "SelfHost/Frontend/Lexer/Lexer.krt")) + r'''
int32 main(){KrtLexer lexer=new KrtLexer();KrtToken token=new KrtToken();
unsafe(using krt.mem;){KrtLexerInit(lexer,(byte*)(int64)"1 1.5 \"text\" 'a' @ if",21);}
KrtLexerNext(lexer,token);if(token.kind!=KrtTokenKind.Number || token.category!=KrtTokenCategory.Integer){return 1;}
KrtLexerNext(lexer,token);if(token.kind!=KrtTokenKind.Number || token.category!=KrtTokenCategory.Floating){return 2;}
KrtLexerNext(lexer,token);if(token.kind!=KrtTokenKind.String || token.category!=KrtTokenCategory.String){return 3;}
KrtLexerNext(lexer,token);if(token.kind!=KrtTokenKind.CharLiteral || token.category!=KrtTokenCategory.Character){return 4;}
KrtLexerNext(lexer,token);if(token.kind!=KrtTokenKind.Unknown || token.category!=KrtTokenCategory.Invalid || token.error==0){return 5;}
KrtLexerNext(lexer,token);if(token.kind!=KrtTokenKind.If || token.category!=KrtTokenCategory.Identifier || token.error!=0){return 6;}
KrtLexerNext(lexer,token);if(token.kind!=KrtTokenKind.Eof || token.category!=KrtTokenCategory.Eof || token.start!=21){return 7;}
return 0;}
'''
        self.execute(source, "literals-recovery")


if __name__ == "__main__":
    unittest.main()
