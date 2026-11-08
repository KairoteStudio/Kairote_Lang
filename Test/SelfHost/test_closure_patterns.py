"""Capture cells for patterns, value parameters and immediate callbacks."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


# Complete sources are frozen with the Native/VM O0/O2 candidate reports.
# Keeping their bytes here also makes the regressions independent of build artifacts.
CASES = {
    'foreach-struct-fixed': 'struct Packet{int32 count;byte tag[3];}int32 main(){Packet[] values=new Packet[2];values[0].count=11;values[0].tag[1]=2;values[1].count=20;values[1].tag[1]=4;fn()->Packet callbacks[2];int32 index=0;foreach(var value in values){callbacks[index]=function()=>{value.count+=1;value.tag[1]+=1;return value;};index+=1;}Packet first=callbacks[0]();Packet second=callbacks[1]();delete callbacks[0];delete callbacks[1];delete values;return first.count==12 && first.tag[1]==3 && second.count==21 && second.tag[1]==5?0:1;}',
    'iife-cleanup-return-and-throw': '''static int32 finalized=0;
int32 Call(int32 seed) {
    int32 value=seed;
    try {
        return (function()=>{value++;return value;})();
    } finally {finalized++;}
}
int32 Throw(int32 seed) {
    int32 value=seed;
    try {
        return (function()->int32=>{value++;throw 7;})();
    } catch(int32 code) {return value+code;}
    finally {finalized++;}
}
int32 main() {
    if(Call(40)!=41 || finalized!=1){return 1;}
    if(Throw(40)!=48 || finalized!=2){return 2;}
    return 0;
}
''',
    'iife-wide-and-struct-results': '''struct Packet { public int32 count; public int32 tags[3]; }
int32 main() {
    int128 value=((int128)1<<100)+17;
    int128 result=(function()=>value+7)();
    if(result!=value+7){return 1;}
    var original=new Packet(); original.count=40; original.tags[0]=2;
    Packet output=(function()=>original)();
    original.count=99; original.tags[0]=8;
    return output.count==40 && output.tags[0]==2?0:2;
}
''',
    'pattern-condition-and-iife': '''class Box { public int32 value; }
int32 main() {
    var box=new Box(); box.value=40;
    bool matched=box is Box live && (function()=>live.value)()==40;
    delete box;
    return matched?0:1;
}
''',
    'pattern-false-null': '''class Box { public int32 value; }
int32 main() {
    Box? box=null;
    fn()->int32 callback=box is Box live ? function()=>live.value : function()=>9;
    int32 result=callback(); delete callback;
    return result==9?0:1;
}
''',
    'pattern-false-incompatible-struct': '''struct Packet { public int32 value; }
int32 main() {
    int32 value=7;
    fn()->int32 callback=value is Packet live ? function()=>live.value : function()=>9;
    int32 result=callback(); delete callback;
    return result==9?0:1;
}
''',
    'pattern-false-incompatible-wide': '''int32 main() {
    int32 value=7;
    fn()->int32 callback=value is int128 live ? function()=>live==7?1:2 : function()=>9;
    int32 result=callback(); delete callback;
    return result==9?0:1;
}
''',
    'pattern-loop-independent': '''class Box { public int32 value; }
int32 main() {
    var first=new Box(); first.value=40;
    var second=new Box(); second.value=80;
    Box[] boxes=[first,second]; fn()->int32 callbacks[2]; int32 index=0;
    while(index<2 && boxes[index] is Box live) {
        callbacks[index]=function()=>live.value;
        index++;
    }
    if(index!=2 || callbacks[0]()!=40 || callbacks[1]()!=80){return 1;}
    delete callbacks[0]; delete callbacks[1]; delete boxes; delete first; delete second;
    return 0;
}
''',
    'pattern-struct-fixed': '''struct Packet { public int32 count; public int32 tags[3]; }
int32 main() {
    var original=new Packet(); original.count=40; original.tags[0]=2;
    fn()->int32 callback=original is Packet live ? function()=>live.count+live.tags[0] : function()=>0;
    original.count=99; original.tags[0]=8;
    int32 result=callback(); delete callback;
    return result==42?0:1;
}
''',
    'pattern-wide-loop': '''int32 main() {
    int128 value=((int128)1<<100)+17;
    int128 initial=value; fn()->int128 callbacks[2]; int32 index=0;
    while(index<2 && value is int128 live) {
        callbacks[index]=function()=>live;
        value+=7; index++;
    }
    if(index!=2 || callbacks[0]()!=initial || callbacks[1]()!=initial+7){return 1;}
    delete callbacks[0]; delete callbacks[1]; return 0;
}
''',
    'struct-fixed-parameter': 'struct Packet{int32 count;byte tag[3];}fn()->Packet Make(Packet value){return function()=>{value.count+=2;value.tag[1]+=1;return value;};}int32 main(){Packet original;original.count=40;original.tag[1]=4;var callback=Make(original);original.count=1;original.tag[1]=1;Packet first=callback();Packet second=callback();delete callback;return first.count==42 && first.tag[1]==5 && second.count==44 && second.tag[1]==6 && original.count==1 && original.tag[1]==1?0:1;}',
    'wide-parameter': 'fn()->int128 Make(int128 value){return function()=>{value+=3;return value;};}int32 main(){var callback=Make(1267650600228229401496703205376);int128 first=callback();int128 second=callback();delete callback;return first==1267650600228229401496703205379 && second==1267650600228229401496703205382?0:1;}',
    'ternary_pattern_typed': '''class Box {public int32 value;} int32 main(){var box=new Box();box.value=40;var callback=box is Box live ? function(int32 amount)=>live.value+amount : function(int32 amount)=>amount;int32 result=callback(2);delete callback;delete box;return result==42?0:1;}
''',
}


CASES['pattern-ref-values-copy-before-capture'] = '''struct Packet { public int32 count; public byte tags[3]; }
fn()->int32 Scalar(ref int32 value) {
    if(value is int32 live) { return function()=>live; }
    return function()=>0;
}
fn()->Packet Aggregate(ref Packet value) {
    if(value is Packet live) { return function()=>live; }
    return function()=>default(Packet);
}
int32 main() {
    int32 number=7; var scalar=Scalar(ref number); number=99;
    Packet original=default(Packet); original.count=40; original.tags[1]=2;
    var aggregate=Aggregate(ref original); original.count=99; original.tags[1]=8;
    Packet result=aggregate();
    int32 kept=scalar(); delete scalar; delete aggregate;
    return kept==7 && result.count==40 && result.tags[1]==2?0:1;
}
'''


CASES['pattern-struct-local-and-captured-source'] = '''struct Packet { public int32 count; public byte tags[3]; }
int32 main() {
    Packet original=default(Packet); original.count=40; original.tags[1]=2;
    var read=function()=>original.count;
    if(original is Packet live) {
        live.count+=2; live.tags[1]+=1;
        int32 result=live.count; delete read;
        return result==42 && original.count==40 && original.tags[1]==2?0:1;
    }
    delete read; return 2;
}
'''


CASES['pattern-wide-local-copy'] = '''int32 main() {
    int128 original=((int128)1<<100)+7;
    if(original is int128 live) {
        live+=3;
        return live==((int128)1<<100)+10 && original==((int128)1<<100)+7?0:1;
    }
    return 2;
}
'''


BORROWED_POINTER_PATTERN = '''fn()->int32 Keep() {
    unsafe(using krt.mem;) {
        int32 value=7;
        int32* source=&value;
        if(source is int32* live) { return function()=>*live; }
    }
    return function()=>0;
}
int32 main() { return 0; }
'''


class ClosurePatternTests(NativeCompilerFixture):
    def execute_targets(self, name):
        path = self.work / (name + '.krt')
        path.write_bytes(CASES[name].encode('utf-8'))
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

    def test_captured_wide_parameter_retains_full_width(self):
        self.execute_targets('wide-parameter')

    def test_aggregate_parameters_and_foreach_capture_value_copies(self):
        self.execute_targets('struct-fixed-parameter')
        self.execute_targets('foreach-struct-fixed')

    def test_condition_pattern_is_available_to_immediate_callback(self):
        self.execute_targets('pattern-condition-and-iife')

    def test_repeated_class_and_wide_patterns_create_independent_cells(self):
        self.execute_targets('pattern-loop-independent')
        self.execute_targets('pattern-wide-loop')

    def test_struct_fixed_pattern_copies_and_null_pattern_selects_false_branch(self):
        self.execute_targets('pattern-struct-fixed')
        self.execute_targets('pattern-false-null')
        self.execute_targets('pattern-false-incompatible-struct')
        self.execute_targets('pattern-false-incompatible-wide')
        self.execute_targets('pattern-ref-values-copy-before-capture')
        self.execute_targets('pattern-struct-local-and-captured-source')
        self.execute_targets('pattern-wide-local-copy')

    def test_typed_pattern_is_available_to_selected_ternary_callback(self):
        self.execute_targets('ternary_pattern_typed')

    def test_immediate_callbacks_preserve_unwinding_and_result_storage(self):
        self.execute_targets('iife-cleanup-return-and-throw')
        self.execute_targets('iife-wide-and-struct-results')

    def test_pointer_pattern_preserves_known_stack_borrow_rejection(self):
        from Test.SelfHost.test_closures import ClosureTests

        # Use the same strict diagnostic, repeatability and output preservation
        # contract as the original closure rejection suite.
        ClosureTests.reject_targets(self, 'pattern-borrowed-pointer-escape',
                                    BORROWED_POINTER_PATTERN, 'live;',
                                    'E_CAPTURE_BORROW')
