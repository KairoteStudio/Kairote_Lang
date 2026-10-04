"""Parser-only contracts: assert preserved syntax, independently of codegen."""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from Test.SelfHost.test_contract import ROOT, KRTC, ARKLINK, compiler_prelude


class ParserTests(unittest.TestCase):
    def check_ast(self, source_text, assertions, compilation_unit=False, expected_errors=0):
        parts = ('Frontend/Lexer/Token.krt', 'Frontend/Lexer/Lexer.krt',
                 'Frontend/Parser/Ast.krt', 'Frontend/Parser/Parser.krt', 'Frontend/Parser/Names.krt')
        prelude = compiler_prelude(parts)
        program = f'''
int32 main() {{
    string input = {json.dumps(source_text)};
    KrtLexer lexer = new KrtLexer(); KrtToken token = new KrtToken();
    KrtParser parser = new KrtParser();
    unsafe(using krt.mem;) {{ KrtLexerInit(lexer, (byte*)(int64)input, {len(source_text.encode())}); }}
    KrtParserInit(parser, lexer, token);
    KrtAstNode unit = {'KrtParserParseCompilationUnit' if compilation_unit else 'KrtParserParseFunction'}(parser);
    if (parser.errors != {expected_errors} || token.kind != KrtTokenKind.Eof) {{ return 1; }}
    {assertions}
    return 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix='krt-parser-') as directory:
            work = Path(directory)
            source = work / 'ParserTest.krt'
            source.write_text(prelude + program)
            binary = work / 'parser-test'
            compiler=os.environ.get("SELFHOST_COMPILER")
            command=([str(Path(compiler).resolve()),'-O2','--linker',str(ARKLINK),str(source),'output',str(binary)]
                     if compiler else [str(KRTC),'-O2',str(source),'output',str(binary)])
            result = subprocess.run(command,cwd=work,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], cwd=work, timeout=10)
            self.assertEqual(result.returncode, 0, source_text)

    def test_function_keyword_and_spans(self):
        for keyword in ('function', 'func', 'fn'):
            with self.subTest(keyword=keyword):
                self.check_ast(f'{keyword} main() {{ return 7; }}', f'''
    if (unit.start != 0 || unit.name_start != {len(keyword)+1} || unit.name_length != 4) {{ return 2; }}
    if (unit.type_length != 0 || unit.left.left.left.value != 7) {{ return 3; }}
''')

    def test_coalescing_is_right_associative_and_binds_above_ternary(self):
        self.check_ast('int32 main(){return a??b??c?d:e??f;}', '''
    KrtAstNode value=unit.left.left.left;
    if(value.kind!=KrtAstKind.Ternary || value.left.kind!=KrtAstKind.Binary || value.left.op!=113){return 2;}
    if(value.left.left.kind!=KrtAstKind.Identifier || value.left.right.kind!=KrtAstKind.Binary || value.left.right.op!=113){return 3;}
    if(value.left.right.left.kind!=KrtAstKind.Identifier || value.left.right.right.kind!=KrtAstKind.Identifier){return 4;}
    if(value.right.kind!=KrtAstKind.Identifier || value.alternate.kind!=KrtAstKind.Binary || value.alternate.op!=113){return 5;}
''')

    def test_conditional_postfix_chains_retain_receivers_and_group_boundaries(self):
        self.check_ast('int32 main(){return Fetch()?.items[Next()]?.Read(Arg());}', '''
    KrtAstNode outer=unit.left.left.left;KrtAstNode inner=outer.right;
    if(outer.kind!=KrtAstKind.NullConditional || outer.left.kind!=KrtAstKind.Call || inner.kind!=KrtAstKind.NullConditional){return 2;}
    if(inner.left.kind!=KrtAstKind.Index || inner.left.right.kind!=KrtAstKind.Call || inner.left.left.kind!=KrtAstKind.Member){return 3;}
    if(inner.left.left.left.kind!=KrtAstKind.GuardReceiver || inner.right.kind!=KrtAstKind.Call || inner.right.right.kind!=KrtAstKind.Call){return 4;}
    if(inner.right.left.kind!=KrtAstKind.Member || inner.right.left.left.kind!=KrtAstKind.GuardReceiver || outer.length!=inner.length){return 5;}
''')
        self.check_ast('int32 main(){return (route?.child).Read();}', '''
    KrtAstNode call=unit.left.left.left;
    if(call.kind!=KrtAstKind.Call || call.left.kind!=KrtAstKind.Member || call.left.left.kind!=KrtAstKind.NullConditional || !call.left.left.grouped){return 2;}
    if(call.left.left.right.kind!=KrtAstKind.Member || call.left.left.right.left.kind!=KrtAstKind.GuardReceiver){return 3;}
''')
        self.check_ast('int32 main(){return routes?[Next()].child;}', '''
    KrtAstNode guard=unit.left.left.left;
    if(guard.kind!=KrtAstKind.NullConditional || guard.left.kind!=KrtAstKind.Identifier || guard.right.kind!=KrtAstKind.Member){return 2;}
    if(guard.right.left.kind!=KrtAstKind.Index || guard.right.left.left.kind!=KrtAstKind.GuardReceiver || guard.right.left.right.kind!=KrtAstKind.Call){return 3;}
''')

    def test_function_type_returns_are_distinct_from_fn_declaration_keywords(self):
        self.check_ast('private class Hidden{}public fn()->Hidden GetFactory(){return &Make;}Hidden Make(){return new Hidden();}fn named(){return 42;}class Holder{public static fn(int32)->fn()->int32 Chain(){return &Factory;}public fn Read(){return 7;}}', '''
    KrtAstNode factory=unit.left;KrtAstNode type=factory.syntax_type;
    if(factory.syntax_modifiers!=1 || factory.type_length!=12 || factory.name_length!=10 || type==null || type.op!=1){return 2;}
    if(type.left.parameter_count!=0 || type.left.type_length!=6 || type.left.syntax_type.name_length!=6){return 3;}
    if(factory.left.left.left.kind!=KrtAstKind.Unary || factory.left.left.left.op!=38){return 4;}
    if(factory.next.next.type_length!=0 || factory.next.next.syntax_type!=null || factory.next.next.left.left.left.value!=42){return 5;}
    KrtAstNode holder=unit.declarations.next;KrtAstNode chain=holder.declarations;
    if(chain.syntax_modifiers!=9 || chain.parameter_count!=0 || chain.syntax_type.op!=1 || chain.syntax_type.left.parameter_count!=1){return 6;}
    if(chain.syntax_type.left.syntax_type.op!=1 || chain.syntax_type.left.syntax_type.left.parameter_count!=0){return 7;}
    if(chain.next.type_length!=0 || chain.next.syntax_type!=null || chain.next.parameter_count!=1 || chain.next.native_owner!=holder){return 8;}
''', compilation_unit=True)

    def test_fixed_fields_preserve_length_expression_and_element_type(self):
        self.check_ast('struct Packet{byte zero[8];uint16 words[2+3];int32[] dynamic;Outer.Inner<int64>* pointers[4];int32 tail;}class Holder{byte fixed_bytes[16];}', '''
    KrtAstNode packet=unit.declarations;KrtAstNode field=packet.body;
    if(packet.value!=2 || field.native_owner!=packet || field.syntax_array_size==null || field.syntax_array_size.value!=8 || field.syntax_type.syntax_array_rank!=0){return 2;}
    field=field.next;
    if(field.syntax_array_size.kind!=KrtAstKind.Binary || field.syntax_array_size.op!=43 || field.syntax_array_size.left.value!=2 || field.syntax_array_size.right.value!=3){return 3;}
    field=field.next;
    if(field.syntax_array_size!=null || field.syntax_type.syntax_array_rank!=1){return 4;}
    field=field.next;
    if(field.syntax_type.syntax_pointer_depth!=1 || field.syntax_type.right==null || field.syntax_type.right.left==null || field.syntax_array_size.value!=4){return 5;}
    if(field.next.name_length!=4 || field.next.syntax_array_size!=null || field.next.next!=null){return 6;}
    KrtAstNode holder=packet.next;
    if(holder.body.native_owner!=holder || holder.body.syntax_array_size.value!=16){return 7;}
''', compilation_unit=True)

    def test_malformed_fixed_field_recovers_following_members(self):
        self.check_ast('struct Packet{byte broken[;int32 good;}int32 main(){return 0;}', '''
    KrtAstNode packet=unit.declarations;
    if(packet.body==null || packet.body.next==null || packet.body.next.name_length!=4 || packet.body.next.native_owner!=packet){return 2;}
    if(unit.left==null || unit.left.name_length!=4 || unit.left.left.left.left.value!=0){return 3;}
''', compilation_unit=True, expected_errors=1)

    def test_struct_field_initializers_keep_default_constructor_with_argument_constructors(self):
        self.check_ast('struct Value{int32 number=5;Value(int32 n){number=n;}}class Reference{int32 number=7;Reference(int32 n){number=n;}}struct Explicit{int32 number=11;Explicit(){number+=2;}Explicit(int32 n){number=n;}}', '''
    KrtAstNode value=unit.declarations;KrtAstNode zero=value.declarations;KrtAstNode argument=zero.next;
    if(zero.op!=220 || zero.parameter_count!=1 || zero.native_owner!=value || zero.syntax_modifiers!=0 || zero.next==null || argument.parameter_count!=2 || (argument.next!=null && argument.next.native_owner==value)){return 2;}
    if(zero.parameter_type_starts[0]!=value.name_start || zero.parameter_type_lengths[0]!=value.name_length || zero.parameter_refs[0]){return 3;}
    if(zero.left.left.kind!=KrtAstKind.Assign || zero.left.left.right.value!=5 || zero.left.left.next!=null || zero.left.child_count!=1){return 4;}
    if(argument.left.left.right.value!=5 || argument.left.left.next.kind!=KrtAstKind.Assign){return 5;}
    KrtAstNode reference=value.next;
    if(reference.declarations.parameter_count!=2 || (reference.declarations.next!=null && reference.declarations.next.native_owner==reference)){return 6;}
    KrtAstNode explicit_type=reference.next;
    if(explicit_type.declarations.parameter_count!=1 || explicit_type.declarations.next.parameter_count!=2 || explicit_type.declarations.next.next!=null){return 7;}
    if(explicit_type.declarations.left.left.right.value!=11 || explicit_type.declarations.left.left.next.kind!=KrtAstKind.CompoundAssign){return 8;}
''', compilation_unit=True)

    def test_function_style_builtin_casts_keep_types_and_operand_once(self):
        self.check_ast('int32 main(){int8(258);return int32(float32(n++));}', '''
    KrtAstNode first=unit.left.left;
    if(first.kind!=KrtAstKind.Expression || first.left.kind!=KrtAstKind.Cast || first.left.type_length!=4 || first.left.length!=9 || first.left.left.value!=258){return 2;}
    KrtAstNode nested=first.next.left;
    if(nested.kind!=KrtAstKind.Cast || nested.type_length!=5 || nested.syntax_type==null || nested.child_count!=1){return 3;}
    if(nested.left.kind!=KrtAstKind.Cast || nested.left.type_length!=7 || nested.left.syntax_type==null){return 4;}
    KrtAstNode operand=nested.left.left;
    if(operand.kind!=KrtAstKind.Unary || operand.op!=201 || operand.value!=0 || operand.left.kind!=KrtAstKind.Identifier || operand.next!=null){return 5;}
''')

    def test_empty_and_comment_only_translation_units_are_valid_ast(self):
        for source in ('', '// Empty compilation unit.\n'*6000, '/* no declarations */\n// final comment'):
            with self.subTest(source_length=len(source)):
                self.check_ast(source, '''
    if(unit.kind!=KrtAstKind.Program || unit.left!=null || unit.body!=null || unit.declarations!=null || unit.child_count!=0){return 2;}
    if(unit.native_using_scope==null || unit.native_scope==null || parser.diagnostics!=null){return 3;}
''',compilation_unit=True)

    def test_stackalloc_preserves_complete_element_type_and_postfix(self):
        self.check_ast('int32 main(){int32** p=stackalloc int32*[1];return stackalloc Outer.Inner<int32?>*?[2][0];}', '''
    KrtAstNode declaration=unit.left.left;
    if(declaration.syntax_type.syntax_pointer_depth!=2){return 2;}
    KrtAstNode allocation=declaration.left;
    if(allocation.kind!=KrtAstKind.StackAlloc || allocation.syntax_type==null || allocation.syntax_type.syntax_pointer_depth!=1 || allocation.type_length!=6 || allocation.left.value!=1){return 3;}
    KrtAstNode access=declaration.next.left;
    if(access.kind!=KrtAstKind.Index || access.right.value!=0 || access.left.kind!=KrtAstKind.StackAlloc || access.left.left.value!=2){return 4;}
    KrtAstNode type=access.left.syntax_type;
    if(type.syntax_pointer_depth!=1 || !type.syntax_nullable || type.right==null || type.right.name_length!=5){return 5;}
    if(type.right.left==null || !type.right.left.left.syntax_nullable || type.right.left.left.name_length!=5){return 6;}
''')

    def test_named_arguments_keep_labels_member_names_ref_and_source_order(self):
        source='int32 main(){pack(second: object.value, first: next());mutate(ref target: object.field, other: ref item);return pack(object.value);}'
        self.check_ast(source, f'''
    KrtAstNode call=unit.left.left.left;KrtAstNode second=call.right;KrtAstNode first=second.next;
    if(call.kind!=KrtAstKind.Call || call.parameter_count!=2 || second.kind!=KrtAstKind.Member || second.name_length!=5){{return 2;}}
    if(second.syntax_argument_name_start!={source.index('second:')} || second.syntax_argument_name_length!=6 || second.name_start!={source.index('value')} || second.native_argument_index!=-1 || second.native_argument_slot!=-1){{return 3;}}
    if(first.kind!=KrtAstKind.Call || first.syntax_argument_name_start!={source.index('first:')} || first.syntax_argument_name_length!=5 || first.start<=second.start || first.next!=null){{return 4;}}
    KrtAstNode referenced=unit.left.left.next.left.right;
    if(referenced.kind!=KrtAstKind.Unary || referenced.op!=200 || referenced.syntax_argument_name_length!=6 || referenced.left.kind!=KrtAstKind.Member || referenced.left.name_length!=5){{return 5;}}
    if(referenced.next.kind!=KrtAstKind.Unary || referenced.next.op!=200 || referenced.next.syntax_argument_name_length!=5 || referenced.next.left.length!=4){{return 6;}}
    KrtAstNode positional=unit.left.left.next.next.left.right;
    if(positional.kind!=KrtAstKind.Member || positional.name_length!=5 || positional.syntax_argument_name_length!=0){{return 7;}}
''')

    def test_constructor_and_base_calls_keep_named_argument_labels(self):
        self.check_ast('class Parent{public Parent(int32 value){}}class Child:Parent{public Child(int32 value):base(value: value){}}int32 main(){return new Child(value: source.field).field;}', '''
    KrtAstNode constructor=unit.declarations.next.declarations;
    if(constructor.op!=220 || constructor.native_base_call==null || constructor.native_base_call.right.syntax_argument_name_length!=5){return 2;}
    KrtAstNode access=unit.left.left.left.left;
    if(access.kind!=KrtAstKind.Member || access.left.kind!=KrtAstKind.New || access.left.parameter_count!=1){return 3;}
    KrtAstNode argument=access.left.right;
    if(argument.kind!=KrtAstKind.Member || argument.name_length!=5 || argument.syntax_argument_name_length!=5 || argument.name_start==argument.syntax_argument_name_start){return 4;}
''',compilation_unit=True)

    def test_missing_named_argument_value_recovers_following_statement(self):
        self.check_ast('int32 main(){pack(value:);return 42;}', '''
    KrtAstNode malformed=unit.left.left;
    if(malformed.kind!=KrtAstKind.Invalid || malformed.left.left.right.kind!=KrtAstKind.Invalid || malformed.left.left.right.syntax_argument_name_length!=5){return 2;}
    if(malformed.next.kind!=KrtAstKind.Return || malformed.next.left.value!=42){return 3;}
''',expected_errors=1)

    def test_seed_executes_function_cast_typed_stackalloc_and_ordered_names(self):
        cases={
            'integer_cast':('int32 main(){return int8(258);}',2),
            'nested_cast':('int32 main(){return int32(float32(42.75));}',42),
            'cast_operand_once':('int32 main(){int32 n=41;int32 r=int32(n++);if(n!=42||r!=41){return 1;}return n;}',42),
            'typed_stackalloc':('int32 main(){unsafe(using krt.mem;){int32 n=42;int32** p=stackalloc int32*[1];p[0]=&n;return *p[0];}}',42),
            'ordered_names':('int32 pack(int32 a,int32 b){return a*10+b;}int32 main(){return pack(a:4,b:2);}',42),
        }
        with tempfile.TemporaryDirectory(prefix='krt-seed-parser-') as directory:
            work=Path(directory)
            for name,(text,expected) in cases.items():
                with self.subTest(case=name):
                    source=work/f'{name}.krt';source.write_text(text);binary=work/name
                    compiled=subprocess.run([str(KRTC),'-O0',str(source),'output',str(binary)],cwd=work,capture_output=True,text=True,timeout=30)
                    self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
                    self.assertEqual(subprocess.run([str(binary)],cwd=work,timeout=10).returncode,expected)

    def test_parameter_boundaries_preserve_all_types_and_receiver_offsets(self):
        parameters=', '.join(f'int32 p{index}' for index in range(127))+', ref int64 p127'
        self.check_ast(f'int32 full({parameters}){{return p0;}}', '''
    if(unit.parameter_count!=128 || unit.parameter_lengths[127]!=4 || !unit.parameter_refs[127] || unit.parameter_type_lengths[127]!=5){return 2;}
    KrtAstNode type=unit.syntax_parameter_types;int32 count=0;
    while(type!=null){count=count+1;if(count==128&&type.name_length!=5){return 3;}type=type.next;}
    if(count!=128){return 4;}
''')
        member_parameters=', '.join(f'int32 p{index}' for index in range(126))+', ref int64 p126'
        self.check_ast(f'class Many{{public int32 Run({member_parameters}){{return p0;}}}} int32 main(){{return 0;}}', '''
    KrtAstNode method=unit.declarations.declarations;
    if(method.parameter_count!=128 || method.parameter_lengths[0]!=0 || method.parameter_type_lengths[0]!=4 || method.parameter_lengths[127]!=4 || !method.parameter_refs[127]){return 2;}
    KrtAstNode type=method.syntax_parameter_types;int32 count=0;
    while(type!=null){count=count+1;type=type.next;}
    if(count!=127){return 3;}
''',compilation_unit=True)

    def test_parameter_overflow_rejects_and_preserves_following_ast(self):
        parameters=','.join(f'int32 p{index}' for index in range(129))
        self.check_ast(f'int32 overflow({parameters}){{return p0;}} int32 valid(){{return 42;}}', '''
    if(unit.left.parameter_count!=128 || unit.left.next==null || unit.left.next.left.left.left.value!=42){return 2;}
    KrtAstNode type=unit.left.syntax_parameter_types;int32 count=0;while(type!=null){count=count+1;type=type.next;}
    if(count!=128 || parser.diagnostics.expected!=KrtTokenKind.RightParen || parser.diagnostics.next!=null){return 3;}
''',compilation_unit=True,expected_errors=1)
        member_parameters=','.join(f'int32 p{index}' for index in range(128))
        self.check_ast(f'class Many{{public int32 Overflow({member_parameters}){{return p0;}} int32 Valid(){{return 42;}}}} int32 main(){{return 0;}}', '''
    KrtAstNode method=unit.declarations.declarations;
    if(method.kind!=KrtAstKind.Invalid || method.left.parameter_count!=128 || method.next==null || method.next.left.left.left.value!=42){return 2;}
    if(method.next.parameter_count!=1 || parser.diagnostics.expected!=KrtTokenKind.RightParen){return 3;}
''',compilation_unit=True,expected_errors=1)

    def test_function_type_parameter_boundary_and_overflow_recovery(self):
        parameters=','.join(['int32']*127+['ref int64'])
        self.check_ast(f'int32 main(){{fn({parameters})->int32 callback;return 42;}}', '''
    KrtAstNode signature=unit.left.left.syntax_type.left;
    if(signature.parameter_count!=128 || !signature.parameter_refs[127] || signature.parameter_type_lengths[127]!=5){return 2;}
    KrtAstNode type=signature.syntax_parameter_types;int32 count=0;while(type!=null){count=count+1;type=type.next;}
    if(count!=128 || unit.left.left.next.left.value!=42){return 3;}
''')
        overflow=','.join(['int32']*129)
        self.check_ast(f'int32 main(){{fn({overflow})->int32 callback;return 42;}} int32 valid(){{return 7;}}', '''
    if(unit.left.left.left.kind!=KrtAstKind.Invalid || unit.left.left.left.left.syntax_type.left.parameter_count!=128){return 2;}
    if(unit.left.left.left.next.left.value!=42 || unit.left.next.left.left.left.value!=7){return 3;}
''',compilation_unit=True,expected_errors=1)
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
    if (lambda_node.parameter_type_lengths[0] != 5 || lambda_node.parameter_type_lengths[1] != 5) { return 4; }
    if (lambda_node.parameter_refs[0] || !lambda_node.parameter_refs[1]) { return 5; }
    if (lambda_node.left.kind != KrtAstKind.Binary || lambda_node.left.op != 43) { return 6; }
''')

    def test_lambda_keywords_keep_expression_and_block_bodies(self):
        declarations=[]
        for keyword in ('function', 'func', 'fn'):
            declarations.extend((
                f'var expression{keyword}={keyword}(int32 value)=>value+1;',
                f'var arrow{keyword}={keyword}(int32 value)->int64=>{{return value;}};',
                f'var block{keyword}={keyword}()->void{{return;}};',
            ))
        self.check_ast('int32 main(){'+''.join(declarations)+'return 0;}', '''
    KrtAstNode declaration=unit.left.left;int32 index=0;
    while(index<9){
        KrtAstNode lambda_node=declaration.left;
        if(declaration.kind!=KrtAstKind.Variable || lambda_node.kind!=KrtAstKind.Lambda || lambda_node.child_count!=1){return 2;}
        if(index%3==0){
            if(lambda_node.parameter_count!=1 || lambda_node.syntax_type!=null || lambda_node.left.kind!=KrtAstKind.Binary || lambda_node.left.op!=43){return 3;}
        }else{
            if(lambda_node.syntax_type==null || lambda_node.left.kind!=KrtAstKind.Block || lambda_node.left.left.kind!=KrtAstKind.Return){return 4;}
            if(index%3==1 && (lambda_node.type_length!=5 || lambda_node.parameter_count!=1 || lambda_node.left.left.left.kind!=KrtAstKind.Identifier)){return 5;}
            if(index%3==2 && (lambda_node.type_length!=4 || lambda_node.parameter_count!=0 || lambda_node.left.left.left!=null)){return 6;}
        }
        declaration=declaration.next;index=index+1;
    }
    if(declaration.kind!=KrtAstKind.Return || declaration.left.value!=0 || declaration.next!=null){return 7;}
''')

    def test_lambda_contextual_parameters_keep_type_slots_and_modifiers(self):
        self.check_ast('int32 main(){var callback=function(first,ref int64 second,readonly third,ref readonly Outer.Inner<int32>?* fourth,fn(int32)->int64 fifth)=>first+second;return 0;}', '''
    KrtAstNode lambda_node=unit.left.left.left;
    if(lambda_node.kind!=KrtAstKind.Lambda || lambda_node.parameter_count!=5){return 2;}
    if(lambda_node.parameter_type_lengths[0]!=0 || lambda_node.parameter_type_lengths[1]!=5 || lambda_node.parameter_type_lengths[2]!=0){return 3;}
    if(lambda_node.parameter_lengths[0]!=5 || lambda_node.parameter_lengths[1]!=6 || lambda_node.parameter_lengths[2]!=5 || lambda_node.parameter_lengths[3]!=6 || lambda_node.parameter_lengths[4]!=5){return 4;}
    if(lambda_node.parameter_refs[0] || !lambda_node.parameter_refs[1] || lambda_node.parameter_refs[2] || !lambda_node.parameter_refs[3] || lambda_node.parameter_refs[4]){return 5;}
    KrtAstNode type=lambda_node.syntax_parameter_types;
    if(type==null || type.type_length!=0 || type.name_length!=0 || type.syntax_modifiers!=0){return 6;}
    type=type.next;if(type.type_length!=5 || type.name_length!=5 || type.syntax_modifiers!=0){return 7;}
    type=type.next;if(type.type_length!=0 || type.name_length!=0 || type.syntax_modifiers!=16 || !type.native_readonly){return 8;}
    type=type.next;if(type.right==null || type.syntax_pointer_depth!=1 || type.syntax_pointee_nullable!=1 || type.syntax_modifiers!=16 || !type.native_readonly){return 9;}
    type=type.next;if(type.op!=1 || type.left.parameter_count!=1 || type.left.type_length!=5 || type.next!=null){return 10;}
    if(lambda_node.left.kind!=KrtAstKind.Binary || lambda_node.left.op!=43 || unit.left.left.next.left.value!=0){return 11;}
''')

    def test_lambda_nested_return_and_immediate_call_shapes(self):
        self.check_ast('int32 main(){var make=fn(int32 first)->fn(int32)->int32=>fn(int32 second)=>first+second;var result=(function(int32 value){return value+1;})(4);return result;}', '''
    KrtAstNode outer=unit.left.left.left;
    if(outer.kind!=KrtAstKind.Lambda || outer.syntax_type.op!=1 || outer.syntax_type.left.parameter_count!=1 || outer.syntax_type.left.type_length!=5){return 2;}
    if(outer.left.kind!=KrtAstKind.Lambda || outer.left.parameter_count!=1 || outer.left.left.kind!=KrtAstKind.Binary || outer.left.next!=null){return 3;}
    KrtAstNode call=unit.left.left.next.left;
    if(call.kind!=KrtAstKind.Call || call.left.kind!=KrtAstKind.Lambda || !call.left.grouped || call.left.left.kind!=KrtAstKind.Block || call.right.value!=4){return 4;}
    if(call.parameter_count!=1 || unit.left.left.next.next.left.kind!=KrtAstKind.Identifier){return 5;}
''')

    def test_lambda_postfix_preserves_expression_and_block_body_boundaries(self):
        self.check_ast('int32 main(){var expression=(function(int32 value)=>value+1)(41);var direct=function(int32 value){return value+1;}(41);var arrow=function(int32 value)=>{return value+1;}(41);return 0;}', '''
    KrtAstNode declaration=unit.left.left;int32 index=0;
    while(index<3){
        KrtAstNode call=declaration.left;
        if(call.kind!=KrtAstKind.Call || call.parameter_count!=1 || call.right.value!=41 || call.left.kind!=KrtAstKind.Lambda || call.left.parameter_count!=1){return 2;}
        if(index==0 && (!call.left.grouped || call.left.left.kind!=KrtAstKind.Binary || call.left.left.op!=43)){return 3;}
        if(index!=0 && (call.left.grouped || call.left.left.kind!=KrtAstKind.Block || call.left.left.left.kind!=KrtAstKind.Return)){return 4;}
        declaration=declaration.next;index=index+1;
    }
    if(declaration.kind!=KrtAstKind.Return || declaration.left.value!=0){return 5;}
''')

    def test_lambda_statements_preserve_fn_type_declaration_lookahead(self):
        statements=[]
        for keyword in ('function','func','fn'):
            statements.extend((f'{keyword}(int32 value){{return value+1;}}(41);',f'{keyword}()=>1;'))
        self.check_ast('int32 main(){'+''.join(statements)+'fn(int32)->int32 callback;return 0;}', '''
    KrtAstNode statement=unit.left.left;int32 index=0;
    while(index<6){
        if(statement.kind!=KrtAstKind.Expression){return 2;}
        if(index%2==0){
            if(statement.left.kind!=KrtAstKind.Call || statement.left.left.kind!=KrtAstKind.Lambda || statement.left.left.left.kind!=KrtAstKind.Block || statement.left.right.value!=41){return 3;}
        }else{
            if(statement.left.kind!=KrtAstKind.Lambda || statement.left.parameter_count!=0 || statement.left.left.kind!=KrtAstKind.Integer || statement.left.left.value!=1){return 4;}
        }
        statement=statement.next;index=index+1;
    }
    if(statement.kind!=KrtAstKind.Variable || statement.syntax_type.op!=1 || statement.syntax_type.left.parameter_count!=1 || statement.syntax_type.left.type_length!=5){return 5;}
    if(statement.next.kind!=KrtAstKind.Return || statement.next.left.value!=0){return 6;}
''')

    def test_lambda_field_and_array_initializers_keep_independent_nodes(self):
        self.check_ast('struct Handler{public fn(int32)->int32 callback=function(value)=>value+1;}int32 main(){fn(int32)->int32 callbacks[2]=[function(first)=>first+1,function(int32 second){return second+2;}];return 0;}', '''
    KrtAstNode handler=unit.declarations;KrtAstNode field=handler.body;
    if(handler.value!=2 || field.syntax_type.op!=1 || field.left.kind!=KrtAstKind.Lambda || field.left.parameter_count!=1 || field.left.parameter_type_lengths[0]!=0 || field.left.next!=null){return 2;}
    if(handler.declarations.op!=220 || handler.declarations.left.left.right!=field.left){return 3;}
    KrtAstNode literal=unit.left.left.left.left;
    if(literal.kind!=KrtAstKind.ArrayLiteral || literal.child_count!=2 || literal.left.kind!=KrtAstKind.Lambda || literal.left.next.kind!=KrtAstKind.Lambda || unit.left.left.left.syntax_type.syntax_array_rank!=1 || unit.left.left.left.syntax_array_size.value!=2){return 4;}
    if(literal.left.parameter_type_lengths[0]!=0 || literal.left.left.kind!=KrtAstKind.Binary || literal.left.next.parameter_type_lengths[0]!=5 || literal.left.next.left.kind!=KrtAstKind.Block || literal.left.next.next!=null){return 5;}
    if(literal.left.left.next!=null || literal.left.next.left.next!=null || unit.left.left.left.next.left.value!=0){return 6;}
''',compilation_unit=True)

    def test_lambda_source_spans_exclude_parameter_modifiers_and_trailing_space(self):
        source='int32 main(){var invoke = function (readonly ref int64 value, next) -> int32 => value + next   ;return 0;}'
        start=source.index('function')
        end=source.index('next   ;')+4
        value_start=source.index('value,')
        next_start=source.index('next)')
        type_start=source.index('int64')
        return_start=source.index('int32 =>')
        self.check_ast(source, f'''
    KrtAstNode lambda_node=unit.left.left.left;
    if(lambda_node.start!={start} || lambda_node.length!={end-start} || lambda_node.parameter_count!=2){{return 2;}}
    if(lambda_node.parameter_starts[0]!={value_start} || lambda_node.parameter_lengths[0]!=5 || lambda_node.parameter_starts[1]!={next_start} || lambda_node.parameter_lengths[1]!=4){{return 3;}}
    if(lambda_node.parameter_type_starts[0]!={type_start} || lambda_node.parameter_type_lengths[0]!=5 || lambda_node.parameter_type_lengths[1]!=0){{return 4;}}
    if(lambda_node.type_start!={return_start} || lambda_node.type_length!=5 || lambda_node.syntax_type.type_start!={return_start}){{return 5;}}
    if(!lambda_node.parameter_refs[0] || lambda_node.syntax_parameter_types.syntax_modifiers!=16 || lambda_node.left.length!=12){{return 6;}}
''')

    def test_lambda_parameter_boundary_and_overflow_recovery(self):
        parameters=','.join(f'int32 p{index}' for index in range(128))
        self.check_ast(f'int32 main(){{var callback=function({parameters})=>p127;return 42;}}', '''
    KrtAstNode lambda_node=unit.left.left.left;
    if(lambda_node.parameter_count!=128 || lambda_node.parameter_lengths[127]!=4 || lambda_node.parameter_type_lengths[127]!=5){return 2;}
    KrtAstNode type=lambda_node.syntax_parameter_types;int32 count=0;while(type!=null){count=count+1;type=type.next;}
    if(count!=128 || lambda_node.left.kind!=KrtAstKind.Identifier || lambda_node.left.length!=4 || unit.left.left.next.left.value!=42){return 3;}
''')
        overflow=parameters+',int32 p128'
        self.check_ast(f'int32 main(){{var broken=function({overflow})=>p0;return 42;}}int32 valid(){{return 7;}}', '''
    KrtAstNode invalid=unit.left.left.left;
    if(invalid.kind!=KrtAstKind.Invalid || invalid.left.left.parameter_count!=128 || invalid.left.left.left.kind!=KrtAstKind.Identifier){return 2;}
    KrtAstNode type=invalid.left.left.syntax_parameter_types;int32 count=0;while(type!=null){count=count+1;type=type.next;}
    if(count!=128 || unit.left.left.left.next.left.value!=42 || unit.left.next.left.left.left.value!=7){return 3;}
    if(parser.diagnostics.expected!=KrtTokenKind.RightParen || parser.diagnostics.next!=null){return 4;}
''',compilation_unit=True,expected_errors=1)

    def test_malformed_lambda_recovers_following_statements_and_declarations(self):
        for source in (
            'int32 main(){var broken=function(int32 value,)=>value;return 42;}int32 valid(){return 7;}',
            'int32 main(){var broken=function(int32 value) value;return 42;}int32 valid(){return 7;}',
            'int32 main(){var broken=function(int32 value)=>;return 42;}int32 valid(){return 7;}',
            'int32 main(){var broken=function(int32 value)=>{int32 bad=;return value;};return 42;}int32 valid(){return 7;}',
            'int32 main(){var broken=function(ref ref int32 value)=>value;return 42;}int32 valid(){return 7;}',
            'int32 main(){var broken=function(readonly readonly value)=>value;return 42;}int32 valid(){return 7;}',
            'int32 main(){var broken=function(int32 value)->=>value;return 42;}int32 valid(){return 7;}',
        ):
            with self.subTest(source=source):
                self.check_ast(source, '''
    if(unit.left.left.left.kind!=KrtAstKind.Invalid || unit.left.left.left.next.kind!=KrtAstKind.Return || unit.left.left.left.next.left.value!=42){return 2;}
    if(unit.left.next==null || unit.left.next.left.left.left.value!=7 || unit.left.next.next!=null){return 3;}
    if(parser.diagnostics==null || parser.diagnostics.next!=null){return 4;}
''',compilation_unit=True,expected_errors=1)

    def test_type_meta_expressions(self):
        self.check_ast('int32 main() { return sizeof(int64) + default(int32); }', '''
    KrtAstNode expression = unit.left.left.left;
    if (expression.kind != KrtAstKind.Binary || expression.left.kind != KrtAstKind.Sizeof ||
        expression.right.kind != KrtAstKind.DefaultValue) { return 2; }
    if (expression.left.type_length != 5 || expression.right.type_length != 5) { return 3; }
''')

    def test_double_pointer_tokens_keep_type_spans_and_unary_nodes(self):
        self.check_ast('int32** main(int32*** pointer) { return **pointer; }', '''
    if(unit.type_length!=7 || unit.parameter_type_lengths[0]!=8){return 2;}
    KrtAstNode value=unit.left.left.left;
    if(value.kind!=KrtAstKind.Unary || value.op!=42 || value.left.kind!=KrtAstKind.Unary || value.left.op!=42){return 3;}
    if(value.left.left.kind!=KrtAstKind.Identifier || value.left.left.length!=7){return 4;}
''')

    def test_prefix_and_postfix_increment_keep_order_and_full_spans(self):
        self.check_ast('int32 main(){int32 n=7; ++n; n--; return (int64)--n;}', '''
    KrtAstNode prefix=unit.left.left.next.left;
    if(prefix.kind!=KrtAstKind.Unary || prefix.op!=201 || prefix.value!=1 || prefix.length!=3){return 2;}
    KrtAstNode postfix=unit.left.left.next.next.left;
    if(postfix.kind!=KrtAstKind.Unary || postfix.op!=202 || postfix.value!=0 || postfix.length!=3){return 3;}
    KrtAstNode converted=unit.left.left.next.next.next.left;
    if(converted.kind!=KrtAstKind.Cast || converted.left.op!=202 || converted.left.value!=1 || converted.left.length!=3){return 4;}
''')

    def test_switch_recovery_keeps_case_bodies_and_later_labels(self):
        self.check_ast('int32 main(){switch(1){case 1:int32 broken=;return 7;case 2:!;return 9;default:return 42;}return 3;}', '''
    if(unit.left.left.kind!=KrtAstKind.Invalid){return 7;}
    KrtAstNode switch_node=unit.left.left.left;
    if(switch_node.kind!=KrtAstKind.Switch || switch_node.child_count!=4){return 2;}
    KrtAstNode first=switch_node.right;
    if(first.right.left.kind!=KrtAstKind.Invalid || first.right.left.next.kind!=KrtAstKind.Return || first.right.left.next.left.value!=7){return 3;}
    KrtAstNode second=first.next;
    if(second.left.value!=2 || second.right.left.kind!=KrtAstKind.Invalid || second.right.left.next.left.value!=9){return 4;}
    if(second.next.kind!=KrtAstKind.Default || second.next.right.left.left.value!=42 || unit.left.left.next.left.value!=3){return 5;}
    if(first.right.native_using_scope==second.right.native_using_scope || first.right.native_using_scope.parent!=second.right.native_using_scope.parent){return 6;}
''',expected_errors=2)

    def test_compound_assignments_and_for_step_keep_statement_nodes(self):
        self.check_ast('int32 main(){int32 n=1;n<<=2;n&=3;for(int32 i=0;i<7;i+=2){n|=i;}return n;}', '''
    KrtAstNode assignment=unit.left.left.next;
    if(assignment.kind!=KrtAstKind.CompoundAssign || assignment.op!=120){return 2;}
    assignment=assignment.next;
    if(assignment.kind!=KrtAstKind.CompoundAssign || assignment.op!=117){return 3;}
    KrtAstNode loop=assignment.next;
    if(loop.kind!=KrtAstKind.For || loop.alternate.kind!=KrtAstKind.CompoundAssign || loop.alternate.op!=108){return 4;}
    if(loop.body.left.kind!=KrtAstKind.CompoundAssign || loop.body.left.op!=118){return 5;}
''')
        self.check_ast('int32 main() { return default(int32[]); }', '''
    if (unit.left.left.left.type_length != 7) { return 2; }
''')

    def test_null_operator_tokens_and_associativity(self):
        self.check_ast('int32 main() { return a?.field ?? b ?? c; }', '''
    KrtAstNode expression = unit.left.left.left;
    if (expression == null || expression.kind != KrtAstKind.Binary || expression.op != 113 || expression.child_count != 2 || expression.start != 22 || expression.length != 18) { return 2; }
    KrtAstNode guard = expression.left; KrtAstNode fallback = expression.right;
    if (guard == null || guard.kind != KrtAstKind.NullConditional || guard.child_count != 2 || guard.op != 0 || guard.start != 22 || guard.length != 8) { return 3; }
    KrtAstNode receiver = guard.left; KrtAstNode member = guard.right;
    if (receiver == null || receiver.kind != KrtAstKind.Identifier || receiver.start != 22 || receiver.length != 1) { return 4; }
    if (member == null || member.kind != KrtAstKind.Member || member.child_count != 1 || member.op != 0 || member.start != 22 || member.length != 8 || member.name_start != 25 || member.name_length != 5 || member.right != null) { return 5; }
    KrtAstNode saved = member.left;
    if (saved == null || saved.kind != KrtAstKind.GuardReceiver || saved.start != receiver.start || saved.length != receiver.length || saved.op != 0 || saved.name_start != 0 || saved.name_length != 0) { return 6; }
    if (fallback == null || fallback.kind != KrtAstKind.Binary || fallback.op != 113 || fallback.child_count != 2 || fallback.start != 34 || fallback.length != 6) { return 7; }
    KrtAstNode b = fallback.left; KrtAstNode c = fallback.right;
    if (b == null || c == null || b.kind != KrtAstKind.Identifier || c.kind != KrtAstKind.Identifier || b.start != 34 || b.length != 1 || c.start != 39 || c.length != 1) { return 8; }
    KrtAstNode[] leaves = [receiver, saved, b, c];
    for (int32 i = 0; i < 4; i++) {
        if (leaves[i].child_count != 0 || leaves[i].left != null || leaves[i].right != null) { return 9; }
    }
    KrtAstNode[] nodes = [expression, guard, receiver, member, saved, fallback, b, c];
    for (int32 i = 0; i < 8; i++) {
        if (nodes[i].next != null || nodes[i].alternate != null || nodes[i].body != null || nodes[i].declarations != null || nodes[i].grouped) { return 10; }
        for (int32 j = i + 1; j < 8; j++) { if (nodes[i] == nodes[j]) { return 11; } }
    }
    unsafe(using krt.mem;) {
        byte* text = (byte*)(int64)input;
        if (text[receiver.start] != 97 || text[b.start] != 98 || text[c.start] != 99 || text[member.name_start] != 102 || text[member.name_start + 1] != 105 || text[member.name_start + 2] != 101 || text[member.name_start + 3] != 108 || text[member.name_start + 4] != 100) { return 12; }
    }
    delete leaves; delete nodes;
''')

    def test_typed_lookahead_retains_type_children_and_grouped_postfix(self):
        self.check_ast('int32 main(){box /*name*/ . item<pair<int32>, int64>[]** value; KrtStart(); int32 total=(value)[0].field; return convert<pair<int32>>/*call*/(value) + (int32)3;}', '''
    KrtAstNode declaration=unit.left.left;
    if(declaration.kind!=KrtAstKind.Variable || declaration.syntax_type==null){return 2;}
    KrtAstNode type=declaration.syntax_type;
    if(type.syntax_pointer_depth!=2 || type.syntax_array_rank!=1 || type.right==null){return 3;}
    if(type.right.left==null || type.right.left.child_count!=2 || type.right.left.left.left==null){return 4;}
    if(declaration.next.kind!=KrtAstKind.Expression || declaration.next.left.kind!=KrtAstKind.Call){return 5;}
    KrtAstNode grouped=declaration.next.next.left;
    if(grouped.kind!=KrtAstKind.Member || grouped.left.kind!=KrtAstKind.Index || !grouped.left.left.grouped){return 6;}
    KrtAstNode result=declaration.next.next.next.left;
    if(result.kind!=KrtAstKind.Binary || result.left.kind!=KrtAstKind.Call || result.right.kind!=KrtAstKind.Cast){return 7;}
    if(result.left.left.native_type_arguments==null || result.left.left.native_type_arguments.left.left==null){return 8;}
    if(result.right.syntax_type==null){return 9;}
''')

    def test_inheritance_and_constraints_keep_complete_type_children(self):
        self.check_ast('class Derived<T>:Box<T?*?>, Ns.Face<T[]> where T:Ns.Constraint<int32?**?[]>,class {} int32 main(){return 0;}', '''
    KrtAstNode declaration=unit.declarations;
    if(unit.syntax_declarations.left!=declaration){return 2;}
    KrtAstNode parent=declaration.left;
    if(parent.syntax_type==null || parent.child_count!=1 || parent.length!=parent.type_length || parent.type_length!=9){return 3;}
    KrtAstNode parent_type=parent.syntax_type;
    if(parent_type.type_start!=parent.type_start || parent_type.type_length!=parent.type_length || parent_type.left.child_count!=1){return 4;}
    KrtAstNode argument=parent_type.left.left;
    if(argument.syntax_pointer_depth!=1 || !argument.syntax_nullable || argument.syntax_pointee_nullable!=1){return 5;}
    KrtAstNode iface=parent.next.syntax_type;
    if(iface.right==null || iface.right.name_length!=4 || iface.right.left.left.syntax_array_rank!=1){return 6;}
    KrtAstNode constraint=declaration.native_constraints;
    if(constraint.op!=6 || constraint.syntax_type==null || constraint.child_count!=1 || constraint.next.op!=1){return 7;}
    KrtAstNode constraint_type=constraint.syntax_type;
    if(constraint_type.type_start!=constraint.type_start || constraint_type.type_length!=constraint.type_length || constraint.length!=constraint.type_length){return 8;}
    KrtAstNode inner=constraint_type.right.left.left;
    if(inner.syntax_pointer_depth!=2 || inner.syntax_array_rank!=1 || inner.syntax_nullable || inner.syntax_pointee_nullable!=2){return 9;}
    int32[] suffix_ops=[63,42,42,63,91];
    KrtAstNode suffix=inner.syntax_suffixes;int32 suffix_index=0;
    while(suffix_index<5){
        if(suffix==null || suffix.op!=suffix_ops[suffix_index]){return 12;}
        suffix=suffix.next;suffix_index++;
    }
    delete suffix_ops;
    if(suffix!=null){return 13;}
    if(constraint_type.right.name_length!=10 || declaration.left.next.next!=null){return 10;}
    if(parent_type.native_using_scope!=declaration.native_using_scope || inner.native_using_scope!=declaration.native_using_scope || constraint_type.native_scope!=declaration.native_scope){return 11;}
''',compilation_unit=True)
        self.check_ast('int32 main(){int32 n=4; return (n)+1 + ((n)==4 ? 2 : 3);}', '''
    if(unit.left.left.next.left.kind!=KrtAstKind.Binary){return 2;}
''')

    def test_using_frames_inherit_and_restore_namespace_and_file_scopes(self):
        self.check_ast('using global.lib; namespace outer { using local.lib; int32 first(){return 1;} namespace inner {using deep.lib; int32 second(){return 2;}} int32 third(){return 3;}} namespace peer {int32 fourth(){return 4;}} namespace; int32 fifth(){return 5;}', '''
    KrtAstNode first=unit.left; KrtAstNode second=first.next; KrtAstNode third=second.next; KrtAstNode fourth=third.next; KrtAstNode fifth=fourth.next;
    if(first.native_using_scope!=third.native_using_scope || second.native_using_scope.parent!=first.native_using_scope){return 2;}
    if(first.native_using_scope.imports==null || first.native_using_scope.parent.imports==null || second.native_using_scope.imports==null){return 3;}
    if(fourth.native_using_scope==first.native_using_scope || fourth.native_using_scope.imports!=null){return 4;}
    if(fourth.native_using_scope.parent!=first.native_using_scope.parent){return 5;}
    if(fifth.native_using_scope.parent!=null || fifth.native_using_scope.imports!=null){return 6;}
    if(first.native_scope.native_using_scope!=first.native_using_scope){return 7;}
    if(first.left.left.native_using_scope.parent!=first.native_using_scope){return 8;}
''', compilation_unit=True)

    def test_recovery_reports_distinct_errors_and_keeps_following_declarations(self):
        self.check_ast('int32 broken(){int32 first=; int32 second=; return 1;} int32 damaged=; int32 valid(){return 42;}', '''
    if(unit.left==null || unit.left.next==null || unit.left.next.left.left.left.value!=42){return 2;}
    if(unit.left.left.left.kind!=KrtAstKind.Invalid || unit.left.left.left.left.kind!=KrtAstKind.Variable){return 3;}
    if(unit.left.left.left.next.kind!=KrtAstKind.Invalid || unit.left.left.left.next.next.kind!=KrtAstKind.Return){return 4;}
    KrtParserDiagnostic diagnostic=parser.diagnostics; int64 previous=-1; int32 count=0;
    while(diagnostic!=null){if(diagnostic.position<=previous){return 5;}previous=diagnostic.position;count=count+1;diagnostic=diagnostic.next;}
    if(count!=3){return 6;}
''', compilation_unit=True, expected_errors=3)

    def test_recovery_keeps_members_after_bad_field_and_function_signature(self):
        self.check_ast('class item {int32 wrong=; int32 good; int32 bad(,){} int32 valid(){return 7;}} int32 main(){return 42;}', '''
    KrtAstNode type=unit.declarations;
    if(type==null || type.body==null || type.body.next==null || type.body.next.name_length!=4){return 2;}
    KrtAstNode method=type.declarations;
    if(method==null || method.next==null || method.next.name_length!=5 || method.next.left==null){return 3;}
    if(unit.left==null || unit.left.left.left.left.value!=42){return 4;}
''', compilation_unit=True, expected_errors=2)

    def test_modifier_lookahead_does_not_consume_global_variable_tokens(self):
        self.check_ast('static int32 number=7; readonly string label="yes"; public int32 main(){return number;}', '''
    if(unit.body==null || unit.body.next==null || unit.body.name_length!=6 || unit.body.left.value!=7){return 2;}
    if(!unit.body.native_static || !unit.body.next.native_readonly || unit.left.name_length!=4){return 3;}
''', compilation_unit=True)

    def test_reachable_nodes_keep_permissions_print_types_and_single_branches(self):
        self.check_ast('int32 main(){unsafe(using krt.mem, krt.asm;){point {print(1,"ok");}} if(value is box bound) return 1; else return 2; while(false) ; for(;false;) return 3; return 42;}', '''
    KrtAstNode guarded=unit.left.left;
    if(guarded.kind!=KrtAstKind.Unsafe || guarded.syntax_permissions==null || guarded.syntax_permissions.next==null){return 2;}
    KrtAstNode point_node=guarded.left.left;
    if(point_node.kind!=KrtAstKind.Point || point_node.left.left.kind!=KrtAstKind.Print || point_node.left.left.child_count!=2){return 3;}
    KrtAstNode conditional=guarded.next;
    if(conditional.kind!=KrtAstKind.If || conditional.left.kind!=KrtAstKind.TypeTest || conditional.left.syntax_type==null || conditional.left.name_length!=5){return 4;}
    if(conditional.right.kind!=KrtAstKind.Block || conditional.right.left.kind!=KrtAstKind.Return || conditional.alternate.left.left.value!=2){return 5;}
    if(conditional.next.kind!=KrtAstKind.While || conditional.next.right.child_count!=0){return 6;}
    if(conditional.next.next.kind!=KrtAstKind.For || conditional.next.next.body.left.left.value!=3){return 7;}
''')

    def test_source_order_tree_and_block_using_frames_are_preserved(self):
        self.check_ast('using root.lib; namespace outer{using local.lib; class item{} int32 main(){using alias=body.lib; {using deep.lib; ;} return 42;}}', '''
    KrtAstNode syntax=unit.syntax_declarations;
    if(syntax.kind!=KrtAstKind.UsingDirective || syntax.next.kind!=KrtAstKind.NamespaceDeclaration){return 2;}
    KrtAstNode nested=syntax.next.syntax_declarations;
    if(nested.kind!=KrtAstKind.UsingDirective || nested.next.kind!=KrtAstKind.TypeDeclaration || nested.next.next.kind!=KrtAstKind.Function){return 3;}
    KrtAstNode block=unit.left.left;
    if(block.left.kind!=KrtAstKind.UsingDirective || block.native_using_scope.imports==null){return 4;}
    KrtAstNode inner=block.left.next;
    if(inner.native_using_scope.parent!=block.native_using_scope || inner.native_using_scope.imports==null){return 5;}
    if(inner.next.native_using_scope!=block.native_using_scope){return 6;}
    if(block.native_using_scope.imports.type_length!=5){return 7;}
''', compilation_unit=True)

    def test_keyword_constructors_and_modified_constants_keep_real_members(self):
        self.check_ast('public const int32 baseValue=7; class item {public function item(int32 x){} public function read(){return 42;}} int32 main(){return 42;}', '''
    if(unit.body==null || !unit.body.native_readonly || unit.body.left.value!=7){return 2;}
    KrtAstNode method=unit.declarations.declarations;
    if(method.op!=220 || method.parameter_count!=2 || method.next.name_length!=4 || method.next.left.left.left.value!=42){return 3;}
''', compilation_unit=True)

    def test_recovery_error_limit_preserves_later_valid_ast(self):
        self.check_ast('int32 broken(){'+('int32 missing=;'*100)+'return 7;} int32 valid(){return 42;}', '''
    if(unit.left.next==null || unit.left.next.left.left.left.value!=42){return 2;}
    int32 count=0;KrtParserDiagnostic diagnostic=parser.diagnostics;
    while(diagnostic!=null){count=count+1;diagnostic=diagnostic.next;}
    if(count!=64){return 3;}
''', compilation_unit=True, expected_errors=64)

    def test_cast_probe_accepts_this_and_grouped_pointer_index(self):
        self.check_ast('int32 main(){return (int64)((byte*)this.source)[index];}', '''
    KrtAstNode cast=unit.left.left.left;
    if(cast.kind!=KrtAstKind.Cast || cast.left.kind!=KrtAstKind.Index || cast.left.left.kind!=KrtAstKind.Cast){return 2;}
    if(cast.left.left.left.kind!=KrtAstKind.Member || cast.left.left.left.left.kind!=KrtAstKind.Identifier){return 3;}
''')

    def test_inferred_annotations_legacy_typed_vars_and_named_array_sizes(self):
        self.check_ast('int32 main(){var first:int32=7;var int64 second=8;let third:int16=9;int32 values[3];int32?* pointer;auto inferred=17;return 42;}', '''
    KrtAstNode first=unit.left.left;
    if(first.type_length!=5 || first.syntax_type==null || first.left.value!=7){return 2;}
    if(first.next.type_length!=5 || first.next.left.value!=8){return 3;}
    if(!first.next.next.native_readonly || first.next.next.type_length!=5 || first.next.next.syntax_modifiers!=4096){return 4;}
    KrtAstNode array=first.next.next.next;
    if(array.syntax_array_size==null || array.syntax_array_size.value!=3 || array.syntax_type.syntax_array_rank!=1){return 5;}
    if(array.next.next.kind!=KrtAstKind.Variable || array.next.next.type_length!=4){return 7;}
    if(array.next.syntax_type.syntax_pointer_depth!=1 || array.next.syntax_type.syntax_pointee_nullable!=1 || array.next.syntax_type.syntax_nullable){return 6;}
''')

    def test_modifiers_retain_visibility_and_dispatch_syntax(self):
        self.check_ast('public const int32 globalValue=7;abstract class item{private int32 secret;protected virtual int32 read(){return 7;}}public static int32 main(){return 42;}', '''
    if(unit.body.syntax_modifiers!=33 || unit.declarations.syntax_modifiers!=256){return 2;}
    if(unit.declarations.body.syntax_modifiers!=2 || unit.declarations.declarations.syntax_modifiers!=68){return 3;}
    if(unit.left.syntax_modifiers!=9){return 4;}
''', compilation_unit=True)

    def test_internal_and_combined_access_keep_flags_and_declaration_owners(self):
        self.check_ast('internal class Holder{protected internal int32 either;private protected int32 both;internal Holder(){}internal static int32 Make(){return 0;}}internal struct Record{int32 field;}internal interface Contract{int32 Run();}internal enum Codes{first,private second}internal const int32 limit=3;internal readonly int32 store=4;internal function helper(){return 1;}int32 main(){return 0;}', '''
    KrtAstNode holder=unit.declarations;
    if(holder.syntax_modifiers!=8192 || holder.body.syntax_modifiers!=8196 || holder.body.next.syntax_modifiers!=6){return 2;}
    if(holder.body.native_owner!=holder || holder.body.next.native_owner!=holder){return 3;}
    if(holder.declarations.syntax_modifiers!=8192 || holder.declarations.op!=220 || holder.declarations.native_owner!=holder){return 4;}
    if(holder.declarations.next.syntax_modifiers!=8200 || holder.declarations.next.native_owner!=holder){return 5;}
    KrtAstNode record=holder.next;KrtAstNode contract=record.next;KrtAstNode codes=contract.next;
    if(record.syntax_modifiers!=8192 || record.body.syntax_modifiers!=0 || record.body.native_owner!=record){return 6;}
    if(contract.syntax_modifiers!=8192 || contract.declarations.syntax_modifiers!=0 || contract.declarations.native_owner!=contract){return 7;}
    if(codes.syntax_modifiers!=8192 || codes.body.syntax_modifiers!=0 || codes.body.next.syntax_modifiers!=2){return 8;}
    if(codes.body.native_owner!=codes || codes.body.next.native_owner!=codes){return 9;}
    if(unit.body.syntax_modifiers!=8224 || unit.body.next.syntax_modifiers!=8208){return 10;}
    if(unit.left.syntax_modifiers!=8192 || unit.left.next.syntax_modifiers!=0 || unit.left.native_owner!=null){return 11;}
''', compilation_unit=True)

    def test_repeated_and_conflicting_access_preserve_declarations_for_validation(self):
        self.check_ast('public public class Repeated{internal internal int32 field;protected internal protected int32 Run(){return 1;}private protected private Repeated(){}}public private class Conflict{}private internal const int32 conflicting=2;internal internal int32 repeated=3;private private int32 helper(){return 4;}int32 main(){return 0;}', '''
    KrtAstNode repeated=unit.declarations;
    if(repeated.syntax_modifiers!=16385 || repeated.body.syntax_modifiers!=24576 || repeated.body.native_owner!=repeated){return 2;}
    if(repeated.declarations.syntax_modifiers!=24580 || repeated.declarations.next.syntax_modifiers!=16390){return 3;}
    if(repeated.next.syntax_modifiers!=3 || repeated.next.kind!=KrtAstKind.TypeDeclaration){return 4;}
    if(unit.body.syntax_modifiers!=8226 || unit.body.next.syntax_modifiers!=24576){return 5;}
    if(unit.left.syntax_modifiers!=16386 || unit.left.next.name_length!=4 || unit.left.next.syntax_modifiers!=0){return 6;}
    if(unit.left.left.left.left.value!=4 || unit.left.next.left.left.left.value!=0){return 7;}
''', compilation_unit=True)

    def test_recovery_recognizes_internal_declaration_boundaries(self):
        self.check_ast('int32 broken(){int32 value=;return 0;}internal class Kept{int32 wrong=;internal int32 good;}internal const int32 count=3;internal int32 valid(){return 42;}', '''
    if(unit.left==null || unit.left.next==null || unit.left.next.syntax_modifiers!=8192 || unit.left.next.left.left.left.value!=42){return 2;}
    KrtAstNode type=unit.declarations;
    if(type==null || type.syntax_modifiers!=8192 || type.body.next==null || type.body.next.syntax_modifiers!=8192){return 3;}
    if(type.body.next.native_owner!=type || unit.body.syntax_modifiers!=8224 || unit.body.left.value!=3){return 4;}
    if(parser.diagnostics==null || parser.diagnostics.next==null || parser.diagnostics.next.next!=null){return 5;}
''', compilation_unit=True, expected_errors=2)

    def test_character_literals_preserve_character_type_and_seed_escapes(self):
        self.check_ast(r"int32 main(){char alert='\a';char back='\b';char form='\f';char unknown='\q';return 42;}", '''
    KrtAstNode declaration=unit.left.left;
    if(!declaration.left.native_character || declaration.left.value!=7){return 2;}
    if(!declaration.next.left.native_character || declaration.next.left.value!=8){return 3;}
    if(!declaration.next.next.left.native_character || declaration.next.next.left.value!=12){return 4;}
    if(declaration.next.next.next.left.value!=113){return 5;}
''')

    def test_nested_declarations_are_retained_and_missing_brace_recovers(self):
        self.check_ast('int32 main(){class nested{int32 field;}int32 inner(){return 7;}function more(){return 8;}return 42;}', '''
    KrtAstNode nested=unit.left.left;
    if(nested.kind!=KrtAstKind.TypeDeclaration || nested.body==null){return 2;}
    if(nested.next.kind!=KrtAstKind.Function || nested.next.left.left.left.value!=7){return 3;}
    if(nested.next.next.kind!=KrtAstKind.Function || nested.next.next.left.left.left.value!=8){return 4;}
''')
        self.check_ast('int32 broken(){int32 wrong=; int32 valid(){return 42;}', '''
    if(unit.left==null || unit.left.next==null || unit.left.next.left.left.left.value!=42){return 2;}
    if(parser.diagnostics.next.expected!=KrtTokenKind.RightBrace){return 3;}
''',compilation_unit=True,expected_errors=2)
