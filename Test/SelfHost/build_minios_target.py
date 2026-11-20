#!/usr/bin/env python3
"""Build a MiniOS compiler using an explicit compatible Linux Stage0 seed.

This builds artifacts, NOT a guest/bootstrap pass certificate. Run the target
unit suite and real MiniOS generation acceptance separately before publishing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", required=True, type=Path)
    parser.add_argument("--linker", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--source-root", type=Path, default=ROOT, help="Immutable source snapshot root (default: this checkout)")
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    seed, linker = args.seed.resolve(), args.linker.resolve()
    source_root = args.source_root.resolve()
    sources = sorted(p for p in (source_root / "SelfHost").rglob("*.krt") if p.name != "project.krt")
    if not sources:
        parser.error("source-root must contain the original SelfHost tree")
    original = "\n".join(p.read_text() for p in sources)
    # Current C Stage0 lowers the target query to false on its supported platforms.
    # All native generations below consume the untouched individual source files.
    stage0 = work / "Stage0.krt"
    stage0.write_text(original)
    (work / "Compiler.krt").write_text(original)
    report = {"status": "building", "seed": str(seed), "seed_sha256": digest(seed),
              "linker": str(linker), "linker_sha256": digest(linker),
              "sources": {str(p.relative_to(source_root)): digest(p) for p in sources}, "commands": []}
    def save():
        (work / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    def run(command, name):
        command = list(map(str, command))
        start = time.monotonic()
        with (work / (name + ".log")).open("w") as log:
            result = subprocess.run(command, cwd=work, stdout=log, stderr=subprocess.STDOUT)
        report["commands"].append({"argv": command, "returncode": result.returncode,
                                    "seconds": time.monotonic() - start, "log": name + ".log"})
        if result.returncode:
            report["status"] = "failed"
            save()
            raise SystemExit(f"Failed: {name}; see {work / (name + '.log')}")
        save()
    save()
    run([seed, "-O2", stage0, "output", work / "host-seed"], "stage0")
    (work / "host-seed").chmod(0o755)
    run([work / "host-seed", "--platform", "linux-x64", "-j2", *sources,
         "-c", "-o", work / "host-native.kro"], "host-native")
    run([linker, work / "host-native.kro", "--target", "elf", "-o", work / "host-native"], "host-link")
    (work / "host-native").chmod(0o755)
    run([work / "host-native", "--platform", "minios-x64", "-j2", *sources,
         "-c", "-o", work / "minios.kro"], "minios-native")
    run([linker, work / "minios.kro", "--target", "elf", "-o", work / "krtc"], "minios-link")
    report["status"] = "built-not-guest-validated"
    report["artifacts"] = {name: digest(work / name) for name in
        ("Stage0.krt", "Compiler.krt", "host-seed", "host-native.kro", "host-native", "minios.kro", "krtc")}
    save()
    print(work / "krtc")

if __name__ == "__main__":
    main()
