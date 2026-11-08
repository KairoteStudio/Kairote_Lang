"""Abrupt exits retain cleanup and semantic checks for unreachable source."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


class ControlFlowTests(NativeCompilerFixture):
    def execute_targets(self, source, name):
        path = self.work / (name + '.krt')
        path.write_text(source)
        for target in ('native', 'vm'):
            for level in range(4):
                with self.subTest(source=name, target=target, optimization=level):
                    output = self.work / f'{name}-{target}-o{level}'
                    flags = ['target', 'vm'] if target == 'vm' else []
                    self.command(path, f'-O{level}', *flags, '-o', output)
                    argv = [str(COMPILER), 'run-vm', str(output)] if target == 'vm' else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def reject_targets(self, source, name, code='E_LOWER'):
        path = self.work / (name + '.krt')
        path.write_text(source)
        for target in ('native', 'vm'):
            for level in range(4):
                for previous in (None, b'previous artifact\x00must survive'):
                    with self.subTest(source=name, target=target, optimization=level,
                                      previous_artifact=previous is not None):
                        output = self.work / f'{name}-{target}-o{level}-{previous is not None}'
                        if previous is not None:
                            output.write_bytes(previous)
                        flags = ['target', 'vm'] if target == 'vm' else []
                        result = subprocess.run([str(COMPILER), '--linker', str(LINKER), str(path),
                                                 f'-O{level}', *flags, '-o', str(output)],
                                                cwd=self.work, env=self.env, capture_output=True,
                                                text=True, timeout=60)
                        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                        self.assertRegex(result.stderr, rf':\d+:\d+: {code}:')
                        self.assertNotIn('E_PARSE', result.stderr)
                        if previous is None:
                            self.assertFalse(output.exists(), 'rejection created an output artifact')
                        else:
                            self.assertEqual(output.read_bytes(), previous)

    def test_router_early_returns_and_nested_permission_blocks(self):
        self.execute_targets('''
static int32 dead=0;
struct Response{int32 status;string body;int64 bytes;}
bool Ready(){return true;}
bool Available(){return Ready();dead++;return false;}
Response Make(int32 status,string body){
    Response response=default(Response);response.status=status;response.body=body;response.bytes=body.Length;
    return response;dead++;return default(Response);
}
Response Route(string method,string path){
    if(!Available()){return Make(503,"busy");dead++;}
    if(method=="GET"){
        if(path=="/health"){return Make(200,"ok");dead++;}
        if(path=="/item"){return Make(200,"record");dead++;}
        return Make(404,"missing");dead++;return default(Response);
    }else{return Make(405,"method");dead++;}
    dead++;return default(Response);
}
Response Wrapped(){
    {unsafe(using krt.mem;){point{return Route("GET","/health");dead++;}dead++;}dead++;}
    dead++;return default(Response);
}
int32 Mode(int32 mode){
    switch(mode){case 0:point{return 7;dead++;}default:unsafe(using krt.mem;){return 9;dead++;}}
}
int32 main(){
    Response healthy=Route("GET","/health");Response item=Route("GET","/item");
    Response missing=Route("GET","/missing");Response method=Route("POST","/item");Response wrapped=Wrapped();
    if(healthy.status!=200||healthy.body!="ok"||healthy.bytes!=2){return 1;}
    if(item.status!=200||item.body!="record"||item.bytes!=6){return 2;}
    if(missing.status!=404||method.status!=405||wrapped.status!=200||Mode(0)!=7||Mode(1)!=9){return 3;}
    return dead==0?0:4;dead++;
}
''', 'router-early-return')

    def test_struct_returns_catch_completion_and_finally_override(self):
        self.execute_targets('''
static int32 dead=0;static int32 cleanups=0;
struct Response{int32 status;int64 bytes;string body;}
Response Make(int32 status,string body){Response value=default(Response);value.status=status;value.body=body;value.bytes=body.Length;return value;}
Response Snapshot(ref Response value){
    try{return value;dead++;}finally{value.status=503;value.bytes=0;cleanups++;}
    dead++;return default(Response);
}
Response Override(){
    Response original=Make(200,"original");
    try{return original;dead++;}finally{cleanups++;Response replacement=Make(202,"accepted");return replacement;dead++;}
    dead++;return default(Response);
}
int32 Resolve(bool failed){
    try{if(failed){throw 17;dead++;}return 3;dead++;}
    catch(int32 error){return error;dead++;}finally{cleanups++;}
    dead++;return -1;
}
int32 CleanupReturn(){try{cleanups++;}finally{return 9;dead++;}dead++;return -1;}
int32 Raise(){throw 19;dead++;return 0;}
int32 main(){
    Response input=Make(200,"payload");Response saved=Snapshot(ref input);
    if(saved.status!=200||saved.bytes!=7||saved.body!="payload"||input.status!=503||input.bytes!=0){return 1;}
    Response replaced=Override();if(replaced.status!=202||replaced.bytes!=8||replaced.body!="accepted"){return 2;}
    if(Resolve(false)!=3||Resolve(true)!=17||CleanupReturn()!=9){return 3;}
    int32 caught=0;try{Raise();}catch(int32 value){caught=value;}
    if(caught!=19||cleanups!=5||dead!=0){return 4;}return 0;
}
''', 'return-cleanup')

    def test_loop_exits_skip_dead_effects_and_switch_break_keeps_fallthrough(self):
        self.execute_targets('''
static int32 dead=0;static int32 cleanups=0;
int32 Collect(){
    int32 total=0;
    for(int32 i=0;i<6;i++){
        try{if(i<2){continue;dead++;}if(i==4){break;dead++;}total+=i;}
        finally{cleanups++;}
    }
    return total;dead++;return -1;
}
int32 main(){
    if(Collect()!=5||cleanups!=5){return 1;}
    int32 total=0;
    for(int32 i=0;i<4;i++){
        switch(i){case 0:continue;dead++;case 1:total+=10;break;dead++;default:total+=20;break;dead++;}
        total++;
    }
    if(total!=53){return 2;}
    while(true){{unsafe(using krt.mem;){point{break;dead++;}dead++;}dead++;}dead++;}
    int32 turns=0;
    do{turns++;{if(turns<3){continue;dead++;}else{break;dead++;}dead++;}dead++;}while(turns<9);
    if(turns!=3||dead!=0){return 3;}return 0;
}
''', 'loop-exits')

    def test_unreachable_source_keeps_type_name_loop_and_return_checks(self):
        cases = {
            'dead-type': 'int32 main(){return 0;int32 value="wrong";}',
            'dead-name': 'int32 main(){return 0;missing++;}',
            'dead-return-type': 'int32 main(){return 0;return "wrong";}',
            'dead-break': 'int32 main(){return 0;break;}',
            'dead-continue': 'int32 main(){return 0;continue;}',
            'dead-nested-break': 'int32 main(){{return 0;}{unsafe(using krt.mem;){point{break;}}}}',
            'dead-finally-continue': 'int32 main(){try{return 0;}finally{return 0;continue;}}',
            'dead-catch-break': 'int32 main(){try{return 0;}catch(int32 value){return value;break;}}',
            'switch-only-continue': 'int32 main(){switch(0){case 0:return 0;continue;default:return 1;}}',
            'missing-if-return': 'int32 Read(bool condition){if(condition){return 7;}}int32 main(){return Read(true);}',
            'missing-switch-return': 'int32 Read(int32 value){switch(value){case 0:break;return 7;default:return 1;}}int32 main(){return Read(0);}',
            'missing-catch-return': 'int32 Read(){try{return 7;}catch(int32 value){value++;}}int32 main(){return Read();}',
            'missing-loop-return': 'int32 Read(){while(false){break;return 7;}}int32 main(){return Read();}',
        }
        for name, source in cases.items():
            self.reject_targets(source, name)

    def test_loop_and_switch_depth_budget_applies_to_unreachable_source(self):
        def nested(count):
            contexts = ['while(false){' if index % 2 == 0 else 'switch(0){default:'
                        for index in range(count)]
            return ''.join(contexts) + 'break;' + '}' * count

        self.execute_targets('int32 main(){' + nested(64) + 'return 0;}', 'context-limit')
        self.reject_targets('int32 main(){return 0;' + nested(65) + '}', 'dead-context-overflow')
