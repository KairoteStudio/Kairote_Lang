"""Execute native stack IR through the real KRO writer and ArkLink ELF path."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
KRTC = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()
ARKLINK = Path(os.environ.get("ARKLINK", ROOT / "ArkLink/build/ArkLink")).resolve()


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
        }
        calls = [I("Const", 9)] + [I("Const", value) for value in range(1, 33)] + [I("Call", 1, 32), I("Add"), I("Return", 0, 1)]
        helper_start = len(calls)
        calls += [I("LoadLocal", 0)]
        for slot in range(1, 32):
            calls += [I("LoadLocal", slot), I("Add")]
        calls += [I("Return", 0, 1)]
        cases["call_32_arguments"] = calls, [0, helper_start], [0, 32], [0, 32], 25
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
        }
        with tempfile.TemporaryDirectory(prefix="krt-native-backend-") as directory:
            work = Path(directory)
            parts = [path.read_text() for path in sorted((ROOT / "SelfHost").rglob("*.krt")) if path.name not in {"Main.krt", "Compiler.krt"}]
            body = ["int32 main() {", "KrtNativeModule m = new KrtNativeModule();", "KrtKroObject o = new KrtKroObject();"]
            for number, (name, specification) in enumerate(list(cases.items()) + list(invalid.items()), 1):
                ops, starts, local_counts, parameter_counts = specification[:4]
                body += ["if (!KrtNativeInit(m)) { return 90; }", f"m.function_count = {len(starts)}; m.entry = 0;"]
                for index, start in enumerate(starts):
                    body += [f"m.starts[{index}] = {start}; m.local_counts[{index}] = {local_counts[index]}; m.parameter_counts[{index}] = {parameter_counts[index]};"]
                body += [f"KrtNativeEmit(m, (int32)KrtNativeOp.{op}, {argument}, {extra});" for op, argument, extra in ops]
                if name in invalid:
                    body += [f"if (KrtKroEmitNativeModule(m, o)) {{ return {number}; }}"]
                else:
                    body += [f'if (!KrtKroEmitNativeModule(m, o) || !KrtKroWriteObjectToPath(o, "{name}.kro")) {{ return {number}; }}']
            body += ["return 0;", "}"]
            source = work / "emitter.krt"
            source.write_text("\n".join(parts + body))
            emitter = work / "emitter"
            built = subprocess.run([str(KRTC), "-O2", str(source), "output", str(emitter)], cwd=work, capture_output=True, text=True, timeout=60)
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
