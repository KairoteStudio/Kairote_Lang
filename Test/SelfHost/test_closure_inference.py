"""Closure type inference and contextual overload probes use isolated state."""
import hashlib
import subprocess

from Test.SelfHost.test_native_optimizer import COMPILER, LINKER, NativeCompilerFixture


# Source bytes are retained from the independent candidate audit.
BASELINE_COMPILER_SHA256 = "5a60ec2b856afb63be5dbd66a71785f28c37980dc7736cb4e9f9ee90ad25a766"
VERIFIED_COMPILER_SHA256 = "bfe445f727e46907e355393745c9373b7db7063be5aa2c33ced5f380189d2f1b"

POSITIVE_CASES = {
    "ternary_pattern_context": "class Box {public int32 value;} int32 main(){var box=new Box();box.value=40;fn(int32)->int32 callback=box is Box live ? function(amount)=>live.value+amount : function(amount)=>amount;int32 result=callback(2);delete callback;delete box;return result==42?0:1;}\n",
    "ternary_pattern_typed": "class Box {public int32 value;} int32 main(){var box=new Box();box.value=40;var callback=box is Box live ? function(int32 amount)=>live.value+amount : function(int32 amount)=>amount;int32 result=callback(2);delete callback;delete box;return result==42?0:1;}\n",
    "ternary_pattern_control": "class Box {public int32 value;} int32 main(){var box=new Box();box.value=40;int32 result=box is Box live ? live.value+2 : 0;delete box;return result==42?0:1;}\n",
    "wide_return_inference_narrow_first": "int32 main(){int64 small=1;int128 large=1267650600228229401496703205376;var callback=function(int32 flag)=>{if(flag==0){return small;}return large;};int128 result=callback(1);delete callback;return result==large?0:1;}\n",
    "wide_return_inference_wide_first": "int32 main(){int64 small=1;int128 large=1267650600228229401496703205376;var callback=function(int32 flag)=>{if(flag!=0){return large;}return small;};int128 result=callback(1);delete callback;return result==large?0:1;}\n",
    "stack_scalar_copy": "int32 main(){unsafe(using krt.mem;){int32* values=stackalloc int32[1];values[0]=42;var scalar=values[0];var callback=function()=>scalar;int32 result=callback();delete callback;return result==42?0:1;}}\n",
    "capture_then_nested_shadow": "int32 main(){int32 value=40;var callback=function()=>{int32 result=value;{int32 value=2;result+=value;}return result;};int32 result=callback();delete callback;return result==42?0:1;}\n",
    "normal_nested_shadow_control": "int32 main(){int32 value=40;int32 result=value;{int32 value=2;result+=value;}return result==42?0:1;}\n",
    "probe_generic_pollution": "T Increment<T>(T value){return value+1;}int32 Choose(fn(int32)->int32 callback){return callback(41);}int32 Choose(fn(string)->int32 callback){return callback(\"bad\");}int32 main(){return Choose(function(value)=>Increment(value))==42?0:1;}\n",
    "probe_generic_control": "T Increment<T>(T value){return value+1;}int32 Choose(fn(int32)->int32 callback){return callback(41);}int32 main(){return Choose(function(value)=>Increment(value))==42?0:1;}\n",
    "typed_only_generic_callback": "T Apply<T>(fn(T)->T callback,T initial){return callback(initial);}int32 main(){return Apply(function(int32 value)=>value+1,41)==42?0:1;}\n",
    "typed_only_generic_single_callback": "T Apply<T>(fn(T)->T callback){return callback(default(T));}int32 main(){return Apply(function(int32 value)=>value+42)==42?0:1;}\n",
    "typed_only_generic_variable_control": "T Apply<T>(fn(T)->T callback){return callback(default(T));}int32 main(){var callback=function(int32 value)=>value+42;int32 result=Apply(callback);delete callback;return result==42?0:1;}\n",
    "nested_this_base": "class Parent{protected int32 seed=5;public virtual int32 Read(){return 7;}}class Child:Parent{public override int32 Read(){return 27;}public fn()->fn()->int32 Make(){return function()=>function()=>base.Read()+Read()+seed;}public void Change(){seed=8;}}int32 main(){var owner=new Child();var factory=owner.Make();var callback=factory();owner.Change();int32 result=callback();delete callback;delete factory;delete owner;return result==42?0:1;}\n",
    "return_nullable_object": "class Box{public int32 value=42;}int32 main(){var callback=function(bool present)=>{if(present){return new Box();}return null;};var object=callback(true);int32 result=0;if(object is Box live){result=live.value;}bool absent=callback(false)==null;delete object;delete callback;return result==42 && absent?0:1;}\n",
    "return_nullable_object_expression": "class Box{public int32 value=42;}int32 main(){var callback=function(bool present)=>present?new Box():null;var object=callback(true);int32 result=0;if(object is Box live){result=live.value;}bool absent=callback(false)==null;delete object;delete callback;return result==42 && absent?0:1;}\n",
    "return_common_base_derived_first": "class Parent{public virtual int32 Read(){return 40;}}class Child:Parent{public override int32 Read(){return 42;}}int32 main(){var callback=function(bool child)=>{if(child){return new Child();}return new Parent();};var object=callback(true);int32 result=object.Read();delete object;delete callback;return result==42?0:1;}\n",
    "return_common_base_base_first": "class Parent{public virtual int32 Read(){return 40;}}class Child:Parent{public override int32 Read(){return 42;}}int32 main(){var callback=function(bool child)=>{if(!child){return new Parent();}return new Child();};var object=callback(true);int32 result=object.Read();delete object;delete callback;return result==42?0:1;}\n",
    "return_common_base_siblings": "class Parent{public virtual int32 Read(){return 40;}}class Child:Parent{public override int32 Read(){return 42;}}class Other:Parent{public override int32 Read(){return 43;}}int32 main(){var callback=function(bool child)=>{if(child){return new Child();}return new Other();};var object=callback(true);int32 result=object.Read();delete object;delete callback;return result==42?0:1;}\n",
    "callback_label_ternary_control": "int32 First(int32 left){return left+1;}int32 Second(int32 right){return right+2;}int32 main(){bool first=false;var callback=first?&First:&Second;return callback(40)==42?0:1;}\n",
    "callback_label_literal_true": "int32 main(){int32 offset=2;bool first=true;var callback=first?function(int32 left)=>left+offset:function(int32 right)=>right+offset+1;int32 result=callback(40);delete callback;return result==42?0:1;}\n",
    "callback_label_literal_false": "int32 main(){int32 offset=1;bool first=false;var callback=first?function(int32 left)=>left+offset:function(int32 right)=>right+offset+1;int32 result=callback(40);delete callback;return result==42?0:1;}\n",
    "callback_label_shared_function": "int32 First(int32 value){return value+1;}int32 Second(int32 value){return value+2;}int32 main(){bool first=false;var callback=first?&First:&Second;return callback(value:40)==42?0:1;}\n",
    "callback_label_shared_literal": "int32 main(){int32 offset=1;bool first=false;var callback=first?function(int32 value)=>value+offset:function(int32 value)=>value+offset+1;int32 result=callback(value:40);delete callback;return result==42?0:1;}\n",
    "callback_label_nested_result": "int32 First(int32 left){return left+1;}int32 Second(int32 right){return right+2;}int32 main(){bool first=false;var factory=first?function()=>&First:function()=>&Second;var callback=factory();int32 result=callback(40);delete factory;return result==42?0:1;}\n",
    "callback_label_deep_result": "int32 First(int32 left){return left+1;}int32 Second(int32 right){return right+2;}int32 main(){bool first=false;var outer=first?function()=>function()=>&First:function()=>function()=>&Second;var factory=outer();var callback=factory();int32 result=callback(40);delete factory;delete outer;return result==42?0:1;}\n",
    "typed_only_generic_nominal": "struct Box<T>{T item;}T Apply<T>(fn(T)->T callback){return callback(default(T));}int32 main(){var box=Apply(function(Box<int32> value)=>{value.item=42;return value;});return box.item==42?0:1;}\n",
    "typed_only_generic_nested_callback": "T Apply<T>(fn()->T callback){return callback();}int32 main(){var callback=Apply(function()=>function(int32 value)=>value+1);int32 result=callback(41);delete callback;return result==42?0:1;}\n",
    "probe_generic_class_pollution": "struct Box<T>{T item;}Box<T> Wrap<T>(T value){Box<T> result=default(Box<T>);result.item=value;return result;}int32 Choose(fn(int32)->int32 callback){return callback(41);}int32 Choose(fn(string)->int32 callback){return callback(\"bad\");}int32 main(){return Choose(function(value)=>Wrap(value).item+1)==42?0:1;}\n",
    "return_nullable_callback": "int32 Answer(){return 42;}int32 main(){var factory=function(bool present)=>{if(present){return &Answer;}return null;};var callback=factory(true);int32 result=0;if(callback is fn()->int32 live){result=live();}bool absent=factory(false)==null;delete factory;return result==42&&absent?0:1;}\n",
    "return_nullable_callback_null_first": "int32 Answer(){return 42;}int32 main(){var factory=function(bool present)=>{if(!present){return null;}return &Answer;};var callback=factory(true);int32 result=0;if(callback is fn()->int32 live){result=live();}bool absent=factory(false)==null;delete factory;return result==42&&absent?0:1;}\n",
    "inferred_self_recursive_explicit_result": "int32 main(){var factorial=function(int32 value)->int32 {if(value==0){return 1;}return value*factorial(value-1);};int32 result=factorial(6);delete factorial;return result==720?0:1;}\n",
    "inferred_self_recursive_escape": "fn(int32)->int32 Factorial(){var factorial=function(int32 value)->int32 {if(value==0){return 1;}return value*factorial(value-1);};return factorial;}int32 main(){var factorial=Factorial();int32 result=factorial(6);delete factorial;return result==720?0:1;}\n",
    "borrowed_holder_scalar_field_copy": "struct Holder{int32* pointer;int32 value;}fn()->int32 Keep(){unsafe(using krt.mem;){int32 local=7;Holder holder=default(Holder);holder.pointer=&local;holder.value=42;var scalar=holder.value;return function()=>scalar;}}int32 main(){var callback=Keep();int32 result=callback();delete callback;return result==42?0:1;}\n",
    "borrowed_array_scalar_element_copy": "fn()->int32 Keep(){unsafe(using krt.mem;){int32 local=42;int32* pointers[1];pointers[0]=&local;var scalar=*pointers[0];return function()=>scalar;}}int32 main(){var callback=Keep();int32 result=callback();delete callback;return result==42?0:1;}\n",
    "pure_struct_value_copy_control": "struct Point{int32 value;}fn()->int32 Keep(){unsafe(using krt.mem;){Point* values=stackalloc Point[1];values[0].value=7;var snapshot=values[0];return function()=>snapshot.value;}}int32 main(){return 0;}\n",
    "pure_struct_value_copy_runtime": "struct Point{int32 value;}fn()->int32 Keep(){unsafe(using krt.mem;){Point* values=stackalloc Point[1];values[0].value=42;var snapshot=values[0];return function()=>snapshot.value;}}int32 main(){var callback=Keep();int32 result=callback();delete callback;return result==42?0:1;}\n",
    "nested_pure_struct_fixed_array_copy": "struct Point{int32 value;}struct Record{Point points[2];}fn()->int32 Keep(){unsafe(using krt.mem;){Record* values=stackalloc Record[1];values[0].points[0].value=20;values[0].points[1].value=22;var snapshot=values[0];return function()=>snapshot.points[0].value+snapshot.points[1].value;}}int32 main(){var callback=Keep();int32 result=callback();delete callback;return result==42?0:1;}\n",
}
SOURCE_SHA256 = {
    "ternary_pattern_context": "54f7120bc0bfeb06c3f7ffc7694f141da0c6ee6e28b01ece656e9fa857d163e2",
    "ternary_pattern_typed": "7c86557876f0eb98c32c1d3e2953b4c964f521f46534bc50c87b2735de91dafb",
    "ternary_pattern_control": "a3ff279dc44486fd83c89bab0e3d06613a01c83e87c058c549966ce6f2f431fe",
    "wide_return_inference_narrow_first": "19ce13a2c3d67566bf69f872de9d6b48082eb5a0ca93d0c77adbba431d489d10",
    "wide_return_inference_wide_first": "fa184a77863f8f5df6e3287216e776ed452c125e94ce969824f1bbc5c92352e6",
    "stack_scalar_copy": "54e70e28a902657bf1f9346d2e8409a19a4c4570b031cea8929cd05d7ac70685",
    "capture_then_nested_shadow": "1b62f8cbd314c64f881ac7b3c6d96e1719fcc1b23e8b39fb4843506b2010ce85",
    "normal_nested_shadow_control": "42f6ada3c837749398f04bb7bfd664d2a044b65f9607fdcedfe916fae76ac513",
    "probe_generic_pollution": "4c97fff302a7bac4bac3fa0380c7137cd3eb1a7f80bb3d4bb2e5146e65403db1",
    "probe_generic_control": "d0a76986d5bac2c38fbf905c3a337d10a0044fdfd653d6d01d461cc3fbf8f637",
    "typed_only_generic_callback": "015ba60f23fd2ee26ed5ecfa1857a7e089f4045ea31eb3ca3d0bb3d6ce509988",
    "typed_only_generic_single_callback": "c4648b65e12d1635d02d39fec6d7ac05da7c674bfe48f613b91118001da6080f",
    "typed_only_generic_variable_control": "38667441ad5a6d916d328663f49d6186e56876b839400731803b0075c8d04256",
    "nested_this_base": "9485a0846c2aab7d54a71a6ce7708f2e3156739ca4d0e08ff0ad77fd6dd7c06f",
    "return_nullable_object": "8e3cf01f92e9ba4fc700b58915f4e3319c846a0a4f7dc640d32fd18d98b9296d",
    "return_nullable_object_expression": "6faedd01c8bc99cde28e8f9cd450bee325b04a4a75baa549dadcf903dfb6c064",
    "return_common_base_derived_first": "6a2fc328ee6751effb1dd026c752382e6d45859ace59d188ad37291c1be9f9ba",
    "return_common_base_base_first": "596a73929247f9e42d50c95caf50f8c3a9c7d8d98d5b94ddfad5401aecad1ddf",
    "return_common_base_siblings": "5ec4ae33a8f9f090713302006f9295b5d7f4a9e20813bb36858caf2efefa3626",
    "callback_label_ternary_control": "c874c0ded0b4974a2d988472146c9a9aa685014d620f34b1fdcdc09096887a65",
    "callback_label_literal_true": "be87b548edbc27f26b422483a8c70fcead715e070935b00e3703207d64a7726f",
    "callback_label_literal_false": "ed09b8ac02543f56b28663bf99e632b238be833ccec424628ead5e9e9e5c7577",
    "callback_label_shared_function": "92405782854e51ddd6c4af8e6b3257930cad5659df4f0f6c0705699918178b68",
    "callback_label_shared_literal": "df8d4f2bf5ac518ed6d1885fce6746149ef0bc4784e62958e84f41d0e941c971",
    "callback_label_nested_result": "91219d5e51470fa64442beab213c31f805a1a7645f8260fc62834aa01fb5a657",
    "callback_label_deep_result": "711b7ae3b45850c35bc694f55c69ad7a47747d0c0b26f46cc7a02b0b6d574017",
    "typed_only_generic_nominal": "e0915a0e2480fd42aedacc73ddd59ba5ab915dedda59cbf2b6cefd8dc93e0f83",
    "typed_only_generic_nested_callback": "e665aca022475dd9e76193f48d39dfd77ed641c97764c93b76de302b87507cb6",
    "probe_generic_class_pollution": "294360c0f557fb2f3bbebee372893b8eee146dc4b7924d1874e89bd7e108a3c4",
    "return_nullable_callback": "85945a13ed8df96fb059e0fd6c352af9cec714058f6a90bf130e6569e84fae97",
    "return_nullable_callback_null_first": "417feb382da5c3b733c05cf5f9cb3cc47ea5a4164e3a67e0c8934cc61e6d34f5",
    "inferred_self_recursive_explicit_result": "a091dd1b5a92504391971961ac64311a6737ec4f99d8b36c0e1e76097f74c577",
    "inferred_self_recursive_escape": "b4f28b6b8ad2b1d242cf13bcac78b6f1c6a4deda36dd22b19475dd906cff3b27",
    "borrowed_holder_scalar_field_copy": "78835b2616ebb07b1419155f77a97fcd9c8306582f3aa5cd824d274ea1540e63",
    "borrowed_array_scalar_element_copy": "5d288d46336bbdbbe46a33478aea56f8b54d8c7e554b1ce3f533e5480e07f541",
    "pure_struct_value_copy_control": "836f36093d3567eaa8e00a86dd7c082243a0af658993434d76f3273eaf3d2b07",
    "pure_struct_value_copy_runtime": "b3b79d1afced0311942493a63978578bf5ce741a8d2ec5e18396e62fd2b94e17",
    "nested_pure_struct_fixed_array_copy": "c701937a95e35b36bc32504834e8551d5e2ca839c55b9c3482c50120fa83ccb6",
}

ERASED_CALLBACK_LABEL_SOURCE = "int32 First(int32 left){return left+1;}int32 Second(int32 right){return right+2;}int32 main(){var factory=function(bool first)=>{if(first){return &First;}return &Second;};var callback=factory(false);int32 result=callback(left:40);delete factory;return result==42?0:1;}\n"

# Candidate3 accepted this exact source using only the first branch's label.
BEFORE_TERNARY_LABEL_SOURCE = "int32 First(int32 left){return left+1;}int32 Second(int32 right){return right+2;}int32 main(){bool first=false;var callback=first?&First:&Second;return callback(left:40)==42?0:1;}\n"
BEFORE_TERNARY_LABEL_SHA256 = "d862789aeb95fedbb727049beb28ce0cf858c5a63412ac883be8edbb84c07e58"

ERASED_LABEL_CASES = {
    "erased-return-label": ERASED_CALLBACK_LABEL_SOURCE,
    "erased-ternary-label": BEFORE_TERNARY_LABEL_SOURCE,
    "erased-literal-label": "int32 main(){int32 offset=1;bool first=false;var callback=first?function(int32 left)=>left+offset:function(int32 right)=>right+offset+1;int32 result=callback(left:40);delete callback;return result==42?0:1;}\n",
    "erased-nested-result-label": "int32 First(int32 left){return left+1;}int32 Second(int32 right){return right+2;}int32 main(){bool first=false;var factory=first?function()=>&First:function()=>&Second;var callback=factory();int32 result=callback(left:40);delete factory;return result==42?0:1;}\n",
    "erased-deep-result-label": "int32 First(int32 left){return left+1;}int32 Second(int32 right){return right+2;}int32 main(){bool first=false;var outer=first?function()=>function()=>&First:function()=>function()=>&Second;var factory=outer();var callback=factory();int32 result=callback(left:40);delete factory;delete outer;return result==42?0:1;}\n",
}

BORROWED_ALIAS_CASES = {
    "borrowed-struct-field-alias": ("struct Holder{int32* pointer;}fn()->int32 Keep(){unsafe(using krt.mem;){int32 local=7;Holder holder=default(Holder);holder.pointer=&local;var alias=holder.pointer;return function()=>*alias;}}int32 main(){return 0;}\n", "alias"),
    "borrowed-fixed-array-alias": ("fn()->int32 Keep(){unsafe(using krt.mem;){int32 local=7;int32* pointers[1];pointers[0]=&local;var alias=pointers[0];return function()=>*alias;}}int32 main(){return 0;}\n", "alias"),
    "borrowed-mixed-array-literal": ("static int32 global=11;fn()->int32 Keep(){unsafe(using krt.mem;){int32 local=7;int32* stable=&global;var pointers=[stable,&local];return function()=>*pointers[1];}}int32 main(){return 0;}\n", "pointers"),
}
BORROWED_ALIAS_SHA256 = {
    "borrowed-struct-field-alias": "ff92a3308034473412cb3cd9488da7acfa1d1f9f70604a2c7a6871a68ebb26dc",
    "borrowed-fixed-array-alias": "c60c489708fa6c17989d1a1fd592357ee57db0c985359ddcee9264314a01a693",
    "borrowed-mixed-array-literal": "e3bcc0a21dc1c5216854e570ce75d3d657131c89f5b2b981c26adaae7c0296e5",
}


class ClosureInferenceTests(NativeCompilerFixture):
    def execute_targets(self, name, source):
        self.assertEqual(hashlib.sha256(source.encode()).hexdigest(), SOURCE_SHA256[name])
        path = self.work / (name + ".krt")
        path.write_bytes(source.encode())
        for target in ("native", "vm"):
            for level in (0, 2):
                with self.subTest(program=name, target=target, optimization=level):
                    output = self.work / f"{name}-{target}-o{level}"
                    flags = ["target", "vm"] if target == "vm" else []
                    self.command(path, f"-O{level}", *flags, "-o", output)
                    argv = [str(COMPILER), "run-vm", str(output)] if target == "vm" else [str(output)]
                    result = subprocess.run(argv, cwd=self.work, env=self.env,
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, b"")
                    self.assertEqual(result.stderr, b"")

    def test_inference_scope_and_probe_regressions(self):
        for name, source in POSITIVE_CASES.items():
            self.execute_targets(name, source)

    def reject_diagnostic(self, name, source, position, code, message):
        path = self.work / (name + ".krt")
        path.write_bytes(source.encode())
        expected = f"{path}:1:{position + 1}: {code}: {message} (combined byte {position})\n"
        for target in ("native", "vm"):
            for level in (0, 2):
                for previous in (None, b"previous artifact\x00must survive"):
                    with self.subTest(program=name, target=target, optimization=level, previous=previous is not None):
                        output = self.work / f"{name}-{target}-o{level}-{previous is not None}"
                        if previous is not None:
                            output.write_bytes(previous)
                        flags = ["target", "vm"] if target == "vm" else []
                        argv = [str(COMPILER), "--linker", str(LINKER), str(path),
                                f"-O{level}", *flags, "-o", str(output)]
                        first = subprocess.run(argv, cwd=self.work, env=self.env,
                                               capture_output=True, timeout=30)
                        second = subprocess.run(argv, cwd=self.work, env=self.env,
                                                capture_output=True, timeout=30)
                        self.assertEqual(first.returncode, 1, first.stdout + first.stderr)
                        self.assertEqual(second.returncode, 1, second.stdout + second.stderr)
                        self.assertEqual(first.stderr, second.stderr)
                        self.assertEqual(first.stdout, b"")
                        self.assertEqual(second.stdout, b"")
                        self.assertEqual(first.stderr, expected.encode())
                        if previous is None:
                            self.assertFalse(output.exists())
                        else:
                            self.assertEqual(output.read_bytes(), previous)
                        self.assertFalse(output.with_name(output.name + ".ebc").exists())

    def test_callback_labels_must_be_shared_by_every_dynamic_choice(self):
        self.assertEqual(hashlib.sha256(BEFORE_TERNARY_LABEL_SOURCE.encode()).hexdigest(),
                         BEFORE_TERNARY_LABEL_SHA256)
        for name, source in ERASED_LABEL_CASES.items():
            self.reject_diagnostic(name, source, source.index("left:40"),
                                   "E_ARGUMENT_NAME", "compilation failed")

    def test_container_aliases_preserve_borrowed_value_origins(self):
        message = "closures cannot capture ref parameters, inline struct receivers, or values borrowed from stack storage"
        for name, (source, captured_name) in BORROWED_ALIAS_CASES.items():
            self.assertEqual(hashlib.sha256(source.encode()).hexdigest(),
                             BORROWED_ALIAS_SHA256[name])
            self.reject_diagnostic(name, source, source.rindex(captured_name),
                                   "E_CAPTURE_BORROW", message)
