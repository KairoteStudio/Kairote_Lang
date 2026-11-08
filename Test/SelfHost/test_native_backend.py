"""Validate semantic metadata invariants, SSA lowering and direct KRO emission."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from Test.SelfHost.Bootstrap import ARKLINK, KRTC

ROOT = Path(__file__).resolve().parents[2]


def instruction(op, argument=0, extra=0):
    return op, argument, extra


I = instruction


class NativeBackendTest(unittest.TestCase):
    def test_native_abi_memory_and_control_flow(self):
        # (instructions, starts, local counts, parameter counts, expected status)
        cases = {
            "logical_not": ([I("Const", 0), I("BoolNot"), I("Const", 6), I("Add"), I("Return", 0, 1)], [0], [0], [0], 7),
            "heap_escape": ([I("Call", 1), I("Load", 0, 8), I("Return", 0, 1), I("Const", 8), I("Allocate"), I("Dup"), I("Const", 42), I("Store", 0, 8), I("Return", 0, 1)], [0, 3], [0, 0], [0, 0], 42),
            "ref_mutation": ([I("Const", 5), I("StoreLocal", 0), I("Ref", 0), I("Call", 1, 1), I("Drop"), I("LoadLocal", 0), I("Return", 0, 1), I("LoadLocal", 0), I("Const", 7), I("Store", 0, 8), I("Const", 0), I("Return", 0, 1)], [0, 7], [1, 1], [0, 1], 7),
            "dynamic_syscall": ([I("Const", 7), I("Const", 39), I("Syscall", 0, 1), I("Drop"), I("Return", 0, 1)], [0], [0], [0], 7),
            "stackalloc_operand": ([I("Const", 7), I("Const", 3), I("StackAllocate"), I("Drop"), I("Return", 0, 1)], [0], [0], [0], 7),
            "postincrement": ([I("Const", 5), I("StoreLocal", 0), I("Ref", 0), I("PostIncrement", 1, 8), I("LoadLocal", 0), I("Add"), I("Return", 0, 1)], [0], [1], [0], 11),
            "signed_cast": ([I("Const", 4294967289), I("Cast", 4), I("Const", -7), I("Compare", 61), I("Return", 0, 1)], [0], [0], [0], 1),
            "signed_byte_cast": ([I("Const", 249), I("Cast", -1), I("Const", -7), I("Compare", 61), I("Return", 0, 1)], [0], [0], [0], 1),
            "signed_word_cast": ([I("Const", 35536), I("Cast", -2), I("Const", -30000), I("Compare", 61), I("Return", 0, 1)], [0], [0], [0], 1),
            "unsigned_dword_cast": ([I("Const", -1), I("Cast", -4), I("Const", 4294967295), I("Compare", 61), I("Return", 0, 1)], [0], [0], [0], 1),
            "unsigned_compare": ([I("Const", -1), I("Const", 1), I("Compare", 62, 1), I("Return", 0, 1)], [0], [0], [0], 1),
            "unsigned_divide": ([I("Const", -1), I("Const", 2), I("Div", 0, 1), I("Const", 9223372036854775807), I("Compare", 61), I("Return", 0, 1)], [0], [0], [0], 1),
            "unsigned_modulo": ([I("Const", -1), I("Const", 2), I("Mod", 0, 1), I("Return", 0, 1)], [0], [0], [0], 1),
            "unsigned_shift": ([I("Const", -1), I("Const", 1), I("Shr", 0, 1), I("Const", 9223372036854775807), I("Compare", 61), I("Return", 0, 1)], [0], [0], [0], 1),
            "signed_shift": ([I("Const", -8), I("Const", 1), I("Shr"), I("Return", 0, 1)], [0], [0], [0], 252),
            "member_offset_zero": ([I("Const", 8), I("Allocate"), I("Dup"), I("Address", 0, 0), I("Const", 42), I("Store", 0, 8), I("Load", 0, 8), I("Return", 0, 1)], [0], [0], [0], 42),
            "nested_calls": ([I("Const", 7), I("Call", 1), I("Add"), I("Return", 0, 1), I("Const", 5), I("Call", 2), I("Add"), I("Return", 0, 1), I("Const", 3), I("Return", 0, 1)], [0, 4, 8], [0, 0, 0], [0, 0, 0], 15),
            "same_target_branch": ([I("Const", 7), I("Const", 1), I("Branch", 3, 3), I("Return", 0, 1)], [0], [0], [0], 7),
            "dead_block_before_merge": ([I("Const", 7), I("Jump", 4), I("Const", 9), I("Jump", 4), I("Return", 0, 1)], [0], [0], [0], 7),
        }
        calls = [I("Const", 9)] + [I("Const", value) for value in range(1, 33)] + [I("Call", 1, 32), I("Add"), I("Return", 0, 1)]
        helper_start = len(calls)
        calls += [I("LoadLocal", 0)]
        for slot in range(1, 32):
            calls += [I("LoadLocal", slot), I("Add")]
        calls += [I("Return", 0, 1)]
        cases["call_32_arguments"] = calls, [0, helper_start], [0, 32], [0, 32], 25
        metadata = {}
        cases["aggregate_entry_alignment"]=[I("AggregateFrameAddress",0,16),I("Const",15),I("And"),I("Return",0,1)],[0],[0],[0],0
        metadata["aggregate_entry_alignment"]=["m.function_frame_bytes[0]=16;"]
        for direction, destination, source in (("backward",8,0),("forward",0,8)):
            copied = [I("AggregateFrameAddress",0,32),I("AggregateZero",32,8)]
            for offset,value in ((0,17),(8,29),(16,43)):
                copied += [I("AggregateFrameAddress",source+offset,8),I("Const",value),I("Store",0,8)]
            copied += [I("AggregateFrameAddress",destination,24),I("AggregateFrameAddress",source,24),I("AggregateCopy",24,8)]
            for offset in (0,8,16):
                copied += [I("AggregateFrameAddress",destination+offset,8),I("Load",0,8)]
                if offset:
                    copied += [I("Add")]
            copied += [I("Return",0,1)]
            name=f"aggregate_overlap_{direction}"
            cases[name]=copied,[0],[0],[0],89
            metadata[name]=["m.function_frame_bytes[0]=32;"]
        for indirect in (False,True):
            copied=[I("Const",7)]
            if indirect:
                copied += [I("FunctionAddress",1)]
            copied += [I("AggregateFrameAddress",0,16)]
            copied += [I("Const",value) for value in range(1,129)]
            copied += [I("IndirectCall" if indirect else "Call",0 if indirect else 1,129),I("Drop"),
                       I("AggregateFrameAddress",0,8),I("Load",0,8),I("Add"),
                       I("AggregateFrameAddress",8,8),I("Load",0,8),I("Add"),I("Return",0,1)]
            helper_start=len(copied)
            copied += [I("LoadLocal",128),I("LoadLocal",127),I("Store",0,8),
                       I("LoadLocal",128),I("Address",0,8),I("LoadLocal",0),I("Store",0,8),
                       I("LoadLocal",128),I("Return",0,1)]
            name=f"aggregate_result_129_{'indirect' if indirect else 'direct'}"
            cases[name]=copied,[0,helper_start],[0,129],[0,128],136
            metadata[name]=["m.function_frame_bytes[0]=16; m.aggregate_return_sizes[1]=16; m.aggregate_return_alignments[1]=8; m.hidden_result_slots[1]=128;"]
        packed = [I("Const", 16), I("Allocate"), I("StoreLocal", 0)]
        for index, width, value in [(0, 1, 255), (1, 4, -7)]:
            packed += [I("LoadLocal", 0), I("Const", index), I("Address", 1, width), I("Const", value), I("Store", 0, width)]
        for index, width, value in [(0, 1, 255), (1, 4, -7)]:
            packed += [I("LoadLocal", 0), I("Const", index), I("Address", 1, width), I("Load", 0, width), I("Const", value), I("Compare", 61)]
        cases["packed_memory"] = packed + [I("Add"), I("Return", 0, 1)], [0], [1], [0], 2
        for name, width, value in [("signed_byte_memory", -1, -7), ("signed_word_memory", -2, -30000), ("unsigned_dword_memory", -4, 4000000000)]:
            cases[name] = ([I("Const", 8), I("Allocate"), I("Dup"), I("Const", value), I("Store", 0, width), I("Load", 0, width), I("Const", value), I("Compare", 61), I("Return", 0, 1)], [0], [0], [0], 1)
        loop = [I("Const", 0), I("StoreLocal", 0), I("LoadLocal", 0), I("Const", 5), I("Compare", 60), I("Branch", 6, 15), I("Const", 13), I("StackAllocate"), I("Dup"), I("Const", 1), I("Store", 0, 1), I("Drop"), I("Ref", 0), I("PostIncrement", 1, 8), I("Drop"), I("LoadLocal", 0), I("Return", 0, 1)]
        loop.insert(15, I("Jump", 2))
        loop[5] = I("Branch", 6, 16)
        cases["loop_stackalloc"] = loop, [0], [1], [0], 5

        invalid = {
            "underflow": ([I("Drop"), I("Return")], [0], [0], [0]),
            "bad_branch": ([I("Jump", -1)], [0], [0], [0]),
            "cross_function_branch": ([I("Jump", 1), I("Const", 7), I("Return", 0, 1)], [0, 1], [0, 0], [0, 0]),
            "unequal_join": ([I("Const", 1), I("Branch", 2, 4), I("Const", 3), I("Jump", 5), I("Jump", 5), I("Const", 0), I("Return", 0, 1)], [0], [0], [0]),
            "growing_loop": ([I("Const", 1), I("Jump", 0)], [0], [0], [0]),
            "invalid_local": ([I("LoadLocal", 1), I("Return", 0, 1)], [0], [1], [0]),
            "unreachable_bad_branch": ([I("Const", 7), I("Return", 0, 1), I("Jump", 99)], [0], [0], [0]),
            "aggregate_outside_arena": ([I("AggregateFrameAddress",8,8),I("Drop"),I("Return")], [0], [0], [0]),
            "aggregate_invalid_alignment": ([I("AggregateFrameAddress",0,8),I("AggregateZero",8,3),I("Return")], [0], [0], [0]),
        }
        metadata["aggregate_outside_arena"]=["m.function_frame_bytes[0]=8;"]
        metadata["aggregate_invalid_alignment"]=["m.function_frame_bytes[0]=8;"]
        with tempfile.TemporaryDirectory(prefix="krt-native-backend-") as directory:
            work = Path(directory)
            parts = [path.read_text() for path in sorted((ROOT / "SelfHost").rglob("*.krt")) if path.name != "Main.krt"]
            body = ["int32 main() {", "KrtNativeModule m = new KrtNativeModule();", "KrtKroObject o = new KrtKroObject();"]
            for number, (name, specification) in enumerate(list(cases.items()) + list(invalid.items()), 1):
                ops, starts, local_counts, parameter_counts = specification[:4]
                body += ["if (!KrtNativeInit(m)) { return 90; }", f"m.function_count = {len(starts)}; m.entry = 0;"]
                for index, start in enumerate(starts):
                    body += [f"m.starts[{index}] = {start}; m.local_counts[{index}] = {local_counts[index]}; m.parameter_counts[{index}] = {parameter_counts[index]};"]
                body += metadata.get(name, [])
                body += [f"KrtNativeEmit(m, (int32)KrtNativeOp.{op}, {argument}, {extra});" for op, argument, extra in ops]
                if name in invalid:
                    body += [f"if (KrtKroEmitNativeModule(m, o)) {{ return {number}; }}"]
                else:
                    body += [f'if (!KrtKroEmitNativeModule(m, o) || !KrtKroWriteObjectToPath(o, "{name}.kro")) {{ return {number}; }}']
            # A diamond creates a stack phi. Definitions inside only one arm
            # must not be accepted as ordinary uses after the merge or as an
            # incoming operand on the other arm's edge.
            body += ["if (!KrtNativeInit(m)) { return 91; }", "m.function_count=1; m.entry=0; m.starts[0]=0;"]
            for op, argument, extra in [I("Const",1),I("Branch",2,4),I("Const",7),I("Jump",6),I("Const",9),I("Jump",6),I("Return",0,1)]:
                body += [f"KrtNativeEmit(m,(int32)KrtNativeOp.{op},{argument},{extra});"]
            body += ["KrtSsaModule graph=KrtSsaBuild(m,null,0); if(graph==null || !KrtSsaValidate(graph)){return 92;}",
                     "int32 merge=-1; int32 arm=-1; int32 returned=-1; int32 scan=0;",
                     "while(scan<graph.count){if(graph.ops[scan]==101){merge=scan;}if(graph.ops[scan]==1 && graph.args[scan]==7){arm=scan;}if(graph.ops[scan]==20){returned=scan;}scan=scan+1;}",
                     "if(merge<0 || arm<0 || returned<0){return 93;}",
                     "int32 position=graph.operand_starts[returned]; int32 saved=graph.operands[position]; graph.operands[position]=arm;",
                     "if(KrtSsaValidate(graph)){return 94;} graph.operands[position]=saved;",
                     "position=graph.operand_starts[merge]+1; saved=graph.operands[position]; graph.operands[position]=arm;",
                     "if(KrtSsaValidate(graph)){return 95;} graph.operands[position]=saved;",
                     "int32 predecessor=graph.phi_predecessors[position]; graph.phi_predecessors[position]=graph.value_blocks[merge];",
                     "if(KrtSsaValidate(graph)){return 96;} graph.phi_predecessors[position]=predecessor;",
                     "int32 type=graph.types[saved]; graph.types[saved]=2; if(KrtSsaValidate(graph)){return 97;} graph.types[saved]=type;",
                     "if(!KrtSsaValidate(graph)){return 98;}",
                     "int32 incoming_count=graph.operand_counts[merge];graph.operand_counts[merge]=1;",
                     "if(KrtSsaValidate(graph)){return 99;}graph.operand_counts[merge]=incoming_count;",
                     "int32 first_incoming=graph.operand_starts[merge];position=first_incoming+1;",
                     "saved=graph.operands[position];predecessor=graph.phi_predecessors[position];",
                     "graph.operands[position]=graph.operands[first_incoming];graph.phi_predecessors[position]=graph.phi_predecessors[first_incoming];",
                     "if(KrtSsaValidate(graph)){return 100;}graph.operands[position]=saved;graph.phi_predecessors[position]=predecessor;",
                     "if(!KrtSsaValidate(graph)){return 101;}"]
            # Two outgoing branch edges from one source block still supply only
            # one incoming phi value. A physically present dead source block
            # with no CFG edge does not add a predecessor.
            for graph_name, ops, expected, error in (
                ('same_target_branch', cases['same_target_branch'][0], 1, 102),
                ('dead_block_before_merge', cases['dead_block_before_merge'][0], 1, 103),
            ):
                body += ["if(!KrtNativeInit(m)){return 104;}", "m.function_count=1;m.entry=0;m.starts[0]=0;"]
                body += [f"KrtNativeEmit(m,(int32)KrtNativeOp.{op},{argument},{extra});" for op, argument, extra in ops]
                body += ["graph=KrtSsaBuild(m,null,0);", f"if(graph==null||!KrtSsaValidate(graph)){{return {error};}}",
                         "scan=0;merge=-1;while(scan<graph.count){if(graph.ops[scan]==101){merge=scan;}scan++;}",
                         f"if(merge<0||graph.operand_counts[merge]!={expected}){{return {error};}}",
                         f"if(!KrtSsaOptimize(graph,2)||!KrtSsaValidate(graph)){{return {error};}}"]
            # Generic bucket keys must survive nominal layout completion. The
            # exact equality check still compares settled widths and strides.
            body += [
                "KrtAstNode cache_module=KrtAstMake(KrtAstKind.Program,0,0);",
                "KrtAstNode cache_declaration=KrtAstMake(KrtAstKind.Type,0,0);",
                "KrtAstNode nominal=KrtAstMake(KrtAstKind.Type,0,0);nominal.value=2;nominal.native_storage_size=8;nominal.native_alignment=8;",
                "KrtAstNode cache_argument=KrtAstMake(KrtAstKind.Type,0,0);cache_argument.native_type=nominal;cache_argument.native_width=8;cache_argument.native_base_width=8;",
                "KrtAstNode cache_instance=KrtAstMake(KrtAstKind.Type,0,0);",
                "if(!KrtGenericRegistryAdd(cache_module,cache_declaration,cache_argument,cache_instance)){return 105;}",
                "if(KrtGenericRegistryFind(cache_module,cache_declaration,cache_argument)!=cache_instance){return 106;}",
                "nominal.native_storage_size=24;nominal.native_layout_state=2;",
                "if(KrtGenericRegistryFind(cache_module,cache_declaration,cache_argument)!=cache_instance){return 107;}",
                "KrtAstNode first_pointer=KrtAstMake(KrtAstKind.Type,0,0);KrtNativeCopyType(first_pointer,cache_argument);first_pointer.native_pointer_depth=1;first_pointer.native_stride=8;",
                "KrtAstNode second_pointer=KrtAstMake(KrtAstKind.Type,0,0);KrtNativeCopyType(second_pointer,first_pointer);second_pointer.native_stride=24;",
                "if(!KrtNativeTypeEqual(first_pointer,second_pointer)){return 108;}",
                "if(KrtGenericTypeHash(first_pointer,14695981039346656037,0)!=KrtGenericTypeHash(second_pointer,14695981039346656037,0)){return 109;}",
                "if(!KrtGenericRegistryAdd(cache_module,cache_declaration,first_pointer,cache_instance)||KrtGenericRegistryFind(cache_module,cache_declaration,second_pointer)!=cache_instance){return 110;}",
                "KrtAstNode low_pointer=KrtAstMake(KrtAstKind.Type,0,0);KrtNativeCopyType(low_pointer,first_pointer);low_pointer.native_pointer_depth=34;",
                "KrtAstNode high_pointer=KrtAstMake(KrtAstKind.Type,0,0);KrtNativeCopyType(high_pointer,low_pointer);high_pointer.native_pointee_nullable=(uint64)1<<32;",
                "KrtAstNode high_instance=KrtAstMake(KrtAstKind.Type,0,0);",
                "if(KrtNativeTypeEqual(low_pointer,high_pointer)){return 111;}",
                "if(KrtGenericTypeHash(low_pointer,14695981039346656037,0)==KrtGenericTypeHash(high_pointer,14695981039346656037,0)){return 112;}",
                "if(!KrtGenericRegistryAdd(cache_module,cache_declaration,low_pointer,cache_instance)||!KrtGenericRegistryAdd(cache_module,cache_declaration,high_pointer,high_instance)){return 113;}",
                "if(KrtGenericRegistryFind(cache_module,cache_declaration,high_pointer)!=high_instance||KrtGenericRegistryFind(cache_module,cache_declaration,low_pointer)!=cache_instance||KrtGenericRegistryFind(cache_module,cache_declaration,second_pointer)!=cache_instance){return 114;}",
                "return 0;", "}",
            ]
            source = work / "emitter.krt"
            source.write_text("\n".join(parts + body))
            emitter = work / "emitter"
            compiler=os.environ.get("SELFHOST_COMPILER")
            command=([str(Path(compiler).resolve()),"-O2","--linker",str(ARKLINK),str(source),"-o",str(emitter)]
                     if compiler else [str(KRTC),"-O2",str(source),"output",str(emitter)])
            built = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            emitted = subprocess.run([str(emitter)], cwd=work, capture_output=True, text=True, timeout=30)
            self.assertEqual(emitted.returncode, 0, f"case #{emitted.returncode}: {emitted.stdout}{emitted.stderr}")
            for name, specification in cases.items():
                with self.subTest(case=name):
                    binary = work / name
                    linked = subprocess.run([str(ARKLINK), name + ".kro", "--target", "elf", "-o", str(binary)], cwd=work, capture_output=True, text=True, timeout=20)
                    self.assertEqual(linked.returncode, 0, linked.stdout + linked.stderr)
                    binary.chmod(0o755)
                    result = subprocess.run([str(binary)], cwd=work, timeout=10)
                    self.assertEqual(result.returncode, specification[4])


if __name__ == "__main__":
    unittest.main()
