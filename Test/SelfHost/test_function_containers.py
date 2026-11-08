"""Function-pointer containers retain their shape until explicitly indexed."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


class FunctionContainerTests(NativeCompilerFixture):
    def execute_vm_and_native(self, source):
        self.execute(source)
        path = self.work / 'function-containers.krt'
        path.write_text(source)
        for level in range(4):
            with self.subTest(vm_level=level):
                output = self.work / f'function-containers-o{level}.ebc'
                self.command(path, 'target', 'vm', f'-O{level}', '-o', output)
                result = subprocess.run([str(COMPILER), 'run-vm', str(output)], cwd=self.work,
                                        env=self.env, capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def reject_preserving_output(self, source):
        path = self.work / 'invalid-function-container.krt'
        path.write_text(source)
        for target, flags in [('kro', ['-c']), ('vm', ['target', 'vm'])]:
            for existing in (False, True):
                with self.subTest(target=target, existing=existing):
                    output = self.work / f'invalid-{target}-{existing}.artifact'
                    preserved = b'previous successful artifact\x00'
                    if existing:
                        output.write_bytes(preserved)
                    result = subprocess.run([str(COMPILER), str(path), *flags,
                                             '-O2', '-o', str(output)], cwd=self.work,
                                            env=self.env, capture_output=True, text=True,
                                            timeout=15)
                    self.assertGreater(result.returncode, 0, result.stdout + result.stderr)
                    self.assertRegex(result.stderr, r'E_[A-Z_]+')
                    self.assertNotIn('E_PARSE', result.stderr)
                    if existing:
                        self.assertEqual(output.read_bytes(), preserved)
                    else:
                        self.assertFalse(output.exists())
                    self.assertFalse(output.with_name(output.name + '.ebc').exists())

    def test_local_arrays_cannot_be_called_or_used_as_scalar_callbacks(self):
        prefix = '''int32 Read(){return 17;}
int32 Invoke(fn()->int32 callback){return callback();}
int32 main(){fn()->int32 callbacks[1];callbacks[0]=&Read;'''
        for body in ('return callbacks();', 'callbacks=&Read;return 0;',
                     'return Invoke(callbacks);'):
            with self.subTest(body=body):
                self.reject_preserving_output(prefix + body + '}')

    def test_indexed_local_callbacks_are_scalar_values(self):
        self.execute_vm_and_native('''
int32 Add(int32 value){return value+7;}
int32 Twice(int32 value){return value*2;}
int32 Invoke(fn(int32)->int32 callback,int32 value){return callback(value);}
int32 main(){
    fn(int32)->int32 callbacks[2];callbacks[0]=&Add;callbacks[1]=&Twice;
    if(callbacks.Length!=2 || callbacks[0](5)!=12 || Invoke(callbacks[1],9)!=18){return 1;}
    fn(int32)->int32 scalar=callbacks[0];callbacks[1]=scalar;callbacks[0]=&Twice;
    if(callbacks[0](5)!=10 || callbacks[1](5)!=12 || scalar(5)!=12){return 2;}
    return 0;
}
''')

    def test_dynamic_function_arrays_are_indexed_before_call_or_conversion(self):
        self.execute_vm_and_native('''
int32 Add(int32 value){return value+7;}
int32 Twice(int32 value){return value*2;}
int32 Invoke(fn(int32)->int32 callback,int32 value){return callback(value);}
T[] Make<T>(T first,T second){T[] callbacks=new T[2];callbacks[0]=first;callbacks[1]=second;return callbacks;}
int32 main(){
    var callbacks=Make(&Add,&Twice);
    if(callbacks.Length!=2 || callbacks[0](5)!=12 || Invoke(callbacks[1],9)!=18){return 1;}
    fn(int32)->int32 scalar=callbacks[0];callbacks[1]=scalar;
    if(callbacks[1](5)!=12 || scalar(5)!=12){return 2;}
    delete callbacks;return 0;
}
''')

    def test_function_pointer_storage_requires_dereference(self):
        self.execute_vm_and_native('''
int32 Add(int32 value){return value+7;}
int32 Twice(int32 value){return value*2;}
int32 Invoke(fn(int32)->int32 callback,int32 value){return callback(value);}
int32 Through<T>(T callback,int32 value){
    T* pointer=&callback;T copied=*pointer;
    if((*pointer)(value)!=12 || pointer[0](value)!=12 || Invoke(copied,value)!=12){return 1;}
    pointer[0]=&Twice;
    if((*pointer)(value)!=10 || copied(value)!=12 || callback(value)!=10){return 2;}
    return 0;
}
int32 main(){return Through(&Add,5);}
''')
        self.reject_preserving_output('''
int32 Read(){return 17;}
int32 Wrong<T>(T callback){T* pointer=&callback;return pointer();}
int32 main(){return Wrong(&Read);}
''')

    def test_fixed_class_callback_fields_copy_through_generic_value_abi(self):
        self.execute_vm_and_native('''
int32 Add(int32 value){return value+7;}
int32 Twice(int32 value){return value*2;}
class Calls{fn(int32)->int32 callbacks[2];int32 Invoke(int32 value){return callbacks[0](value);}}
T Copy<T>(T callbacks){callbacks[0]=&Twice;return callbacks;}
void Swap<T>(ref T callbacks){callbacks[1]=&Twice;}
int32 Invoke(fn(int32)->int32 callback,int32 value){return callback(value);}
int32 main(){
    Calls holder=new Calls();holder.callbacks[0]=&Add;holder.callbacks[1]=&Add;
    var copied=Copy(holder.callbacks);Swap(ref holder.callbacks);
    if(copied.Length!=2 || copied[0](5)!=10 || copied[1](5)!=12){return 1;}
    if(holder.Invoke(5)!=12 || Invoke(holder.callbacks[1],5)!=10){return 2;}
    holder.callbacks=copied;copied[0]=&Add;
    if(holder.Invoke(5)!=10 || copied[0](5)!=12 || holder.callbacks[1](5)!=12){return 3;}
    delete holder;return 0;
}
''')
