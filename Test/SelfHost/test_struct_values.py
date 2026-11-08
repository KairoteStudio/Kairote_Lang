"""Physical struct layout, independent value copies, and aggregate call ABI."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


class StructValueTests(NativeCompilerFixture):
    def execute_value(self, source):
        self.execute(source)
        path = self.work / 'aggregate-vm.krt'
        path.write_text(source)
        output = self.work / 'aggregate.vm'
        self.command(path, 'target', 'vm', '-O2', '-o', output)
        result = subprocess.run([str(COMPILER), 'run-vm', str(output)], cwd=self.work,
                                env=self.env, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_natural_alignment_nested_layout_and_inline_wide_fields(self):
        self.execute_value('''
struct Small{byte tag;int32 number;uint16 end;}
struct Outer{byte tag;Small inner;int64 last;}
struct Wide{byte tag;uint128 number;byte end;}
struct Empty{}
int32 main(){unsafe(using krt.mem;){
    Small a=default(Small);Outer b=default(Outer);Wide c=default(Wide);Empty e=default(Empty);
    if(sizeof(Small)!=12 || sizeof(Outer)!=24 || sizeof(Wide)!=48 || sizeof(Empty)!=1){return 1;}
    if((int64)&a.number-(int64)&a!=4 || (int64)&a.end-(int64)&a!=8){return 2;}
    if((int64)&b.inner-(int64)&b!=4 || (int64)&b.last-(int64)&b!=16){return 3;}
    if((int64)&c.number-(int64)&c!=16 || (int64)&c.end-(int64)&c!=32 || ((int64)&c&15)!=0){return 4;}
    c.tag=17;c.number=((uint128)1<<100)+43;c.end=29;
    if(c.tag!=17 || c.end!=29 || c.number!=((uint128)1<<100)+43){return 5;}
    uint64* words=(uint64*)&c.number;
    if(words[0]!=43 || words[1]!=((uint64)1<<36)){return 6;}
    return 0;
}}
''')

    def test_fixed_fields_have_inline_storage_constant_counts_and_zero_padding(self):
        self.execute_value('''
const int32 Count=2+2;
struct Header{uint16 family;uint16 port;uint32 address;byte zero[8];}
struct Packet{byte tag;Header entries[Count];uint16 tail;}
int32 main(){unsafe(using krt.mem;){
    Header header=default(Header);Packet packet=default(Packet);
    if(sizeof(Header)!=16 || sizeof(Packet)!=72){return 1;}
    if((int64)&header.port-(int64)&header!=2 || (int64)&header.address-(int64)&header!=4 || (int64)&header.zero[0]-(int64)&header!=8){return 2;}
    if((int64)&packet.entries[0]-(int64)&packet!=4 || (int64)&packet.entries[1]-(int64)&packet.entries[0]!=16 || (int64)&packet.tail-(int64)&packet!=68){return 3;}
    int32 i=0;while(i<sizeof(Packet)){if(((byte*)&packet)[i]!=0){return 4;}i++;}
    header.family=2;header.port=0x1234;header.address=0x0100007f;header.zero[7]=23;
    packet.entries[2]=header;header.zero[7]=99;
    if(packet.entries[2].zero[7]!=23 || packet.entries[1].family!=0 || packet.entries[3].address!=0){return 5;}
    byte* raw=(byte*)&packet.entries[2];
    if(raw[0]!=2 || raw[2]!=0x34 || raw[3]!=0x12 || raw[4]!=127 || raw[7]!=1 || raw[15]!=23){return 6;}
    return 0;
}}
''')

    def test_local_nested_and_conditional_assignment_make_independent_values(self):
        self.execute_value('''
struct Pair{int64 x;int64 y;}
struct Outer{Pair pair;int32 tag;}
int32 main(){
    Pair uninitialized;if(uninitialized.x!=0 || uninitialized.y!=0){return 3;}
    Pair first=default(Pair);first.x=7;first.y=11;
    Pair second=first;second.x=19;
    Outer a=default(Outer);a.pair=first;a.tag=3;
    Outer b=a;b.pair.y=29;b.tag=5;
    Pair chosen=true?second:b.pair;chosen.y=31;
    second=second;
    if(first.x!=7 || first.y!=11 || second.x!=19 || second.y!=11){return 1;}
    if(a.pair.y!=11 || a.tag!=3 || b.pair.y!=29 || b.tag!=5 || chosen.x!=19 || chosen.y!=31){return 2;}
    return 0;
}
''')

    def test_value_parameters_return_storage_and_refs_have_distinct_lifetimes(self):
        self.execute_value('''
struct Pair{int64 x;int64 y;}
Pair change(Pair input){input.x+=100;return input;}
void mutate(ref Pair input){input.y+=9;}
int64 churn(int64 n){Pair scratch=default(Pair);scratch.x=n;scratch.y=n+1;return scratch.x+scratch.y;}
int32 main(){
    Pair source=default(Pair);source.x=7;source.y=11;
    Pair result=change(source);int64 total=0;int32 i=0;while(i<40){total+=churn(i);i++;}
    if(source.x!=7 || source.y!=11 || result.x!=107 || result.y!=11 || total!=1600){return 1;}
    mutate(ref source);if(source.y!=20 || result.y!=11){return 2;}
    Pair another=change(result);another.x=0;
    if(result.x!=107 || another.y!=11){return 3;}
    return 0;
}
''')

    def test_argument_copies_capture_source_evaluation_order_including_named_calls(self):
        self.execute_value('''
struct Pair{int32 x;int32 y;}
int32 later(ref Pair value){value.x=99;return 3;}
int32 consume(Pair value,int32 marker){int32 result=value.x*100+marker;value.x=55;return result;}
int32 main(){
    Pair original=default(Pair);original.x=7;
    if(consume(original,later(ref original))!=703 || original.x!=99){return 1;}
    original.x=7;
    if(consume(marker:later(ref original),value:original)!=9903 || original.x!=99){return 2;}
    original.x=7;
    if(consume(value:original,marker:later(ref original))!=703 || original.x!=99){return 3;}
    return 0;
}
''')

    def test_overlapping_pointer_assignment_copies_in_both_directions(self):
        self.execute_value('''
struct Triple{int64 a;int64 b;int64 c;}
int32 main(){unsafe(using krt.mem;){
    byte* bytes=stackalloc byte[32];Triple* first=(Triple*)bytes;Triple* second=(Triple*)(bytes+8);
    first[0].a=17;first[0].b=29;first[0].c=43;second[0]=first[0];
    if(second[0].a!=17 || second[0].b!=29 || second[0].c!=43){return 1;}
    first[0]=second[0];
    if(first[0].a!=17 || first[0].b!=29 || first[0].c!=43){return 2;}
    return 0;
}}
''')

    def test_arrays_class_fields_pointer_stride_and_ref_targets_are_real_storage(self):
        self.execute_value('''
struct Pair{int64 x;int64 y;}
class Holder{byte before;Pair pair;int32 after;}
void SetValue(ref Pair value){value.x=37;value.y=41;}
int32 main(){unsafe(using krt.mem;){
    Pair[] values=new Pair[3];Holder holder=new Holder();
    values[0].x=7;values[0].y=11;values[1]=values[0];values[0].x=19;
    holder.before=23;holder.pair=values[1];holder.after=29;SetValue(ref holder.pair);
    if(values[1].x!=7 || values[2].x!=0 || holder.before!=23 || holder.after!=29 || holder.pair.x!=37 || holder.pair.y!=41){return 1;}
    Pair* pointer=&values[0];pointer[2]=holder.pair;SetValue(ref pointer[1]);
    if((int64)&values[1]-(int64)&values[0]!=16 || values[1].x!=37 || values[2].y!=41 || values[0].x!=19){return 2;}
    delete holder;delete values;return 0;
}}
''')

    def test_global_structs_initialize_in_order_copy_and_preserve_neighboring_storage(self):
        self.execute_value('''
struct Wide{byte tag;uint128 number;byte end;}
Wide Make(int32 n){Wide result=default(Wide);result.tag=(byte)n;result.number=((uint128)1<<100)+n;result.end=(byte)(n+1);return result;}
static int64 before=23;
static Wide original=Make(7);
static Wide copied=original;
static int64 after=29;
static Wide zero;
int32 main(){unsafe(using krt.mem;){
    if(((int64)&original&15)!=0 || ((int64)&copied&15)!=0 || ((int64)&zero&15)!=0){return 1;}
    copied.number+=11;copied.tag=19;
    if(before!=23 || after!=29 || original.tag!=7 || original.end!=8 || original.number!=((uint128)1<<100)+7){return 2;}
    if(copied.tag!=19 || copied.end!=8 || copied.number!=((uint128)1<<100)+18 || zero.tag!=0 || zero.number!=0 || zero.end!=0){return 3;}
    return 0;
}}
''')

    def test_default_new_field_initializers_constructors_and_mutating_this(self):
        self.execute_value('''
struct Counter{int32 value=5;Counter(){value+=2;}Counter(int32 n){value=n;}void Tick(){value++;}}
struct ArgumentOnly{int32 value=11;ArgumentOnly(int32 n){value=n;}}
int32 main(){
    Counter zero=default(Counter);Counter constructed=new Counter();Counter parameterized=new Counter(19);
    ArgumentOnly default_new=new ArgumentOnly();ArgumentOnly arg=new ArgumentOnly(23);
    if(zero.value!=0 || constructed.value!=7 || parameterized.value!=19 || default_new.value!=11 || arg.value!=23){return 1;}
    constructed.Tick();Counter copied=constructed;copied.Tick();
    if(constructed.value!=8 || copied.value!=9 || zero.value!=0){return 2;}
    return 0;
}
''')

    def test_generic_specializations_use_concrete_layout_and_value_constraints(self):
        self.execute_value('''
struct Box<T>{byte tag;T value;}
struct Pair{int64 x;int64 y;}
T fresh<T>() where T:struct{return default(T);}
int32 main(){
    Box<byte> small=default(Box<byte>);Box<int64> large=default(Box<int64>);Box<Pair> nested=default(Box<Pair>);
    if(sizeof(Box<byte>)!=2 || sizeof(Box<int64>)!=16 || sizeof(Box<Pair>)!=24){return 1;}
    small.tag=3;small.value=7;large.tag=11;large.value=19;nested.value.x=23;
    Box<Pair> copied=nested;copied.value.x=29;Pair zero=fresh<Pair>();
    if(nested.value.x!=23 || copied.value.x!=29 || zero.x!=0 || small.value!=7 || large.value!=19){return 2;}
    return 0;
}
''')

    def test_unmanaged_constraints_allow_only_transitively_unmanaged_storage(self):
        self.execute_value('''
struct Atom{uint16 a;byte b;}
struct Native{Atom atoms[3];int32* address;fn(int32)->int32 callback;byte bytes[5];}
struct Pointers{int32* pointers[2];fn()->int32 callbacks[2];string* text;}
struct Node{Node* next;int64 value;}
int32 Size<T>() where T:unmanaged{return sizeof(T);}
int32 main(){
    if(Size<Native>()!=40 || Size<Atom>()!=4 || Size<Node>()!=16 || Size<uint128>()!=16 || Size<Pointers>()!=40){return 1;}
    if(Size<int32*>()!=8 || Size<fn(int32)->int32>()!=8 || Size<fn()->int32[]>()!=8){return 2;}
    return 0;
}
''')
        prefix='int32 Size<T>() where T:unmanaged{return sizeof(T);}'
        layouts=(
            'struct Inner{string text;}struct Outer{Inner inner;}',
            'class Ref{int32 value;}struct Inner{Ref reference;}struct Outer{Inner inner;}',
            'struct Inner{int32[] values;}struct Outer{Inner inner;}',
            'class Ref{int32 value;}struct Outer{Ref objects[2];}',
            'struct Outer{string strings[2];}',
            'struct Box<T>{T value;}struct Outer{Box<string> inner;}',
            'struct Outer{int32*[] values;}',
        )
        for index,layout in enumerate(layouts):
            with self.subTest(layout=layout):
                path=self.work/f'managed-layout-{index}.krt';path.write_text(layout+prefix+'int32 main(){return Size<Outer>();}')
                output=path.with_suffix('.kro')
                result=subprocess.run([str(COMPILER),str(path),'-c','-o',str(output)],cwd=self.work,
                                      env=self.env,capture_output=True,timeout=60)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertNotIn(b'E_PARSE',result.stderr)
                self.assertFalse(output.exists())
        function_arrays=(
            'int32 Check<T>(T value) where T:unmanaged{return 0;}int32 main(){fn()->int32 values[1];return Check(values);}',
            'int32 Check<T>(T value) where T:unmanaged{return 0;}int32 Read(){return 17;}T[] Make<T>(T callback){T[] values=new T[1];values[0]=callback;return values;}int32 main(){var values=Make(&Read);return Check(values);}',
            'struct Wrapper<T>{T callbacks;}int32 Require<T>() where T:unmanaged{return 0;}int32 Check<T>(T value){return Require<Wrapper<T>>();}int32 Read(){return 17;}T[] Make<T>(T callback){T[] values=new T[1];values[0]=callback;return values;}int32 main(){var values=Make(&Read);return Check(values);}',
        )
        for index,source in enumerate(function_arrays):
            with self.subTest(function_array=source):
                path=self.work/f'managed-functions-{index}.krt';path.write_text(source)
                output=path.with_suffix('.kro')
                result=subprocess.run([str(COMPILER),str(path),'-c','-o',str(output)],cwd=self.work,
                                      env=self.env,capture_output=True,timeout=60)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertNotIn(b'E_PARSE',result.stderr)
                self.assertFalse(output.exists())

    def test_fixed_array_assignment_inference_and_length_preserve_value_and_evaluation_order(self):
        self.execute_value('''
struct Atom{int64 value;}
class Holder{byte bytes[4];Atom atoms[2];}
static Holder storage;
static int32 visits=0;
Holder Get(){visits++;return storage;}
int32 main(){
    storage=new Holder();Holder other=new Holder();storage.bytes[0]=3;storage.bytes[1]=7;storage.bytes[2]=11;storage.bytes[3]=13;
    var snapshot=storage.bytes;snapshot[1]=19;other.bytes=storage.bytes;other.bytes[2]=23;storage.bytes=storage.bytes;
    storage.atoms[0].value=29;other.atoms=storage.atoms;other.atoms[0].value=31;
    if(storage.bytes[1]!=7 || storage.bytes[2]!=11 || snapshot[1]!=19 || snapshot[2]!=11 || other.bytes[2]!=23){return 1;}
    if(storage.atoms[0].value!=29 || other.atoms[0].value!=31 || other.atoms[1].value!=0){return 2;}
    if(Get().bytes.Length!=4 || Get().atoms.Length!=2 || visits!=2 || snapshot.Length!=4){return 3;}
    delete storage;delete other;return 0;
}
''')

    def test_fixed_arrays_cannot_alias_dynamic_array_headers_or_be_deleted(self):
        prefix='class Holder{int32 values[4];}'
        statements=(
            'Holder holder=new Holder();int32[] dynamic=holder.values;',
            'Holder holder=new Holder();holder.values=new int32[4];',
            'Holder holder=new Holder();delete holder.values;',
        )
        for index,statement in enumerate(statements):
            with self.subTest(statement=statement):
                path=self.work/f'fixed-shape-{index}.krt';path.write_text(prefix+'int32 main(){'+statement+'return 0;}')
                output=path.with_suffix('.kro')
                result=subprocess.run([str(COMPILER),str(path),'-c','-o',str(output)],cwd=self.work,
                                      env=self.env,capture_output=True,timeout=60)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertFalse(output.exists())

    def test_hidden_return_pointer_preserves_source_128_arity_direct_method_and_indirect(self):
        parameters=', '.join(f'int64 p{i}' for i in range(1,128))
        method_parameters=', '.join(f'int64 p{i}' for i in range(1,127))
        arguments=', '.join(str(i) for i in range(1,128))
        method_arguments=', '.join(str(i) for i in range(1,127))
        pointer_types=', '.join(['Pair']+['int64']*127)
        source=f'''
struct Pair{{int64 x;int64 y;}}
Pair change(Pair value,{parameters}){{value.x+=p1;value.y+=p127;return value;}}
class Calls{{Pair Change(Pair value,{method_parameters}){{value.x+=p1;value.y+=p126;return value;}}}}
int32 main(){{
    Pair original=default(Pair);original.x=7;original.y=11;
    Pair direct=change(original,{arguments});Calls calls=new Calls();Pair method=calls.Change(original,{method_arguments});
    fn({pointer_types})->Pair callback=&change;Pair indirect=callback(original,{arguments});
    if(original.x!=7 || original.y!=11 || direct.x!=8 || direct.y!=138 || method.x!=8 || method.y!=137 || indirect.x!=8 || indirect.y!=138){{return 1;}}
    delete calls;return 0;
}}
'''
        self.execute_value(source)

    def test_return_value_is_copied_before_finally_mutates_original(self):
        self.execute_value('''
struct Pair{int64 x;int64 y;}
static int32 cleanups=0;
Pair make(int32 n){Pair result=default(Pair);result.x=n;result.y=11;try{return result;}finally{result.x=99;cleanups++;}}
Pair handled(){try{throw 17;}catch(int32 n){return make(n);}finally{cleanups++;}}
int32 main(){Pair result=handled();return result.x==17 && result.y==11 && cleanups==2 ? 0 : 1;}
''')

    def test_readonly_struct_fields_use_defensive_receiver_copies(self):
        self.execute_value('''
struct Counter{int32 value;Counter(int32 n){value=n;}void Tick(){value++;}readonly int32 Read(){return value;}}
class Holder{public readonly Counter counter;Holder(){counter=new Counter(7);}void Tick(){counter.Tick();}}
int32 main(){
    Holder holder=new Holder();holder.Tick();holder.counter.Tick();Counter copy=holder.counter;copy.Tick();
    if(holder.counter.Read()!=7 || copy.Read()!=8){return 1;}
    delete holder;return 0;
}
''')

    def test_rvalues_and_readonly_storage_cannot_be_assigned_or_passed_by_ref(self):
        prefix='''
struct Pair{int64 x;int64 y;}
Pair Make(){return default(Pair);}
void Mutate(ref Pair value){value.x=17;}
class Holder{public readonly Pair pair;Holder(){pair=default(Pair);}}
'''
        statements=(
            'Make().x=17;',
            'Mutate(ref Make());',
            'Holder holder=new Holder();holder.pair.x=17;',
            'Holder holder=new Holder();holder.pair=Make();',
            'Holder holder=new Holder();Mutate(ref holder.pair);',
        )
        for index,statement in enumerate(statements):
            with self.subTest(statement=statement):
                path=self.work/f'immutable-{index}.krt';path.write_text(prefix+'int32 main(){'+statement+'return 0;}')
                output=path.with_suffix('.kro')
                result=subprocess.run([str(COMPILER),str(path),'-c','-o',str(output)],cwd=self.work,
                                      env=self.env,capture_output=True,timeout=60)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertFalse(output.exists())

    def test_aggregate_arithmetic_scalar_casts_null_and_readonly_mutators_are_rejected(self):
        prefix='struct Pair{int64 x;int64 y;}'
        statements=(
            'Pair a=default(Pair);Pair b=default(Pair);a+b;',
            'Pair a=default(Pair);int64 number=(int64)a;',
            'Pair a=default(Pair);-(a);',
            'Pair a=null;',
            'Pair a=default(Pair);a+=default(Pair);',
        )
        sources=[prefix+'int32 main(){'+statement+'return 0;}' for statement in statements]
        sources += ['struct Counter{int32 value;readonly void Tick(){value++;}}int32 main(){return 0;}']
        for index,source in enumerate(sources):
            with self.subTest(source=source):
                path=self.work/f'invalid-value-{index}.krt';path.write_text(source)
                output=path.with_suffix('.kro')
                result=subprocess.run([str(COMPILER),str(path),'-c','-o',str(output)],cwd=self.work,
                                      env=self.env,capture_output=True,timeout=60)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertNotIn(b'E_PARSE',result.stderr)
                self.assertFalse(output.exists())

    def test_value_layout_cycles_and_invalid_fixed_lengths_are_rejected(self):
        sources=(
            'struct Cycle{Cycle child;}int32 main(){return 0;}',
            'struct Left{Right next;}struct Right{Left next;}int32 main(){return 0;}',
            'struct Bad{byte values[0];}int32 main(){return 0;}',
            'struct Bad{byte values[-1];}int32 main(){return 0;}',
            'int32 size(){return 3;}struct Bad{byte values[size()];}int32 main(){return 0;}',
        )
        for index,source in enumerate(sources):
            with self.subTest(source=source):
                path=self.work/f'invalid-{index}.krt';path.write_text(source)
                output=path.with_suffix('.kro');output.write_bytes(b'previous output')
                result=subprocess.run([str(COMPILER),str(path),'-c','-o',str(output)],cwd=self.work,
                                      env=self.env,capture_output=True,timeout=60)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertEqual(output.read_bytes(),b'previous output')
        self.execute_value('''
struct Node{Node* next;int64 value;}
int32 main(){unsafe(using krt.mem;){Node first=default(Node);Node second=default(Node);first.next=&second;second.value=17;return first.next.value==17 && sizeof(Node)==16 ? 0 : 1;}}
''')
