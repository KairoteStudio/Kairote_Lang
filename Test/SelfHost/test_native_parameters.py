"""Re.KrtC-compatible parameter boundaries through native and VM calling paths."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


class NativeParameterTests(NativeCompilerFixture):
    def source(self, count, method=False, indirect=False):
        parameters = ','.join(f'int64 a{i}' for i in range(count))
        arguments = ','.join(str(i+1) for i in range(count))
        addition = '+'.join(f'a{i}' for i in range(count))
        expected = count * (count + 1) // 2
        if method:
            return (f'class Sum{{public int64 offset;public int64 Calculate({parameters}){{return this.offset+{addition};}}}}'
                    f'int32 main(){{Sum value=new Sum();value.offset=7;int64 result=value.Calculate({arguments});'
                    f'delete value;return result=={expected+7} ? 0 : 1;}}')
        if indirect:
            parameters = ','.join(f'int64 a{i}' for i in range(1,count)) + ',ref int64 result'
            types = ','.join('int64' for _ in range(1,count)) + ',ref int64'
            arguments = ','.join(str(i) for i in range(1,count)) + ',ref result'
            addition = '+'.join(f'a{i}' for i in range(1,count))
            expected = 7 + count * (count-1) // 2
            return (f'int64 Calculate({parameters}){{result=result+{addition};return result;}}'
                    f'int32 main(){{unsafe(using krt.mem;){{fn({types})->int64 call=&Calculate;'
                    f'int64 result=7;int64 returned=call({arguments});'
                    f'return returned=={expected} && result=={expected} ? 0 : 1;}}}}')
        return (f'int64 Calculate({parameters}){{return {addition};}}'
                f'int32 main(){{return Calculate({arguments})=={expected} ? 0 : 1;}}')

    def test_ordinary_32_33_128_parameters(self):
        for count in (32,33,128):
            with self.subTest(parameters=count):
                self.execute(self.source(count))

    def test_method_32_33_128_total_parameters_including_receiver(self):
        for count in (31,32,127):
            with self.subTest(total_parameters=count+1):
                self.execute(self.source(count,method=True))

    def test_indirect_ref_calls_32_33_128_parameters(self):
        for count in (32,33,128):
            with self.subTest(parameters=count):
                self.execute(self.source(count,indirect=True))

    def test_128_mixed_precision_arguments_preserve_wide_scratch_and_live_values(self):
        types = ('int8','uint64','float32','uint128')
        parameters = ','.join(f'{types[index % 4]} a{index}' for index in range(128))
        arguments = ','.join(f'({types[index % 4]}){index+1}' for index in range(128))
        addition = '+'.join(f'(int64)a{index}' for index in range(128))
        self.execute(f'int64 Calculate({parameters}){{return {addition};}}'
                     f'int32 main(){{uint128 marker=(uint128)1<<80;int64 live=37;'
                     f'int64 result=Calculate({arguments});'
                     f'return result==8256 && live==37 && (marker>>80)==1 ? 0 : 1;}}')

    def test_vm_128_parameter_direct_method_and_indirect_ref_calls(self):
        for source in (self.source(128),self.source(127,method=True),self.source(128,indirect=True)):
            with self.subTest(source=source[:60]):
                code = self.work / 'parameters.krt'
                artifact = self.work / 'parameters.ebc'
                code.write_text(source)
                self.command(code,'target','vm','-o',artifact)
                self.command('run-vm',artifact)

    def test_excess_arity_is_diagnosed_without_publishing(self):
        for source in (self.source(129),self.source(128,method=True),self.source(129,indirect=True)):
            with self.subTest(source=source[:60]):
                code = self.work / 'excess.krt'
                output = self.work / 'existing.kro'
                code.write_text(source)
                output.write_bytes(b'previous object')
                result = subprocess.run([str(COMPILER),str(code),'-c','-o',str(output)],cwd=self.work,env=self.env,
                                        capture_output=True,text=True,timeout=30)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('E_PARSE',result.stderr)
                self.assertEqual(output.read_bytes(),b'previous object')
