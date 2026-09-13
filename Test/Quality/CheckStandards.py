"""Check changed C code against the mechanical rules in DevStand.md."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FORMAT = Path(__file__).with_name("ClangFormat.yaml")
SOURCE_SUFFIXES = {".c", ".h", ".inc"}
LEXICAL_LITERAL = re.compile(r'//(?:\\\r?\n|[^\n])*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
MACRO = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)", re.M)
TOKEN = re.compile(r"[A-Za-z_]\w*|[^\s]")
STORAGE = {"static", "extern", "inline", "__inline", "__inline__", "_Noreturn"}
TYPE_WORDS = {"void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "_Bool"}


@dataclass(frozen=True)
class Declaration:
    name: str
    modifiers: tuple
    start: int
    line: int
    length: int
    definition: bool


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def blank(text):
    return "".join("\n" if character == "\n" else " " for character in text)


def code_only(source):
    return LEXICAL_LITERAL.sub(lambda match: blank(match[0]), source)


def declaration_code(source):
    code = code_only(source)
    lines = code.splitlines(keepends=True)
    continuation = False
    for index, line in enumerate(lines):
        directive = continuation or line.lstrip().startswith("#")
        continuation = directive and line.rstrip("\r\n").endswith("\\")
        if directive:
            lines[index] = blank(line)
    return "".join(lines)


def matching_token(tokens, start, limit):
    opening = tokens[start][0]
    closing = {"(": ")", "[": "]", "{": "}"}[opening]
    depth = 1
    for index in range(start + 1, limit):
        depth += (tokens[index][0] == opening) - (tokens[index][0] == closing)
        if depth == 0:
            return index
    return None


def annotation_suffix(tokens, start):
    """Recognize trailing attribute macros without mistaking them for an API."""
    while start < len(tokens):
        name = tokens[start][0]
        if not (re.fullmatch(r"[A-Z_][A-Z0-9_]*", name) or name in {"__attribute__", "__attribute", "__declspec"}):
            return False
        start += 1
        if start < len(tokens) and tokens[start][0] == "(":
            close = matching_token(tokens, start, len(tokens))
            if close is None:
                return False
            start = close + 1
    return True


def function_signature(tokens):
    if not tokens or any(token[0] in {"=", "typedef"} for token in tokens):
        return None
    index = 0
    while index + 1 < len(tokens):
        name = tokens[index][0]
        if (re.fullmatch(r"[A-Za-z_]\w*", name) and name not in TYPE_WORDS
                and tokens[index + 1][0] == "("
                and (index + 2 == len(tokens) or tokens[index + 2][0] != "*")):
            close = matching_token(tokens, index + 1, len(tokens))
            if close is None:
                return None
            prefix = tuple(token[0] for token in tokens[:index])
            has_type = any(re.fullmatch(r"[A-Za-z_]\w*", token) and token not in STORAGE for token in prefix)
            if name not in TYPE_WORDS and has_type and annotation_suffix(tokens, close + 1):
                return name, prefix
            index = close + 1
        elif tokens[index][0] in {"(", "["}:
            close = matching_token(tokens, index, len(tokens))
            if close is None:
                return None
            # A function may return a function pointer: int (*select(void))(int).
            after = close + 1
            if tokens[index][0] == "(" and after < len(tokens) and tokens[after][0] == "(":
                return_close = matching_token(tokens, after, len(tokens))
                if return_close is not None and annotation_suffix(tokens, return_close + 1):
                    nested = function_signature(tokens[:index] + tokens[index + 1:close])
                    if nested:
                        return nested
            index = close + 1
        else:
            index += 1
    return None


def declarations(source):
    """Yield top-level function declarations, excluding bodies and macro text."""
    code = declaration_code(source)
    tokens = [(match[0], match.start(), match.end()) for match in TOKEN.finditer(code)]

    def scan(start, limit):
        declaration_start = start
        index = start
        while index < limit:
            spelling = tokens[index][0]
            if spelling in {"(", "["}:
                close = matching_token(tokens, index, limit)
                if close is None:
                    return
                index = close + 1
                continue
            if spelling not in {";", "{"}:
                index += 1
                continue
            signature = tokens[declaration_start:index]
            function = function_signature(signature)
            if spelling == "{":
                close = matching_token(tokens, index, limit)
                if close is None:
                    return
                # A C++ linkage wrapper encloses public C declarations.
                if [token[0] for token in signature] == ["extern"]:
                    yield from scan(index + 1, close)
                if function:
                    offset = tokens[declaration_start][1]
                    end = tokens[close][2]
                    yield Declaration(function[0], function[1], offset, code.count("\n", 0, offset) + 1,
                                      code.count("\n", offset, end) + 1, True)
                index = close + 1
                if function or [token[0] for token in signature] == ["extern"]:
                    declaration_start = index
            else:
                if function:
                    offset = tokens[declaration_start][1]
                    yield Declaration(function[0], function[1], offset, code.count("\n", 0, offset) + 1, 0, False)
                index += 1
                declaration_start = index

    yield from scan(0, len(tokens))


def functions(source):
    for declaration in declarations(source):
        if declaration.definition:
            yield declaration.name, declaration.modifiers, declaration.line, declaration.length


def documented(source, declaration):
    prefix = source[:declaration.start].rstrip()
    if prefix.endswith("*/"):
        opening = prefix.rfind("/*")
        return opening >= 0 and prefix[opening:opening + 3] in {"/**", "/*!"}
    return bool(re.search(r"(?:^|\n)[ \t]*//[/!][^\n]*$", prefix))


def public_declaration(declaration, header):
    return "static" not in declaration.modifiers or (header and bool(re.fullmatch(r"Krt[A-Z][A-Za-z0-9]*", declaration.name)))


def check_rules(path, source, baseline):
    relative = str(path.relative_to(ROOT))
    findings = []
    notices = []
    if len(source.splitlines()) > 5000:
        findings.append(f"{relative}: file exceeds 5000 lines")
    old_functions = {name: length for name, _, _, length in functions(baseline)}
    current = list(declarations(source))
    for declaration in current:
        name, line, length = declaration.name, declaration.line, declaration.length
        if ("static" in declaration.modifiers
                and not public_declaration(declaration, path.suffix == ".h")
                and not re.fullmatch(r"[a-z][a-z0-9_]*", name)):
            findings.append(f"{relative}:{line}: internal function {name} must use snake_case")
        if length > 500:
            if old_functions.get(name, 0) > 500:
                notices.append(f"{relative}:{line}: {name} has {length} lines (pre-existing; report under DevStand 1.4)")
            else:
                findings.append(f"{relative}:{line}: {name} has {length} lines; split or report under DevStand 1.4")
    if relative.startswith("Re.KrtC/"):
        old_macros = set(MACRO.findall(code_only(baseline)))
        for macro in sorted(set(MACRO.findall(code_only(source))) - old_macros):
            if not re.fullmatch(r"KRT_[A-Z0-9_]+", macro):
                findings.append(f"{relative}: new macro {macro} must use the KRT_ prefix and uppercase")
    if path.suffix == ".h":
        clean = code_only(source)
        guard = re.search(r"^[ \t]*#\s*ifndef\s+(\w+)\s*\n\s*#\s*define\s+\1\b", clean, re.M)
        if not guard:
            guard = re.search(r"^[ \t]*#\s*if\s+!\s*defined\s*\(?\s*(\w+)\s*\)?\s*\n\s*#\s*define\s+\1\b", clean, re.M)
        if not guard:
            findings.append(f"{relative}: missing matching include guard")
        old_public = {item.name for item in declarations(baseline) if public_declaration(item, True)}
        for declaration in current:
            if not public_declaration(declaration, True) or declaration.name in old_public:
                continue
            name = declaration.name
            valid_name = re.fullmatch(r"Krt[A-Z][A-Za-z0-9]*", name)
            naming_rule = "Krt-prefixed PascalCase"
            if relative.startswith("ArkLink/"):
                valid_name = re.fullmatch(r"(?:ark_|arklink_)[a-z][a-z0-9_]*", name)
                naming_rule = "the existing ark_/arklink_ prefix and snake_case"
            if not valid_name:
                findings.append(f"{relative}:{declaration.line}: new API {name} must use {naming_rule}")
            if not documented(source, declaration):
                findings.append(f"{relative}:{declaration.line}: new API {name} needs a preceding documentation comment")
    return findings, notices


def check_source(path, baseline):
    source = path.read_text()
    findings, notices = check_rules(path, source, baseline)
    formatted = subprocess.run(
        ["clang-format", "--style=file:" + str(FORMAT), str(path)],
        text=True, capture_output=True, check=True,
    ).stdout
    if formatted != source:
        findings.insert(0, f"{path.relative_to(ROOT)}: formatting differs from the shared K&R/4-space rules")
    return findings, notices


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="main", help="Base revision used to distinguish existing interfaces")
    args = parser.parse_args()
    git("rev-parse", "--verify", args.base + "^{commit}")
    changed = set(git("diff", "--name-only", "-z", args.base).decode().split("\0"))
    changed.update(git("ls-files", "--others", "--exclude-standard", "-z").decode().split("\0"))
    tracked = set(git("ls-files", "-z").decode().split("\0"))
    original = set(git("ls-tree", "-r", "--name-only", "-z", args.base).decode().split("\0"))
    paths = sorted(ROOT / name for name in changed if name and (ROOT / name).is_file()
                   and Path(name).suffix in SOURCE_SUFFIXES)
    work = []
    for path in paths:
        name = str(path.relative_to(ROOT))
        baseline = git("show", args.base + ":" + name).decode() if name in original else ""
        work.append((path, baseline))
    findings = []
    notices = []
    with ThreadPoolExecutor(max_workers=4) as executor:
        for errors, reports in executor.map(lambda item: check_source(*item), work):
            findings.extend(errors)
            notices.extend(reports)
    for name in sorted(changed - original):
        if not name or not (ROOT / name).is_file():
            continue
        path = Path(name)
        # The user explicitly excluded new JSON/ASM/KRO outputs from this PR.
        # ELF/object/log outputs share that PR policy; DevStand permits configuration JSON.
        if name in tracked and path.suffix.lower() in {".json", ".asm", ".kro", ".elf", ".o", ".log"}:
            findings.append(f"{name}: generated artifact must remain untracked")
        if path.name.startswith("."):
            continue
        # Section 2.1 explicitly requires test_ names; tool-owned names retain their canonical spelling.
        if name.startswith("Test/") and path.name.startswith("test_"):
            continue
        if path.suffix in SOURCE_SUFFIXES | {".py", ".md"} and not re.fullmatch(r"[A-Z][A-Za-z0-9]*", path.stem):
            findings.append(f"{name}: new source/document filename must use PascalCase")
    for finding in findings:
        print("FAIL", finding)
    for notice in notices:
        print("REPORT", notice)
    print(f"{len(paths)} changed C source/header/include files: {len(findings)} violations, {len(notices)} length reports")
    return bool(findings)


if __name__ == "__main__":
    sys.exit(main())
