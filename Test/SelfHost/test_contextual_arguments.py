"""Array literals acquire parameter types without weakening stored array identity."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


class ContextualArgumentTests(NativeCompilerFixture):
    def execute_targets(self, source, name):
        path = self.work / (name + '.krt')
        path.write_text(source)
        for target in ('native', 'vm'):
            for level in range(4):
                with self.subTest(target=target, level=level):
                    output = self.work / f'{name}-{target}-{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_numeric_literals_in_direct_indirect_and_constructor_parameters(self):
        self.execute_targets('''
static int32 trace=0;
int32 Next(int32 value){trace=trace*10+value;return value;}
int32 Read(byte[] bytes,float32[] fractions,uint66[] wide){
    if(bytes.Length!=2 || bytes[0]!=255 || bytes[1]!=3){return 1;}
    if(fractions[0]!=1.25 || fractions[1]!=2.5 || (uint128)wide[0]!=73786976294838206463){return 2;}
    delete bytes;delete fractions;delete wide;return 0;
}
class Packet{
    public int32 total;
    Packet(byte[] values){foreach(var value in values){total+=value;}delete values;}
    int32 Sum(int16[] values){int32 total=0;foreach(var value in values){total+=value;}delete values;return total;}
}
int32 main(){
    if(Read(wide:[(uint66)-1],fractions:[1.25,2.5],bytes:[255,3])!=0){return 1;}
    fn(byte[],float32[],uint66[])->int32 callback=&Read;
    if(callback([255,3],[1.25,2.5],[(uint66)-1])!=0){return 2;}
    Packet packet=new Packet([Next(1),Next(2)]);
    if(trace!=12 || packet.total!=3 || packet.Sum([-3,2000,17])!=2014){return 3;}
    delete packet;return 0;
}
''', 'numeric-context')

    def test_nullable_empty_and_generic_callback_literals_keep_the_selected_shape(self):
        self.execute_targets('''
int32 Read(){return 17;}
int32 Check(int32*?[] pointers,string[] labels){unsafe(using krt.mem;){
    if(pointers.Length!=2 || pointers[0]!=null || labels[0]!=null || labels[1]!="ok"){return 1;}
    if(pointers[1] is int32* live){if(*live!=17){return 2;}}else{return 3;}
    delete pointers;delete labels;return 0;
}}
int32 Empty(float32[] values){int32 length=(int32)values.Length;delete values;return length;}
int32 Missing(int32*?[] values){if(values.Length!=2 || values[0]!=null || values[1]!=null){return 1;}delete values;return 0;}
int32 Invoke<T>(T exemplar,T[] values){int32 result=values[0]();delete values;return result;}
int32 InferredEmpty<T>(T exemplar,T[] values){int32 length=(int32)values.Length;delete values;return length;}
int32 main(){unsafe(using krt.mem;){
    int32 value=17;
    if(Check([null,&value],[null,"ok"])!=0){return 1;}
    fn(int32*?[],string[])->int32 callback=&Check;
    if(callback([null,&value],[null,"ok"])!=0){return 2;}
    if(Empty([])!=0 || Missing([null,null])!=0){return 3;}
    fn(float32[])->int32 empty=&Empty;fn(int32*?[])->int32 missing=&Missing;
    if(empty([])!=0 || missing([null,null])!=0){return 4;}
    if(Invoke(&Read,[&Read])!=17 || InferredEmpty("ok",[])!=0){return 5;}
    return 0;
}}
''', 'nullable-empty-context')

    def test_overloads_use_complete_element_types_and_contextual_base_classes(self):
        self.execute_targets('''
int32 Pick(uint8[] values){delete values;return 1;}
int32 Pick(int8[] values){delete values;return 2;}
int32 Flag(bool[] values){delete values;return 3;}
int32 Flag(byte[] values){delete values;return 4;}
int32 Character(char[] values){delete values;return 5;}
int32 Character(byte[] values){delete values;return 6;}
class Base{public int32 value;}
class Left:Base{Left(){value=17;}}
class Right:Base{Right(){value=25;}}
int32 Read(Base[] values){int32 result=values[0].value+values[1].value;delete values;return result;}
int32 Choose(Base[] values){delete values;return 7;}
int32 Choose(Left[] values){delete values;return 8;}
class Collector{
    public int32 total;
    Collector(Base[] values){total=Read(values);}
}
int32 main(){
    if(Pick([(uint8)1])!=1 || Pick([(int8)1])!=2){return 1;}
    if(Flag([true])!=3 || Flag([(byte)1])!=4){return 2;}
    if(Character(['a'])!=5 || Character([(byte)97])!=6){return 3;}
    Left left=new Left();Right right=new Right();
    if(Read([left,right])!=42){return 4;}
    fn(Base[])->int32 read=&Read;if(read([left,right])!=42){return 5;}
    Collector collector=new Collector([left,right]);
    if(collector.total!=42 || Choose([left])!=8){return 6;}
    delete collector;delete left;delete right;return 0;
}
''', 'complete-element-overloads')

    def test_context_does_not_convert_stored_arrays_refs_or_invalid_element_shapes(self):
        statements = {
            'stored-array': 'int32[] values=[255];Take(values);',
            'stored-array-indirect': 'int32[] values=[255];fn(byte[])->int32 call=&Take;call(values);',
            'ref-array': 'int32[] values=[255];Ref(ref values);',
            'ref-array-indirect': 'int32[] values=[255];fn(ref byte[])->int32 call=&Ref;call(ref values);',
            'string-element': 'Take(["bad"]);',
            'nullable-element': 'int32 value=17;Pointers([null,&value]);',
            'nullable-element-indirect': 'int32 value=17;fn(int32*[])->int32 call=&Pointers;call([null,&value]);',
            'different-pointee': 'int64 value=17;Pointers([&value]);',
            'unknown-generic-empty': 'Infer([]);',
            'unknown-generic-null': 'Infer([null]);',
            'ambiguous-empty': 'Select([]);',
        }
        prefix = '''
int32 Take(byte[] values){return 0;}
int32 Ref(ref byte[] values){return 0;}
int32 Pointers(int32*[] values){return 0;}
int32 Infer<T>(T[] values){return 0;}
int32 Select(byte[] values){return 0;}
int32 Select(float32[] values){return 0;}
'''
        for name, statement in statements.items():
            path = self.work / (name + '.krt')
            path.write_text(prefix + 'int32 main(){unsafe(using krt.mem;){' + statement + 'return 0;}}')
            for target in ('native', 'vm'):
                for level in range(4):
                    for previous in (None, b'original output must survive\x00'):
                        with self.subTest(case=name, target=target, level=level,
                                          previous=previous is not None):
                            output = self.work / f'{name}-{target}-{level}-{previous is not None}'
                            if previous is not None:
                                output.write_bytes(previous)
                            flags = ['target', 'vm'] if target == 'vm' else []
                            result = subprocess.run([str(COMPILER), '--linker', str(LINKER), str(path),
                                                     f'-O{level}', *flags, '-o', str(output)],
                                                    cwd=self.work, env=self.env, capture_output=True,
                                                    text=True, timeout=60)
                            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                            self.assertRegex(result.stderr, r':\d+:\d+: E_(LOWER|UNDEFINED_METHOD|WRITABLE_REF|NOT_NULLABLE):')
                            if previous is None:
                                self.assertFalse(output.exists())
                            else:
                                self.assertEqual(output.read_bytes(), previous)
