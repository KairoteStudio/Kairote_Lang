"""Native scope, type, and reference checks used by compiler generations."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
KRTC = Path(os.environ.get('KRTC', ROOT / 'Re.KrtC/build/KrtC')).resolve()


class NativeBindingTests(unittest.TestCase):
    def test_bindings_and_rejections(self):
        cases = [
            ('nested shadow', 'int32 main() { int32 x=1; if(x) { int32 x=7; x=x+1; } return x; }', True),
            ('unknown name', 'int32 main() { return missing; }', False),
            ('scope escape', 'int32 main() { if(1) { int32 hidden=2; } return hidden; }', False),
            ('duplicate local', 'int32 main() { int32 x=1; int32 x=2; return x; }', False),
            ('duplicate parameter', 'int32 f(int32 x, int32 x) { return x; } int32 main() { return f(1,2); }', False),
            ('bad scalar initializer', 'int32 main() { int32 x="bad"; return x; }', False),
            ('bad scalar assignment', 'int32 main() { int32 x=0; x="bad"; return x; }', False),
            ('malformed progress', 'int32 main() { return ; broken }', False),
            ('unknown type', 'int32 main() { Unknown x=null; return 0; }', False),
            ('unknown field', 'class Box { public int32 x; } int32 main() { Box b=new Box(); return b.missing; }', False),
            ('wrong arity', 'int32 f(int32 x) { return x; } int32 main() { return f(); }', False),
            ('missing ref', 'void f(ref int32 x) { x=7; } int32 main() { int32 x=0; f(x); return x; }', False),
            ('extra ref', 'void f(int32 x) { return; } int32 main() { int32 x=0; f(ref x); return x; }', False),
            ('ref literal', 'void f(ref int32 x) { x=7; } int32 main() { f(ref 1); return 0; }', False),
            ('ref wrong width', 'void f(ref int32 x) { x=7; } int32 main() { int64 x=0; f(ref x); return 0; }', False),
            ('ref field', 'class Box { public int32 x; } void f(ref int32 x) { x=7; } int32 main() { Box b=new Box(); f(ref b.x); return b.x; }', True),
            ('ref array element', 'void f(ref int32 x) { x=7; } int32 main() { int32[] x=new int32[2]; f(ref x[0]); return x[0]; }', True),
            ('grouped comparison', 'class Box { public int32 x; } int32 main() { Box b=new Box(); if((b.x < 2 || b.x > 7)) { return 1; } return 0; }', True),
        ]
        parts = [str(path.relative_to(ROOT / 'SelfHost')) for folder in ('Frontend', 'Middle')
                 for path in sorted((ROOT / 'SelfHost' / folder).rglob('*.krt'))]
        prelude = '\n'.join((ROOT / 'SelfHost' / part).read_text() for part in parts)
        harness = '''
bool check(string text) {
    unsafe(using krt.mem;) {
        byte* source=(byte*)(int64)text; int64 length=0;
        while(source[length]!=0) { length=length+1; }
        KrtLexer lexer=new KrtLexer(); KrtToken token=new KrtToken(); KrtParser parser=new KrtParser();
        KrtLexerInit(lexer,source,length); KrtParserInit(parser,lexer,token);
        KrtAstNode module=KrtParserParseCompilationUnit(parser);
        if(parser.errors!=0) { return false; }
        KrtAstNode unit=module.left;
        while(unit!=null) {
            if(!KrtNativeBindFunction(unit,module,(int64)source)) { return false; }
            unit=unit.next;
        }
        return true;
    }
}
int32 main() {
'''
        for index, (_, source, accepted) in enumerate(cases, 1):
            harness += f'if(check({json.dumps(source)}) != {str(accepted).lower()}) {{ return {index}; }}\n'
        harness += 'return 0; }\n'
        with tempfile.TemporaryDirectory(prefix='krt-native-binding-') as directory:
            work = Path(directory)
            source = work / 'bindings.krt'
            source.write_text(prelude + harness)
            executable = work / 'bindings'
            build = subprocess.run([str(KRTC), '-O2', str(source), 'output', str(executable)],
                                   cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            executable.chmod(0o755)
            result = subprocess.run([str(executable)], cwd=work, timeout=10)
            failed = cases[result.returncode - 1][0] if 0 < result.returncode <= len(cases) else str(result.returncode)
            self.assertEqual(result.returncode, 0, failed)
