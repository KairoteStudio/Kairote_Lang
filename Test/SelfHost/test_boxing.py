"""Typed value boxes, checked copies and object/interface runtime contracts."""
import re
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


POSITIVE_CASES = {
    'root-object-construction-defaults-and-reference-identity': '''
T Create<T>() where T:class,new(){return new T();}
int32 main(){
    object first=new object();object second=new object();object alias=first;
    object generic=Create<object>();
    object absent=default(object);
    if(first==null||second==null||generic==null||first==second||first==generic||first!=alias||absent!=null){return 1;}
    if(!(first is object)||first is int32||first is string){return 2;}
    if(first is object copied){if(copied!=first){return 3;}}else{return 4;}
    int32 caught=0;try{int32 wrong=(int32)first;}catch(int32 fault){caught=fault;}
    delete first;delete second;delete generic;return caught==-2147483647?0:5;
}
''',
    'integer-precision-and-boolean-character-identity': '''
int32 main(){
    object small=(int8)-7;object bits=(uint14)8191;object flag=true;object letter=(char)65;
    if((int8)small!=-7 || (uint14)bits!=8191 || !(bool)flag || (char)letter!=65){return 1;}
    if(!(small is int8) || small is int16 || bits is uint16 || flag is uint8 || letter is uint8){return 2;}
    if(small is int8 value && value!=-7){return 3;}
    delete small;delete bits;delete flag;delete letter;return 0;
}
''',
    'floating-and-wide-integer-values': '''
int32 main(){
    object small=(float32)1.25;object large=(float64)-9.5;
    object unsigned_wide=((uint128)1<<117)+19;object signed_wide=-((int128)1<<103)+7;
    if((float32)small!=1.25 || (float64)large!=-9.5){return 1;}
    if((uint128)unsigned_wide!=((uint128)1<<117)+19 || (int128)signed_wide!=-((int128)1<<103)+7){return 2;}
    if(small is float64 || large is float32 || unsigned_wide is int128 || signed_wide is uint128){return 3;}
    if(unsigned_wide is uint128 value && value!=((uint128)1<<117)+19){return 4;}
    delete small;delete large;delete unsigned_wide;delete signed_wide;return 0;
}
''',
    'mixed-wide-inference-conditionals-and-callback-boxes': '''
uint128 Choose(bool first,uint128 value){return first?7:value;}
int32 main(){
    uint128 large=((uint128)1<<117)+19;bool first=false;
    var inferred=first?7:large;object boxed=inferred;
    object promoted=(int64)7+large;
    fn()->uint128 callback=function()=>first?7:large;object wrapped=callback;
    fn()->uint128 restored=(fn()->uint128)wrapped;
    if((uint128)boxed!=large||(uint128)promoted!=large+7||Choose(false,large)!=large||
        Choose(true,large)!=7||restored()!=large||inferred!=large){return 1;}
    delete boxed;delete promoted;delete wrapped;delete callback;return 0;
}
''',
    'class-field-slots-preserve-declared-box-precision': '''
class Fields{public int8 small;public char letter;public float32 weight;public uint14 bits;public uint128 wide;}
int32 main(){
    Fields source=new Fields();source.small=-7;source.letter=(char)65;source.weight=(float32)1.25;source.bits=8191;source.wide=((uint128)1<<103)+11;
    object small=source.small;object letter=source.letter;object weight=source.weight;object bits=source.bits;object wide=source.wide;
    if((int8)small!=-7 || (char)letter!=65 || (float32)weight!=1.25 || (uint14)bits!=8191 || (uint128)wide!=((uint128)1<<103)+11){return 1;}
    if(small is int64 || letter is uint8 || weight is float64 || bits is uint16 || wide is int128){return 2;}
    delete small;delete letter;delete weight;delete bits;delete wide;delete source;return 0;
}
''',
    'enum-identity-is-separate-from-underlying-integer': '''
enum Status{Ready=7,Finished=11}enum Mode{Ready=7,Finished=11}
int32 main(){
    object boxed=Status.Ready;
    if((Status)boxed!=Status.Ready || !(boxed is Status) || boxed is Mode || boxed is int64){return 1;}
    int32 caught=0;try{Mode wrong=(Mode)boxed;}catch(int32 fault){caught=fault;}
    delete boxed;return caught==-2147483647?0:2;
}
''',
    'struct-fixed-fields-and-unboxed-copies': '''
struct Details{bool ready;float32 ratio;int32 counts[3];}
struct Header{int16 status;byte tag[5];uint128 length;Details details;}
object Make(){Header value=default(Header);value.status=201;value.tag[0]=7;value.tag[4]=19;value.length=((uint128)1<<103)+11;value.details.ready=true;value.details.ratio=1.25;value.details.counts[2]=37;return value;}
int32 main(){
    object boxed=Make();Header first=(Header)boxed;Header second=(Header)boxed;
    first.status=500;first.tag[4]=99;first.length=0;first.details.ready=false;first.details.counts[2]=99;
    if(second.status!=201 || second.tag[0]!=7 || second.tag[4]!=19 || second.length!=((uint128)1<<103)+11){return 1;}
    if(!second.details.ready || second.details.ratio!=1.25 || second.details.counts[2]!=37){return 4;}
    if(boxed is Header matched_copy){matched_copy.tag[0]=31;Header again=(Header)boxed;if(again.tag[0]!=7){return 2;}}
    else{return 3;}delete boxed;return 0;
}
''',
    'static-value-patterns-box-only-successful-binding': '''
interface ICounter{int32 Add(int32 amount);}struct Counter:ICounter{int32 value;public int32 Add(int32 amount){value+=amount;return value;}}
int32 main(){
    int32 number=42;if(!(number is object)){return 1;}
    if(number is object boxed){if((int32)boxed!=42){return 2;}delete boxed;}else{return 3;}
    Counter source=default(Counter);source.value=7;
    if(!(source is ICounter)){return 4;}
    if(source is ICounter face){if(face.Add(5)!=12 || source.value!=7){return 5;}delete face;}else{return 6;}
    string missing=null;if(missing is object boxed){return 7;}return 0;
}
''',
    'pure-stack-struct-input-is-copied-into-independent-box': '''
struct Point{int32 value;byte tag[2];}
object Make(){unsafe(using krt.mem;){Point* values=stackalloc Point[1];values[0].value=42;values[0].tag[1]=7;return values[0];}}
int32 main(){object boxed=Make();Point copy=(Point)boxed;delete boxed;return copy.value==42 && copy.tag[1]==7?0:1;}
''',
    'mutable-struct-interface-preserves-box-and-source-copy': '''
interface ICounter{int32 Add(int32 amount);int32 Read();}
struct Counter:ICounter{public int32 value;public int32 Add(int32 amount){value+=amount;return value;}public int32 Read(){return value;}}
int32 main(){
    Counter source=default(Counter);source.value=7;ICounter boxed=source;ICounter alias=boxed;
    if(boxed.Add(5)!=12 || alias.Add(2)!=14 || boxed.Read()!=14 || source.value!=7){return 1;}
    Counter copy=(Counter)boxed;copy.value=99;if(boxed.Read()!=14){return 2;}
    object erased=boxed;ICounter restored=(ICounter)erased;if(restored.Add(3)!=17 || boxed.Read()!=17){return 3;}
    if(boxed is Counter matched){matched.value=42;if(boxed.Read()!=17){return 4;}}else{return 5;}
    delete boxed;return 0;
}
''',
    'inherited-multiple-interface-methods-and-overloads': '''
interface IBase{int32 Read();}interface IExtended:IBase{int32 Read(int32 factor);}
interface ITag{int32 Tag();}
struct Row:IExtended,ITag{int32 value;public int32 Read(){return value;}public int32 Read(int32 factor){return value*factor;}public int32 Tag(){return value+1;}}
int32 main(){
    Row source=default(Row);source.value=7;IExtended extended=source;IBase parent=extended;
    object erased=extended;ITag tag=(ITag)erased;
    if(parent.Read()!=7 || extended.Read(6)!=42 || tag.Tag()!=8){return 1;}
    if(!(erased is IBase) || !(erased is IExtended) || !(erased is ITag)){return 2;}
    delete extended;return 0;
}
''',
    'class-object-upcasts-downcasts-and-virtual-dispatch': '''
interface IValue{int32 Read();}class Base{public int32 value;public virtual int32 Read(){return value;}}
class Derived:Base,IValue{public override int32 Read(){return value+1;}}class Other{public int32 value;}
int32 main(){
    Derived source=new Derived();source.value=41;object erased=source;
    Base parent=(Base)erased;Derived exact=(Derived)erased;IValue face=(IValue)erased;
    IValue through_parent=(IValue)parent;
    Derived down=(Derived)parent;
    if(parent.Read()!=42 || exact.Read()!=42 || face.Read()!=42 || through_parent.Read()!=42 || down.Read()!=42){return 1;}
    if(!(erased is Base) || !(erased is Derived) || !(erased is IValue) || erased is Other){return 2;}
    exact.value=7;if(source.value!=7){return 3;}
    Base missing_base=null;IValue absent_face=(IValue)missing_base;if(absent_face!=null){return 4;}
    Base plain=new Base();int32 caught=0;try{IValue wrong=(IValue)plain;}catch(int32 fault){caught=fault;}
    delete plain;if(caught!=-2147483647){return 5;}
    object absent=null;Base missing=(Base)absent;if(missing!=null || absent is Base){return 4;}
    delete erased;return 0;
}
''',
    'checked-mismatches-and-finally-unwind-without-payload-read': '''
struct First{byte data[5];}struct Second{uint128 data;}
int32 main(){
    object boxed=(int32)7;int32 faults=0;int32 cleanup=0;
    try{int64 wrong=(int64)boxed;}catch(int32 fault){if(fault!=-2147483647){return 1;}faults++;}finally{cleanup++;}
    try{Second wrong=(Second)boxed;}catch(int32 fault){if(fault!=-2147483647){return 2;}faults++;}finally{cleanup++;}
    object absent=null;try{int32 wrong=(int32)absent;}catch(int32 fault){faults++;}finally{cleanup++;}
    if(boxed is Second wrong){return 3;}if(absent is First wrong){return 4;}
    delete boxed;return faults==3 && cleanup==3?0:5;
}
''',
    'generic-boxing-unboxing-and-nominal-arguments': '''
struct Record<T>{T value;byte tag[2];}
object Box<T>(T value){return value;}T Unbox<T>(object value){return (T)value;}
int32 main(){
    object number=Box((int16)-19);if(Unbox<int16>(number)!=-19){return 1;}
    Record<int32> first=default(Record<int32>);first.value=42;first.tag[1]=7;
    object boxed=Box(first);Record<int32> copy=Unbox<Record<int32>>(boxed);
    if(copy.value!=42 || copy.tag[1]!=7 || boxed is Record<int64>){return 2;}
    copy.tag[1]=99;Record<int32> again=Unbox<Record<int32>>(boxed);if(again.tag[1]!=7){return 3;}
    delete number;delete boxed;return 0;
}
''',
    'generic-struct-interface-and-direct-constrained-calls': '''
interface IValue<T>{T Read();}struct Holder<T>:IValue<T>{T value;public T Read(){return value;}}
T Read<T,U>(U value)where U:IValue<T>{return value.Read();}
int32 main(){
    Holder<int32> source=default(Holder<int32>);source.value=42;
    if(Read<int32,Holder<int32>>(source)!=42){return 1;}
    IValue<int32> face=source;object boxed=face;IValue<int32> restored=(IValue<int32>)boxed;
    if(restored.Read()!=42 || boxed is IValue<int64> || !(boxed is Holder<int32>)){return 2;}
    delete face;return 0;
}
''',
    'ref-and-readonly-input-copy-with-overload-ranking': '''
struct Row{int32 value;}void Change(ref Row value){value.value=99;}
object Copy(Row value){return value;}
object Snapshot(ref Row value){return value;}
int32 Select(int32 value){return 1;}int32 Select(object value){return 2;}
int32 main(){
    Row source=default(Row);source.value=7;let read_only=source;object boxed=Copy(read_only);object ref_copy=Snapshot(ref source);Change(ref source);
    Row copy=(Row)boxed;Change(ref copy);Row again=(Row)boxed;
    Row snapshot=(Row)ref_copy;
    if(source.value!=99 || again.value!=7 || snapshot.value!=7 || Select(42)!=1 || Select(boxed)!=2){return 1;}
    delete boxed;delete ref_copy;return 0;
}
''',
    'array-string-wrapper-shape-and-shallow-reference-ownership': '''
int32 main(){
    int32[] source=new int32[2];source[0]=7;source[1]=11;object values=source;object text="hello";
    int32[] alias=(int32[])values;alias[1]=19;
    if(source[1]!=19 || (string)text!="hello" || !(values is int32[]) || values is int64[] || text is int32[]){return 1;}
    string missing=null;object absent=missing;string restored=(string)absent;
    if(absent!=null || restored!=null || absent is string){return 2;}
    delete values;delete text;delete source;return 0;
}
''',
    'reference-identity-and-independent-value-boxes': '''
class Item{int32 value;}
int32 main(){
    object number=(int32)7;object same=number;object other=(int32)7;
    if(number!=same || number==other){return 1;}
    Item item=new Item();object first_class=item;object second_class=item;
    if(first_class!=second_class || first_class!=item){return 2;}
    int32[] array=new int32[1];array[0]=7;int32[] distinct=new int32[1];distinct[0]=7;
    object first_array=array;object second_array=array;object other_array=distinct;
    if(first_array!=second_array || first_array!=array || first_array==other_array){return 3;}
    string text="same storage";object first_string=text;object second_string=text;
    if(first_string!=second_string || first_string!=text){return 4;}
    delete number;delete other;delete item;delete first_array;delete second_array;delete other_array;
    delete first_string;delete second_string;delete array;delete distinct;return 0;
}
''',
    'callback-wrapper-retains-shape-environment-and-reference-identity': '''
int32 AddSeven(int32 value){return value+7;}
fn(int32)->int32 Counter(int32 seed){return function(int32 amount)=>{seed+=amount;return seed;};}
object Keep<T>(T value){return value;}
T? Missing<T>(T prototype){return null;}T? UnboxMaybe<T>(object value,T prototype){return (T?)value;}
T? Present<T>(T value){return value;}
int32 main(){
    fn(int32)->int32 free=&AddSeven;var callback=Counter(7);
    object first=Keep(callback);object second=callback;object free_box=free;
    var restored=(fn(int32)->int32)first;var free_copy=(fn(int32)->int32)free_box;
    if(restored(5)!=12 || callback(2)!=14 || free_copy(35)!=42 || first!=second || first!=callback){return 1;}
    if(!(first is fn(int32)->int32) || first is fn(string)->int32){return 2;}
    int32 caught=0;try{fn(string)->int32 wrong=(fn(string)->int32)first;}catch(int32 fault){caught=fault;}
    if(caught!=-2147483647){return 3;}
    object absent=Missing(callback);var maybe=UnboxMaybe(absent,callback);
    if(absent!=null || maybe!=null || absent is fn(int32)->int32){return 5;}
    object present=Present(callback);var recovered=(fn(int32)->int32)present;
    if(recovered(3)!=17){return 6;}delete present;
    delete first;delete second;delete free_box;if(callback(3)!=20){return 4;}delete callback;return 0;
}
''',
    'object-fields-arrays-return-and-null-coalescing': '''
class Holder{public object value;}object Make(int32 value){return value;}
int32 main(){
    Holder holder=new Holder();holder.value=Make(7);object[] values=new object[2];values[0]=holder.value;values[1]=Make(11);
    object absent=null;object selected=absent??values[1];
    if((int32)values[0]!=7 || (int32)selected!=11 || !(values[1] is object)){return 1;}
    delete holder.value;delete values[1];delete values;delete holder;return 0;
}
''',
    'contextual-branch-and-coalescing-value-conversions': '''
struct Row{int32 value;}class Node{public int32 value;}object Choose(bool row,Row value){return row?value:19;}
int32 main(){
    Row source=default(Row);source.value=42;
    object first=true?source:19;object second=false?source:19;object absent=null;object fallback=absent??42;
    object returned=Choose(true,source);
    var right_erased=false?7:second;var left_erased=true?second:7;
    Node missing=null;var coalesced=missing??second;
    Node node=new Node();var inferred=true?node:second;
    if(right_erased!=second||left_erased!=second||coalesced!=second||inferred!=node){return 3;}
    if(!(first is Row) || (int32)second!=19 || (int32)fallback!=42){return 1;}
    Row copy=(Row)returned;if(copy.value!=42){return 2;}
    delete first;delete second;delete fallback;delete returned;delete node;return 0;
}
''',
    'escaping-box-in-closure-and-pattern-scope-copy': '''
struct Row{int32 value;byte tag[3];}
fn()->int32 Keep(){Row row=default(Row);row.value=7;row.tag[2]=11;object boxed=row;return function()=>{Row value=(Row)boxed;int32 result=value.value+value.tag[2];delete boxed;return result;};}
int32 main(){var callback=Keep();int32 value=callback();delete callback;return value==18?0:1;}
''',
    'contextual-conditional-and-lambda-object-arguments': '''
struct Row{int32 value;}
int32 Inspect(object value){int32 result=0;if(value is Row row){result=row.value;}else if(value is int32 number){result=number;}delete value;return result;}
int32 Select(fn(int32)->int32 callback){int32 value=callback(5);delete callback;return value;}
int32 Select(object boxed){fn(int32)->int32 callback=(fn(int32)->int32)boxed;int32 value=callback(5);delete boxed;delete callback;return value+100;}
object Echo<T>(T value){return value;}
int32 main(){Row row=default(Row);row.value=41;bool flag=true;
    if(Inspect(flag?row:19)!=41||Inspect(!flag?row:19)!=19){return 1;}
    object absent=null;if(Inspect(absent??42)!=42){return 2;}
    object boxed=function(int32 number)=>number+37;fn(int32)->int32 callback=(fn(int32)->int32)boxed;
    if(callback(5)!=42||Select(function(int32 number)=>number+37)!=42){return 3;}
    delete boxed;delete callback;
    object selected=Echo<object>(flag?row:19);Row copied=(Row)selected;delete selected;
    return copied.value==41?0:4;
}
''',
    'boxed-typed-exception-payloads-and-rethrow': '''
struct Failure{int32 code;byte detail[3];}
void Raise(object value){try{throw value;}finally{}}
int32 main(){
    object number=(int32)19;int32 caught=0;
    try{Raise(number);}catch(int32 value){caught=value;}
    if(caught!=19){return 1;}
    Failure original=default(Failure);original.code=503;original.detail[2]=7;object boxed=original;
    int32 code=0;try{Raise(boxed);}catch(Failure value){code=value.code+value.detail[2];value.detail[2]=99;}
    Failure unchanged=(Failure)boxed;if(code!=510 || unchanged.detail[2]!=7){return 2;}
    int32 restored=0;try{throw 42;}catch(object value){restored=(int32)value;delete value;}
    try{try{Raise(number);}catch(int32 value){throw;}}catch(int32 value){caught=value;}
    int32 exact=0;try{throw (int14)-19;}catch(int16 wrong){return 4;}catch(int14 value){exact=value;}
    if(exact!=-19){return 5;}
    int32 character=0;try{throw (char)65;}catch(uint8 wrong){return 6;}catch(char value){character=value;}
    if(character!=65){return 7;}
    delete number;delete boxed;return restored==42 && caught==19?0:3;
}
''',
    'boxed-struct-request-response-routing': '''
struct Request{int32 method;byte route[4];}struct Response{int32 status;byte tag[6];uint128 bytes;}
interface IRule{Response Handle(Request request);int32 Requests();}
struct Rule:IRule{int32 count;public Response Handle(Request request){count++;Response response=default(Response);response.status=request.method==1?200:405;response.tag[0]=request.route[0];response.tag[5]=(byte)count;response.bytes=((uint128)1<<99)+request.route[3];return response;}public int32 Requests(){return count;}}
int32 main(){
    Rule local=default(Rule);IRule handler=local;Request request=default(Request);request.method=1;request.route[0]=47;request.route[3]=7;
    Response first=handler.Handle(request);request.method=2;Response second=handler.Handle(request);object saved=first;
    first.tag[0]=99;Response copied=(Response)saved;
    if(copied.status!=200 || copied.tag[0]!=47 || copied.tag[5]!=1 || copied.bytes!=((uint128)1<<99)+7){return 1;}
    if(second.status!=405 || second.tag[5]!=2 || handler.Requests()!=2 || local.count!=0){return 2;}
    delete saved;delete handler;return 0;
}
''',
    'manual-owned-box-churn-and-catchable-conversion-failures': '''
interface ICounter{int32 Add(int32 amount);}struct Counter:ICounter{int32 value;public int32 Add(int32 amount){value+=amount;return value;}}
int32 Check(int32 seed){
    object scalar=seed;if((int32)scalar!=seed){return 1;}
    Counter source=default(Counter);source.value=seed;ICounter face=source;
    if(face.Add(7)!=seed+7 || source.value!=seed){return 2;}
    Counter copy=(Counter)face;if(copy.value!=seed+7){return 3;}
    int32 caught=0;try{int64 wrong=(int64)scalar;}catch(int32 fault){caught=fault;}
    delete scalar;delete face;return caught==-2147483647?0:4;
}
int32 main(){for(int32 index=0;index<2048;index++){int32 status=Check(index);if(status!=0){return status;}}return 0;}
''',
}


NEGATIVE_CASES = {
    'object-references-have-no-unary-integer-arithmetic': ('int32 main(){object value=7;object broken=-value;return 0;}', 'value;return', 'E_LOWER'),
    'object-and-scalar-have-no-implicit-address-equality': ('int32 main(){object value=7;bool result=value==7;delete value;return 0;}', 'value==7', 'E_LOWER'),
    'object-references-have-no-implicit-address-ordering': ('int32 main(){object first=7;object second=9;bool result=first<second;delete first;delete second;return 0;}', 'first<second', 'E_LOWER'),
    'root-object-constructor-has-no-value-parameters': ('int32 main(){object value=new object(7);return 0;}', 'new object', 'E_LOWER'),
    'implicit-unboxing': ('int32 main(){object value=7;int32 result=value;return 0;}', 'int32 result', 'E_LOWER'),
    'ref-boxing-is-not-a-reference-conversion': ('void Take(ref object value){}int32 main(){int32 value=7;Take(ref value);return 0;}', 'Take(ref value)', 'E_UNDEFINED_METHOD'),
    'pointer-contract-cannot-be-erased': ('int32 main(){unsafe(using krt.mem;){int32 value=7;int32* pointer=&value;object boxed=pointer;return 0;}}', 'object boxed', 'E_LOWER'),
    'ref-callable-boxing-is-not-a-reference-conversion': ('void Take(ref object value){}int32 Read(){return 7;}int32 main(){fn()->int32 callback=&Read;Take(ref callback);return 0;}', 'Take(ref callback)', 'E_UNDEFINED_METHOD'),
    'missing-struct-interface-member': ('interface I{int32 Read();}struct Row:I{int32 value;}int32 main(){return 0;}', 'struct Row', 'E_LOWER'),
    'wrong-struct-interface-return': ('interface I{int32 Read();}struct Row:I{public int64 Read(){return 7;}}int32 main(){return 0;}', 'struct Row', 'E_LOWER'),
    'unrelated-struct-interface': ('interface I{}struct Row{}int32 main(){I value=default(Row);return 0;}', 'I value', 'E_LOWER'),
    'ref-unbox-cast-is-not-an-lvalue': ('void Change(ref int32 value){}int32 main(){object boxed=7;Change(ref (int32)boxed);return 0;}', '(int32)boxed', 'E_LOWER'),
    'boxing-does-not-erase-known-stack-borrow': ('struct Borrow{int32* pointer;}fn()->int32 Keep(){unsafe(using krt.mem;){int32 local=7;Borrow row=default(Borrow);row.pointer=&local;object boxed=row;return function()=>{Borrow copy=(Borrow)boxed;return *copy.pointer;};}}int32 main(){return 0;}', 'boxed;return *copy', 'E_CAPTURE_BORROW'),
}


class BoxingTests(NativeCompilerFixture):
    def execute_targets(self, name):
        path = self.work / (name + '.krt')
        path.write_text(POSITIVE_CASES[name], encoding='utf-8')
        for target in ('native', 'vm'):
            for level in range(4):
                with self.subTest(program=name, target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env, capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b'')
                    self.assertEqual(result.stderr, b'')

    def test_reject_invalid_implicit_boxing_borrow_and_reference_contracts(self):
        for name, (source, marker, code) in NEGATIVE_CASES.items():
            path = self.work / (name + '.krt')
            path.write_text(source, encoding='utf-8')
            position = source.index(marker)
            line = source.count('\n', 0, position) + 1
            column = position - source.rfind('\n', 0, position)
            for target in ('native', 'vm'):
                for level in range(4):
                    for previous in (None, b'previous box artifact\x00'):
                        with self.subTest(program=name, target=target, optimization=level, existing=previous is not None):
                            output = self.work / f'{name}-{target}-o{level}-{previous is not None}'
                            if previous is not None:
                                output.write_bytes(previous)
                            flags = ['target', 'vm'] if target == 'vm' else []
                            argv = [str(COMPILER), '--linker', str(LINKER), str(path), f'-O{level}', *flags, '-o', str(output)]
                            first = subprocess.run(argv, cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
                            second = subprocess.run(argv, cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
                            self.assertEqual(first.returncode, 1, first.stdout + first.stderr)
                            self.assertEqual(second.returncode, 1, second.stdout + second.stderr)
                            self.assertEqual(first.stderr, second.stderr)
                            self.assertEqual(re.findall(r':\d+:\d+: E_[A-Z_]+:', first.stderr), [f':{line}:{column}: {code}:'], first.stderr)
                            if previous is None:
                                self.assertFalse(output.exists())
                            else:
                                self.assertEqual(output.read_bytes(), previous)
                            self.assertFalse(output.with_name(output.name + '.ebc').exists())


def _case_test(name):
    def test(self):
        self.execute_targets(name)
    test.__name__ = 'test_' + name.replace('-', '_')
    return test


for _name in POSITIVE_CASES:
    setattr(BoxingTests, 'test_' + _name.replace('-', '_'), _case_test(_name))
