"""Execute native constructor, array literal and foreach behavior end to end."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.bootstrap import Bootstrap


class NativeCollectionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='krt-native-collections-')
        cls.build = Bootstrap(Path(cls.directory.name))
        cls.compiler = Path(os.environ['SELFHOST_COMPILER']).resolve() if 'SELFHOST_COMPILER' in os.environ else cls.build.seed()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, name, source, expected=0):
        binary, _ = self.build.compile(self.compiler, source, name)
        result = subprocess.run([binary], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, expected, result.stderr.decode())

    def test_constructor_fields_methods_overloads_and_initializers(self):
        self.check('constructors', '''
class Box {
    public int32 base = 7;
    public int32 value = base + 2;
    public Box(int32 value) { this.value += value; }
    public Box(int32 x, int32 y) { value = x + y + base; }
    public int32 Get() { return value; }
}
class Default { public int32 x = 19; }
int32 main() {
    Box a = new Box(3); Box b = new Box(10, 20); Default c = new Default();
    if (a.Get() != 12 || b.Get() != 37 || c.x != 19) { return 1; }
    if (new Box(5).Get() != 14) { return 2; }
    delete a; delete b; delete c; return 0;
}
''')

    def test_constructor_argument_order_ref_and_nested_calls(self):
        self.check('constructor-order', '''
class Box {
    public int32 value;
    public Box(ref int32 counter, int32 x, int32 y) { value = x * 10 + y; counter += 10; }
}
int32 bump(ref int32 n) { n++; return n; }
int32 main() {
    int32 n=0; Box b=new Box(ref n,bump(ref n),bump(ref n));
    if (n!=12 || b.value!=12) { return 1; } delete b; return 0;
}
''')

    def test_typed_inferred_empty_and_returned_array_literals(self):
        self.check('array-literals', '''
byte[] small() { return [1, 255, 42]; }
int32 main() {
    int16[] a=[-3, 2000, 17]; byte[] b=small(); int64[] empty=[];
    var inferred=[4, 7, 9];
    if (a.Length!=3 || b.Length!=3 || empty.Length!=0 || empty==null) { return 1; }
    if (a[0]!=-3 || a[1]!=2000 || b[1]!=255 || inferred[1]!=7 || [9,8][0]!=9) { return 2; }
    delete a; delete b; delete empty; delete inferred; return 0;
}
''')

    def test_array_literal_evaluation_order_and_objects(self):
        self.check('array-order', '''
class Box { public int32 n; public Box(int32 x) { n=x; } }
int32 bump(ref int32 n) { n++; return n; }
int32 main() {
    int32 n=0; int32[] a=[bump(ref n),bump(ref n),bump(ref n)];
    Box[] objects=[new Box(17),new Box(25)];
    if (n!=3 || a[0]!=1 || a[2]!=3 || objects[0].n+objects[1].n!=42) { return 1; }
    foreach (Box b in objects) { delete b; } delete objects; delete a; return 0;
}
''')

    def test_foreach_signed_empty_nested_break_continue_and_string(self):
        self.check('foreach-control', '''
int32 main() {
    int8[] values=[-2, 3, 8, 17]; int32 sum=0;
    foreach (int32 x in values) { if(x==3) { continue; } if(x==17) { break; } sum+=x; }
    if(sum!=6) { return 1; }
    foreach (var x in [1,2,3]) { foreach (int32 y in [4,5]) { sum+=x*y; } }
    if(sum!=60) { return 2; }
    int32 count=0; foreach(char c in "abc") { count+=c; }
    if(count!=294) { return 3; }
    int32[] empty=[]; foreach(var x in empty) { return 4; }
    delete empty; delete values; return 0;
}
''')

    def test_foreach_collection_evaluated_once_and_variable_scope(self):
        self.check('foreach-evaluation', '''
int32[] make(ref int32 n) { n++; return [3,5,7]; }
int32 main() {
    int32 n=0; int32 sum=0;
    foreach(var x in make(ref n)) { sum+=x; }
    int32 x=9;
    if(n!=1 || sum!=15 || x!=9) { return 1; } return 0;
}
''')

    def test_string_arrays_and_foreach(self):
        self.check('string-arrays', '''
int32 main() {
    string[] words=["ab","cde",""]; int32 total=0;
    foreach(string word in words) { total+=word.Length; }
    if(total!=5 || words[0]!="ab" || words[1].Length!=3) { return 1; }
    delete words; return 0;
}
''')

    def test_contextual_call_literals_and_float_collections(self):
        self.check('context-and-float', '''
int32 sum(byte[] values){int32 n=0;foreach(var x in values){n+=x;}return n;}
class Number {public float64 value;public Number(float64 x){value=x;}}
int32 main(){
    if(sum([1,2,255])!=258 || sum([])!=0){return 1;}
    float32[] values=[1,2.5,3.25];float64 total=0.0;
    foreach(float64 x in values){total+=x;}
    Number n=new Number(17);if(total!=6.75 || n.value!=17.0){return 2;}
    delete n;delete values;return 0;
}
''')

    def test_index_results_used_as_indices_sizes_and_strings(self):
        self.check('nested-indices', '''
int32 main(){int32[] values=[5,8];int32[] indices=[1];byte[] data=new byte[indices[0]];
string[] words=["ab","cd"];string word=words[0];
if(values[indices[0]]!=8 || data.Length!=1 || words[0][1]!=98 || word!="ab"){return 1;}
delete values;delete indices;delete data;delete words;return 0;}
''')

    def test_diagnostics_reject_bad_constructor_or_collection(self):
        cases = {
            'missing-constructor': 'class Box { public Box(int32 x) {} } int32 main(){Box b=new Box();return 0;}',
            'unknown-constructor': 'class Box {} int32 main(){Box b=new Box(1);return 0;}',
            'wrong-constructor-type': 'class Box {public Box(int32 x){}} int32 main(){Box b=new Box("bad");return 0;}',
            'non-array-foreach': 'int32 main(){foreach(int32 x in 17){}return 0;}',
            'scope-escape': 'int32 main(){foreach(var x in [1,2]){}return x;}',
            'mixed-array': 'int32 main(){int32[] a=[1,"bad"];return 0;}',
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                work=Path(self.directory.name)/name;work.mkdir()
                (work/'program.krt').write_text(source)
                result=subprocess.run([self.compiler],cwd=work,capture_output=True,text=True,timeout=10)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('E_LOWER',result.stdout+result.stderr)
                self.assertFalse((work/'stage1-probe.kro').exists())


if __name__ == '__main__':
    unittest.main()
