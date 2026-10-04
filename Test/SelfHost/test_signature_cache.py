"""Repeated declaration types retain source access and concrete value semantics."""
import re
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


class SignatureCacheTests(NativeCompilerFixture):
    def write_sources(self, name, files):
        directory = self.work / name
        directory.mkdir()
        paths = {}
        for filename, source in files.items():
            path = directory / filename
            path.write_text(source, encoding='utf-8')
            paths[filename] = path
        return directory, paths

    def execute_targets(self, name, files):
        directory, paths = self.write_sources(name, files)
        for target in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(program=name, target=target, level=level):
                    output = directory / f'program-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(*paths.values(), f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b'')

    def reject_targets(self, name, files, diagnostics):
        directory, paths = self.write_sources(name, files)
        for target in ('native', 'vm'):
            for level in (0, 2):
                for previous in (None, b'published router\x00must survive a failed build'):
                    with self.subTest(program=name, target=target, level=level,
                                      previous_output=previous is not None):
                        output = directory / f'program-{target}-o{level}-{previous is not None}'
                        if previous is not None:
                            output.write_bytes(previous)
                        flags = ['target', 'vm'] if target == 'vm' else []
                        result = subprocess.run([str(COMPILER), '--linker', str(LINKER),
                                                 *map(str, paths.values()), f'-O{level}', *flags,
                                                 '-o', str(output)], cwd=self.work, env=self.env,
                                                capture_output=True, text=True, timeout=60)
                        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                        self.assertNotIn('E_PARSE', result.stderr)
                        reported = re.findall(r':\d+:\d+: E_[A-Z_]+:', result.stderr)
                        self.assertEqual(len(reported), len(diagnostics), result.stderr)
                        for filename, marker, codes in diagnostics:
                            source = files[filename]
                            position = source.rindex(marker)
                            line = source[:position].count('\n') + 1
                            column = len(source[:position].rsplit('\n', 1)[-1]) + 1
                            prefix = re.escape(f'{paths[filename]}:{line}:{column}: ')
                            self.assertRegex(result.stderr, prefix + '(?:' + '|'.join(codes) + '):')
                        if previous is None:
                            self.assertFalse(output.exists(), 'a rejected source created an artifact')
                        else:
                            self.assertEqual(output.read_bytes(), previous)

    def test_file_private_router_records_keep_their_original_layouts_and_names(self):
        self.execute_targets('file-private-records', {
            'left.krt': '''
namespace Routes;
private struct Header{int32 status;byte tag[2];}
private Header Copy(Header source){return source;}
public int32 Left(){
    Header header=default(Header);header.status=17;header.tag[0]=3;
    Header saved=Copy(header);saved.status=99;saved.tag[0]=9;
    if(header.status!=17 || header.tag[0]!=3 || sizeof(Header)!=8){return -1;}
    return Copy(header).status;
}
''',
            'right.krt': '''
namespace Routes;
private struct Header{int64 status;byte tag[3];}
private Header Copy(Header source){return source;}
public int32 Right(){
    Header header=default(Header);header.status=25;header.tag[0]=5;
    Header saved=Copy(header);saved.status=99;saved.tag[0]=9;
    if(header.status!=25 || header.tag[0]!=5 || sizeof(Header)!=16){return -1;}
    return (int32)Copy(header).status;
}
''',
            'main.krt': '''
int32 main(){
    if(Routes.Left()+Routes.Right()!=42){return 1;}
    if(Routes.Right()+Routes.Left()!=42){return 2;}
    return 0;
}
''',
        })

    def test_closed_generic_sessions_preserve_fields_callbacks_and_response_copies(self):
        self.execute_targets('generic-responses', {'main.krt': '''
struct Record{int32 status;int64 length;}
struct Response<T>{
    T value;byte tag[2];
    Response(T item){value=item;tag[0]=7;tag[1]=11;}
    Response<T> Snapshot(){return this;}
}
class Session<T>{
    public T current;
    Session(T value){current=value;}
    T Read(){return current;}
    static T Forward(T value){return value;}
}
Response<T> Respond<T>(T value){return new Response<T>(value);}
int32 main(){
    Session<int8> small=new Session<int8>((int8)-7);
    Session<uint66> wide=new Session<uint66>((uint66)-1);
    Session<float32> fraction=new Session<float32>((float32)1.25);
    Session<string> text=new Session<string>("ready");
    Record record=default(Record);record.status=42;record.length=4294967313;
    Session<Record> values=new Session<Record>(record);
    fn(int8)->int8 narrow=&Session<int8>.Forward;
    fn(uint66)->uint66 full=&Session<uint66>.Forward;
    if(narrow(small.Read())!=-7 || (uint128)full(wide.Read())!=73786976294838206463){return 1;}
    if(fraction.Read()!=1.25 || text.Read()!="ready"){return 2;}
    Response<int8> first=Respond(small.Read());
    Response<uint66> second=Respond(wide.Read());
    Response<Record> third=Respond(values.Read());
    Response<Record> saved=third.Snapshot();saved.value.status=59;saved.tag[0]=13;
    Record from_field=values.current;from_field.status=71;
    if(first.value!=-7 || (uint128)second.value!=73786976294838206463){return 3;}
    if(third.value.status!=42 || third.value.length!=4294967313 || third.tag[0]!=7){return 4;}
    if(saved.value.status!=59 || saved.tag[0]!=13 || values.Read().status!=42){return 5;}
    small.current=(int8)-9;fraction.current=(float32)2.5;
    if(small.Read()!=-9 || fraction.Read()!=2.5 || text.Read()!="ready"){return 6;}
    delete small;delete wide;delete fraction;delete text;delete values;return 0;
}
'''})
        # Resolving Graph's reference layout creates Deferred<int32> without
        # finishing its base hierarchy. Reading the child must resolve it.
        self.execute_targets('forward-inheritance', {'main.krt': '''class Base<T>{public int32 marker=42;}
class Deferred<T>:Base<T>{}
class Graph<T>{public Deferred<T> child;}
int32 Read(Graph<int32> graph){return graph.child.marker;}
int32 main(){
    Graph<int32> graph=new Graph<int32>();Deferred<int32> child=new Deferred<int32>();
    graph.child=child;if(Read(graph)!=42){return 1;}
    delete child;delete graph;return 0;
}
'''})
        # A settled Box can still contain an unresolved generic interface
        # argument. Every function body must bind its inherited method.
        self.execute_targets('nested-generic-inheritance', {'main.krt': '''interface IBase<T>{int32 Read();}
interface IDerived<T>:IBase<T>{}
class Box<T>{public T item;Box(){}T Get(){return item;}}
class Graph<T>{public Box<IDerived<T>> container;}
class Later<T>{
    public Box<IDerived<T>> container;
    Later(){}
    int32 Check(){return container.Get().Read();}
}
int32 Warm(Graph<int32> graph){return 0;}
int32 main(){Later<int32> later=new Later<int32>();delete later;return 0;}
'''})
        # A reference field can be laid out before its target's base prefix.
        # Inferred factories must allocate the final target payload size.
        self.execute_targets('field-inferred-allocation-size', {'main.krt': '''class Holder{public Child current;}
class Base{public int64 prefix;public Base(){}}
class Child:Base{public int64 tail;public Child(){}}
T CopyNew<T>(T item)where T:new(){return new T();}
int32 main(){
    Holder holder=new Holder();Child sample=new Child();holder.current=sample;
    Child inferred=CopyNew(holder.current);
    int32 result=0;
    unsafe(using krt.mem;){
        int64 direct_bytes=((int64*)(int64)sample)[-2];
        int64 inferred_bytes=((int64*)(int64)inferred)[-2];
        if(direct_bytes!=24){result=1;}
        else if(inferred_bytes!=direct_bytes){result=(int32)inferred_bytes;}
    }
    delete inferred;delete sample;delete holder;return result;
}
'''})

    def test_recursive_nullable_elements_and_high_pointer_layers_keep_their_types(self):
        high = 'int32?' + '*' * 64
        inner = 'int32*?' + '*' * 63
        source = '''
int32 ReadOptional(ref int32*? pointer){unsafe(using krt.mem;){
    if(pointer is int32* live){return *live;}return 0;
}}
int32 ReadSlots(int32*?*[] slots,fn(ref int32*?)->int32 reader){unsafe(using krt.mem;){
    if(slots.Length!=2){return -1;}
    return reader(ref slots[0][0])+reader(ref slots[1][0]);
}}
struct Registry{int32*?*[] slots;fn(ref int32*?)->int32 reader;}
class Relay<T>{static T Forward(T value){return value;}}
HIGH High(HIGH value){return Relay<HIGH>.Forward(value);}
INNER Inner(INNER value){return Relay<INNER>.Forward(value);}
int32 main(){unsafe(using krt.mem;){
    int32 value=17;int32*? present=&value;int32*? missing=null;
    int32*?*[] slots=[&present,&missing];Registry registry=default(Registry);
    registry.slots=slots;registry.reader=&ReadOptional;
    if(ReadSlots(slots,&ReadOptional)!=17 || ReadSlots(registry.slots,registry.reader)!=17){return 1;}
    value=25;if(ReadSlots(registry.slots,registry.reader)!=25){return 2;}
    present=null;if(ReadSlots(slots,&ReadOptional)!=0){return 3;}
    HIGH first=(HIGH)0;INNER second=(INNER)0;
    HIGH high=High(first);INNER inner=Inner(second);
    if(high!=first || inner!=second || High(first)!=first){return 4;}
    delete slots;return 0;
}}
'''.replace('HIGH', high).replace('INNER', inner)
        self.execute_targets('recursive-nullable-shapes', {'main.krt': source})

    def test_known_callback_names_and_struct_receiver_refs_remain_independent(self):
        self.execute_targets('callback-names-and-receivers', {'main.krt': '''
static int32 trace=0;
int32 Next(int32 value){trace=trace*10+value;return value;}
int32 Pack(int32 high,int32 low){return high*10+low;}
int32 Route(int32 route,int32 status){return route*10+status;}
struct Counter{
    int32 total;
    int32 Advance(int32 amount){total+=amount;return total;}
}
int32 main(){
    var packet=&Pack;var route=&Route;
    if(packet(low:Next(2),high:Next(4))!=42 || trace!=24){return 1;}
    trace=0;if(route(status:Next(3),route:Next(5))!=53 || trace!=35){return 2;}
    if(packet(high:6,low:7)!=67 || (&Route)(status:9,route:8)!=89){return 3;}
    Counter counter=default(Counter);counter.total=17;
    var advance=&Counter.Advance;
    if(advance(ref counter,amount:5)!=22 || counter.total!=22){return 4;}
    fn(ref Counter,int32)->int32 anonymous=&Counter.Advance;
    if(anonymous(ref counter,3)!=25 || counter.total!=25){return 5;}
    if(advance(ref counter,amount:17)!=42 || counter.total!=42){return 6;}
    return 0;
}
'''})

    def test_warmed_signatures_do_not_grant_other_files_or_receivers_access(self):
        self.reject_targets('file-private-signatures', {
            'trusted.krt': '''
private class Credential{public int32 code=42;}
public Credential Create(){return new Credential();}
public int32 Validate(Credential value){return value==null?0:value.code;}
public int32 Warm(){
    Credential value=Create();int32 result=Validate(value);delete value;return result;
}
''',
            'caller.krt': '''
int32 Read(){var value=Create();return 0;}
int32 Check(){return Validate(null);}
int32 main(){return 0;}
''',
        }, [('caller.krt', 'Create();', ('E_ACCESS',)),
            ('caller.krt', 'Validate(null);', ('E_ACCESS',))])
        self.reject_targets('member-signatures', {'main.krt': '''
class Vault{
    private int32 secret=17;
    private int32 Read(int32 value){return secret+value;}
    public int32 Warm(){return Read(25);}
}
class Parent{
    protected int32 token=25;
    protected int32 Encode(int32 value){return token+value;}
}
class Child:Parent{
    public int32 Warm(Child other){return base.Encode(other.token);}
    public int32 WrongField(Parent other){return other.token;}
    public int32 WrongCall(Parent other){return other.Encode(17);}
}
class Stranger{
    public int32 WrongField(Vault value){return value.secret;}
    public int32 WrongCall(Vault value){return value.Read(25);}
}
int32 main(){return 0;}
'''}, [('main.krt', 'token;', ('E_ACCESS',)),
       ('main.krt', 'Encode(17);', ('E_ACCESS',)),
       ('main.krt', 'secret;', ('E_ACCESS',)),
       ('main.krt', 'Read(25);', ('E_ACCESS',))])

    def test_reused_shapes_still_reject_anonymous_names_ref_and_nullable_mismatches(self):
        cases = {
            'anonymous-names': ('''
int32 Pack(int32 high,int32 low){return high*10+low;}
int32 Warm(){var known=&Pack;return known(low:2,high:4);}
int32 main(){fn(int32,int32)->int32 callback=&Pack;return callback(low:2,high:4);}
''', 'low:2,high:4);', ('E_ARGUMENT_NAME',)),
            'receiver-ref': ('''
struct Counter{int32 total;int32 Advance(int32 amount){total+=amount;return total;}}
int32 Warm(){Counter value=default(Counter);var known=&Counter.Advance;return known(ref value,amount:17);}
int32 main(){Counter value=default(Counter);var known=&Counter.Advance;return known(value,amount:25);}
''', 'value,amount:25);', ('E_WRITABLE_REF',)),
            'recursive-nullability': ('''
int32 Inspect(int32*?*[] slots){return 0;}
int32 Warm(){unsafe(using krt.mem;){int32*? pointer=null;return Inspect([&pointer]);}}
int32 main(){unsafe(using krt.mem;){
    int32 value=17;int32* pointer=&value;int32**? outer=&pointer;
    int32**?[] slots=[outer];return Inspect(slots);
}}
''', 'Inspect(slots);', ('E_UNDEFINED_METHOD',)),
        }
        # A ref actual adds one address layer. Its pointee can therefore use
        # 63 layers, including the highest nullable bit available below ref.
        high = 'int32?' + '*' * 63
        inner = 'int32*?' + '*' * 62
        deep = '''
class Router<T>{static int32 Inspect(ref T value){return 0;}}
int32 Warm(){unsafe(using krt.mem;){HIGH value=(HIGH)0;return Router<HIGH>.Inspect(ref value);}}
int32 main(){unsafe(using krt.mem;){INNER value=(INNER)0;return Router<HIGH>.Inspect(ref value);}}
'''.replace('HIGH', high).replace('INNER', inner)
        cases['high-nullability-ref'] = (deep, f'Router<{high}>.Inspect(ref value);', ('E_NOT_NULLABLE',))
        for name, (source, marker, codes) in cases.items():
            self.reject_targets(name, {'main.krt': source}, [('main.krt', marker, codes)])


if __name__ == '__main__':
    import unittest
    unittest.main()
