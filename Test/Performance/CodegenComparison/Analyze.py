"""Count executed Fib paths from retained disassembly; these are not perf counters."""
from collections import Counter
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def local_path(details, n, loop):
    rows = details["instructions"]
    indices = {row["address"]: index for index, row in enumerate(rows)}
    children = list(range(n - 1, 0, -2)) if loop else [n - 1, n - 2]
    if n <= 1:
        children = []
    calls = 0
    index = 0
    counts = Counter()
    stack = frame = maximum_stack = 0
    for _ in range(1000):
        row = rows[index]
        opcode, operands = row["opcode"], row["operands"]
        counts["instructions"] += 1
        if opcode in ("movsx", "movsxd"):
            counts["sign_extensions"] += 1
        if opcode in ("push", "pop") or (opcode.startswith("mov") and "[" in operands):
            counts["explicit_stack_accesses"] += 1
        if opcode == "push":
            stack += 8
        elif opcode == "pop":
            stack -= 8
        elif opcode in ("sub", "add") and operands.startswith("rsp,"):
            amount = int(operands.split(",")[1], 0)
            stack += amount if opcode == "sub" else -amount
        elif opcode == "mov" and operands == "rbp,rsp":
            frame = stack
        elif opcode == "mov" and operands == "rsp,rbp":
            stack = frame
        maximum_stack = max(maximum_stack, stack)
        if opcode == "ret":
            if calls != len(children) or stack != 0:
                raise RuntimeError("Unexpected call sequence or stack balance")
            return counts, children, maximum_stack
        if opcode == "call":
            calls += 1
        if opcode.startswith("j"):
            target = int(operands.split()[0], 16)
            if opcode == "jmp":
                taken = True
            elif opcode == "jle":
                taken = n <= 1
            elif opcode == "jg":
                taken = calls < len(children) if loop and target < row["address"] else n > 1
            else:
                raise RuntimeError(f"Unsupported branch {opcode}")
            if taken:
                index = indices[target]
                continue
        index += 1
    raise RuntimeError("Unexpected local loop")


def main():
    result = json.loads((HERE / "Results.json").read_text())
    if not result["completed"]:
        raise SystemExit("Run the ten-sample benchmark first")
    report = {
        "method": "Offline execution-path accounting for this Fib recurrence and these exact disassemblies; not measured hardware events",
        "instruction_scope": "Fib only, including its recursive call and return instructions, excluding caller/timer/output",
        "memory_scope": "Explicit stack memory accesses including push/pop, excluding implicit call/ret accesses; LEA does not count as a load",
        "stack_scope": "Maximum additional stack space of one invocation, excluding incoming return address and child frames",
        "krtc_sha256": result["krtc_sha256"], "variants": {},
        "artifact_sha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                            for path in sorted((HERE / "Artifacts").iterdir()) if path.is_file()},
    }
    fibonacci = [0, 1]
    for _ in range(40):
        fibonacci.append(fibonacci[-1] + fibonacci[-2])
    for name in ("Krt", "GccRecursive", "GccO2", "KrtBytes", "KrtAligned", "KrtFastLeaf"):
        details = result["machine_code"][name + "35"]
        loop = name == "GccO2"
        data = {"entry_address_mod_16": details["start_address"] % 16,
                "entry_address_mod_64": details["start_address"] % 64,
                "code_bytes": details["code_bytes"],
                "static_instructions_excluding_padding": sum(row["opcode"] != "nop" for row in details["instructions"]),
                "recursive_call_sites": details["recursive_call_sites"],
                "cases": {}}
        for n, key in ((0, "leaf"), (2, "nonleaf_n2")):
            counts, children, stack = local_path(details, n, loop)
            data[key] = {**counts, "recursive_calls": len(children), "additional_stack_bytes": stack}
        totals = []
        for n in range(41):
            counts, children, _ = local_path(details, n, loop)
            counts["function_entries"] = 1
            counts["leaf_entries" if n <= 1 else "nonleaf_entries"] = 1
            for child in children:
                counts.update(totals[child])
            expected_entries = fibonacci[n + 1] if loop else 2 * fibonacci[n + 1] - 1
            if counts["function_entries"] != expected_entries:
                raise RuntimeError("Path accounting disagrees with independent call-count recurrence")
            totals.append(counts)
            if n in (35, 40):
                data["cases"][f"fib{n}"] = dict(counts)
        report["variants"][name] = data
    (HERE / "OperationCounts.json").write_text(json.dumps(report, indent=2) + "\n")
    for name, data in report["variants"].items():
        print(name, "bytes", data["code_bytes"], "leaf", data["leaf"],
              "fib40", data["cases"]["fib40"])


if __name__ == "__main__":
    main()
