"""Unbounded inheritance distances and wide overload rank accumulation."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


class DeepInheritanceTests(NativeCompilerFixture):
    def execute_native_and_vm(self, source):
        self.execute(source, levels=(0, 2))
        path=self.work/'deep-inheritance.krt';path.write_text(source)
        output=self.work/'deep-inheritance.vm'
        self.command(path,'target','vm','-O2','-o',output)
        result=subprocess.run([str(COMPILER),'run-vm',str(output)],cwd=self.work,
                              env=self.env,capture_output=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_beyond_1024_inherited_methods_upcasts_indirect_arguments_and_nearest_overloads(self):
        depth=1026
        classes=['class C0{int32 Read(){return 17;}}']
        classes += [f'class C{index}:C{index-1}{{}}' for index in range(1,depth+1)]
        source='\n'.join(classes)+f'''
int32 Consume(C0 value){{return value.Read();}}
int32 Forward(C0 value){{return 1;}}
int32 Forward(C1 value){{return 2;}}
int32 Reverse(C1 value){{return 2;}}
int32 Reverse(C0 value){{return 1;}}
int32 main(){{
    C{depth} value=new C{depth}();
    if(value.Read()!=17 || Consume(value)!=17){{return 1;}}
    fn(C0)->int32 callback=&Consume;
    if(callback(value)!=17){{return 2;}}
    if(Forward(value)!=2 || Reverse(value)!=2){{return 3;}}
    delete value;return 0;
}}
'''
        self.execute_native_and_vm(source)

    def test_128_argument_overload_ranks_keep_precision_in_both_declaration_orders(self):
        base_parameters=','.join(f'Base p{index}' for index in range(128))
        child_parameters=','.join(f'Child p{index}' for index in range(128))
        arguments=','.join(['value']*128)
        source=f'''
class Base{{int32 Read(){{return 7;}}}}
class Child:Base{{}}
int32 Forward({base_parameters}){{return p127.Read()+1;}}
int32 Forward({child_parameters}){{return p127.Read()+2;}}
int32 Reverse({child_parameters}){{return p127.Read()+2;}}
int32 Reverse({base_parameters}){{return p127.Read()+1;}}
int32 main(){{
    Child value=new Child();
    if(Forward({arguments})!=9 || Reverse({arguments})!=9){{return 1;}}
    delete value;return 0;
}}
'''
        self.execute_native_and_vm(source)
