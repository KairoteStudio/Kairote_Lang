"""Validate both native optimization levels; optionally use a sanitizer build."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT/"Re.KrtC/build/KrtC")
    parser.add_argument("--sanitized", action="store_true")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    label = "Sanitizer" if args.sanitized else "Native"
    output = HERE/"Validation"
    output.mkdir(exist_ok=True)
    environment = dict(os.environ, KRTC=str(compiler))
    if args.sanitized:
        environment.update(ASAN_OPTIONS="detect_leaks=0:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
    results = []
    for level in (2, 3):
        for suite, name in (("Pointers", "Pointers"), ("IntegerWidths", "Integers"),
                            ("Performance", "Performance"), ("SyntaxAudit", "Language")):
            stem = f"{label}O{level}{name}"
            command = [sys.executable, str(ROOT/"Test/Pointers/RunTests.py"),
                       "--suite", str(ROOT/"Test"/suite), f"--compiler-flag=-O{level}",
                       "--output", str(output/(stem+".json"))]
            with (output/(stem+".log")).open("w") as log:
                result = subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=300)
            results.append({"name":stem, "command":command, "returncode":result.returncode})
            print(stem, "PASS" if result.returncode == 0 else "FAIL", flush=True)
        # This older shell suite expects one compiler executable, so give it a
        # temporary wrapper while retaining the actual compiler hash below.
        with tempfile.TemporaryDirectory(prefix="kairote-syntax-level-") as directory:
            wrapper = Path(directory)/"KrtC"
            wrapper.write_text(f'#!/bin/sh\nexec {shlex.quote(str(compiler))} -O{level} "$@"\n')
            wrapper.chmod(0o755)
            stem = f"{label}O{level}Syntax"
            command = ["bash", str(ROOT/"Test/SyntaxAudit/run.sh")]
            with (output/(stem+".log")).open("w") as log:
                result = subprocess.run(command, cwd=ROOT, env=dict(environment,KRTC=str(wrapper)),
                                        stdout=log, stderr=subprocess.STDOUT, timeout=300)
            shutil.copy2(ROOT/"Test/SyntaxAudit/RESULTS.md",output/(stem+".md"))
            results.append({"name":stem, "command":command, "returncode":result.returncode})
            print(stem, "PASS" if result.returncode == 0 else "FAIL", flush=True)
    report = {"date_utc":datetime.now(timezone.utc).isoformat(), "compiler":str(compiler),
              "compiler_sha256":hashlib.sha256(compiler.read_bytes()).hexdigest(),
              "sanitized":args.sanitized, "leak_sanitizer":False, "results":results,
              "passed":all(row["returncode"] == 0 for row in results)}
    (output/(label+".json")).write_text(json.dumps(report,indent=2)+"\n")
    raise SystemExit(not report["passed"])


if __name__ == "__main__":
    main()
