"""String updates preserve the normal lvalue and operand evaluation order."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, NativeCompilerFixture


class StringCompoundTests(NativeCompilerFixture):
    def execute_vm_and_native(self, source):
        self.execute(source)
        path = self.work / 'string-update.krt'
        path.write_text(source)
        for level in range(4):
            with self.subTest(vm_level=level):
                output = self.work / f'string-update-o{level}.ebc'
                self.command(path, 'target', 'vm', f'-O{level}', '-o', output)
                result = subprocess.run([str(COMPILER), 'run-vm', str(output)],
                                        cwd=self.work, env=self.env, capture_output=True,
                                        timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_local_ref_null_alias_and_for_increment(self):
        self.execute_vm_and_native('''
void Add(ref string text,string suffix){text+=suffix;}
int32 main(){
    string text=null;text+="HTTP";string original=text;
    Add(ref text,"/1.1");text+=null;
    if(text!="HTTP/1.1" || original!="HTTP"){return 1;}
    text+=text;if(text!="HTTP/1.1HTTP/1.1"){return 2;}
    string line="";for(int32 i=0;i<3;line+="x"){i++;}
    if(line!="xxx"){return 3;}return 0;
}
''')

    def test_struct_field_captures_old_value_before_rhs_mutation(self):
        self.execute_vm_and_native('''
struct Response{public string text;}
string Append(ref Response response){response.text="changed";return "!";}
int32 main(){Response response=default(Response);response.text="ok";
    response.text+=Append(ref response);
    return response.text=="ok!"?0:1;
}
''')

    def test_receiver_index_rhs_each_evaluated_once_in_source_order(self):
        self.execute_vm_and_native('''
class Headers{public string value="A";}
Headers Target(Headers headers,ref int32 trace){trace=trace*10+1;return headers;}
int32 Index(ref int32 trace){trace=trace*10+2;return 0;}
string Suffix(ref int32 trace){trace=trace*10+3;return "B";}
int32 main(){Headers headers=new Headers();int32 trace=0;
    Target(headers,ref trace).value+=Suffix(ref trace);
    if(trace!=13 || headers.value!="AB"){return 1;}
    trace=0;string[] lines=["C"];lines[Index(ref trace)]+=Suffix(ref trace);
    if(trace!=23 || lines[0]!="CB"){return 2;}
    delete lines;delete headers;return 0;
}
''')

    def test_incompatible_operations_and_readonly_targets_preserve_outputs(self):
        sources = ['int32 main(){string text="x";' + body + 'return 0;}'
                   for body in ('text-= "y";', 'text*=2;', 'text/=2;', 'text%=2;',
                                'text&="y";', 'text|="y";', 'text^="y";',
                                'text<<=1;', 'text>>=1;', 'text+=1;')]
        sources += ['class Header{public readonly string value="x";}'
                    'int32 main(){Header header=new Header();header.value+="y";return 0;}']
        for source in sources:
            path = self.work / 'invalid-string-update.krt'
            path.write_text(source)
            for flags in (['-c'], ['target', 'vm']):
                with self.subTest(source=source, flags=flags):
                    output = self.work / 'previous.artifact'
                    preserved = b'previous successful artifact\x00'
                    output.write_bytes(preserved)
                    result = subprocess.run([str(COMPILER), str(path), *flags,
                                             '-O2', '-o', str(output)], cwd=self.work,
                                            env=self.env, capture_output=True, text=True,
                                            timeout=15)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertRegex(result.stderr, r'E_[A-Z_]+')
                    self.assertNotIn('E_PARSE', result.stderr)
                    self.assertEqual(output.read_bytes(), preserved)


if __name__ == '__main__':
    import unittest
    unittest.main()
