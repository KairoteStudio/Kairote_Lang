"""Compile every compiler/linker translation unit with GCC and Clang diagnostics."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SOURCE_ROOTS = [ROOT / "Re.KrtC/src", ROOT / "Re.KrtC/Shared", ROOT / "Re.KrtC/stub_include",
                ROOT / "ArkLink/src", ROOT / "ArkLink/include"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=HERE / "AuditResults.json")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    standards = subprocess.run([sys.executable, str(HERE / "CheckStandards.py")],
                               cwd=ROOT, capture_output=True, text=True, timeout=90)
    sources = sorted({p for root in SOURCE_ROOTS for p in root.rglob("*") if p.suffix in (".c", ".h", ".inc")})
    units = [p for p in sources if p.suffix == ".c"]
    compilers = [shutil.which(name) for name in ("cc", "clang")]
    if not all(compilers):
        raise SystemExit("Both cc (GCC) and clang are required")
    checks = []
    with tempfile.TemporaryDirectory(prefix="kairote-audit-") as directory:
        def check(item):
            index, compiler, source = item
            includes = [ROOT / "ArkLink/include"] if "ArkLink" in source.parts else [
                ROOT / "Re.KrtC" / name for name in ("src", "src/Core", "src/Tools", "src/Bytecode", "Shared", "stub_include", "vm")]
            alignment = "-Wcast-align" if "clang" in compiler else "-Wcast-align=strict"
            flags = ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Wformat=2", "-Wshadow", "-Wundef", alignment]
            command = [compiler, *flags, *["-I" + str(path) for path in includes], "-c", str(source), "-o", f"{directory}/{index}.o"]
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            return {"compiler": compiler, "source": str(source.relative_to(ROOT)), "flags": flags,
                    "returncode": result.returncode, "diagnostics": result.stderr.replace(str(ROOT) + "/", ""),
                    "warnings": len(re.findall(r"\bwarning:", result.stderr)), "errors": len(re.findall(r"\berror:", result.stderr))}
        work = [(i, c, p) for i, (c, p) in enumerate((c, p) for c in compilers for p in units)]
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            checks = list(executor.map(check, work))
    style = []
    for path in sources:
        content = path.read_text()
        tabs = sum(bool(re.match(r"^[ \t]*\t", line)) for line in content.splitlines())
        trailing = sum(line != line.rstrip() for line in content.splitlines())
        if tabs or trailing or not content.endswith("\n"):
            style.append({"source": str(path.relative_to(ROOT)), "tab_indent_lines": tabs,
                          "trailing_whitespace_lines": trailing, "missing_final_newline": not content.endswith("\n")})
    # A declaration and a definition alone do not prove an exported API is safe to delete.
    strip = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    reference_files = sources + sorted((ROOT / "Test").rglob("*.c"))
    text = {path: strip.sub(" ", path.read_text()) for path in reference_files}
    candidates = []
    definitions = re.compile(r'^(?!static\b)(?:[A-Za-z_]\w*[ \t*]+)+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{', re.M)
    for path in units:
        for match in definitions.finditer(text[path]):
            name = match[1]
            if name == "main": continue
            pattern = re.compile(r'\b' + re.escape(name) + r'\b')
            usages = [(p, len(pattern.findall(code))) for p, code in text.items()]
            if sum(count for p, count in usages if p.suffix in (".c", ".inc")) == 1:
                candidates.append({"name": name, "source": str(path.relative_to(ROOT)),
                                   "declaration_references": sum(count for p, count in usages if p.suffix == ".h")})
    report = {"date_utc": datetime.now(timezone.utc).isoformat(), "source_file_count": len(sources),
              "translation_unit_count": len(units), "compiler_versions": {c: subprocess.check_output([c, "--version"], text=True).splitlines()[0] for c in compilers},
              "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
              "warnings": sum(c["warnings"] for c in checks), "errors": sum(c["errors"] for c in checks),
              "checks": checks, "formatting": style,
              "development_standards": {"returncode": standards.returncode,
                                        "output": standards.stdout + standards.stderr},
              "unreferenced_external_api_candidates": candidates,
              "external_api_policy": "Review candidates manually; external consumers, dynamic lookup and optional backends are not represented by a lexical scan."}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    for item in checks:
        if item["diagnostics"]: print(item["diagnostics"])
    print(f"{len(units)} units, {len(compilers)} compilers: {report['warnings']} warnings, {report['errors']} errors; {len(style)} files have formatting findings")
    print(standards.stdout + standards.stderr, end="")
    raise SystemExit(any(c["returncode"] or c["warnings"] for c in checks) or bool(style) or standards.returncode != 0)


if __name__ == "__main__":
    main()
