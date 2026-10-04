"""Ordinary and guarded void calls cannot provide values to other expressions."""
import subprocess

from Test.SelfHost import test_nullable_operators as nullable_cases
from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


DECLARATIONS = '''
class Worker{void Touch(){}}
void Plain(){}
int32 Read(int32 value){return value;}
void Change(ref int32 value){value=7;}
'''

# The marker names the AST node that owns each diagnostic, rather than an
# arbitrary enclosing statement. Each case has one failure and one exact code.
CONSUMERS = {
    'inferred-initializer': ('var result=VALUE;return 0;', 'var result'),
    'typed-initializer': ('int32 result=VALUE;return 0;', 'int32 result'),
    'assignment': ('int32 result=0;result=VALUE;return 0;', 'result=VALUE'),
    'compound-assignment': ('int32 result=0;result+=VALUE;return 0;', 'result+=VALUE'),
    'direct-argument': ('return Read(VALUE);', 'VALUE'),
    'indirect-argument': ('fn(int32)->int32 callback=&Read;return callback(VALUE);', 'VALUE'),
    'syscall-argument': ('syscall(39,VALUE,0,0,0,0,0);return 0;', 'VALUE'),
    'ref-argument': ('Change(ref VALUE);return 0;', 'VALUE'),
    'condition': ('if(VALUE){return 1;}return 0;', 'VALUE'),
    'binary-operand': ('return VALUE+1;', 'VALUE'),
    'ternary-arm': ('return true?VALUE:0;', 'true?VALUE'),
    'unary-operand': ('return !VALUE;', 'VALUE'),
    'cast-operand': ('return (int32)VALUE;', '(int32)VALUE'),
    'index': ('int32[] values=[1];return values[VALUE];', 'values[VALUE]'),
    'array-element': ('var values=[VALUE];return 0;', 'VALUE'),
    'allocation-size': ('var values=new int32[VALUE];return 0;', 'VALUE'),
    'print-argument': ('print(VALUE);return 0;', 'VALUE'),
    'throw-value': ('throw VALUE;', 'throw VALUE'),
    'type-test-value': ('if(VALUE is int32 number){return 1;}return 0;', 'VALUE'),
    'coalescing-result': ('return null??VALUE;', 'null??VALUE'),
    'return-value': ('return VALUE;', 'return VALUE'),
}

NEGATIVE_CASES = {}
for consumer, (template, marker) in CONSUMERS.items():
    for label, value in (('ordinary', 'Plain()'), ('guarded', 'missing?.Touch()')):
        body = template.replace('VALUE', value)
        source = DECLARATIONS + 'int32 main(){Worker missing=null;' + body + '}\n'
        NEGATIVE_CASES[consumer + '-' + label] = (source, marker.replace('VALUE', value), 'E_LOWER')

BOUNDARY_CASES = {
    'default-void': ('int32 main(){var result=default(void);return 0;}', 'default(void)', 'E_LOWER'),
    'sizeof-void': ('int32 main(){return sizeof(void);}', 'sizeof(void)', 'E_LOWER'),
    'catch-void': ('int32 main(){try{throw 7;}catch(void value){}return 0;}', 'void value', 'E_LOWER'),
}
NEGATIVE_CASES.update(BOUNDARY_CASES)

POSITIVE_CASES = {
    'standalone-void-calls': '''
static int32 calls=0;
class Worker{void Touch(){calls++;}}
void Plain(){calls+=10;}
void Relay(){return;}
int32 main(){
    Worker live=new Worker();Worker missing=null;
    Plain();missing?.Touch();live?.Touch();
    if(calls!=11){return 1;}
    fn()->void callback=&Plain;callback();Relay();
    if(calls!=21){return 2;}
    delete live;return 0;
}
''',
}


class VoidValueTests(NativeCompilerFixture):
    def test_standalone_direct_indirect_and_guarded_void_calls_remain_executable(self):
        name = 'standalone-void-calls'
        path = self.work / (name + '.krt')
        path.write_text(POSITIVE_CASES[name], encoding='utf-8')
        for target in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b'')
                    self.assertEqual(result.stderr, b'')

    def test_core_consumers_reject_ordinary_and_guarded_void_values_identically(self):
        for name, (source, marker, code) in NEGATIVE_CASES.items():
            if name not in BOUNDARY_CASES:
                nullable_cases.NullableOperatorTests.reject_targets(self, name, source, marker, code)

    def test_default_sizeof_and_typed_catch_reject_void(self):
        for name, (source, marker, code) in BOUNDARY_CASES.items():
            nullable_cases.NullableOperatorTests.reject_targets(self, name, source, marker, code)
