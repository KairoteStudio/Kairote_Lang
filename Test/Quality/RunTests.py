"""Run memory, IR lifetime and serialized-linker regressions with ASan/UBSan."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--leaks", action="store_true", help="Enable LSan on hosts without ptrace restrictions")
    args = parser.parse_args()
    prefix = "Re.KrtC/src/"
    common = [prefix + "Core/Memory/Allocator.c", prefix + "Core/Memory/Arena.c", prefix + "compiler/Middle/Ir/IrMemory.c"]
    suites = {
        "test_memory": common,
        "test_ir": common + [prefix + "compiler/Middle/Ir/" + name for name in ("Ir.c", "IrType.c", "IrOptimizer.c")] + [prefix + "compiler/Frontend/Lexer/Tokenizer.c"],
        "test_linker": ["ArkLink/src/Backend/BackendCommon.c", prefix + "Tools/KroWriter.c"],
        "test_elf_failure": [],
        "test_backend_allocation": [],
        "test_frontend_allocation": [prefix + name for name in (
            "compiler/Frontend/Parser/Ast.c", "compiler/Frontend/Semantic/SemanticAnalyzer.c",
            "compiler/Frontend/Semantic/SymbolTable.c", "compiler/Frontend/Semantic/Generics.c",
            "compiler/Frontend/CompilerError.c", "Core/Memory/Allocator.c", "Core/Memory/Arena.c",
            "Core/Utils/OutputCache.c")],
    }
    results = []
    with tempfile.TemporaryDirectory(prefix="kairote-quality-") as temporary:
        for name, sources in suites.items():
            binary = Path(temporary) / name
            command = [os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections",
                       "-I" + str(ROOT / "Re.KrtC/src"), "-I" + str(ROOT / "ArkLink/include"),
                       "-I" + str(ROOT / "Re.KrtC/Shared"), "-I" + str(ROOT / "ArkLink/src"),
                       str(HERE / (name + ".c")), *[str(ROOT / path) for path in sources],
                       "-Wl,--gc-sections", "-lm", "-lpthread", "-o", str(binary)]
            if name == "test_elf_failure":
                command += ["-L" + str(ROOT / "ArkLink/build"), "-larklink"]
            if name == "test_frontend_allocation":
                command += ["-Wl,--wrap=" + function for function in (
                    "KrtSafeMalloc", "KrtSafeCalloc", "KrtSafeRealloc", "KrtSafeStrdup", "KrtArenaAlloc")]
            subprocess.run(command, check=True, timeout=120)
            environment = dict(os.environ, ASAN_OPTIONS=f"detect_leaks={int(args.leaks)}:halt_on_error=1",
                               UBSAN_OPTIONS="halt_on_error=1")
            result = subprocess.run([str(binary), str(Path(temporary) / "Output.kro")] if name == "test_linker" else [str(binary)],
                                    env=environment, capture_output=True, text=True, timeout=60)
            results.append({"suite": name, "returncode": result.returncode, "stdout": result.stdout,
                            "stderr": result.stderr if result.returncode else "", "command": command})
            print(name, "PASS" if not result.returncode else "FAIL", flush=True)
            if result.returncode: print(result.stdout + result.stderr)
    native = subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", str(HERE), "-p", "test_*.py"],
                            capture_output=True, text=True, timeout=60)
    results.append({"suite": "NativeQuality", "returncode": native.returncode,
                    "stdout": native.stdout, "stderr": native.stderr})
    print("NativeQuality", "PASS" if not native.returncode else "FAIL", flush=True)
    report = {"date_utc": datetime.now(timezone.utc).isoformat(), "address_sanitizer": True,
              "undefined_behavior_sanitizer": True, "leak_sanitizer": args.leaks,
              "tracked_allocation_balance_asserted": True, "results": results}
    compiler = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()
    report["compiler"] = str(compiler)
    report["compiler_sha256"] = hashlib.sha256(compiler.read_bytes()).hexdigest()
    (HERE / "Results.json").write_text(json.dumps(report, indent=2) + "\n")
    raise SystemExit(any(result["returncode"] for result in results))


if __name__ == "__main__":
    main()
