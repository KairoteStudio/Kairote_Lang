"""Record pointers use completed element layouts in every storage form."""
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


class StructPointerLayoutTests(NativeCompilerFixture):
    def reject_pointer_arithmetic(self, source, name):
        path = self.work / (name + '.krt')
        path.write_text(source)
        for target in ('native', 'vm'):
            for level in range(4):
                for previous in (None, b'previous artifact\x00must survive'):
                    with self.subTest(source=name, target=target, optimization=level,
                                      previous_artifact=previous is not None):
                        output = self.work / f'{name}-{target}-o{level}-{"new" if previous is None else "old"}.artifact'
                        if previous is not None:
                            output.write_bytes(previous)
                        argv = [str(COMPILER), '--linker', str(LINKER), str(path),
                                f'-O{level}', '-o', str(output)]
                        if target == 'vm':
                            argv += ['target', 'vm']
                        result = subprocess.run(argv, cwd=self.work, env=self.env,
                                                capture_output=True, text=True, timeout=60)
                        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                        self.assertIn('E_LOWER', result.stderr)
                        self.assertNotIn('E_PARSE', result.stderr)
                        self.assertRegex(result.stderr, r':\d+:\d+: E_LOWER:')
                        if previous is None:
                            self.assertFalse(output.exists(), 'rejection created an output artifact')
                        else:
                            self.assertEqual(output.read_bytes(), previous)

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

    @staticmethod
    def forward_fields_source(wide):
        extra = 'int64 checksum;' if wide else ''
        size = 24 if wide else 16
        return '''
struct Packet{Node* head;Node** link;Node* slots[2];Node*[] rows;}
class Index{Node* head;Node** link;Node* slots[2];Node*[] rows;}
struct Node{int64 value;Node* next;EXTRA}
int32 main(){unsafe(using krt.mem;){
    if(sizeof(Node)!=SIZE){return 1;}
    Node[] nodes=new Node[3];nodes[0].value=17;nodes[1].value=25;nodes[2].value=42;
    Node* start=&nodes[0];nodes[0].next=&nodes[1];nodes[1].next=&nodes[2];nodes[2].next=start;
    Packet packet=default(Packet);packet.head=start;packet.link=&packet.head;
    packet.slots[0]=start;packet.slots[1]=&nodes[1];packet.rows=new Node*[2];
    packet.rows[0]=start;packet.rows[1]=&nodes[2];
    Index index=new Index();index.head=start;index.link=&index.head;
    index.slots[0]=start;index.slots[1]=&nodes[1];index.rows=new Node*[2];
    index.rows[0]=start;index.rows[1]=&nodes[2];
    if(packet.head[1].value!=25 || packet.link[0][2].value!=42 || packet.slots[1][1].value!=42){return 2;}
    if(packet.rows[0][1].value!=25 || packet.rows[1][0].value!=42){return 3;}
    if(index.head[1].value!=25 || index.link[0][2].value!=42 || index.slots[1][1].value!=42){return 4;}
    if(index.rows[0][1].value!=25 || index.rows[1][0].value!=42){return 5;}
    if(packet.head[0].next[0].next[0].next!=start){return 6;}
    if((int64)&packet.slots[1]-(int64)&packet.slots[0]!=8 ||
       (int64)&packet.rows[1]-(int64)&packet.rows[0]!=8){return 7;}
    Packet copied=packet;copied.head[1].value=31;
    if(nodes[1].value!=31 || index.head[1].value!=31){return 8;}
    delete packet.rows;delete index.rows;delete index;delete nodes;return 0;
}}
'''.replace('EXTRA', extra).replace('SIZE', str(size))

    @staticmethod
    def pointer_arithmetic_source():
        return '''
struct Node{int64 value;Node* next;int64 checksum;}
T Second<T>(T[] values){T* pointer=&values[0];pointer+=1;return *pointer;}
int32 main(){unsafe(using krt.mem;){
    Node[] nodes=new Node[4];nodes[0].value=17;nodes[1].value=25;nodes[2].value=42;nodes[3].value=59;
    Node* start=&nodes[0];Node* pointer=start;
    if((int64)(pointer+1)-(int64)pointer!=24 || (int64)(pointer-1)-(int64)pointer!=-24){return 1;}
    if((int64)(2+pointer)-(int64)pointer!=48){return 2;}
    pointer=2+pointer;if(pointer-start!=2 || pointer[0].value!=42 || (*pointer).value!=42){return 3;}
    Node* old=pointer++;if(old!=&nodes[2] || pointer!=&nodes[3]){return 4;}
    old=pointer--;if(old!=&nodes[3] || pointer!=&nodes[2]){return 5;}
    Node* updated=++pointer;if(updated!=&nodes[3] || pointer!=updated){return 6;}
    updated=--pointer;if(updated!=&nodes[2] || pointer!=updated){return 7;}
    pointer-=2;pointer+=1;if(pointer!=&nodes[1] || pointer-start!=1){return 8;}
    Node copied=Second(nodes);copied.value=71;
    if(copied.value!=71 || nodes[1].value!=25){return 9;}
    delete nodes;return 0;
}}
'''

    @staticmethod
    def typed_difference_source():
        return '''
struct Node{int64 value;Node* next;int64 checksum;}
int32 main(){unsafe(using krt.mem;){
    Node[] nodes=new Node[3];Node* first=&nodes[0];Node* last=&nodes[2];
    int64 forward=last-first;int64 backward=first-last;
    if(forward!=2 || backward!=-2){return 1;}
    delete nodes;return 0;
}}
'''

    @staticmethod
    def indexed_update_source(dynamic):
        declaration = 'Node*[] cursors=new Node*[2];' if dynamic else 'Node* cursors[2];'
        cleanup = 'delete cursors;' if dynamic else ''
        return '''
static int32 indices=0;
int32 Next(){indices++;return 0;}
struct Node{int64 value;Node* next;int64 checksum;}
int32 main(){unsafe(using krt.mem;){
    Node[] nodes=new Node[4];nodes[0].value=17;nodes[1].value=25;nodes[2].value=42;nodes[3].value=59;
    DECLARATION
    cursors[0]=&nodes[0];cursors[1]=&nodes[3];Node** pointer=&cursors[0];
    pointer[Next()]+=1;
    if((int64)cursors[0]-(int64)&nodes[0]!=24 || indices!=1){return 1;}
    if(cursors[0][0].value!=25 || cursors[1][0].value!=59){return 2;}
    pointer[Next()]-=1;if(cursors[0]!=&nodes[0] || indices!=2){return 3;}
    Node* old=cursors[Next()]++;if(old!=&nodes[0] || cursors[0]!=&nodes[1] || indices!=3){return 4;}
    Node* updated=++pointer[Next()];if(updated!=&nodes[2] || cursors[0]!=updated || indices!=4){return 5;}
    old=pointer[Next()]--;if(old!=&nodes[2] || cursors[0]!=&nodes[1] || indices!=5){return 6;}
    updated=--cursors[Next()];if(updated!=&nodes[0] || cursors[0]!=updated || indices!=6){return 7;}
    if(cursors[0][2].value!=42 || cursors[1][0].value!=59){return 8;}
    CLEANUP
    delete nodes;return 0;
}}
'''.replace('DECLARATION', declaration).replace('CLEANUP', cleanup)

    @staticmethod
    def receiver_copy_source():
        return '''
struct Counter{
    int64 value;int64 checksum;
    Counter(int64 n){value=n;checksum=n+1;}
    readonly Counter Copy(){return this;}
    Counter Snapshot(){Counter saved=this;this.value++;return saved;}
}
struct Box<T>{
    T value;int64 marker;
    readonly Box<T> Copy(){return this;}
    Box<T> Snapshot(){Box<T> saved=this;this.marker++;return saved;}
}
class Archive{
    readonly Counter saved;
    Archive(){saved=new Counter(17);}
    Counter Read(){return saved.Copy();}
    Counter MutatingRead(){return saved.Snapshot();}
}
int32 main(){
    Counter counter=new Counter(25);Counter copied=counter.Copy();copied.value=42;
    Counter snapshot=counter.Snapshot();
    if(counter.value!=26 || copied.value!=42 || copied.checksum!=26 || snapshot.value!=25){return 1;}
    Box<Counter> box=default(Box<Counter>);box.value=counter;box.marker=7;
    Box<Counter> copy=box.Copy();copy.value.value=59;copy.marker=11;
    Box<Counter> before=box.Snapshot();
    if(box.value.value!=26 || box.marker!=8 || copy.value.value!=59 || copy.marker!=11 || before.marker!=7){return 2;}
    Box<int64> scalar=default(Box<int64>);scalar.value=31;scalar.marker=13;
    Box<int64> scalar_copy=scalar.Copy();scalar_copy.value=71;
    if(scalar.value!=31 || scalar_copy.value!=71 || scalar_copy.marker!=13){return 3;}
    Archive archive=new Archive();Counter first=archive.Read();Counter second=archive.MutatingRead();
    first.value=83;second.value=89;Counter last=archive.Read();
    if(last.value!=17 || last.checksum!=18){return 4;}
    delete archive;return 0;
}
'''

    def test_forward_cyclic_node_pointers_in_struct_and_class_containers(self):
        for wide in (False, True):
            self.execute_targets(self.forward_fields_source(wide), f'forward-node-{24 if wide else 16}')

    def test_pointer_arithmetic_and_indexed_updates_use_value_element_stride_once(self):
        self.execute_targets(self.pointer_arithmetic_source(), 'node-pointer-arithmetic')
        self.execute_targets(self.typed_difference_source(), 'typed-pointer-difference')
        for dynamic in (False, True):
            self.execute_targets(self.indexed_update_source(dynamic), f'indexed-cursors-{dynamic}')

    def test_readonly_and_generic_receivers_copy_and_return_this_as_values(self):
        self.execute_targets(self.receiver_copy_source(), 'receiver-value-copies')

    def test_reject_invalid_pointer_offsets_operators_and_reference_arithmetic(self):
        pointer_setup = 'Node[] nodes=new Node[2];Node* p=&nodes[0];Node* q=&nodes[1];int32 n=1;int32* other=&n;'
        pointer_statements = {
            'pointer-plus-pointer': 'p+q;',
            'integer-minus-pointer': 'n-p;',
            'pointer-multiply': 'p*2;',
            'pointer-divide': 'p/2;',
            'pointer-remainder': 'p%2;',
            'pointer-bit-and': 'p&1;',
            'pointer-bit-or': 'p|1;',
            'pointer-bit-xor': 'p^1;',
            'pointer-shift-left': 'p<<1;',
            'pointer-shift-right': 'p>>1;',
            'pointer-plus-float': 'p+1.5;',
            'float-plus-pointer': '1.5+p;',
            'pointer-minus-float': 'p-1.5;',
            'pointer-plus-bool': 'p+true;',
            'bool-plus-pointer': 'true+p;',
            'pointer-minus-bool': 'p-false;',
            'pointer-plus-null': 'p+null;',
            'null-plus-pointer': 'null+p;',
            'pointer-minus-null': 'p-null;',
            'different-pointee-difference': 'p-other;',
            'pointer-multiply-assign': 'p*=2;',
            'pointer-divide-assign': 'p/=2;',
            'pointer-remainder-assign': 'p%=2;',
            'pointer-bit-and-assign': 'p&=1;',
            'pointer-bit-or-assign': 'p|=1;',
            'pointer-bit-xor-assign': 'p^=1;',
            'pointer-shift-left-assign': 'p<<=1;',
            'pointer-shift-right-assign': 'p>>=1;',
            'pointer-plus-assign-pointer': 'p+=q;',
            'pointer-minus-assign-pointer': 'p-=q;',
            'pointer-plus-assign-float': 'p+=1.5;',
            'pointer-minus-assign-float': 'p-=1.5;',
            'pointer-plus-assign-bool': 'p+=true;',
            'pointer-minus-assign-bool': 'p-=false;',
            'pointer-plus-assign-null': 'p+=null;',
            'pointer-minus-assign-null': 'p-=null;',
        }
        for name, statement in pointer_statements.items():
            source = 'struct Node{int64 value;Node* next;int64 checksum;}int32 main(){unsafe(using krt.mem;){'
            source += pointer_setup + statement + 'return 0;}}'
            self.reject_pointer_arithmetic(source, name)
        reference_setups = {
            'class': ('class Reference{}', 'Reference value=new Reference();'),
            'dynamic-array': ('', 'int32[] value=new int32[2];'),
            'function-value': ('int32 Identity(){return 0;}', 'fn()->int32 value=&Identity;'),
        }
        for shape, (declarations, initialization) in reference_setups.items():
            for name, statement in {'addition': 'value+1;', 'post-increment': 'value++;',
                                    'pre-increment': '++value;'}.items():
                source = declarations + 'int32 main(){unsafe(using krt.mem;){'
                source += initialization + statement + 'return 0;}}'
                self.reject_pointer_arithmetic(source, shape + '-' + name)
        self.reject_pointer_arithmetic(
            'int32 main(){unsafe(using krt.mem;){int32[] value=new int32[2];value+=1;return 0;}}',
            'dynamic-array-plus-assign')
