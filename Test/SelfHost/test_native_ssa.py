"""Exercise observable SSA graphs and native phi edge copies."""
import re
import subprocess

from Test.SelfHost.test_native_optimizer import NativeCompilerFixture


class NativeSsaTests(NativeCompilerFixture):
    def ir(self, source, level=0):
        path = self.work / 'graph.krt'
        output = self.work / 'graph.ir'
        path.write_text(source)
        self.command(path, 'target', 'ir', f'-O{level}', '-o', output)
        return output.read_text()

    def test_loop_cfg_has_explicit_value_ids_and_predecessor_phis(self):
        source = """
int64 rotate(int32 count,int64 a,int64 b){
    while(count>0){int64 saved=a;a=b;b=saved+1;count=count-1;}
    return a*1000+b;
}
int32 main(){return rotate(10,3,7)==8012 ? 0 : 1;}
"""
        self.execute(source)
        graph = self.ir(source)
        self.assertIn('typed SSA IR', graph)
        self.assertNotIn('load.local', graph)
        self.assertNotIn('store.local', graph)
        definitions = re.findall(r'^\s+%(\d+):i64 = ', graph, re.M)
        self.assertEqual(len(definitions), len(set(definitions)))
        labels = set(re.findall(r'^bb(\d+):', graph, re.M))
        edges = re.findall(r'\[bb(\d+):%(\d+)\]', graph)
        self.assertTrue(edges, graph)
        self.assertTrue(all(block in labels and value in definitions for block, value in edges))
        self.assertRegex(graph, r'= add\s+%\d+\s+%\d+')
        self.assertRegex(graph, r'branch\s+\d+\s+\d+\s+%\d+')

    def test_conditional_expression_values_survive_calls_and_phi_copies(self):
        source = """
static int32 visits=0;
int64 mark(int64 value){visits=visits+1;return value;}
int64 choose(bool condition,int64 a,int64 b){
    int64 left=condition?mark(a+3):mark(b+5);
    int64 right=condition?mark(b+7):mark(a+11);
    return left*100+right;
}
int32 main(){return choose(true,13,17)==1624 && choose(false,13,17)==2224 && visits==4 ? 0 : 1;}
"""
        self.execute(source)
        graph = self.ir(source)
        self.assertRegex(graph, r':i64 = phi .*\[bb\d+:%\d+\].*\[bb\d+:%\d+\]')

    def test_phi_integer_storage_preserves_explicit_narrowing(self):
        source = """
int64 sum(int32 count){
    int8 narrow=120;int64 result=0;
    while(count>0){narrow=(int8)(narrow+7);result=result+narrow;count=count-1;}
    return result;
}
int32 main(){return sum(3)==127-122-115 ? 0 : 1;}
"""
        self.execute(source)
        graph = self.ir(source)
        self.assertRegex(graph, r':i64 = phi ')
        self.assertRegex(graph, r'= cast -1 %\d+')
        self.assertNotRegex(graph, r':i(?:1|2|4|8) = phi ')

    def test_exception_edges_copy_values_preserved_during_finally(self):
        self.execute("""
int32 value(ref int32 result){try{return 17;}finally{try{throw 9;}catch(int32 caught){result=caught;}}}
int32 main(){int32 result=0;int32 returned=value(ref result);return returned+result==26 ? 0 : 1;}
""")
