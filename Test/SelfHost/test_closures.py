"""Lexical closures, shared heap captures, callable ABI and manual ownership."""
import os
from pathlib import Path
import hashlib
import json
import re
import select
import subprocess
import time

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, ROOT, NativeCompilerFixture


POSITIVE_CASES = {
    'noncapturing-expression-and-blocks': '''
int32 Apply(fn(int32,int32)->int32 callback,int32 a,int32 b){return callback(a,b);}
int32 main(){
    var expression=function(int32 left,int32 right)=>left*10+right;
    fn(int32,int32)->int32 block=function(int32 left,int32 right)=>{
        if(left<0){return -left+right;}return left*10+right;
    };
    var constant=function()=>42;
    var short_alias=func(int32 value)=>value+1;
    var type_alias=fn(int32 value)=>value+2;
    var direct_block=func(int32 value){return value+7;};
    var explicit_return=function(int32 value)->int32{return value+11;};
    if(expression(4,2)!=42 || expression(right:2,left:4)!=42 || constant()!=42){return 1;}
    if(Apply(block,4,2)!=42 || block(-40,2)!=42){return 2;}
    if(short_alias(41)!=42 || type_alias(40)!=42 || (function(int32 value)=>value+1)(41)!=42){return 3;}
    if(direct_block(35)!=42 || explicit_return(31)!=42){return 4;}
    return 0;
}
''',
    'contextual-parameters-ref-and-void': '''
int32 Apply(fn(int32,int32)->int32 callback){return callback(4,2);}
void ApplyRef(fn(ref int32)->void callback,ref int32 value){callback(ref value);}
int32 main(){
    fn(int32,int32)->int32 sum=function(left,right)=>left*10+right;
    fn(ref int32)->void change=function(ref value)=>{value+=7;};
    int32 item=5;ApplyRef(change,ref item);
    int32 touched=0;var touch=function()=>{touched++;};var noop=function()=>{};
    touch();noop();
    if(sum(4,2)!=42 || Apply(function(a,b)=>a*10+b)!=42 || item!=12 || touched!=1){return 1;}
    return 0;
}
''',
    'contextual-field-element-return-constructor-and-branches': '''
class Router{public fn(int32)->int32 action;Router(fn(int32)->int32 callback){action=callback;}}
fn(int32)->int32 Make(int32 offset){return function(value)=>value+offset;}
T? Missing<T>(T prototype){return null;}
int32 main(){
    int32 offset=19;Router holder=new Router(function(value)=>value+offset);
    holder.action=function(value)=>value+offset;
    fn(int32)->int32 callbacks[2];callbacks[0]=function(value)=>value+offset;callbacks[1]=Make(7);
    fn(int32)->int32 selected=true?function(value)=>value+offset:function(value)=>value+7;
    var absent=Missing(callbacks[0]);
    fn(int32)->int32 fallback=absent??function(value)=>value+offset;
    if(holder.action(23)!=42 || callbacks[0](23)!=42 || callbacks[1](5)!=12 || selected(23)!=42 || fallback(23)!=42){return 1;}
    delete holder;return 0;
}
''',
    'generic-inline-context-and-overload-candidates': '''
T Map<T>(T value,fn(T)->T callback){return callback(value);}
T FirstMap<T>(fn(T)->T callback,T value){return callback(value);}
int32 Apply(fn(int32)->int32 callback,int32 value){return callback(value);}
int32 Apply(fn(string)->int32 callback,string value){return callback(value);}
int32 main(){
    int32 offset=7;
    if(Map(5,function(item)=>item+offset)!=12 || FirstMap(function(item)=>item+offset,5)!=12){return 1;}
    if(Apply(function(value)=>value+offset,35)!=42 || Apply(function(value)=>(int32)value.Length+37,"hello")!=42){return 2;}
    return 0;
}
''',
    'escaping-capture-and-factory-isolation': '''
fn(int32)->int32 Counter(int32 initial){
    int32 value=initial;
    return function(int32 amount)=>{value+=amount;return value;};
}
int64 Churn(int64 seed){int64 scratch[8];scratch[0]=seed;scratch[7]=seed+1;return scratch[0]+scratch[7];}
int32 main(){
    var first=Counter(5);var second=Counter(100);var same=first;
    int64 noise=0;for(int32 i=0;i<100;i++){noise+=Churn(i);}
    if(noise!=10000 || first(7)!=12 || same(3)!=15 || second(9)!=109 || first(0)!=15){return 1;}
    return 0;
}
''',
    'shared-mutable-cell-and-outer-reference': '''
struct Pair{fn(int32)->int32 add;fn()->int32 read;}
Pair Make(int32 initial){
    int32 value=initial;Pair callbacks=default(Pair);
    callbacks.add=function(int32 amount)=>{value+=amount;return value;};
    callbacks.read=function()=>value;return callbacks;
}
void Change(ref int32 value){value+=11;}
int32 main(){
    int32 value=7;var read=function()=>value;var add=function(int32 amount)=>{value+=amount;return value;};
    value=19;if(read()!=19 || add(3)!=22 || value!=22){return 1;}
    Change(ref value);if(read()!=33){return 2;}
    Pair callbacks=Make(5);if(callbacks.read()!=5 || callbacks.add(7)!=12 || callbacks.read()!=12){return 3;}
    return 0;
}
''',
    'nested-transitive-capture': '''
fn(int32)->fn(int32)->int32 Make(int32 seed){
    int32 shared=seed;
    return function(int32 first)=>{
        int32 local=first;
        return function(int32 second)=>{shared+=second;local+=1;return shared*100+local;};
    };
}
fn()->fn()->fn()->int32 Deep(int32 seed){
    return function()=>function()=>function()=>seed;
}
int32 main(){
    var outer=Make(5);var a=outer(7);var b=outer(11);
    if(a(3)!=808 || b(2)!=1012 || a(1)!=1109){return 1;}
    var first=Deep(42);var second=first();var third=second();if(third()!=42){return 2;}
    return 0;
}
''',
    'lexical-scope-shadowing': '''
int32 main(){
    int32 value=5;fn()->int32 outer=function()=>value;fn()->int32 inner=&Zero;
    {int32 value=7;inner=function()=>value;value=11;}
    value=13;
    var param=function(int32 value)=>value+outer()+inner();
    if(outer()!=13 || inner()!=11 || param(18)!=42){return 1;}
    return 0;
}
int32 Zero(){return 0;}
''',
    'loop-declarations-create-distinct-cells': '''
int32 main(){
    fn()->int32 callbacks[3];int32 values[3];values[0]=7;values[1]=11;values[2]=13;
    int32 index=0;foreach(var item in values){callbacks[index]=function()=>item;item+=100;index++;}
    if(callbacks[0]()!=107 || callbacks[1]()!=111 || callbacks[2]()!=113){return 1;}
    index=0;while(index<3){int32 snapshot=index;callbacks[index]=function()=>snapshot;index++;}
    if(callbacks[0]()!=0 || callbacks[1]()!=1 || callbacks[2]()!=2){return 2;}
    for(int32 current=0;current<3;current++){callbacks[current]=function()=>current;}
    if(callbacks[0]()!=3 || callbacks[1]()!=3 || callbacks[2]()!=3){return 3;}
    return 0;
}
''',
    'recursive-shared-callback-and-mutual-recursion': '''
int32 One(int32 n){return 1;}
bool Yes(int32 n){return true;}
bool No(int32 n){return false;}
fn(int32)->int32 Factorial(){
    fn(int32)->int32 callback=&One;
    callback=function(int32 n)=>n<=1?1:n*callback(n-1);
    return callback;
}
struct Predicates{fn(int32)->bool even;fn(int32)->bool odd;}
Predicates Mutual(){
    fn(int32)->bool even=&Yes;fn(int32)->bool odd=&No;
    even=function(int32 n)=>n==0?true:odd(n-1);
    odd=function(int32 n)=>n==0?false:even(n-1);
    Predicates result=default(Predicates);result.even=even;result.odd=odd;return result;
}
int32 main(){
    var factorial=Factorial();Predicates pair=Mutual();
    if(factorial(8)!=40320 || !pair.even(20) || pair.even(21) || !pair.odd(21) || pair.odd(20)){return 1;}
    return 0;
}
''',
    'captured-struct-fixed-array-and-result-copy': '''
struct Response{int32 status;byte tag[3];uint128 length;}
fn(int32)->Response Handler(Response initial){
    Response state=initial;
    return function(int32 code)=>{state.status=code;state.tag[0]+=1;return state;};
}
fn(int32)->int32 Fixed(){
    int32 values[3];values[0]=7;values[1]=11;values[2]=13;
    return function(int32 amount)=>{values[1]+=amount;return values[0]+values[1]+values[2];};
}
int32 main(){
    Response seed=default(Response);seed.status=200;seed.tag[0]=5;seed.tag[2]=7;seed.length=((uint128)1<<100)+11;
    var handler=Handler(seed);Response first=handler(201);Response second=handler(202);
    first.tag[2]=99;
    if(first.status!=201 || first.tag[0]!=6 || second.status!=202 || second.tag[0]!=7 || second.tag[2]!=7){return 1;}
    if(seed.status!=200 || seed.tag[0]!=5 || second.length!=((uint128)1<<100)+11){return 2;}
    var retained=Fixed();if(retained(1)!=32 || retained(2)!=34){return 3;}
    return 0;
}
''',
    'captured-value-type-width-and-float-shapes': '''
fn(int8)->int8 Narrow(int8 initial){return function(int8 amount)=>{initial+=amount;return initial;};}
fn(uint128)->uint128 Wide(uint128 initial){return function(uint128 amount)=>{initial+=amount;return initial;};}
fn(float32)->float32 Small(float32 initial){return function(float32 amount)=>{initial+=amount;return initial;};}
fn(float64)->float64 Large(float64 initial){return function(float64 amount)=>{initial+=amount;return initial;};}
int32 main(){
    var narrow=Narrow((int8)120);var wide=Wide(((uint128)1<<100)+7);
    var small=Small((float32)1.25);var large=Large(3.5);
    if(narrow((int8)10)!=-126 || narrow((int8)1)!=-125){return 1;}
    if(wide(((uint128)1<<110)+11)!=((uint128)1<<110)+((uint128)1<<100)+18){return 2;}
    if(small((float32)0.5)!=1.75 || small((float32)0.25)!=2.0 || large(0.75)!=4.25){return 3;}
    return 0;
}
''',
    'this-base-protected-and-virtual-dispatch': '''
class Parent{protected int32 seed=5;public virtual int32 Read(){return 7;}}
class Child:Parent{
    public override int32 Read(){return 27;}
    public fn()->int32 Make(){return function()=>base.Read()+this.Read()+seed;}
    public void Change(){seed=8;}
}
int32 main(){Child value=new Child();var callback=value.Make();value.Change();
    if(callback()!=42){return 1;}delete value;return 0;}
''',
    'generic-instantiations-and-generic-owner-captures': '''
fn()->T Keep<T>(T initial){return function()=>initial;}
fn(T)->T Last<T>(T initial){return function(T item)=>{initial=item;return initial;};}
class Factory<T>{public T stored;Factory(T value){stored=value;}public fn()->T Make(){return function()=>stored;}}
struct Record{int32 status;byte tag[2];}
int32 main(){
    var number=Keep(42);var word=Keep("ready");var wide=Keep(((uint128)1<<100)+7);var last=Last(5);
    Record record=default(Record);record.status=200;record.tag[1]=11;var aggregate=Keep(record);var bytes=Keep(record.tag);
    Record first=aggregate();first.tag[1]=99;Record second=aggregate();
    var first_bytes=bytes();first_bytes[1]=99;var second_bytes=bytes();
    Factory<string> factory=new Factory<string>("first");var member=factory.Make();factory.stored="second";
    if(number()!=42 || word()!="ready" || wide()!=((uint128)1<<100)+7 || last(7)!=7 || last(11)!=11){return 1;}
    if(second.status!=200 || second.tag[1]!=11 || second_bytes.Length!=2 || second_bytes[1]!=11 || member()!="second"){return 2;}delete factory;return 0;
}
''',
    'mixed-free-and-closure-containers-and-coalescing': '''
int32 AddSeven(int32 value){return value+7;}
int32 Invoke(fn(int32)->int32 callback,int32 value){return callback(value);}
class Router{public fn(int32)->int32 action;public fn(int32)->int32 items[2];}
T[] MakeArray<T>(T first,T second){return [first,second];}
T? Missing<T>(T prototype){return null;}
int32 main(){
    int32 offset=11;var closure=function(int32 value)=>value+offset;
    Router holder=new Router();holder.action=closure;holder.items[0]=&AddSeven;holder.items[1]=closure;
    fn(int32)->int32 callbacks[2];callbacks[0]=holder.items[0];callbacks[1]=holder.items[1];
    var absent=Missing(closure);var chosen=absent??closure;
    var dynamic=MakeArray(&AddSeven,closure);offset=19;
    if(Invoke(holder.action,23)!=42 || callbacks[0](5)!=12 || callbacks[1](23)!=42 || chosen(23)!=42){return 1;}
    if(dynamic[0](5)!=12 || dynamic[1](23)!=42){return 2;}
    delete dynamic;delete holder;return 0;
}
''',
    'finally-mutations-and-cross-closure-exceptions': '''
static int32 cleanup=0;
fn()->int32 Returned(){int32 value=5;try{return function()=>value;}finally{value=42;cleanup++;}}
fn()->int32 Thrower(int32 code){return function()=>{try{throw code;}finally{cleanup+=10;}};}
int32 main(){
    var result=Returned();if(result()!=42 || cleanup!=1){return 1;}
    var fail=Thrower(17);int32 caught=0;
    try{fail();}catch(int32 value){caught=value;}finally{cleanup+=100;}
    if(caught!=17 || cleanup!=111){return 2;}return 0;
}
''',
    'catch-parameter-escape-and-ref-calls-in-body': '''
int32 Zero(){return 0;}
void Change(ref int32 value){value+=11;}
struct Actions{fn()->int32 change;fn()->int32 read;}
Actions Make(int32 initial){
    Actions result=default(Actions);
    result.change=function()=>{Change(ref initial);return initial;};
    result.read=function()=>initial;return result;
}
int32 main(){
    fn()->int32 saved=&Zero;
    try{throw 42;}catch(int32 captured){saved=function()=>captured;captured+=7;}
    Actions callbacks=Make(5);
    if(saved()!=49 || callbacks.change()!=16 || callbacks.read()!=16 || callbacks.change()!=27){return 1;}
    return 0;
}
''',
    'captured-array-reference-and-callback-slot-updates': '''
int32 Seven(){return 7;}
int32 Eleven(){return 11;}
int32 main(){
    int32[] first=[3,5];int32[] second=[7,11];int32[] selected=first;
    fn()->int32 active=&Seven;
    var read=function()=>selected[0]+active();
    var mutate=function(int32 amount)=>{selected[0]+=amount;return selected[0];};
    if(read()!=10 || mutate(2)!=5 || first[0]!=5){return 1;}
    selected=second;active=&Eleven;
    if(read()!=18 || mutate(3)!=10 || read()!=21 || first[0]!=5 || second[0]!=10){return 2;}
    delete first;delete second;return 0;
}
''',
    'evaluation-order-and-one-time-callback-evaluation': '''
static int32 trace=0;
int32 Argument(int32 digit){trace=trace*10+digit;return digit;}
fn(int32,int32)->int32 Select(int32 seed){trace=trace*10+seed;return function(int32 left,int32 right)=>left*10+right;}
int32 main(){
    var callback=Select(1);if(trace!=1){return 1;}
    trace=0;if(callback(Argument(2),Argument(3))!=23 || trace!=23){return 2;}
    trace=0;if(Select(1)(Argument(2),Argument(3))!=23 || trace!=123){return 3;}
    var named=function(int32 left,int32 right)=>left*10+right;
    trace=0;if(named(right:Argument(3),left:Argument(2))!=23 || trace!=32){return 4;}
    return 0;
}
''',
    'readonly-capture-reads-and-defensive-struct-receiver': '''
struct Counter{int32 value;Counter(int32 seed){value=seed;}void Tick(){value++;}readonly int32 Read(){return value;}}
int32 main(){
    let constant=7;let counter=new Counter(11);
    var scalar=function()=>constant;
    var observe=function()=>{counter.Tick();return counter.Read();};
    if(scalar()!=7 || observe()!=11 || observe()!=11 || counter.Read()!=11){return 1;}
    return 0;
}
''',
    'callable-pointer-storage-identity-and-delete': '''
int32 AddSeven(int32 value){return value+7;}
int32 Through<T>(T original,int32 value){
    unsafe(using krt.mem;){
        T callback=original;T* pointer=&callback;T copied=pointer[0];
        if(sizeof(T)!=8 || copied!=callback || copied==null || copied(value)!=42){return 1;}
        pointer[0]=&AddSeven;
        if(callback(5)!=12 || copied(value)!=42 || original(value)!=42){return 2;}
        return 0;
    }
}
int32 main(){
    int32 offset=19;var callback=function(int32 value)=>value+offset;
    if(Through(callback,23)!=0){return 1;}
    delete callback;var free=&AddSeven;delete free;delete free;
    if(free(5)!=12){return 2;}return 0;
}
''',
}


STRESS_SOURCE = '''
struct Pair{fn(int32)->int32 add;fn()->int32 read;}
struct Cycle{fn()->int32 left;fn()->int32 right;}
int32 Zero(){return 0;}
fn()->int32 Escape(int32 initial){
    int32 value=initial;try{return function()=>value;}finally{value++;}
}
Pair Shared(int32 initial){
    int32 value=initial;Pair result=default(Pair);
    result.add=function(int32 amount)=>{value+=amount;return value;};
    result.read=function()=>value;return result;
}
fn()->fn()->int32 Nested(int32 value){return function()=>function()=>value;}
fn()->int32 Self(int32 value){
    fn()->int32 callback=&Zero;
    callback=function()=>value<0?callback():value;return callback;
}
Cycle Mutual(int32 value){
    fn()->int32 left=&Zero;fn()->int32 right=&Zero;
    left=function()=>value<0?right():value;
    right=function()=>value<0?left():value+1;
    Cycle result=default(Cycle);result.left=left;result.right=right;return result;
}
int32 Ordinary(int32 initial){
    int32 value=initial;var callback=function()=>value;int32 result=callback();
    delete callback;value++;return result==initial&&value==initial+1?0:1;
}
void Raised(int32 initial){
    int32 value=initial;var callback=function()=>value;
    try{int32 result=callback();delete callback;throw result;}finally{value++;}
}
int32 Loops(int32 value){
    int32 numbers[3];numbers[0]=value;numbers[1]=value+1;numbers[2]=value+2;
    int32 total=0;int32 count=0;
    foreach(var item in numbers){
        var callback=function()=>item;total+=callback();delete callback;count++;
        if(count==1){continue;}break;
    }
    for(int32 index=0;index<3;index++){
        int32 local=value;var callback=function()=>local;total+=callback();delete callback;
        if(index==0){continue;}break;
    }
    delete numbers;
    return total==value*4+1?0:1;
}
int32 Check(int32 value){
    var escaped=Escape(value);int32 result=escaped();delete escaped;
    if(result!=value+1){return 1;}
    Pair pair=Shared(value);
    if(pair.read()!=value || pair.add(7)!=value+7 || pair.read()!=value+7){return 2;}
    delete pair.add;if(pair.read()!=value+7){return 8;}delete pair.read;
    var outer=Nested(value);var inner=outer();delete outer;
    result=inner();delete inner;if(result!=value){return 3;}
    var recursive=Self(value);result=recursive();delete recursive;if(result!=value){return 4;}
    Cycle cycle=Mutual(value);
    if(cycle.left()!=value || cycle.right()!=value+1){return 5;}
    delete cycle.left;delete cycle.right;
    if(Ordinary(value)!=0 || Loops(value)!=0){return 6;}
    int32 caught=-1;
    try{Raised(value);}catch(int32 error){var read=function()=>error;caught=read();delete read;}
    return caught==value?0:7;
}
void Mark(string message,int32 length){syscall(1,1,(int64)message,length,0,0,0);}
int32 Gate(){unsafe(using krt.mem;){byte token[1];int32 result=syscall(0,0,(int64)&token[0],1,0,0,0)==1?1:0;delete token;return result;}}
int32 main(){
    for(int32 warm=0;warm<16;warm++){int32 status=Check(warm);if(status!=0){return status;}}
    Mark("READY\\n",6);if(Gate()!=1){return 201;}
    for(int32 index=0;index<25000;index++){int32 status=Check(index);if(status!=0){return status;}}
    Mark("DONE\\n",5);if(Gate()!=1){return 202;}return 0;
}
'''


# Each rejected source has one invalid operation and one exact diagnostic.
NEGATIVE_CASES = {
    'untyped-without-context': ('int32 main(){var callback=function(value)=>value;return 0;}', 'value)=>', 'E_LOWER'),
    'parameter-count': ('int32 main(){fn(int32)->int32 callback=function(int32 a,int32 b)=>a+b;return 0;}', 'function', 'E_LOWER'),
    'parameter-type': ('int32 main(){fn(string)->int32 callback=function(int32 value)=>value;return 0;}', 'function', 'E_LOWER'),
    'ref-mode': ('int32 main(){fn(ref int32)->int32 callback=function(int32 value)=>value;return 0;}', 'function', 'E_LOWER'),
    'return-type': ('int32 main(){fn()->int32 callback=function()=>"wrong";return 0;}', 'function', 'E_LOWER'),
    'return-shape': ('struct Row{byte values[2];}int32 main(){fn()->int32 callback=function()=>default(Row);return 0;}', 'function', 'E_LOWER'),
    'void-value': ('void Done(){}int32 main(){fn()->int32 callback=function()=>Done();return 0;}', 'Done();', 'E_LOWER'),
    'missing-block-return': ('int32 main(){fn(int32)->int32 callback=function(int32 value)=>{if(value>0){return value;}};return 0;}', 'function', 'E_LOWER'),
    'duplicate-parameters': ('int32 main(){var callback=function(int32 value,int32 value)=>value;return 0;}', 'value)=>', 'E_LOWER'),
    'undefined-capture': ('int32 main(){var callback=function()=>missing;return 0;}', 'missing', 'E_LOWER'),
    'lambda-parameter-scope-escape': ('int32 main(){var callback=function(int32 hidden)=>hidden;return hidden;}', 'hidden;}', 'E_LOWER'),
    'readonly-capture-assignment': ('int32 main(){let value=7;var callback=function()=>{value=9;return value;};return 0;}', 'value=9', 'E_LOWER'),
    'readonly-capture-ref': ('void Change(ref int32 value){value++;}int32 main(){let value=7;var callback=function()=>{Change(ref value);return value;};return 0;}', 'value);', 'E_LOWER'),
    'readonly-struct-capture-field': ('struct Row{int32 value;}int32 main(){let row=default(Row);var callback=function()=>{row.value=9;return row.value;};return 0;}', 'row.value=9', 'E_LOWER'),
    'ref-parameter-escape': ('fn()->int32 Keep(ref int32 value){return function()=>value;}int32 main(){return 0;}', 'value;}', 'E_CAPTURE_BORROW'),
    'nested-ref-parameter-escape': ('fn(ref int32)->fn()->int32 Factory(){return function(ref int32 value)=>function()=>value;}int32 main(){return 0;}', 'value;}', 'E_CAPTURE_BORROW'),
    'struct-this-escape': ('struct Counter{int32 value;fn()->int32 Make(){return function()=>this.value;}}int32 main(){return 0;}', 'this.value', 'E_CAPTURE_BORROW'),
    'local-address-alias-escape': ('fn()->int32 Keep(){unsafe(using krt.mem;){int32 local=7;int32* first=&local;int32* second=first;return function()=>*second;}}int32 main(){return 0;}', 'second;}', 'E_CAPTURE_BORROW'),
    'stackalloc-escape': ('fn()->int32 Keep(){unsafe(using krt.mem;){int32* values=stackalloc int32[2];return function()=>values[0];}}int32 main(){return 0;}', 'values[0]', 'E_CAPTURE_BORROW'),
    'stackalloc-ternary-alias-escape': ('fn()->int32 Keep(){unsafe(using krt.mem;){int32* values=stackalloc int32[2];var alias=true?values:values;return function()=>alias[0];}}int32 main(){return 0;}', 'alias[0]', 'E_CAPTURE_BORROW'),
    'borrow-assignment-after-capture': ('int32 main(){unsafe(using krt.mem;){int32[] values=new int32[1];int32* pointer=&values[0];var callback=function()=>*pointer;int32 local=7;pointer=&local;delete callback;delete values;return 0;}}', 'local;delete', 'E_CAPTURE_BORROW'),
    'readonly-lambda-parameter-write': ('int32 main(){var callback=function(readonly int32 value)=>{value++;return value;};return 0;}', 'value++', 'E_LOWER'),
    'unreachable-incompatible-return': ('int32 main(){var callback=function()=>{return 5;return "wrong";};return 0;}', '"wrong"', 'E_LOWER'),
    'protected-access': ('class Owner{protected int32 value=7;}fn()->int32 Keep(Owner owner){return function()=>owner.value;}int32 main(){return 0;}', 'value;}', 'E_ACCESS'),
}


class ClosureTests(NativeCompilerFixture):
    def execute_targets(self, name):
        path = self.work / (name + '.krt')
        path.write_text(POSITIVE_CASES[name], encoding='utf-8')
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
        position = source.index(marker)
        line = source.count('\n', 0, position) + 1
        column = position - source.rfind('\n', 0, position)
        for target in ('native', 'vm'):
            for level in (0, 2):
                for previous in (None, b'previous closure artifact\x00'):
                    with self.subTest(program=name, target=target, optimization=level,
                                      existing=previous is not None):
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
                        self.assertEqual(first.stderr, second.stderr)
                        self.assertEqual(re.findall(r':\d+:\d+: E_[A-Z_]+:', first.stderr),
                                         [f':{line}:{column}: {code}:'], first.stderr)
                        if previous is None:
                            self.assertFalse(output.exists())
                        else:
                            self.assertEqual(output.read_bytes(), previous)
                        self.assertFalse(output.with_name(output.name + '.ebc').exists())

    def test_noncapturing_expression_and_block_callbacks(self):
        self.execute_targets('noncapturing-expression-and-blocks')

    def test_contextual_parameter_inference_ref_and_void_calls(self):
        self.execute_targets('contextual-parameters-ref-and-void')

    def test_contextual_fields_elements_returns_constructors_and_branches(self):
        self.execute_targets('contextual-field-element-return-constructor-and-branches')

    def test_generic_inline_context_uses_other_arguments_and_overload_candidates(self):
        self.execute_targets('generic-inline-context-and-overload-candidates')

    def test_escaping_capture_and_separate_factory_invocations(self):
        self.execute_targets('escaping-capture-and-factory-isolation')

    def test_multiple_callbacks_and_outer_writes_share_one_variable(self):
        self.execute_targets('shared-mutable-cell-and-outer-reference')

    def test_nested_callbacks_capture_through_multiple_environments(self):
        self.execute_targets('nested-transitive-capture')

    def test_lexical_scope_and_shadowing(self):
        self.execute_targets('lexical-scope-shadowing')

    def test_loop_declared_variables_have_the_correct_cell_lifetime(self):
        self.execute_targets('loop-declarations-create-distinct-cells')

    def test_recursive_and_mutually_recursive_callback_variables(self):
        self.execute_targets('recursive-shared-callback-and-mutual-recursion')

    def test_captured_structs_fixed_arrays_and_return_copies(self):
        self.execute_targets('captured-struct-fixed-array-and-result-copy')

    def test_narrow_wide_and_float_capture_storage(self):
        self.execute_targets('captured-value-type-width-and-float-shapes')

    def test_this_base_protected_and_virtual_dispatch(self):
        self.execute_targets('this-base-protected-and-virtual-dispatch')

    def test_generic_closures_and_generic_owner_bindings(self):
        self.execute_targets('generic-instantiations-and-generic-owner-captures')

    def test_closures_and_free_functions_share_containers_and_nullable_selection(self):
        self.execute_targets('mixed-free-and-closure-containers-and-coalescing')

    def test_finally_mutations_and_unwinding_cross_callback_frames(self):
        self.execute_targets('finally-mutations-and-cross-closure-exceptions')

    def test_catch_parameters_escape_and_ref_calls_modify_capture_cells(self):
        self.execute_targets('catch-parameter-escape-and-ref-calls-in-body')

    def test_captured_array_and_callback_variables_observe_reassignment(self):
        self.execute_targets('captured-array-reference-and-callback-slot-updates')

    def test_callable_word_pointer_storage_identity_and_free_function_delete(self):
        self.execute_targets('callable-pointer-storage-identity-and-delete')

    def test_independently_compiled_factories_callbacks_aggregate_returns_and_release(self):
        shared = 'public struct Record{int32 status;byte tag[2];uint128 length;}\n'
        first_source = '''
import "Shared.krt";
fn(int32)->int32 First(int32 seed){return function(int32 amount)=>{seed+=amount;return seed;};}
fn()->Record Keep(Record value){return function()=>value;}
'''
        second_source = '''
fn(int32)->int32 Second(int32 seed){return function(int32 amount)=>{seed+=amount;return seed;};}
int32 Apply(fn(int32)->int32 callback,int32 value){return callback(value);}
void Release(fn(int32)->int32 callback){delete callback;}
'''
        consumer_source = '''
import "Shared.krt";
extern fn(int32)->int32 First(int32 seed);extern fn(int32)->int32 Second(int32 seed);
extern fn()->Record Keep(Record value);extern int32 Apply(fn(int32)->int32 callback,int32 value);
extern void Release(fn(int32)->int32 callback);
int32 main(){
    var first=First(5);var second=Second(100);
    if(first(7)!=12 || second(9)!=109 || Apply(first,3)!=15){return 1;}
    Record seed=default(Record);seed.status=200;seed.tag[1]=11;seed.length=((uint128)1<<100)+7;
    var callback=Keep(seed);Record value=callback();value.tag[1]=99;Record again=callback();
    if(again.status!=200 || again.tag[1]!=11 || again.length!=((uint128)1<<100)+7){return 2;}
    Release(first);Release(second);delete callback;return 0;
}
'''
        for level in (0, 2):
            folder = self.work / f'closure-objects-o{level}'
            folder.mkdir()
            (folder / 'Shared.krt').write_text(shared, encoding='utf-8')
            objects = []
            for name, source in (('first', first_source), ('second', second_source),
                                 ('consumer', consumer_source)):
                path = folder / (name + '.krt')
                path.write_text(source, encoding='utf-8')
                output = path.with_suffix('.kro')
                self.command(path, f'-O{level}', '-c', '-o', output)
                self.assertEqual(output.read_bytes()[:4], b'KRO\x00')
                objects.append(output)
            for order in ((0, 1, 2), (2, 1, 0), (1, 0, 2)):
                with self.subTest(optimization=level, order=order):
                    output = folder / ('program-' + ''.join(map(str, order)))
                    self.command(*(objects[index] for index in order), '-o', output)
                    result = subprocess.run([str(output)], cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b'')
                    self.assertEqual(result.stderr, b'')

    def test_legacy_callback_object_abi_is_rejected_without_replacing_outputs(self):
        folder = ROOT / 'Test/SelfHost/fixtures/closure-abi'
        metadata = json.loads((folder / 'provenance.json').read_text())
        self.assertEqual(metadata['compiler_sha256'],
                         '0a321ed3962220a309d5d9c69f8cbc6af08d3ffc00e2d21b773ab4a81abba859')
        source = '''
extern int32 Apply(fn(int32)->int32 callback,int32 value);
int32 AddSeven(int32 value){return value+7;}
int32 main(){return Apply(&AddSeven,5)==12?0:1;}
'''
        for level in (0, 2):
            record = next(entry for entry in metadata['objects'] if entry['level'] == level)
            legacy = self.work / f'legacy-callback-o{level}.kro'
            data = bytes.fromhex((folder / record['hex_path']).read_text())
            self.assertEqual(hashlib.sha256(data).hexdigest(), record['object_sha256'])
            self.assertIn(b'_KRT1$Apply$fn(i32;)>i32;i32;$i32', data)
            legacy.write_bytes(data)
            consumer_path = self.work / f'new-callback-consumer-o{level}.krt'
            consumer_path.write_text(source)
            consumer = consumer_path.with_suffix('.kro')
            self.command(consumer_path, f'-O{level}', '-c', '-o', consumer)
            self.assertIn(b'_KRT1$Apply$fn2(i32;)>i32;i32;$i32', consumer.read_bytes())
            for reverse in (False, True):
                for previous in (None, b'previous callable executable\x00'):
                    with self.subTest(optimization=level, reversed_order=reverse,
                                      existing=previous is not None):
                        output = self.work / f'legacy-rejected-o{level}-{reverse}-{previous is not None}'
                        if previous is not None:
                            output.write_bytes(previous)
                        objects = (consumer, legacy) if reverse else (legacy, consumer)
                        argv = [str(COMPILER), '--linker', str(LINKER), *map(str, objects), '-o', str(output)]
                        first = subprocess.run(argv, cwd=self.work, env=self.env,
                                               capture_output=True, text=True, timeout=60)
                        second = subprocess.run(argv, cwd=self.work, env=self.env,
                                                capture_output=True, text=True, timeout=60)
                        self.assertEqual(first.returncode, 1, first.stdout + first.stderr)
                        self.assertEqual(second.returncode, 1, second.stdout + second.stderr)
                        self.assertEqual(first.stderr, second.stderr)
                        self.assertIn('E_LINK', first.stderr)
                        if previous is None:
                            self.assertFalse(output.exists())
                        else:
                            self.assertEqual(output.read_bytes(), previous)

    def test_twenty_five_thousand_manual_closure_lifetimes_keep_process_memory_bounded(self):
        path = self.work / 'closure-lifetimes.krt'
        path.write_text(STRESS_SOURCE, encoding='utf-8')

        def read_marker(process, expected):
            deadline = time.monotonic() + 120
            result = b''
            while b'\n' not in result and time.monotonic() < deadline:
                ready, _, _ = select.select([process.stdout], [], [], 0.1)
                if ready:
                    chunk = os.read(process.stdout.fileno(), 4096)
                    if not chunk:
                        break
                    result += chunk
                if process.poll() is not None:
                    break
            self.assertEqual(result, expected,
                             f'closure lifetime runner status={process.poll()}, marker={result!r}')

        def memory(process):
            status = Path(f'/proc/{process.pid}/status').read_text()
            values = {}
            for key in ('VmRSS', 'VmSize'):
                match = re.search(rf'^{key}:\s+(\d+) kB$', status, re.MULTILINE)
                self.assertIsNotNone(match, status)
                values[key] = int(match.group(1)) * 1024
            return values

        for target in ('native', 'vm'):
            for level in (0, 2):
                with self.subTest(target=target, optimization=level):
                    output = self.work / f'closure-lifetimes-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    process = subprocess.Popen(argv, cwd=self.work, env=self.env,
                                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                               stderr=subprocess.PIPE)
                    try:
                        read_marker(process, b'READY\n')
                        before = memory(process)
                        process.stdin.write(b'1');process.stdin.flush()
                        read_marker(process, b'DONE\n')
                        after = memory(process)
                        print(f'closure memory target={target} O{level} before={before} after={after}')
                        # One leaked touched mmap page per iteration is about
                        # 100 MiB. Retained VM metadata must also stay bounded.
                        self.assertLessEqual(after['VmRSS'] - before['VmRSS'], 16 * 1024 * 1024,
                                             (before, after))
                        self.assertLessEqual(after['VmSize'] - before['VmSize'], 32 * 1024 * 1024,
                                             (before, after))
                        process.stdin.write(b'2');process.stdin.flush()
                        stdout, stderr = process.communicate(timeout=15)
                        self.assertEqual(process.returncode, 0, stdout + stderr)
                        self.assertEqual(stdout, b'')
                        self.assertEqual(stderr, b'')
                    finally:
                        if process.poll() is None:
                            process.kill()
                        process.communicate(timeout=3)

    def test_callback_and_named_argument_evaluation_order(self):
        self.execute_targets('evaluation-order-and-one-time-callback-evaluation')

    def test_readonly_captures_keep_existing_defensive_receiver_rules(self):
        self.execute_targets('readonly-capture-reads-and-defensive-struct-receiver')

    def test_invalid_lambda_types_scope_readonly_and_borrowed_captures_preserve_outputs(self):
        for name, (source, marker, code) in NEGATIVE_CASES.items():
            self.reject_targets(name, source, marker, code)
