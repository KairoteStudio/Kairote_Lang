"""Nullable operators preserve guarded evaluation and complete value shapes."""
import re
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


POSITIVE_CASES = {
'coalesce-lazy-references': '''
class Route{public int32 port;Route(int32 value){port=value;}}
static int32 trace=0;
Route Probe(Route value,int32 digit){trace=trace*10+digit;return value;}
Route Crash(){throw 99;}
int32 main(){
    Route missing=null;Route live=new Route(8080);Route fallback=new Route(8081);
    Route first=Probe(live,1)??Crash();if(first!=live || trace!=1){return 1;}
    trace=0;Route second=Probe(missing,2)??Probe(fallback,3);
    if(second!=fallback || trace!=23){return 2;}
    trace=0;Route third=Probe(missing,1)??Probe(missing,2)??Probe(live,3);
    if(third!=live || trace!=123){return 3;}
    if((missing??live).port!=8080 || (live??fallback).port!=8080){return 4;}
    trace=0;Route choose=true?live:Probe(missing,4)??Probe(fallback,5);
    if(choose!=live || trace!=0){return 5;}
    delete live;delete fallback;return 0;
}
''',
'service-response-values': '''
struct Response{int32 status;int64 length;byte tag[3];fn(int32)->int32 handler;}
class Route{public Response reply;public byte[] body;}
static int32 calls=0;
int32 Argument(){calls++;return 5;}
int32 ReplyCode(int32 value){return value+37;}
T Copy<T>(T value){return value;}
int32 main(){
    Route missing=null;Route live=new Route();live.reply.status=200;
    live.reply.length=4294967313;live.reply.tag[0]=7;live.reply.tag[1]=11;live.reply.tag[2]=13;
    live.reply.handler=&ReplyCode;live.body=[3,9];
    Response empty=missing?.reply;Response selected=Copy(live?.reply);
    if(empty.status!=0 || empty.length!=0 || empty.tag[0]!=0 || empty.tag[1]!=0 || empty.tag[2]!=0 || empty.handler!=null){return 1;}
    if(selected.status!=200 || selected.length!=4294967313 || selected.tag[2]!=13){return 2;}
    selected.status=503;selected.tag[0]=19;if(live.reply.status!=200 || live.reply.tag[0]!=7){return 3;}
    var empty_tags=Copy(missing?.reply.tag);var live_tags=Copy(live?.reply.tag);
    if(empty_tags.Length!=3 || empty_tags[0]!=0 || empty_tags[1]!=0 || empty_tags[2]!=0 || live_tags[2]!=13){return 4;}
    live_tags[2]=23;if(live.reply.tag[2]!=13){return 5;}
    if(missing?.reply.handler(Argument())!=0 || calls!=0){return 6;}
    if(live?.reply.handler(Argument())!=42 || calls!=1){return 7;}
    var empty_callback=missing?.reply.handler;var callback=empty_callback??&ReplyCode;
    if(callback(5)!=42){return 8;}
    byte[] absent=missing?.body;byte[] present=live?.body;
    if(absent!=null || present.Length!=2 || present[1]!=9){return 9;}
    delete live.body;delete live;return 0;
}
''',
'guarded-index-and-chains': '''
class Child{public int32 value;Child(int32 n){value=n;}int32 Add(int32 n){return value+n;}}
class Route{public Child child;public int32 items[2];}
static int32 trace=0;
Route Probe(Route value){trace=trace*10+1;return value;}
int32 Index(int32 digit){trace=trace*10+digit;return 1;}
int32 Argument(int32 digit){trace=trace*10+digit;return 5;}
int32 main(){
    Route missing=null;Route live=new Route();live.child=new Child(37);live.items[1]=42;
    if(Probe(missing)?.child.Add(Argument(2))!=0 || trace!=1){return 1;}
    trace=0;if(Probe(live)?.child.Add(Argument(2))!=42 || trace!=12){return 2;}
    trace=0;if(Probe(missing)?.items[Index(3)]!=0 || trace!=1){return 3;}
    trace=0;if(Probe(live)?.items[Index(3)]!=42 || trace!=13){return 4;}
    Route[] empty=null;Route[] routes=[live,live];
    trace=0;if(empty?[Index(2)].child.Add(Argument(3))!=0 || trace!=0){return 5;}
    trace=0;if(routes?[Index(2)].child.Add(Argument(3))!=42 || trace!=23){return 6;}
    Child saved=live.child;live.child=null;trace=0;
    if(Probe(live)?.child?.Add(Argument(4))!=0 || trace!=1){return 7;}
    live.child=saved;delete saved;delete live;delete routes;return 0;
}
''',
'guarded-call-ref-and-exceptions': '''
class Worker{
    int32 Update(ref int32 target,int32 first,int32 second){target+=7;return first*10+second;}
    void Touch(ref int32 target,int32 value){target+=value;}
}
static int32 trace=0;
Worker Probe(Worker value){trace=trace*10+1;return value;}
int32 Index(){trace=trace*10+2;return 0;}
int32 Value(int32 digit){trace=trace*10+digit;return digit;}
int32 Fail(){throw 42;}
int32 main(){
    Worker missing=null;Worker live=new Worker();int32[] targets=[5];
    if(Probe(missing)?.Update(ref targets[Index()],Value(3),Value(4))!=0 || trace!=1 || targets[0]!=5){return 1;}
    trace=0;if(Probe(live)?.Update(second:Value(4),target:ref targets[Index()],first:Value(3))!=34 || trace!=1423 || targets[0]!=12){return 2;}
    trace=0;Probe(missing)?.Touch(ref targets[Index()],Value(3));
    if(trace!=1 || targets[0]!=12){return 3;}
    trace=0;Probe(live)?.Touch(ref targets[Index()],Value(3));
    if(trace!=123 || targets[0]!=15){return 4;}
    int32 caught=0;try{missing?.Touch(ref targets[0],Fail());}catch(int32 e){caught=1;}
    if(caught!=0 || targets[0]!=15){return 5;}
    try{live?.Touch(ref targets[0],Fail());}catch(int32 e){caught=e;}
    if(caught!=42 || targets[0]!=15){return 6;}
    delete targets;delete live;return 0;
}
''',
'generic-scalar-aggregate-results': '''
struct Record{int32 status;byte tag[2];}
class Box<T>{public T value;Box(T item){value=item;}T Read(){return value;}}
T Optional<T>(Box<T> source){return source?.Read();}
T Present<T>(T optional,T fallback){return optional??fallback;}
int32 main(){
    Box<int8> narrow=new Box<int8>((int8)-7);Box<uint66> wide=new Box<uint66>((uint66)-1);
    Box<float32> fraction=new Box<float32>((float32)1.25);Box<string> text=new Box<string>("ready");
    Record value=default(Record);value.status=42;value.tag[0]=7;value.tag[1]=11;
    Box<Record> record=new Box<Record>(value);
    Box<int8> no_narrow=null;Box<uint66> no_wide=null;Box<float32> no_fraction=null;
    Box<string> no_text=null;Box<Record> no_record=null;
    if(Optional(narrow)!=-7 || Optional(no_narrow)!=0 || (uint128)Optional(wide)!=73786976294838206463 || Optional(no_wide)!=0){return 1;}
    if(Optional(fraction)!=1.25 || Optional(no_fraction)!=0.0 || Optional(text)!="ready" || Optional(no_text)!=null){return 2;}
    Record empty=Optional(no_record);Record selected=Optional(record);selected.tag[0]=19;
    if(empty.status!=0 || empty.tag[0]!=0 || empty.tag[1]!=0 || selected.status!=42 || record.value.tag[0]!=7){return 3;}
    if(Present(Optional(no_text),"fallback")!="fallback" || Present(Optional(text),"fallback")!="ready"){return 4;}
    delete narrow;delete wide;delete fraction;delete text;delete record;return 0;
}
''',
'virtual-interface-results': '''
interface Reader{uint128 Read(uint128 value);void Change(ref int32 value);}
class Base{public virtual float64 Scale(float64 value){return value;}}
class Worker:Base,Reader{
    public override float64 Scale(float64 value){return value+0.5;}
    public uint128 Read(uint128 value){return value+7;}
    public void Change(ref int32 value){value+=11;}
}
int32 main(){
    Worker live=new Worker();Base present=live;Base missing=null;Reader reader=live;Reader absent=null;
    uint128 wide=(uint128)1<<100;int32 counter=5;
    if(present?.Scale(2.0)!=2.5 || missing?.Scale(2.0)!=0.0){return 1;}
    if(reader?.Read(wide)!=wide+7 || absent?.Read(wide)!=0){return 2;}
    absent?.Change(ref counter);if(counter!=5){return 3;}
    reader?.Change(ref counter);if(counter!=16){return 4;}
    Reader recovered=absent??reader;if(recovered.Read(wide)!=wide+7){return 5;}
    delete live;return 0;
}
''',
'pointer-layers-and-defaults': '''
struct Packet{int32 code;byte tag[2];}
int32 main(){unsafe(using krt.mem;){
    int32 value=42;int32*? element=&value;int32*?*? outer=&element;
    var selected=outer??&element;int32*? preserved=selected[0];int32* concrete=preserved??&value;
    if(*concrete!=42){return 1;}
    var indexed=outer?[0];int32* available=indexed??&value;if(*available!=42){return 2;}
    outer=null;var absent=outer?[0];if(absent!=null){return 3;}
    int32* fallback=absent??&value;if(*fallback!=42){return 4;}
    int32*? nullable=null;int32*?[]? array=[nullable,&value];int32*?[] usable=array??new int32*?[1];
    if(usable.Length!=2 || usable[0]!=null || usable[1]!=&value){return 5;}
    var zero=null??default(Packet);if(zero.code!=0 || zero.tag[0]!=0 || zero.tag[1]!=0){return 6;}
    uint128 wide=null??((uint128)1<<95);float64 fraction=null??1.25;
    if(wide!=((uint128)1<<95) || fraction!=1.25){return 7;}
    delete usable;return 0;
}}
''',
'parentheses-end-guard': '''
class Child{int32 Ping(int32 value){return value+7;}}
class Route{public Child child;}
static int32 calls=0;
int32 Argument(){calls++;return 5;}
int32 main(){
    Route missing=null;
    if(missing?.child.Ping(Argument())!=0 || calls!=0){return 1;}
    if((missing?.child).Ping(Argument())!=12 || calls!=1){return 2;}
    if((missing?.child)?.Ping(Argument())!=0 || calls!=1){return 3;}
    return 0;
}
''',
'common-reference-types': '''
interface Reader{int32 Read();}
class Base{public int32 marker=42;}
class Left:Base,Reader{public int32 Read(){return marker;}}
class Right:Base,Reader{public int32 Read(){return marker+1;}}
int32 main(){
    Left left=new Left();Right right=new Right();Left absent=null;
    Base chosen=absent??right;Base retained=left??right;
    Reader reader=left;Reader fallback=right;Reader empty=null;
    if(chosen.marker!=42 || retained!=left || (empty??reader).Read()!=42 || (reader??fallback).Read()!=42){return 1;}
    delete left;delete right;return 0;
}
''',
'coalesce-before-ternary': '''
class Route{public int32 port;Route(int32 value){port=value;}}
int32 main(){
    Route live=new Route(8080);Route chosen=new Route(9090);Route other=new Route(9091);
    Route selected=live??null?chosen:other;
    bool present=live??null?true:false;
    if(selected!=chosen || !present){return 1;}
    Route absent=null;bool missing=absent??null?true:false;
    if(missing){return 2;}
    delete live;delete chosen;delete other;return 0;
}
''',
'callback-labels-and-arrays': '''
int32 Alpha(int32 left,int32 right){return left*10+right;}
int32 Beta(int32 first,int32 second){return first*100+second;}
int32 One(int32 first,int32 second){return first*10+second;}
int32 Two(int32 first,int32 second){return first*100+second;}
int32 main(){
    var a=&Alpha;var b=&Beta;var positional=a??b;
    if(positional(5,7)!=57){return 1;}
    var c=&One;var d=&Two;var named=c??d;
    if(named(second:7,first:5)!=57 || c(second:3,first:2)!=23){return 2;}
    var left=[&Alpha,&Alpha];var right=[&Beta,&Beta];
    var callbacks=left??right;
    if(callbacks.Length!=2 || callbacks[0](5,7)!=57 || callbacks[1](5,7)!=57){return 3;}
    if(left[0](right:7,left:5)!=57 || right[0](second:7,first:5)!=507){return 4;}
    delete left;delete right;return 0;
}
''',
}


NEGATIVE_CASES = {
    'scalar-left': (
        'int32 main(){int32 number=3;return number??7;}', 'number??7', 'E_LOWER'),
    'aggregate-left': (
        'struct Record{int32 status;}int32 main(){Record value=default(Record);var result=value??default(Record);return 0;}',
        'value??default(Record)', 'E_LOWER'),
    'scalar-receiver': (
        'int32 main(){int32 number=3;return number?.field;}', 'number?.field', 'E_LOWER'),
    'assignment': (
        'class Route{public int32 number;}int32 main(){Route value=new Route();value?.number=3;return 0;}',
        'value?.number=3', 'E_LOWER'),
    'compound-assignment': (
        'class Route{public int32 number;}int32 main(){Route value=new Route();value?.number+=3;return 0;}',
        'value?.number+=3', 'E_LOWER'),
    'increment': (
        'class Route{public int32 number;}int32 main(){Route value=new Route();value?.number++;return 0;}',
        'value?.number++', 'E_LOWER'),
    'post-decrement': (
        'class Route{public int32 number;}int32 main(){Route value=new Route();value?.number--;return 0;}',
        'value?.number--', 'E_LOWER'),
    'pre-increment': (
        'class Route{public int32 number;}int32 main(){Route value=new Route();++value?.number;return 0;}',
        '++value?.number', 'E_LOWER'),
    'pre-decrement': (
        'class Route{public int32 number;}int32 main(){Route value=new Route();--value?.number;return 0;}',
        '--value?.number', 'E_LOWER'),
    'conditional-index-assignment': (
        'int32 main(){int32[] values=[1,2];values?[0]=3;return 0;}',
        'values?[0]=3', 'E_LOWER'),
    'conditional-index-compound-assignment': (
        'int32 main(){int32[] values=[1,2];values?[0]+=3;return 0;}',
        'values?[0]+=3', 'E_LOWER'),
    'conditional-index-post-increment': (
        'int32 main(){int32[] values=[1,2];values?[0]++;return 0;}',
        'values?[0]++', 'E_LOWER'),
    'conditional-index-post-decrement': (
        'int32 main(){int32[] values=[1,2];values?[0]--;return 0;}',
        'values?[0]--', 'E_LOWER'),
    'conditional-index-pre-increment': (
        'int32 main(){int32[] values=[1,2];++values?[0];return 0;}',
        '++values?[0]', 'E_LOWER'),
    'conditional-index-pre-decrement': (
        'int32 main(){int32[] values=[1,2];--values?[0];return 0;}',
        '--values?[0]', 'E_LOWER'),
    'writable-ref': (
        'class Route{public int32 number;}void Touch(ref int32 number){number=3;}int32 main(){Route value=new Route();Touch(ref value?.number);return 0;}',
        'value?.number', 'E_LOWER'),
    'address': (
        'class Route{public int32 number;}int32 main(){unsafe(using krt.mem;){Route value=new Route();int32* pointer=&value?.number;return 0;}}',
        'value?.number', 'E_LOWER'),
    'private-member': (
        'class Route{private int32 secret=7;}int32 main(){Route missing=null;return missing?.secret;}',
        'secret', 'E_ACCESS'),
    'invalid-skipped-argument': (
        'class Route{int32 Read(int32 value){return value;}}int32 main(){Route missing=null;return missing?.Read("bad");}',
        'missing?.Read("bad")', 'E_UNDEFINED_METHOD'),
    'grouped-nullable-pointer': (
        'class Route{public int32* pointer;}int32 main(){unsafe(using krt.mem;){Route missing=null;return (missing?.pointer)[0];}}',
        'missing?.pointer', 'E_NOT_NULLABLE'),
    'grouped-nullable-callback': (
        'class Route{public fn(int32)->int32 callback;}int32 main(){Route missing=null;return (missing?.callback)(5);}',
        'missing?.callback', 'E_NOT_NULLABLE'),
    'array-element-nullability': (
        'int32 main(){unsafe(using krt.mem;){int32 value=7;int32*?[] optional=[null,&value];int32*?[]? absent=null;var chosen=absent??optional;int32*[] erased=chosen;return 0;}}',
        'int32*[] erased', 'E_LOWER'),
    'callback-ref-shape': (
        'class Route{public fn(ref int32)->void callback;}void Change(int32 value){}int32 main(){Route missing=null;var callback=missing?.callback??&Change;return 0;}',
        'missing?.callback??&Change', 'E_LOWER'),
    'different-callback-labels': (
        'int32 Alpha(int32 left,int32 right){return left+right;}int32 Beta(int32 first,int32 second){return first+second;}int32 main(){var a=&Alpha;var b=&Beta;var selected=a??b;return selected(left:5,right:7);}',
        'left:5', 'E_ARGUMENT_NAME'),
    'different-array-callback-labels': (
        'int32 Alpha(int32 left,int32 right){return left+right;}int32 Beta(int32 first,int32 second){return first+second;}int32 main(){var a=[&Alpha];var b=[&Beta];var selected=a??b;return selected[0](left:5,right:7);}',
        'left:5', 'E_ARGUMENT_NAME'),
    'array-callback-ref-shape': (
        'void Change(ref int32 value){value=7;}void Read(int32 value){}T[] Single<T>(T value){T[] items=new T[1];items[0]=value;return items;}int32 main(){var selected=Single(&Change)??Single(&Read);return 0;}',
        'Single(&Change)??Single(&Read)', 'E_LOWER'),
}


class NullableOperatorTests(NativeCompilerFixture):
    def execute_targets(self, name):
        source = POSITIVE_CASES[name]
        path = self.work / (name + '.krt')
        path.write_text(source, encoding='utf-8')
        for target in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(program=name, target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b'')
                    self.assertEqual(result.stderr, b'')

    def reject_targets(self, name, source, marker, code):
        path = self.work / (name + '.krt')
        path.write_text(source, encoding='utf-8')
        position = source.rindex(marker)
        line = source[:position].count('\n') + 1
        column = len(source[:position].rsplit('\n', 1)[-1]) + 1
        expected = re.escape(f'{path}:{line}:{column}: {code}:')
        for target in ('native', 'vm'):
            for level in (0, 2):
                for previous in (None, b'previous server artifact\x00must survive'):
                    with self.subTest(program=name, target=target, optimization=level,
                                      previous_output=previous is not None):
                        output = self.work / f'{name}-{target}-o{level}-{previous is not None}'
                        if previous is not None:
                            output.write_bytes(previous)
                        flags = ['target', 'vm'] if target == 'vm' else []
                        argv = [str(COMPILER), '--linker', str(LINKER), str(path),
                                f'-O{level}', *flags, '-o', str(output)]
                        first = subprocess.run(argv, cwd=self.work, env=self.env,
                                               capture_output=True, text=True, timeout=60)
                        second = subprocess.run(argv, cwd=self.work, env=self.env,
                                                capture_output=True, text=True, timeout=60)
                        self.assertEqual(first.returncode, 1, first.stdout + first.stderr)
                        self.assertEqual(second.returncode, 1, second.stdout + second.stderr)
                        self.assertEqual(first.stderr, second.stderr, 'diagnostic changed between identical builds')
                        self.assertEqual(re.findall(r':\d+:\d+: E_[A-Z_]+:', first.stderr),
                                         [f':{line}:{column}: {code}:'], first.stderr)
                        self.assertRegex(first.stderr, expected)
                        self.assertNotIn('E_PARSE', first.stderr)
                        if previous is None:
                            self.assertFalse(output.exists(), 'a rejected source created an artifact')
                        else:
                            self.assertEqual(output.read_bytes(), previous)
                        self.assertFalse(output.with_name(output.name + '.ebc').exists())

    def test_coalescing_short_circuits_references_and_preserves_precedence(self):
        self.execute_targets('coalesce-lazy-references')

    def test_server_response_structs_fixed_arrays_and_callbacks_are_copied_or_zeroed(self):
        self.execute_targets('service-response-values')

    def test_guarded_index_member_and_call_chains_evaluate_receivers_once(self):
        self.execute_targets('guarded-index-and-chains')

    def test_guarded_calls_preserve_named_ref_order_and_skip_throwing_arguments(self):
        self.execute_targets('guarded-call-ref-and-exceptions')

    def test_generic_results_keep_narrow_wide_float_and_aggregate_shapes(self):
        self.execute_targets('generic-scalar-aggregate-results')

    def test_virtual_interface_and_void_calls_share_conditional_semantics(self):
        self.execute_targets('virtual-interface-results')

    def test_pointer_layers_array_elements_and_literal_null_defaults_are_preserved(self):
        self.execute_targets('pointer-layers-and-defaults')

    def test_parentheses_end_the_guard_and_explicit_guards_can_restart_it(self):
        self.execute_targets('parentheses-end-guard')

    def test_coalescing_uses_a_common_class_or_interface_reference(self):
        self.execute_targets('common-reference-types')

    def test_coalescing_binds_before_ternary_conditions(self):
        self.execute_targets('coalesce-before-ternary')

    def test_dynamic_callbacks_keep_only_shared_names_and_preserve_array_shapes(self):
        self.execute_targets('callback-labels-and-arrays')

    def test_invalid_operands_targets_access_and_type_shapes_preserve_old_outputs(self):
        for name, (source, marker, code) in NEGATIVE_CASES.items():
            self.reject_targets(name, source, marker, code)
