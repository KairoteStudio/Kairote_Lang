"""Aggregate values cannot become conditions through their storage addresses."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


class AggregateConditionTests(NativeCompilerFixture):
    def reject_condition(self, source, name):
        path = self.work / (name + '.krt')
        path.write_text(source)
        for target in ('native', 'vm'):
            for level in range(4):
                for previous in (None, b'previous artifact\x00must survive'):
                    with self.subTest(source=name, target=target, optimization=level,
                                      previous_artifact=previous is not None):
                        output = self.work / f'{name}-{target}-o{level}-{"new" if previous is None else "old"}.artifact'
                        if previous is not None:
                            output.write_bytes(previous)
                        argv = [str(COMPILER), '--linker', str(LINKER), str(path),
                                f'-O{level}', '-o', str(output)]
                        if target == 'vm':
                            argv += ['target', 'vm']
                        result = subprocess.run(argv, cwd=self.work, env=self.env,
                                                capture_output=True, text=True, timeout=60)
                        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                        self.assertIn('E_LOWER', result.stderr)
                        self.assertNotIn('E_PARSE', result.stderr)
                        self.assertRegex(result.stderr, r':\d+:\d+: E_LOWER:')
                        if previous is None:
                            self.assertFalse(output.exists(), 'rejection created an output artifact')
                        else:
                            self.assertEqual(output.read_bytes(), previous)

    @staticmethod
    def aggregate_sources():
        return {
            'struct': ('struct Value{int32 number;}', 'Value value=default(Value);', 'value'),
            'fixed': ('struct Value{int32 items[2];}', 'Value holder=default(Value);', 'holder.items'),
            'inferred-fixed': ('struct Value{int32 items[2];}T Copy<T>(T value){return value;}',
                               'Value holder=default(Value);var value=Copy(holder.items);', 'value'),
            'generic-fixed-expression': ('struct Value{int32 items[2];}T Copy<T>(T value){return value;}',
                                        'Value holder=default(Value);', 'Copy(holder.items)'),
        }

    def test_reject_aggregate_if_while_do_for_and_ternary_conditions(self):
        statements = {
            'if': 'if(CONDITION){return 1;}return 0;',
            'while': 'while(CONDITION){break;}return 0;',
            'do': 'do{break;}while(CONDITION);return 0;',
            'for': 'for(int32 i=0;CONDITION;i++){break;}return 0;',
            'ternary': 'return CONDITION?1:0;',
        }
        for shape, (declarations, initialization, condition) in self.aggregate_sources().items():
            for statement, body in statements.items():
                source = declarations + 'int32 main(){' + initialization
                source += body.replace('CONDITION', condition) + '}'
                self.reject_condition(source, shape + '-' + statement)

    def test_reject_aggregate_logical_operands_on_either_side(self):
        for shape, (declarations, initialization, condition) in self.aggregate_sources().items():
            for operator in ('&&', '||'):
                for left in (True, False):
                    expression = condition + operator + 'true' if left else 'true' + operator + condition
                    source = declarations + 'int32 main(){' + initialization + 'return (' + expression + ')?1:0;}'
                    self.reject_condition(source, f'{shape}-logical-{operator}-{left}')

    def test_scalar_boolean_reference_and_dynamic_array_conditions_keep_side_effects(self):
        source = '''
static int32 calls=0;
int32 Scalar(int32 value){calls++;return value;}
bool Boolean(bool value){calls++;return value;}
class Reference{}
Reference Object(Reference value){calls++;return value;}
int32[] Array(int32[] value){calls++;return value;}
int32 main(){
    if(Scalar(0)){return 1;}if(!Scalar(3)){return 2;}
    if(Scalar(0)||Boolean(false)){return 3;}
    if(!(Boolean(true)&&Scalar(2)) || calls!=6){return 4;}
    int32 loop=0;while(Scalar(loop<2)){loop++;}
    if(loop!=2||calls!=9){return 5;}
    do{loop--;}while(Boolean(loop>0));
    if(loop!=0||calls!=11){return 6;}
    int32 sum=0;for(int32 i=0;Scalar(i<3);i++){sum+=i;}
    if(sum!=3||calls!=15){return 7;}
    int32 chosen=Scalar(0)?31:42;
    if(chosen!=42||calls!=16){return 8;}
    Reference object=new Reference();Reference missing=null;
    if(!Object(object)){return 9;}if(Object(missing)){return 10;}
    int32[] values=new int32[2];int32[] absent=null;
    if(!Array(values)){return 11;}if(Array(absent)){return 12;}
    if(Object(missing)&&Scalar(1)){return 13;}
    if(!(Array(values)||Scalar(0))||calls!=22){return 14;}
    delete object;delete values;return 0;
}
'''
        self.execute(source)
        path = self.work / 'valid-conditions.krt'
        path.write_text(source)
        for level in range(4):
            with self.subTest(target='vm', optimization=level):
                output = self.work / f'valid-conditions-o{level}.vm'
                self.command(path, f'-O{level}', 'target', 'vm', '-o', output)
                result = subprocess.run([str(COMPILER), 'run-vm', str(output)],
                                        cwd=self.work, env=self.env, capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
