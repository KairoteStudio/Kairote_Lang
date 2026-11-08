"""Direct and indirect calls share nullable argument and ref rules."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


class IndirectArgumentTests(NativeCompilerFixture):
    def execute_targets(self, source, name):
        path = self.work / (name + '.krt')
        path.write_text(source)
        for target in ('native', 'vm'):
            for level in range(4):
                with self.subTest(source=name, target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def reject_targets(self, source, name):
        path = self.work / (name + '.krt')
        path.write_text(source)
        for target in ('native', 'vm'):
            for level in range(4):
                for previous in (None, b'previous artifact\x00must survive'):
                    with self.subTest(source=name, target=target, optimization=level,
                                      previous_artifact=previous is not None):
                        output = self.work / f'{name}-{target}-o{level}-{previous is not None}'
                        if previous is not None:
                            output.write_bytes(previous)
                        flags = ['target', 'vm'] if target == 'vm' else []
                        result = subprocess.run([str(COMPILER), '--linker', str(LINKER), str(path),
                                                 f'-O{level}', *flags, '-o', str(output)],
                                                cwd=self.work, env=self.env, capture_output=True,
                                                text=True, timeout=60)
                        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                        self.assertRegex(result.stderr, r':\d+:\d+: E_(LOWER|NOT_NULLABLE|WRITABLE_REF):')
                        self.assertNotIn('E_PARSE', result.stderr)
                        if previous is None:
                            self.assertFalse(output.exists(), 'invalid argument created an artifact')
                        else:
                            self.assertEqual(output.read_bytes(), previous)

    def test_null_matches_every_nullable_parameter_through_direct_and_indirect_calls(self):
        self.execute_targets('''
class Reference{int32 value;}
interface Contract{}
struct Packet{int64 value;}
int32 Accept(void*? raw,void**? deep,int32*? typed,Packet*? packet,Reference object,
             Contract contract,string text,int64[] values){
    if(raw!=null || deep!=null || typed!=null || packet!=null || object!=null ||
       contract!=null || text!=null || values!=null){return 1;}
    return 0;
}
int32 main(){
    fn(void*?,void**?,int32*?,Packet*?,Reference,Contract,string,int64[])->int32 call=&Accept;
    if(Accept(null,null,null,null,null,null,null,null)!=0){return 1;}
    if(call(null,null,null,null,null,null,null,null)!=0){return 2;}
    return 0;
}
''', 'nullable-parameters')

    def test_null_keeps_reference_side_effects_and_callback_container_calls(self):
        self.execute_targets('''
static int32 addresses=0;
int32 Next(){addresses++;return 1;}
void*? Change(ref int64 value,void**? argument){if(argument!=null){return (void*?)argument;}value+=7;return null;}
int32 Ref(ref void**? argument){return argument==null?0:1;}
struct Holder{fn(ref int64,void**?)->void*? callback;}
int32 main(){
    int64[] values=[3,11];fn(ref int64,void**?)->void*? callback=&Change;
    if(callback(ref values[Next()],null)!=null || values[1]!=18 || addresses!=1){return 1;}
    Holder holder=default(Holder);holder.callback=&Change;
    if(holder.callback(ref values[Next()],null)!=null || values[1]!=25 || addresses!=2){return 2;}
    fn(ref int64,void**?)->void*? callbacks[1];callbacks[0]=&Change;
    if(callbacks[0](ref values[Next()],null)!=null || values[1]!=32 || addresses!=3){return 3;}
    void**? pointer=null;fn(ref void**?)->int32 reference=&Ref;
    if(reference(ref pointer)!=0 || Ref(ref pointer)!=0){return 4;}
    delete values;return 0;
}
''', 'nullable-ref-effects')
        self.execute_targets('''
void Clear(ref int32*? pointer){pointer=null;}
int32 Read(int32*? pointer){unsafe(using krt.mem;){
    if(pointer is int32* live){
        int32 value=*live;Clear(ref pointer);
        if(pointer!=null){return -1;}return value;
    }
    return 0;
}}
int32 main(){unsafe(using krt.mem;){
    int32 value=17;int32*? pointer=&value;
    if(Read(pointer)!=17 || pointer==null){return 1;}
    pointer=null;if(Read(pointer)!=0){return 2;}
    return 0;
}}
''', 'nullable-pointer-pattern')
        self.execute_targets('''
int32 Read(){return 17;}
int32 InvokeNullable<T>(T exemplar,bool missing){unsafe(using krt.mem;){
    T? callback=exemplar;if(missing){callback=null;}
    if(callback is T live){return live();}return 0;
}}
int32 PointerLayers<T>(T exemplar){unsafe(using krt.mem;){
    T? item=null;T?* inner=&item;T*? outer=&exemplar;
    if(inner[0]!=null){return 1;}inner[0]=exemplar;
    if(inner[0] is T live){if(live[0]!=17){return 2;}}else{return 3;}
    if(outer is T* live_outer){if(*live_outer!=exemplar){return 4;}}else{return 5;}
    outer=null;if(outer is T* missing){return 6;}
    inner[0]=null;if(item!=null){return 7;}return 0;
}}
int32 main(){unsafe(using krt.mem;){
    int32 value=17;
    if(InvokeNullable(&Read,false)!=17 || InvokeNullable(&Read,true)!=0){return 1;}
    return PointerLayers(&value);
}}
''', 'nullable-generic-callback-and-pointer-layers')
        self.execute_targets('''
void Clear(ref int32*? pointer){pointer=null;}
void Replace(ref int32* pointer,int32* replacement){pointer=replacement;}
int32 main(){unsafe(using krt.mem;){
    int32 value=17;var first=[null,&value];var second=[&value,null];
    if(first.Length!=2 || second.Length!=2 || first[0]!=null || second[1]!=null){return 1;}
    if(first[1] is int32* live){if(*live!=17){return 2;}}else{return 3;}
    if(second[0] is int32* live){if(*live!=17){return 4;}}else{return 5;}
    Clear(ref first[1]);Clear(ref second[0]);
    if(first[1]!=null || second[0]!=null){return 6;}
    int32*?[] elements=[null,&value];int32*[]? array=[&value];int32*?[]? both=[&value,null];
    Clear(ref elements[1]);Replace(ref array[0],&value);Clear(ref both[0]);
    if(elements[1]!=null || *array[0]!=17 || both[0]!=null || both[1]!=null){return 7;}
    int32*? item=&value;int32*?*[] inner=[&item];int32*?*?[]? layers=[null,&item];
    Clear(ref inner[0][0]);if(item!=null){return 8;}
    if(layers[1] is int32*?* live_layer){live_layer[0]=&value;}else{return 9;}
    if(item is int32* live_item){if(*live_item!=17){return 10;}}else{return 11;}
    delete first;delete second;delete elements;delete array;delete both;delete inner;delete layers;
    elements=null;array=null;both=null;layers=null;
    if(elements!=null || array!=null || both!=null || layers!=null){return 12;}
    return 0;
}}
''', 'nullable-inference-and-suffix-ledger-containers')
        self.execute_targets('''
int32 Read(){return 17;}
int32 Choose<T>(T exemplar,bool missing){unsafe(using krt.mem;){
    var callback=missing?null:exemplar;
    if(callback==null){return 0;}
    if(callback is T live){return live();}return -1;
}}
int32 ChooseReverse<T>(T exemplar,bool missing){unsafe(using krt.mem;){
    var callback=missing?exemplar:null;
    if(callback==null){return 0;}
    if(callback is T live){return live();}return -1;
}}
int32 main(){
    if(Choose(&Read,false)!=17 || Choose(&Read,true)!=0){return 1;}
    if(ChooseReverse(&Read,true)!=17 || ChooseReverse(&Read,false)!=0){return 2;}
    return 0;
}
''', 'nullable-generic-function-ternary-patterns')
        self.execute_targets('''
int32 main(){unsafe(using krt.mem;){
    byte[] bytes=[17,25,42];byte* raw=(byte*)bytes;
    if(raw[0]!=17 || raw[1]!=25 || raw[2]!=42){return 1;}
    raw[1]=31;if(bytes[1]!=31){return 2;}
    string text="Kairote";byte* characters=(byte*)text;
    if(characters[0]!=75 || characters[1]!=97 || characters[6]!=101){return 3;}
    int32 value=59;int32*?[] elements=[null,&value];int32*?* pointers=(int32*?*)elements;
    if(pointers[0]!=null){return 4;}
    if(pointers[1] is int32* live){if(*live!=59){return 5;}}else{return 6;}
    pointers[0]=&value;
    if(elements[0] is int32* first){if(*first!=59){return 7;}}else{return 8;}
    delete bytes;delete elements;return 0;
}}
''', 'nullable-pointer-and-byte-address-views')
        self.execute_targets('''
int32 main(){unsafe(using krt.mem;){
    int32 value=17;int32* item=&value;int32*[] pointers=[item];
    int32*?[] optional=(int32*?[])pointers;
    if(optional[0] is int32* live){if(*live!=17){return 1;}}else{return 2;}
    int32*[]? nullable_container=pointers;int32*[] ordinary=(int32*[])nullable_container;
    if(ordinary.Length!=1 || *ordinary[0]!=17){return 3;}
    nullable_container=null;ordinary=(int32*[])nullable_container;if(ordinary!=null){return 4;}
    int32**[] deep=[&item];int32*?*[] inner=(int32*?*[])deep;
    int32**?[] outer=(int32**?[])deep;int32*?*?[] both=(int32*?*?[])deep;
    if(inner[0][0] is int32* live_inner){if(*live_inner!=17){return 5;}}else{return 6;}
    if(outer[0] is int32** live_outer){if(live_outer[0][0]!=17){return 7;}}else{return 8;}
    if(both[0] is int32*?* live_both){
        if(live_both[0] is int32* live_value){if(*live_value!=17){return 9;}}else{return 10;}
    }else{return 11;}
    delete pointers;delete deep;return 0;
}}
''', 'nullable-array-cast-widening-and-container-null')

    def test_null_is_not_a_numeric_inline_or_ref_argument_and_preserves_outputs(self):
        declarations = 'enum Choice{First}struct Value{int32 number;}'
        for type_name in ('int64', 'bool', 'float32', 'uint128', 'Choice', 'Value'):
            prefix = declarations + f'int32 Accept({type_name} value){{return 0;}}'
            for indirect in (False, True):
                body = (f'fn({type_name})->int32 callback=&Accept;return callback(null);'
                        if indirect else 'return Accept(null);')
                self.reject_targets(prefix + 'int32 main(){' + body + '}',
                                    f'nonnullable-{type_name}-{indirect}')
        for argument in ('null', 'ref null'):
            for indirect in (False, True):
                prefix = 'int32 Accept(ref void** value){return 0;}'
                body = ('fn(ref void**)->int32 callback=&Accept;return callback(ARG);'
                        if indirect else 'return Accept(ARG);')
                self.reject_targets(prefix + 'int32 main(){' + body.replace('ARG', argument) + '}',
                                    f'null-ref-{argument.replace(" ", "-")}-{indirect}')
        self.reject_targets('''
struct Buffer{byte payload[3];}
class Relay<T>{static int32 Accept(T value){return 0;}}
int32 Wrong<T>(T exemplar){fn(T)->int32 callback=&Relay<T>.Accept;return callback(null);}
int32 main(){Buffer buffer=default(Buffer);return Wrong(buffer.payload);}
''', 'null-fixed-generic')
        enum_sources = {
            'initializer': 'int32 main(){Choice value=null;return 0;}',
            'return': 'Choice Read(){return null;}int32 main(){Choice value=Read();return 0;}',
            'array-element': 'int32 main(){Choice[] values=new Choice[2];values[1]=null;delete values;return 0;}',
        }
        for name, source in enum_sources.items():
            self.reject_targets('enum Choice{First}' + source, 'null-enum-' + name)
        pointer_declarations = 'struct Value{int32 number;}int32 Read(){return 17;}'
        pointer_initializers = {
            'void*': 'void* pointer=(void*)&value;',
            'void**': 'void* item=(void*)&value;void** pointer=&item;',
            'int32*': 'int32* pointer=&value;',
            'Value*': 'Value item=default(Value);Value* pointer=&item;',
            'fn()->int32': 'fn()->int32 pointer=&Read;',
        }
        for type_name in ('void*', 'void**', 'int32*', 'Value*', 'fn()->int32'):
            prefix = pointer_declarations + f'int32 Accept({type_name} value){{return 0;}}'
            for indirect in (False, True):
                body = (f'fn({type_name})->int32 callback=&Accept;return callback(null);'
                        if indirect else 'return Accept(null);')
                self.reject_targets(prefix + 'int32 main(){unsafe(using krt.mem;){' + body + '}}',
                                    f'null-plain-pointer-call-{type_name}-{indirect}')
            statements = {
                'initializer': f'{type_name} pointer=null;',
                'assignment': pointer_initializers[type_name] + 'pointer=null;',
                'default': f'{type_name} pointer=default({type_name});',
                'uninitialized': f'{type_name} pointer;',
            }
            for name, statement in statements.items():
                self.reject_targets(pointer_declarations + 'int32 main(){unsafe(using krt.mem;){'
                                    + 'int32 value=17;' + statement + 'return 0;}}',
                                    f'null-plain-pointer-{name}-{type_name}')
            self.reject_targets(pointer_declarations + f'{type_name} Missing(){{return null;}}'
                                + 'int32 main(){return 0;}', f'null-plain-pointer-return-{type_name}')
        nullable_setup = 'int32 value=17;int32*? pointer=&value;'
        for indirect in (False, True):
            prefix = 'int32 Accept(int32* pointer){return 0;}'
            body = ('fn(int32*)->int32 callback=&Accept;return callback(pointer);'
                    if indirect else 'return Accept(pointer);')
            self.reject_targets(prefix + 'int32 main(){unsafe(using krt.mem;){'
                                + nullable_setup + body + '}}', f'nullable-to-plain-call-{indirect}')
        nullable_operations = {
            'initializer': 'int32* plain=pointer;',
            'assignment': 'int32* plain=&value;plain=pointer;',
            'cast': 'int32* plain=(int32*)pointer;',
            'integer-cast': 'int64 address=(int64)pointer;',
            'dereference': 'int32 item=*pointer;',
            'index': 'int32 item=pointer[0];',
            'add': 'pointer+1;',
            'reverse-add': '1+pointer;',
            'subtract': 'pointer-1;',
            'difference': 'pointer-&value;',
            'post-increment': 'pointer++;',
            'pre-increment': '++pointer;',
            'post-decrement': 'pointer--;',
            'pre-decrement': '--pointer;',
            'add-assign': 'pointer+=1;',
            'subtract-assign': 'pointer-=1;',
            'pattern-keeps-original-nullable': 'if(pointer is int32* live){int32 item=*pointer;}',
            'nullable-pattern-target': 'if(pointer is int32*? live){return 1;}',
        }
        for name, statement in nullable_operations.items():
            self.reject_targets('int32 main(){unsafe(using krt.mem;){' + nullable_setup
                                + statement + 'return 0;}}', 'nullable-pointer-' + name)
        self.reject_targets('int32* Plain(int32*? pointer){return pointer;}int32 main(){return 0;}',
                            'nullable-to-plain-return')
        reference_shapes = {
            'outer-to-plain': ('int32*', 'int32*? pointer=&value;'),
            'outer-to-nullable': ('int32*?', 'int32* pointer=&value;'),
            'inner-to-plain': ('int32**', 'int32*? item=&value;int32*?* pointer=&item;'),
            'inner-to-nullable': ('int32*?*', 'int32* item=&value;int32** pointer=&item;'),
        }
        for name, (parameter, declaration) in reference_shapes.items():
            prefix = f'int32 Accept(ref {parameter} pointer){{return 0;}}'
            for indirect in (False, True):
                body = (f'fn(ref {parameter})->int32 callback=&Accept;return callback(ref pointer);'
                        if indirect else 'return Accept(ref pointer);')
                self.reject_targets(prefix + 'int32 main(){unsafe(using krt.mem;){int32 value=17;'
                                    + declaration + body + '}}', f'nullable-ref-{name}-{indirect}')
        inner_operations = {
            'initializer': 'int32** plain=pointer;',
            'cast': 'int32** plain=(int32**)pointer;',
            'pattern': 'if(pointer is int32** plain){return 1;}',
        }
        for name, statement in inner_operations.items():
            self.reject_targets('int32 main(){unsafe(using krt.mem;){int32 value=17;int32*? item=&value;'
                                + 'int32*?* pointer=&item;' + statement + 'return 0;}}',
                                'nullable-pointee-mask-' + name)
        for name, statement in {
            'call': 'return callback();',
            'cast': 'T plain=(T)callback;return 0;',
        }.items():
            self.reject_targets('int32 Read(){return 17;}int32 Wrong<T>(T exemplar){unsafe(using krt.mem;){'
                                + 'T? callback=exemplar;' + statement + '}}'
                                + 'int32 main(){return Wrong(&Read);}', 'nullable-generic-function-' + name)
        for shape, expression in {'null-first': 'false?null:exemplar',
                                  'null-last': 'true?exemplar:null'}.items():
            for name, statement in {'call': 'return callback();',
                                    'cast': 'T plain=(T)callback;return 0;'}.items():
                self.reject_targets('int32 Read(){return 17;}int32 Wrong<T>(T exemplar){unsafe(using krt.mem;){'
                                    + 'var callback=' + expression + ';' + statement + '}}'
                                    + 'int32 main(){return Wrong(&Read);}',
                                    'nullable-generic-function-ternary-' + shape + '-' + name)
        for name, statement in {
            'nullable-elements-to-plain': 'int32*?[] values=[null,&value];int32*[]? plain=values;',
            'nullable-element-to-plain': 'int32*?[]? values=[&value,null];int32* plain=values[0];',
            'null-in-nonnullable-elements': 'int32*[]? values=[&value,null];',
        }.items():
            self.reject_targets('int32 main(){unsafe(using krt.mem;){int32 value=17;'
                                + statement + 'return 0;}}', 'nullable-array-ledger-' + name)
        for name, statement in {
            'pointer-to-float': 'int32* pointer=&value;float64 number=(float64)pointer;',
            'float-to-pointer': 'float64 number=1.5;int32* pointer=(int32*)number;',
            'array-removes-pointee-mask': 'int32*?[] elements=[null,&value];int32** pointers=(int32**)elements;',
            'array-removes-outer-nullable': 'byte[]? bytes=[17,25];byte* pointer=(byte*)bytes;',
            'array-cast-removes-element-nullable': 'int32*?[] elements=[null,&value];int32*[] pointers=(int32*[])elements;',
            'array-cast-removes-inner-mask': 'int32*? item=&value;int32*?*[] elements=[&item];int32**[] pointers=(int32**[])elements;',
            'array-cast-removes-deep-element-nullable': 'int32* item=&value;int32**?[] elements=[&item,null];int32**[] pointers=(int32**[])elements;',
        }.items():
            self.reject_targets('int32 main(){unsafe(using krt.mem;){int32 value=17;'
                                + statement + 'return 0;}}', 'invalid-address-view-' + name)
        self.reject_targets('''
int32 Read(){return 17;}
int32 Wrong<T>(T exemplar){unsafe(using krt.mem;){
    T?[] optional=[exemplar,null];T[] plain=(T[])optional;return 0;
}}
int32 main(){return Wrong(&Read);}
''', 'array-cast-removes-generic-function-nullable')
        self.reject_targets('''
int32 Optional(int32*? pointer){return 0;}
int32 Plain(int32* pointer){return 0;}
int32 Wrong<T,U>(T exemplar,U expected){unsafe(using krt.mem;){
    T[] original=[exemplar];U[] converted=(U[])original;return 0;
}}
int32 main(){return Wrong(&Optional,&Plain);}
''', 'array-cast-changes-generic-function-signature')
