"""Retain equivalent C/Krt Fibonacci artifacts and compare their native execution."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import statistics
import struct
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
KRTC = Path(os.environ.get("KRTC", ROOT / "Re.KrtC/build/KrtC")).resolve()
FIB_SYMBOL = "_ZN3FibEi"


def checked(command, **kwargs):
    result = subprocess.run([str(value) for value in command], capture_output=True, timeout=120, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout!r}\n{result.stderr!r}")
    return result.stdout.decode()


def instructions(assembly):
    pattern = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*([a-z][a-z0-9.]*)\s*(.*)$")
    result = []
    for line in assembly.splitlines():
        match = pattern.match(line)
        if match:
            result.append({"address": int(match[1], 16), "bytes": match[2].split(),
                           "opcode": match[3], "operands": match[4].strip()})
    return result


def save_disassembly(binary, symbol, stem):
    full = checked(["objdump", "-d", "-w", "-Mintel", binary])
    start = int(re.search(r"^([0-9a-f]+) <" + re.escape(symbol) + r">:", full, re.M)[1], 16)
    if symbol == FIB_SYMBOL:
        end = int(re.search(r"^([0-9a-f]+) <main>:", full, re.M)[1], 16)
        body = checked(["objdump", "-d", "-w", "-Mintel", f"--start-address={start}", f"--stop-address={end}", binary])
    else:
        body = checked(["objdump", "-d", "-w", "-Mintel", "--disassemble=" + symbol, binary])
    stem.with_suffix(".asm").write_text(full)
    Path(str(stem) + ".Fib.asm").write_text(body)
    rows = instructions(body)
    # Alignment padding after the final return is not part of an executed path.
    while rows and (rows[-1]["opcode"] in ("nop", "xchg") or "nop " in rows[-1]["operands"]):
        rows.pop()
    calls = [row for row in rows if row["opcode"] == "call" and f"<{symbol}>" in row["operands"]]
    return {"symbol": symbol, "start_address": start, "instructions": rows,
            "static_instructions": len(rows),
            "code_bytes": rows[-1]["address"] + len(rows[-1]["bytes"]) - start,
            "recursive_call_sites": len(calls),
            "sign_extensions": sum(row["opcode"] in ("movsx", "movsxd") for row in rows)}


def check_kro(kro, binary, details, artifacts):
    data = kro.read_bytes()
    header = struct.unpack_from("<16I", data)
    if header[0:2] != (0x004F524B, 2):
        raise RuntimeError("Expected KRO v2")
    text_size, rodata_size, data_size = header[4:7]
    text_reloc_count, rodata_reloc_count, data_reloc_count = header[8:11]
    symbol_count, string_size = header[12:14]
    symbol_offset = 64 + text_size + rodata_size + data_size
    relocation_offset = symbol_offset + symbol_count * 32
    string_offset = relocation_offset + 16 * (text_reloc_count + rodata_reloc_count + data_reloc_count)
    if string_offset + string_size != len(data):
        raise RuntimeError("Unexpected KRO section sizes")
    symbols = []
    for index in range(symbol_count):
        fields = struct.unpack_from("<8I", data, symbol_offset + index * 32)
        name = data[string_offset + fields[0]:].split(b"\0", 1)[0].decode()
        symbols.append({"name": name, "value": fields[1], "section": fields[3], "type": fields[5]})
    symbol = next(entry for entry in symbols if entry["name"] == FIB_SYMBOL)
    start, size = symbol["value"], details["code_bytes"]
    raw = bytearray(data[64 + start:64 + start + size])
    linked = bytearray.fromhex(" ".join(byte for row in details["instructions"] for byte in row["bytes"]))
    if len(raw) != len(linked):
        raise RuntimeError("Non-contiguous function disassembly")
    raw_file = artifacts / (kro.stem + ".RawFib.bin")
    raw_file.write_bytes(raw)
    raw_file.with_suffix(".asm").write_text(checked([
        "objdump", "-D", "-w", "-b", "binary", "-m", "i386:x86-64", "-Mintel",
        f"--adjust-vma={start}", raw_file]))
    relocations = []
    for index in range(text_reloc_count):
        offset, symbol_index, kind, addend = struct.unpack_from("<IIIi", data, relocation_offset + index * 16)
        if start <= offset < start + size:
            width = 8 if kind == 1 else 4
            if kind not in (1, 2, 3) or offset + width > start + size:
                raise RuntimeError("Unsupported relocation")
            relative = offset - start
            relocations.append({"function_offset": relative, "type": kind, "addend": addend,
                                "symbol": symbols[symbol_index]["name"], "width": width})
            raw[relative:relative + width] = bytes(width)
            linked[relative:relative + width] = bytes(width)
    if raw != linked:
        raise RuntimeError("Linker changed bytes outside relocation fields")
    result = {"kro": str(kro.relative_to(ROOT)), "elf": str(binary.relative_to(ROOT)),
              "function_text_offset": start, "code_bytes": size,
              "identical_except_relocations": True, "relocations": relocations}
    (artifacts / (kro.stem + ".Relocations.json")).write_text(json.dumps(result, indent=2) + "\n")
    return result


def save_diagnostic_assembly(details, name, artifacts):
    """Keep original body bytes/layout; optionally change alignment or add a leaf gate."""
    prefix = 9 if name == "KrtFastLeaf" else 0
    padding = 0 if name == "KrtAligned" else (details["start_address"] - prefix) % 64
    lines = [".intel_syntax noprefix", ".text", ".p2align 6", f".fill {padding}, 1, 0x90",
             ".globl Fib", ".type Fib, @function", "Fib:"]
    if prefix:
        lines += ["    cmp edi, 1", "    jg .Lbody", "    movsxd rax, edi", "    ret", ".Lbody:"]
    for row in details["instructions"]:
        code = bytearray.fromhex(" ".join(row["bytes"]))
        if prefix and row["opcode"] == "call":
            if len(code) != 5 or f"<{FIB_SYMBOL}>" not in row["operands"]:
                raise RuntimeError("Diagnostic requires only direct self calls")
            struct.pack_into("<i", code, 1, struct.unpack_from("<i", code, 1)[0] - prefix)
        lines.append("    .byte " + ", ".join(f"0x{byte:02x}" for byte in code) +
                     f" # {row['opcode']} {row['operands']}")
    lines += [".size Fib, .-Fib", '.section .note.GNU-stack,"",@progbits']
    path = artifacts / (name + ".S")
    path.write_text("\n".join(lines) + "\n")
    return path


def run(binary, expected, *arguments):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, timeout=120)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if result.returncode or result.stdout or len(result.stderr) != 16:
        raise RuntimeError(f"{binary}: exit={result.returncode}, stderr={result.stderr!r}")
    duration, value = struct.unpack("<qq", result.stderr)
    if duration < 0 or value != expected:
        raise RuntimeError(f"{binary}: wrong result {value}, duration {duration}")
    return {"duration_ns": duration, "result": value,
            "process_cpu_ms": ((after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)) * 1000}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument("--cpu", type=int, default=0)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    if args.rounds < 1 or args.cpu not in os.sched_getaffinity(0):
        parser.error("Positive rounds and an available logical CPU are required")
    os.sched_setaffinity(0, {args.cpu})
    artifacts = HERE / "Artifacts"
    artifacts.mkdir(exist_ok=True)
    gcc = os.environ.get("CC", "gcc")
    flags = ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fwrapv", "-fno-lto", "-fno-pie"]
    source = HERE.parent / "NativeFib/Fib.c"
    variants = {"GccRecursive": ["-fno-optimize-sibling-calls"], "GccO2": []}
    commands = []

    def build(command):
        commands.append(list(map(str, command)))
        return checked(command, cwd=artifacts)

    for name, extra in variants.items():
        assembly, obj = artifacts / (name + ".s"), artifacts / (name + ".o")
        build([gcc, *flags, *extra, "-DFIB_NAME=Fib", "-S", "-masm=intel", "-fverbose-asm", source, "-o", assembly])
        build([gcc, "-c", assembly, "-o", obj])
    report = {"date_utc": datetime.now(timezone.utc).isoformat(), "platform": platform.platform(),
              "cpu_model": next(line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                                if line.startswith("model name")),
              "cpu": args.cpu, "rounds": args.rounds, "gcc": checked([gcc, "--version"]).splitlines()[0],
              "krtc": str(KRTC), "krtc_sha256": hashlib.sha256(KRTC.read_bytes()).hexdigest(),
              "flags": flags, "variant_extra_flags": variants,
              "clock": "CLOCK_MONOTONIC via syscall on both sides",
              "timing_policy": "One fib(n), excluding compilation/startup/output; serial rotating order, all samples retained",
              "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in (source, HERE / "Driver.c")},
              "commands": commands, "machine_code": {}, "cases": {}, "completed": False}
    binaries = {}
    for n in (35, 40):
        program = artifacts / f"KrtFib{n}.krt"
        shutil.copy2(HERE.parent / "Runtime" / f"test_fib{n}.krt", program)
        report["source_sha256"][str(program.relative_to(ROOT))] = hashlib.sha256(program.read_bytes()).hexdigest()
        binary = artifacts / f"KrtFib{n}.elf"
        build([KRTC, program, "output", binary])
        details = save_disassembly(binary, FIB_SYMBOL, artifacts / f"KrtFib{n}")
        details["kro_link_check"] = check_kro(program.with_suffix(".kro"), binary, details, artifacts)
        report["machine_code"][f"Krt{n}"] = details
        binaries[("Krt", n)] = binary
        for name in variants:
            binary = artifacts / f"{name}{n}.elf"
            build([gcc, *flags, "-no-pie", f"-DFIB_INPUT={n}", HERE / "Driver.c", artifacts / (name + ".o"), "-o", binary])
            details = save_disassembly(binary, "Fib", artifacts / f"{name}{n}")
            report["machine_code"][f"{name}{n}"] = details
            binaries[(name, n)] = binary
    primary_names = list(report["machine_code"])
    for name in primary_names:
        details = report["machine_code"][name]
        if details["recursive_call_sites"] != (1 if name.startswith("GccO2") else 2):
            raise RuntimeError(f"Unexpected recursion shape: {name}")
    original = report["machine_code"]["Krt35"]
    original_bytes = [row["bytes"] for row in original["instructions"]]
    if original_bytes != [row["bytes"] for row in report["machine_code"]["Krt40"]["instructions"]]:
        raise RuntimeError("Fib35/40 Krt function bodies differ")
    diagnostics = ["KrtBytes", "KrtAligned", "KrtFastLeaf"]
    report["diagnostics"] = {
        "KrtBytes": "Original KRO function bytes and address modulo 64, called by the C timing driver",
        "KrtAligned": "Same original bytes, function entry aligned to 64 bytes, same C driver",
        "KrtFastLeaf": "Add n<=1 return before original prologue, retain original body alignment and both recursive calls",
    }
    for name in diagnostics:
        assembly = save_diagnostic_assembly(original, name, artifacts)
        obj = artifacts / (name + ".o")
        build([gcc, "-c", assembly, "-o", obj])
        for n in (35, 40):
            binary = artifacts / f"{name}{n}.elf"
            build([gcc, *flags, "-no-pie", f"-DFIB_INPUT={n}", HERE / "Driver.c", obj, "-o", binary])
            details = save_disassembly(binary, "Fib", artifacts / f"{name}{n}")
            if details["recursive_call_sites"] != 2:
                raise RuntimeError("Diagnostic changed recursive call count")
            actual = bytes.fromhex(" ".join(byte for row in details["instructions"] for byte in row["bytes"]))
            expected = bytearray.fromhex(" ".join(byte for row in original["instructions"] for byte in row["bytes"]))
            prefix = 9 if name == "KrtFastLeaf" else 0
            if prefix:
                for row in original["instructions"]:
                    if row["opcode"] == "call":
                        offset = row["address"] - original["start_address"] + 1
                        struct.pack_into("<i", expected, offset, struct.unpack_from("<i", expected, offset)[0] - prefix)
                if actual[:prefix] != bytes.fromhex("83 ff 01 7f 04 48 63 c7 c3"):
                    raise RuntimeError("Unexpected leaf gate encoding")
            if actual[prefix:] != expected:
                raise RuntimeError("Diagnostic unexpectedly changed original instructions")
            alignment = (details["start_address"] + prefix) % 64
            if alignment != (0 if name == "KrtAligned" else original["start_address"] % 64):
                raise RuntimeError("Diagnostic failed to preserve intended body alignment")
            details["verified_original_body"] = True
            report["machine_code"][f"{name}{n}"] = details
            binaries[(name, n)] = binary
    for variant in (*variants, *diagnostics):
        for n, value in ((-100, -100), (0, 0), (1, 1), (2, 1), (10, 55)):
            run(binaries[(variant, 35)], value, n)
    report["base_case_checks"] = {"c_inputs": [-100, 0, 1, 2, 10], "passed": True}
    (HERE / "Results.json").write_text(json.dumps(report, indent=2) + "\n")
    if args.prepare_only:
        print("Built C/KRO executables, retained assembly, checked KRO relocations.", flush=True)
        return
    for n, expected in ((35, 9227465), (40, 102334155)):
        samples = {name: [] for name in ("Krt", *variants, *diagnostics)}
        order = list(samples)
        for index in range(args.rounds):
            rotated = order[index % len(order):] + order[:index % len(order)]
            for name in rotated:
                sample = {"run": index + 1, **run(binaries[(name, n)], expected)}
                samples[name].append(sample)
                print(f"fib{n} {name} {index+1}/{args.rounds}: {sample['duration_ns']/1e6:.6f} ms", flush=True)
        report["cases"][f"fib{n}"] = {}
        for name, rows in samples.items():
            times = [row["duration_ns"] / 1e6 for row in rows]
            report["cases"][f"fib{n}"][name] = {"samples": rows, "mean_ms": statistics.mean(times),
                "min_ms": min(times), "max_ms": max(times),
                "stdev_ms": statistics.stdev(times) if len(times) > 1 else 0}
        (HERE / "Results.json").write_text(json.dumps(report, indent=2) + "\n")
    report["completed"] = True
    (HERE / "Results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Saved", HERE / "Results.json", flush=True)


if __name__ == "__main__":
    main()
