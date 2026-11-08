"""Dynamic callbacks retain only shared names throughout their signatures."""
import subprocess

from Test.SelfHost import test_nullable_operators as nullable_cases
from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


FACTORIES = '''int32 Alpha(int32 left,int32 right){return left*10+right;}
int32 Beta(int32 first,int32 second){return first*100+second;}
class StoreA<T>{public static T value;public static T Read(){return value;}}
class StoreB<T>{public static T value;public static T Read(){return value;}}
fn()->T FactoryA<T>(T callback){StoreA<T>.value=callback;return &StoreA<T>.Read;}
fn()->T FactoryB<T>(T callback){StoreB<T>.value=callback;return &StoreB<T>.Read;}
T? Missing<T>(T prototype){return null;}
'''

HIGHER_ORDER = '''
int32 Alpha(int32 left,int32 right){return left*10+right;}
int32 Beta(int32 first,int32 second){return first*100+second;}
class RouteA<T>{public static T Select(T[] options){return options[0];}}
class RouteB<T>{public static T Select(T[] options){return options[0];}}
fn(T[])->T FactoryA<T>(T prototype){return &RouteA<T>.Select;}
fn(T[])->T FactoryB<T>(T prototype){return &RouteB<T>.Select;}
T? Missing<T>(T prototype){return null;}
'''

POSITIVE_CASES = {
    # These three factory programs use the exact sources preserved in the
    # independent review report, so its before/after comparison stays valid.
    'factory-label-controls': FACTORIES + '''int32 main(){var a=FactoryA(&Alpha);var b=FactoryB(&Beta);var first=a();var second=b();if(first(right:7,left:5)!=57 || second(second:7,first:5)!=507){return 1;}return 0;}
''',
    'coalesced-nested-positional': FACTORIES + '''int32 main(){var a=Missing(FactoryA(&Alpha));var b=FactoryB(&Beta);var selected=a??b;var callback=selected();return callback(5,7)==507?0:1;}
''',
    'higher-order-context': HIGHER_ORDER + '''
int32 main(){
    var original=&Alpha;
    var a=FactoryA(original);var b=FactoryB(&Beta);
    var first=a([original]);var second=b([&Beta]);
    if(first(right:7,left:5)!=57 || second(second:7,first:5)!=507){return 1;}
    var absent=Missing(a);var selected=absent??b;
    var callback=selected([&Beta]);
    if(callback(5,7)!=507 || original(right:7,left:5)!=57){return 2;}
    // Merging the result must leave both original factory signatures intact.
    var retained=a([original]);var fallback=b([&Beta]);
    if(retained(right:7,left:5)!=57 || fallback(second:7,first:5)!=507){return 3;}
    return 0;
}
''',
    'base-guard-dispatch-and-access': '''
class Parent{
    protected int32 seed=5;
    public virtual int32 Read(){return 7;}
    protected virtual int32 Hidden(int32 value){return seed+value;}
}
class Child:Parent{
    public override int32 Read(){return 27;}
    protected override int32 Hidden(int32 value){return seed+value+100;}
    public int32 Check(){
        if(base?.Read()!=7 || this?.Read()!=27){return 1;}
        if(base?.seed!=5 || base?.Hidden(3)!=8 || this?.Hidden(3)!=108){return 2;}
        return 0;
    }
}
int32 main(){
    Child value=new Child();Parent parent=value;Parent absent=null;
    if(value.Check()!=0 || parent?.Read()!=27 || absent?.Read()!=0){return 1;}
    delete value;return 0;
}
''',
}

NEGATIVE_CASES = {
    'coalesced-nested-labels': (
        FACTORIES + '''int32 main(){var a=Missing(FactoryA(&Alpha));var b=FactoryB(&Beta);var selected=a??b;var callback=selected();return callback(right:7,left:5)==507?0:1;}
''',
        'right:7', 'E_ARGUMENT_NAME'),
    'higher-order-nested-labels': (
        HIGHER_ORDER + '''
int32 main(){
    var a=Missing(FactoryA(&Alpha));var b=FactoryB(&Beta);
    var selected=a??b;var callback=selected([&Beta]);
    return callback(right:7,left:5);
}
''',
        'right:7', 'E_ARGUMENT_NAME'),
    'guarded-protected-access': (
        '''class Parent{protected virtual int32 Hidden(){return 7;}}
int32 main(){Parent absent=null;return absent?.Hidden();}
''',
        'Hidden();', 'E_ACCESS'),
    'guarded-parent-protected-receiver': (
        '''class Parent{protected int32 seed=7;}
class Child:Parent{public int32 Inspect(Parent value){return value?.seed;}}
int32 main(){return 0;}
''',
        'seed;', 'E_ACCESS'),
}


class NestedCallbackContractTests(NativeCompilerFixture):
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

    def reject_targets(self, name):
        source, marker, code = NEGATIVE_CASES[name]
        nullable_cases.NullableOperatorTests.reject_targets(self, name, source, marker, code)

    def test_known_nested_callbacks_keep_their_own_names(self):
        self.execute_targets('factory-label-controls')

    def test_dynamic_nested_callbacks_remain_callable_by_position(self):
        self.execute_targets('coalesced-nested-positional')

    def test_dynamic_nested_callbacks_reject_unshared_names(self):
        self.reject_targets('coalesced-nested-labels')

    def test_higher_order_array_arguments_preserve_context_and_original_contracts(self):
        self.execute_targets('higher-order-context')
        self.reject_targets('higher-order-nested-labels')

    def test_guarded_base_calls_keep_base_dispatch_and_protected_access(self):
        self.execute_targets('base-guard-dispatch-and-access')

    def test_guards_do_not_grant_protected_access(self):
        self.reject_targets('guarded-protected-access')
        self.reject_targets('guarded-parent-protected-receiver')
