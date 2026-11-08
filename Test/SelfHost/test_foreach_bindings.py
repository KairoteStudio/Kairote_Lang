"""Loop variables share ordinary inference, readonly and value-copy contracts."""
import re
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


POSITIVE_CASES = {
    'numeric-arrays': '''
void Float32(ref float32 value){value+=0.5;}
void Float64(ref float64 value){value+=0.25;}
void Narrow(ref int30 value){value+=1;}
void Wide(ref uint66 value){value+=1;}
void Boolean(ref bool value){value=!value;}
int32 main(){
    float32[] small=[1.25,2.5];float64[] large=[3.25,4.5];
    int30[] narrow=[-7,19];uint66[] wide=[(uint66)73786976294838206463];bool[] flags=[true,false];
    float64 total=0.0;foreach(auto item in small){Float32(ref item);total+=item;}
    if(total!=4.75 || small[0]!=1.25 || small[1]!=2.5){return 1;}
    total=0.0;foreach(auto item in large){Float64(ref item);total+=item;}
    if(total!=8.25 || large[0]!=3.25 || large[1]!=4.5){return 2;}
    int32 count=0;foreach(auto item in narrow){Narrow(ref item);count+=(int32)item;}
    if(count!=14 || narrow[0]!=-7){return 3;}
    foreach(auto item in wide){Wide(ref item);if(item!=0){return 4;}}
    count=0;foreach(auto item in flags){Boolean(ref item);if(item){count++;}}
    if(count!=1 || !flags[0] || flags[1]){return 5;}
    delete small;delete large;delete narrow;delete wide;delete flags;return 0;
}
''',
    'strings-and-characters': '''
void Character(ref char value){value='z';}
T Identity<T>(T value){return value;}
int32 main(){
    string[] words=["ab","cde",""];int32 total=0;
    foreach(auto word in words){total+=(int32)word.Length;}
    if(total!=5){return 1;}
    total=0;foreach(auto character in "abc"){total+=character;Character(ref character);if(character!='z'){return 2;}}
    if(total!=294 || words[0]!="ab" || words[1]!="cde"){return 3;}
    var initial="xy"[0];Character(ref initial);auto copied=Identity("xy"[1]);Character(ref copied);
    if(initial!='z' || copied!='z'){return 4;}
    delete words;return 0;
}
''',
    'fixed-and-generic-values': '''
struct Record<T>{T value;int64 marker;}
struct Batch{float32 readings[2];Record<int64> entries[2];}
T First<T>(T[] values){foreach(auto item in values){return item;}return default(T);}
void Change(ref Record<int64> value){value.value+=100;value.marker+=1;}
int32 main(){
    Batch batch=default(Batch);batch.readings[0]=1.25;batch.readings[1]=2.5;
    batch.entries[0].value=17;batch.entries[0].marker=3;batch.entries[1].value=25;batch.entries[1].marker=5;
    float64 total=0.0;foreach(auto item in batch.readings){total+=item;}
    if(total!=3.75){return 1;}
    int64 sum=0;foreach(auto item in batch.entries){Change(ref item);sum+=item.value+item.marker;}
    if(sum!=252 || batch.entries[0].value!=17 || batch.entries[1].marker!=5){return 2;}
    Record<string>[] names=new Record<string>[2];names[0].value="first";names[1].value="second";
    Record<string> first=First(names);first.value="changed";
    if(names[0].value!="first" || first.value!="changed"){return 3;}
    total=0.0;float32[] readings=[1.5,2.75];foreach(auto item in readings){total+=item;}
    if(First(readings)!=1.5 || total!=4.25){return 4;}
    delete names;delete readings;return 0;
}
''',
    'readonly-struct-receivers': '''
struct Counter{int64 value;Counter(int64 n){value=n;}void Tick(){value++;}readonly int64 Read(){return value;}}
struct Box<T>{T payload;int64 marker;void Tick(){marker++;}readonly int64 Read(){return marker;}}
int32 main(){
    Counter[] counters=new Counter[2];counters[0]=new Counter(7);counters[1]=new Counter(11);
    int64 total=0;foreach(let item in counters){int64 before=item.Read();item.Tick();if(item.Read()!=before){return 1;}total+=item.Read();}
    if(total!=18 || counters[0].Read()!=7 || counters[1].Read()!=11){return 2;}
    total=0;foreach(var item in counters){item.Tick();total+=item.Read();}
    if(total!=20 || counters[0].Read()!=7 || counters[1].Read()!=11){return 3;}
    Box<string>[] boxes=new Box<string>[1];boxes[0].payload="entry";boxes[0].marker=19;
    foreach(let item in boxes){item.Tick();if(item.Read()!=19 || item.payload!="entry"){return 4;}}
    if(boxes[0].Read()!=19){return 5;}
    delete counters;delete boxes;return 0;
}
''',
    'evaluation-and-scope': '''
float32[] Make(ref int32 calls){calls++;return [1.5,2.5,3.5];}
int32 main(){
    int32 calls=0;float64 total=0.0;auto outer=7.25;let preserved="scope";
    foreach(auto item in Make(ref calls)){
        if(item==2.5){continue;}
        foreach(let item in [2,3]){total+=item;}
        total+=item;if(item==3.5){break;}
    }
    auto item="outside";
    if(calls!=1 || total!=15.0 || item!="outside" || outer!=7.25 || preserved!="scope"){return 1;}
    int32[] empty=[];foreach(let item in empty){return 2;}delete empty;return 0;
}
''',
}


def scalar_rejection(statement):
    return '''
void Mutate(ref int32 value){value++;}
int32 main(){
    int32[] values=[1,2];
    foreach(let item in values){
        ''' + statement + '''
    }
    delete values;return 0;
}
'''


def struct_rejection(statement):
    return '''
struct Record{int64 value;}
void Mutate(ref Record value){value.value++;}
int32 main(){
    Record[] values=new Record[1];
    foreach(let item in values){
        ''' + statement + '''
    }
    delete values;return 0;
}
'''


NEGATIVE_CASES = {
    'let-assignment': (scalar_rejection('item=3;'), 'item=3'),
    'let-compound-assignment': (scalar_rejection('item+=3;'), 'item+=3'),
    'let-postincrement': (scalar_rejection('item++;'), 'item++'),
    'let-preincrement': (scalar_rejection('++item;'), '++item'),
    'let-reference': (scalar_rejection('Mutate(ref item);'), 'item);'),
    'let-struct-assignment': (struct_rejection('item=default(Record);'), 'item=default'),
    'let-struct-field': (struct_rejection('item.value=3;'), 'item.value=3'),
    'let-struct-reference': (struct_rejection('Mutate(ref item);'), 'item);'),
    'loop-variable-escape': ('''
int32 main(){
    foreach(auto item in [1,2]){}
    return item;
}
''', 'item;'),
    'auto-needs-initializer': ('''
int32 main(){
    auto item;
    return 0;
}
''', 'auto item'),
}


class ForeachBindingTests(NativeCompilerFixture):
    def execute_case(self, name):
        path = self.work / (name + '.krt')
        path.write_text(POSITIVE_CASES[name])
        for target in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(case=name, target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_auto_preserves_numeric_element_identity_and_writable_copies(self):
        self.execute_case('numeric-arrays')

    def test_auto_preserves_strings_and_character_identity(self):
        self.execute_case('strings-and-characters')

    def test_auto_copies_fixed_and_generic_struct_elements(self):
        self.execute_case('fixed-and-generic-values')

    def test_let_struct_methods_use_defensive_receiver_copies(self):
        self.execute_case('readonly-struct-receivers')

    def test_loop_collection_evaluation_and_nested_variable_scope(self):
        self.execute_case('evaluation-and-scope')

    def test_reject_readonly_writes_references_and_variable_escape(self):
        for name, (source, marker) in NEGATIVE_CASES.items():
            path = self.work / (name + '.krt')
            path.write_text(source)
            position = source.index(marker)
            line = source.count('\n', 0, position) + 1
            column = position - source.rfind('\n', 0, position)
            for target in ('native', 'vm'):
                for level in (0, 2):
                    with self.subTest(case=name, target=target, optimization=level):
                        output = self.work / f'{name}-{target}-o{level}'
                        flags = ['target', 'vm'] if target == 'vm' else []
                        result = subprocess.run([str(COMPILER), '--linker', str(LINKER), str(path),
                                                 f'-O{level}', *flags, '-o', str(output)],
                                                cwd=self.work, env=self.env, capture_output=True,
                                                text=True, timeout=60)
                        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                        self.assertRegex(result.stderr, re.escape(f':{line}:{column}: E_LOWER:'))
                        self.assertNotIn('E_PARSE', result.stderr)
                        self.assertFalse(output.exists(), 'rejection created an output artifact')


if __name__ == '__main__':
    import unittest
    unittest.main()
