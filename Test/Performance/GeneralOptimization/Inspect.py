"""Verify measured artifacts and summarize their retained function disassembly."""
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(text, name, krt):
    headers = list(re.finditer(r"^([0-9a-f]+) <([^>]+)>:\n", text, re.M))
    prefix = f"_ZN{len(name)}{name}E" if krt else name
    index = next(i for i, header in enumerate(headers)
                 if (header[2].startswith(prefix) if krt else header[2] == name))
    start = headers[index]
    end = next((header.start() for header in headers[index+1:]
                if not header[2].startswith("__krt_bb_")), len(text))
    body = text[start.start():end]
    instructions = []
    for row in body.splitlines():
        match = re.match(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(\S+)(.*)$", row)
        if match:
            instructions.append({"address": int(match[1],16), "bytes": match[2].strip(),
                                 "opcode": match[3], "operands": match[4].strip()})
    # Padding is excluded from static function size, but retained in the snippet.
    while instructions and (instructions[-1]["opcode"] in ("nop", "data16") or
                            "nop" in instructions[-1]["operands"]):
        instructions.pop()
    code = bytes.fromhex(" ".join(row["bytes"] for row in instructions))
    return body, {"symbol":start[2],"code_bytes":len(code),"instructions":len(instructions),
                  "calls":sum(row["opcode"] == "call" for row in instructions),
                  "code_sha256":hashlib.sha256(code).hexdigest()}


def main():
    report = json.loads((HERE/"Results.json").read_text())
    assert report["completed"] and report["correctness"]["After"] and report["correctness"]["Gcc"]
    for path, expected in report["artifact_sha256"].items():
        assert digest(ROOT/path) == expected, path
    mapping = {"Fib35":"Recur", "Fib40":"Recur", "TailSum":"Sum", "Product":"Product",
               "Gcd":"Divisor", "Branching":"Paths", "BinaryTree":"Tree",
               "IntegerLoop":"Kernel", "ExpandedLoop":"Kernel"}
    output = {"results_sha256":digest(HERE/"Results.json"), "functions":{}, "snippets_sha256":{},
              "notes":"Static counts exclude trailing padding; these are not hardware execution counts."}
    snippets = HERE/"Functions"
    snippets.mkdir(exist_ok=True)
    for name, function in mapping.items():
        output["functions"][name] = {}
        for variant in ("Before", "After", "Gcc"):
            samples = report["cases"][name][variant]["samples"]
            assert len(samples) == report["rounds"]
            if report["cases"][name][variant]["valid"]:
                assert all(sample["valid"] and sample["result"] == report["workloads"][name]["expected"] for sample in samples)
            assembly = HERE/"Artifacts"/(name+variant+".asm")
            snippet, stats = extract(assembly.read_text(), function, variant != "Gcc")
            path = snippets/(name+variant+".asm")
            path.write_text(snippet)
            output["snippets_sha256"][str(path.relative_to(ROOT))] = digest(path)
            output["functions"][name][variant] = stats
    for variant in ("Before", "After"):
        assert output["functions"]["Fib35"][variant]["code_sha256"] == output["functions"]["Fib40"][variant]["code_sha256"]
    assert output["functions"]["ExpandedLoop"]["Before"]["code_sha256"] == output["functions"]["ExpandedLoop"]["After"]["code_sha256"]
    (HERE/"Analysis.json").write_text(json.dumps(output,indent=2)+"\n")
    count = sum(len(case[variant]["samples"]) for case in report["cases"].values() for variant in case)
    print(f"Verified all measured binaries, {count} samples, identical Fib35/40 bodies and unchanged expanded loop code.")
    for name, variants in output["functions"].items():
        print(name, {variant:{key:stats[key] for key in ("code_bytes","instructions","calls")} for variant,stats in variants.items()})


if __name__ == "__main__":
    main()
