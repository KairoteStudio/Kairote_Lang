"""Fixed arrays keep their shape and value semantics through generic calls."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


class FixedGenericValueTests(NativeCompilerFixture):
    def execute_inline(self, source):
        self.execute(source)
        path = self.work / 'fixed-generic.krt'
        path.write_text(source)
        output = self.work / 'fixed-generic.ebc'
        self.command(path, 'target', 'vm', '-O2', '-o', output)
        self.assertEqual(int.from_bytes(output.read_bytes()[4:6], 'little'), 3)
        result = subprocess.run([str(COMPILER), 'run-vm', str(output)], cwd=self.work,
                                env=self.env, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_length_identity_value_parameters_refs_and_caller_owned_returns(self):
        self.execute_inline('''
struct Buffers{byte two[2];byte three[3];uint128 wide[2];}
int32 Bytes<T>(T value){return sizeof(T);}
T Change<T>(T value){value[0]=19;return value;}
void Touch<T>(ref T value){value[0]=31;}
T Keep<T>(T value){return value;}
T Clear<T>(T value){return default(T);}
int32 main(){
    Buffers data=default(Buffers);data.two[0]=7;data.two[1]=11;data.three[0]=13;
    data.wide[0]=((uint128)1<<100)+17;data.wide[1]=((uint128)1<<120)+23;
    if(Bytes(data.two)!=2 || Bytes(data.three)!=3 || Bytes(data.wide)!=32){return 1;}
    var two=Change(data.two);var three=Change(data.three);var wide=Keep(data.wide);
    if(two.Length!=2 || three.Length!=3 || two[0]!=19 || two[1]!=11 || data.two[0]!=7 || data.three[0]!=13){return 2;}
    Touch(ref data.two);two[1]=43;
    if(data.two[0]!=31 || data.two[1]!=11 || two[0]!=19 || two[1]!=43){return 3;}
    data.wide[0]=29;
    if(wide[0]!=((uint128)1<<100)+17 || wide[1]!=((uint128)1<<120)+23){return 4;}
    var zero=Clear(data.three);if(zero.Length!=3 || zero[0]!=0 || zero[1]!=0 || zero[2]!=0){return 5;}
    return 0;
}
''')

    def test_named_argument_snapshots_and_return_before_finally(self):
        self.execute_inline('''
struct Buffer{byte bytes[3];}
int32 Later<T>(ref T value){value[0]=99;return 3;}
int32 Take<T>(T value,int32 marker){int32 result=value[0]*100+marker;value[0]=77;return result;}
T Final<T>(T value){try{return value;}finally{value[0]=88;}}
int32 main(){
    Buffer data=default(Buffer);data.bytes[0]=7;data.bytes[2]=11;
    if(Take(data.bytes,Later(ref data.bytes))!=703 || data.bytes[0]!=99){return 1;}
    data.bytes[0]=7;
    if(Take(marker:Later(ref data.bytes),value:data.bytes)!=9903 || data.bytes[0]!=99){return 2;}
    data.bytes[0]=7;
    if(Take(value:data.bytes,marker:Later(ref data.bytes))!=703 || data.bytes[0]!=99){return 3;}
    var captured=Final(data.bytes);data.bytes[2]=29;
    if(captured[0]!=99 || captured[2]!=11 || data.bytes[0]!=99){return 4;}
    return 0;
}
''')

    def test_nested_fixed_fields_dynamic_rows_and_generic_owner_indirect_abi(self):
        self.execute_inline('''
struct Input{byte bytes[3];}
struct Box<T>{T rows[2];}
class Relay<T>{T saved;T Echo(T value){saved=value;return saved;}static T Bounce(T value){return value;}}
T Nested<T>(T value){
    Box<T> box=default(Box<T>);box.rows[0]=value;box.rows[1]=value;box.rows[0][0]=29;
    if(sizeof(Box<T>)!=sizeof(T)*2 || box.rows.Length!=2 || box.rows[1].Length!=value.Length){return default(T);}
    return box.rows[1];
}
T Dynamic<T>(T value){
    T[] rows=new T[3];rows[0]=value;rows[1]=value;rows[0][0]=47;
    var result=rows[1];delete rows;return result;
}
T Indirect<T>(T value){
    Relay<T> relay=new Relay<T>();var result=relay.Echo(value);
    fn(T)->T callback=&Relay<T>.Bounce;var indirect=callback(result);delete relay;return indirect;
}
int32 main(){
    Input data=default(Input);data.bytes[0]=7;data.bytes[1]=11;data.bytes[2]=13;
    var nested=Nested(data.bytes);var dynamic=Dynamic(data.bytes);var indirect=Indirect(data.bytes);
    if(nested.Length!=3 || dynamic.Length!=3 || indirect.Length!=3){return 1;}
    if(nested[0]!=7 || nested[1]!=11 || dynamic[0]!=7 || dynamic[2]!=13 || indirect[0]!=7 || indirect[2]!=13){return 2;}
    nested[0]=59;dynamic[1]=61;indirect[2]=67;
    if(data.bytes[0]!=7 || data.bytes[1]!=11 || data.bytes[2]!=13){return 3;}
    return 0;
}
''')

    def test_generic_array_literal_copies_each_fixed_element(self):
        self.execute_inline('''
struct Input{byte bytes[2];}
T Literal<T>(T value){
    T* pointer=&value;var saved=pointer[0];pointer[0][0]=37;
    if(saved[0]!=7 || value[0]!=37){return default(T);}
    value=saved;
    T[] rows={value,value};rows[0][0]=41;var result=rows[1];delete rows;return result;
}
int32 main(){
    Input data=default(Input);data.bytes[0]=7;data.bytes[1]=11;
    var result=Literal(data.bytes);result[0]=43;
    if(result.Length!=2 || result[0]!=43 || result[1]!=11 || data.bytes[0]!=7 || data.bytes[1]!=11){return 1;}
    return 0;
}
''')
