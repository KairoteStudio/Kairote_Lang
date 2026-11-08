"""Native parser ambiguity: named casts and grouped callable expressions."""
import subprocess

from Test.SelfHost import test_native_optimizer as native


CASES = {
    'grouped-callbacks-and-arithmetic': '''
int32 Named(int32 value){return value+7;}
int32 main(){int32 seed=10;var callback=function(int32 value)=>seed+value;
if((callback)(5)!=15||((callback))(6)!=16||(Named)(7)!=14){return 1;}
var factory=function(int32 initial)=>function(int32 value)=>initial+value;
var nested=(factory)(20);if((nested)(22)!=42){return 2;}
int32 left=6;int32 right=7;if((left)*right!=42||(left)+right!=13||(left)-right!=-1){return 3;}
delete nested;delete factory;delete callback;return 0;}
''',
    'named-cast-chains-and-parenthesized-operands': '''
interface Face{int32 Read();}class Base{public int32 value;}
class Derived:Base,Face{public int32 Read(){return value;}}
Base Identity(Base value){return value;}
int32 main(){Derived original=new Derived();original.value=42;Base erased=original;
Derived first=(Derived)(Base)erased;Derived second=(Derived)(erased);
Derived third=(Derived)(((Base)(original)));Derived fourth=((Derived))((erased));
Derived fifth=(Derived)(Identity(erased));Face face=(Face)(Base)(original);
Derived empty=(Derived)(Base)null;
if(first!=original||second!=original||third!=original||fourth!=original||fifth!=original||empty!=null||face.Read()!=42){return 1;}
delete original;return 0;}
''',
    'postfix-binding-preserves-cast-operand-precedence': '''
class Item{public int32 value;}class Holder{public object erased;public object values[1];}
object Identity(object value){return value;}
int32 main(){Item item=new Item();item.value=42;Holder holder=new Holder();holder.erased=item;holder.values[0]=item;
Item first=(Item)(holder).erased;Item second=(Item)(holder).values[0];
var callback=function()=>Identity(item);Item third=(Item)(callback)();
Item fourth=(Item)(Identity)(item);int32 read=((Item)(holder.erased)).value;
if(first!=item||second!=item||third!=item||fourth!=item||read!=42){return 1;}
delete callback;delete holder;delete item;return 0;}
''',
    'qualified-callables-and-value-shadowing': '''
class Callback{}class Holder{public fn(int32)->int32 callback;}
namespace Helpers{int32 Increment(int32 value){return value+1;}}
int32 main(){var Callback=function(int32 value)=>value+2;Holder holder=new Holder();holder.callback=Callback;
if((Callback)(40)!=42||(holder.callback)(40)!=42||(Helpers.Increment)(41)!=42){return 1;}
var use=function(int32 value)=>(Callback)(value);if((use)(40)!=42){return 2;}
delete use;delete holder;delete Callback;return 0;}
''',
    'using-aliases-and-closed-generic-type-contexts': '''
namespace Models{class Item{public int32 value;}class Holder<T>{public T value;}}
using Alias=Models.Item;
T Restore<T>(object value){return (T)(value);}
class Reader<T>{public T Read(object value){return (T)((value));}}
int32 main(){Alias original=new Alias();original.value=42;object erased=original;
Alias first=(Alias)(erased);Models.Item second=(Models.Item)((erased));
Models.Holder<Alias> holder=new Models.Holder<Alias>();holder.value=original;object boxed_holder=holder;
var restored=(Models.Holder<Alias>)(boxed_holder);Reader<Alias> reader=new Reader<Alias>();
if(first!=original||second!=original||restored.value!=original||Restore<Alias>(erased)!=original||reader.Read(erased)!=original){return 1;}
{using Models;Item local=(Item)(erased);if(local!=original){return 2;}}
delete reader;delete holder;delete original;return 0;}
''',
    'candidate-mutations-rollback-during-lambda-overload-probing': '''
struct Row{int32 value;}int32 Apply(fn(object)->Row transform,object value){return transform(value).value;}
int32 Apply(fn(object)->int32 transform,object value){return transform(value)+1000;}
int32 main(){Row original=default(Row);original.value=42;object boxed=original;
int32 result=Apply(function(object value)=>(Row)(value),boxed);delete boxed;return result==42?0:1;}
''',
    'grouped-call-named-and-reference-arguments': '''
int32 Update(ref int32 value,int32 amount){value+=amount;return value;}
int32 main(){fn(ref int32,int32)->int32 callback=&Update;int32 value=40;
if((callback)(ref value,2)!=42||value!=42){return 1;}
if((Update)(amount:3,ref value:value)!=45||value!=45){return 2;}return 0;}
''',
}


class ParenthesizedCastTests(native.NativeCompilerFixture):
    def verify(self, source):
        for level in range(4):
            with self.subTest(backend='native', optimization=level):
                binary, _ = self.compile(source, level)
                result = subprocess.run([str(binary)], cwd=self.work, env=self.env,
                                        capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with self.subTest(backend='vm', optimization=level):
                path = self.work / f'parenthesized-o{level}.krt'
                path.write_text(source)
                artifact = path.with_suffix('.ebc')
                self.command(path, f'-O{level}', '--target', 'vm', '-o', artifact)
                self.command('run-vm', artifact)

    def test_grouped_callback_and_arithmetic_baseline_semantics(self):
        self.verify(CASES['grouped-callbacks-and-arithmetic'])

    def test_chained_named_casts_and_parenthesized_operands(self):
        self.verify(CASES['named-cast-chains-and-parenthesized-operands'])

    def test_cast_applies_to_complete_unary_operand_with_postfixes(self):
        self.verify(CASES['postfix-binding-preserves-cast-operand-precedence'])

    def test_qualified_callback_fields_and_local_type_name_shadowing(self):
        self.verify(CASES['qualified-callables-and-value-shadowing'])

    def test_using_aliases_and_scoped_closed_generic_types(self):
        self.verify(CASES['using-aliases-and-closed-generic-type-contexts'])

    def test_overload_probe_restores_unresolved_cast_candidate(self):
        self.verify(CASES['candidate-mutations-rollback-during-lambda-overload-probing'])

    def test_grouped_calls_keep_named_and_reference_argument_grammar(self):
        self.verify(CASES['grouped-call-named-and-reference-arguments'])

    def test_deep_cast_and_callback_candidates_keep_linear_syntax_size(self):
        cast = 'erased'
        callback = '0'
        for _ in range(96):
            cast = f'(Item)({cast})'
            callback = f'(callback)({callback})'
        self.verify(f'''
class Item{{public int32 value;}}
int32 main(){{Item original=new Item();original.value=42;object erased=original;
var callback=function(int32 value)=>value+1;
Item restored={cast};int32 result={callback};
int32 status=restored==erased&&result==96?0:1;
delete callback;delete original;return status;}}
''')
