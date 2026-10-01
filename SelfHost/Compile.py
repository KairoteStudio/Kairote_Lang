"""Compile Kairote sources with Stage 2 and ArkLink, without a seed fallback."""
import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAX_SOURCE_BYTES = 1048576
TOKEN = re.compile(rb'//[^\r\n]*|/\*.*?(?:\*/|$)|"(?:\\.|[^"\\])*(?:"|$)|\'(?:\\.|[^\'\\])*(?:\'|$)|[A-Za-z_][A-Za-z_0-9]*|[^\s]', re.S)
MODULE = re.compile(rb'[A-Za-z_][A-Za-z_0-9]*(?:\.[A-Za-z_][A-Za-z_0-9]*)*(?:\.\*)?')
NATIVE_DIAGNOSTIC = re.compile(r'^(E_[A-Z_]+) at byte (\d+)$', re.M)
MESSAGES = {
    "E_PARSE": "invalid syntax",
    "E_MODULE": "invalid module or entry point",
    "E_CONDITIONAL_ARRAY": "Conditional array operands must have the same element type and storage",
    "E_ARGUMENT_COUNT": "argument count does not match any callable declaration",
    "E_WRITABLE_REF": "reference arguments require writable storage and the ref marker",
    "E_UNDEFINED_METHOD": "Undefined method for the supplied receiver and argument types",
    "E_NOT_NULLABLE": "raw pointer parameters are not nullable; use an explicit pointer conversion",
    "E_GENERIC_CONSTRAINT": "generic type arguments do not satisfy the declared constraints",
    "E_LOWER": "invalid or unsupported expression, type, name, or control flow",
    "E_EMIT": "machine code generation failed",
    "E_LIBRARY_DATA": "object data requires a supported scalar constant initializer",
    "E_LIBRARY_SYMBOL": "unable to emit library symbols",
    "E_LINKAGE": "conflicting or unsupported native function prototype",
    "E_SIZE": "combined source must contain 1 to 1048576 bytes",
}


class DriverError(Exception):
    pass


@dataclass
class SourceFile:
    path: Path
    original: bytes
    transformed: bytes
    start: int = 0

    def location(self, offset):
        offset = max(0, min(offset, len(self.original)))
        prefix = self.original[:offset]
        line = prefix.count(b'\n') + 1
        column = len(prefix.rsplit(b'\n', 1)[-1].decode('utf-8', errors='replace')) + 1
        return f"{self.path}:{line}:{column}"


@dataclass
class SourceBundle:
    data: bytes
    files: list

    def diagnostic(self, output):
        def replace(match):
            code, raw_offset = match.groups()
            offset = int(raw_offset)
            selected = self.files[0]
            for source in self.files:
                if source.start > offset:
                    break
                selected = source
            location = selected.location(offset - selected.start)
            detail = MESSAGES.get(code, "compilation failed")
            return f"{location}: {code}: {detail} (combined byte {offset})"
        return NATIVE_DIAGNOSTIC.sub(replace, output)


def source_directives(source):
    """Find file directives, excluding comments, literals and unsafe capabilities.

    Imports become equal-length whitespace; using declarations reach native
    namespace binding. Both preserve original UTF-8 diagnostic offsets.
    """
    tokens = [m for m in TOKEN.finditer(source.original) if not m.group().startswith((b'//', b'/*'))]
    brace_depth = paren_depth = index = 0
    transformed = bytearray(source.original)
    directives = []
    while index < len(tokens):
        token = tokens[index]
        word = token.group()
        if brace_depth == 0 and paren_depth == 0 and word in (b'using', b'import'):
            end = index + 1
            while end < len(tokens) and tokens[end].group() not in (b';', b'{', b'}'):
                end += 1
            if end == len(tokens) or tokens[end].group() != b';':
                raise DriverError(f"{source.location(token.start())}: E_IMPORT: expected ';' after {word.decode()}")
            parts = b''.join(part.group() for part in tokens[index + 1:end])
            if word == b'using' and b'=' in parts:
                alias, separator, target = parts.partition(b'=')
                if not re.fullmatch(rb'[A-Za-z_][A-Za-z_0-9]*', alias) or not MODULE.fullmatch(target) or target.endswith(b'.*'):
                    raise DriverError(f"{source.location(token.start())}: E_IMPORT: invalid using alias")
                parts = target
            if word == b'import' and parts.startswith(b'"') and parts.endswith(b'"'):
                name = parts[1:-1].decode('utf-8')
                if not name or '\\' in name or '\0' in name:
                    raise DriverError(f"{source.location(token.start())}: E_IMPORT: invalid import path")
                quoted = True
            elif MODULE.fullmatch(parts):
                name = parts.decode('ascii')
                quoted = False
            else:
                raise DriverError(f"{source.location(token.start())}: E_IMPORT: expected a module name or quoted import path")
            directives.append((name, quoted, token.start()))
            # Native name binding also needs using declarations. Quoted imports
            # select files only and retain their original offsets as whitespace.
            if word == b'import':
                for byte in range(token.start(), tokens[end].end()):
                    if transformed[byte] not in (10, 13):
                        transformed[byte] = 32
            index = end
        elif word == b'{':
            brace_depth += 1
        elif word == b'}':
            brace_depth -= 1
        elif word == b'(':
            paren_depth += 1
        elif word == b')':
            paren_depth -= 1
        index += 1
    source.transformed = bytes(transformed)
    return directives


def collect_sources(paths, include_dirs=()):
    """Resolve imports once, in dependency-first order and sorted directory order.

    The importing file's directory wins, followed by -I paths, explicit source
    directories, then the repository libraries. Cycles share declarations once.
    """
    roots = [Path(path).resolve() for path in paths]
    search = list(dict.fromkeys([Path(path).resolve() for path in include_dirs] +
                               [path.parent for path in roots] +
                               [path.parent / 'libs' for path in roots] + [ROOT / 'libs']))
    ordered, visited, visiting = [], set(), []
    total_bytes = 0
    declared_namespaces = set()
    declared_types = set()

    def index_namespaces(data):
        tokens = [match.group() for match in TOKEN.finditer(data)
                  if not match.group().startswith((b'//', b'/*'))]
        current, scopes, index = '', [], 0
        while index < len(tokens):
            token = tokens[index]
            if token == b'namespace':
                end = index + 1
                while end < len(tokens) and tokens[end] not in (b';', b'{', b'}'):
                    end += 1
                name = b''.join(tokens[index + 1:end])
                if MODULE.fullmatch(name) and not name.endswith(b'.*'):
                    name = name.decode('ascii')
                    previous = current
                    current = f'{current}.{name}' if current else name
                    components = current.split('.')
                    declared_namespaces.update('.'.join(components[:part]) for part in range(1, len(components) + 1))
                    if end < len(tokens) and tokens[end] == b'{':
                        scopes.append(previous)
                    index = end
            elif token in (b'class', b'struct', b'enum', b'interface') and index + 1 < len(tokens):
                name = tokens[index + 1]
                if re.fullmatch(rb'[A-Za-z_][A-Za-z_0-9]*', name):
                    name = name.decode('ascii')
                    declared_types.add(f'{current}.{name}' if current else name)
            elif token == b'{':
                scopes.append(current)
            elif token == b'}' and scopes:
                current = scopes.pop()
            index += 1

    # A using can refer to a namespace in another explicit source file rather
    # than a filesystem module. Index these before visiting dependency edges.
    for path in roots:
        with path.open('rb') as stream:
            index_namespaces(stream.read(MAX_SOURCE_BYTES + 1))

    def resolve(source, name, quoted, position):
        directories = list(dict.fromkeys([source.path.parent] + search))
        relative = Path(name) if quoted else Path(*name.removesuffix('.*').split('.'))
        wildcard = not quoted and name.endswith('.*')
        for directory in directories:
            candidate = directory / relative
            if not quoted and candidate.is_dir():
                matches = sorted(candidate.glob('*.krt'))
                if matches:
                    return matches
            if not wildcard:
                candidate = candidate if quoted and candidate.suffix else candidate.with_suffix('.krt')
                if candidate.is_file():
                    return [candidate]
        if not quoted and name.removesuffix('.*') in declared_namespaces | declared_types:
            return []
        raise DriverError(f"{source.location(position)}: E_IMPORT: cannot find module {name!r} ({relative})")

    def visit(path, importer=None, position=0):
        nonlocal total_bytes
        path = path.resolve()
        if path in visiting:
            # Namespace dependencies may be mutually recursive. All units are
            # bound together, so traversing an in-progress unit again adds no
            # declarations; its other dependencies still get visited normally.
            return
        if path in visited:
            return
        with path.open('rb') as stream:
            original = stream.read(MAX_SOURCE_BYTES + 1)
        total_bytes += len(original) + (len(b'\nnamespace;\n') if visited or visiting else 0)
        if total_bytes > MAX_SOURCE_BYTES:
            raise DriverError(f'{path}: E_SIZE: combined source exceeds 1048576 bytes')
        try:
            original.decode('utf-8')
        except UnicodeDecodeError as error:
            raise DriverError(f"{path}: E_ENCODING: invalid UTF-8 at byte {error.start}") from error
        source = SourceFile(path, original, original)
        index_namespaces(original)
        visiting.append(path)
        for name, quoted, offset in source_directives(source):
            for dependency in resolve(source, name, quoted, offset):
                visit(dependency, source, offset)
        visiting.pop()
        visited.add(path)
        ordered.append(source)

    for path in roots:
        visit(path)
    pieces, offset = [], 0
    for source in ordered:
        if pieces:
            separator = b'\nnamespace;\n'
            pieces.append(separator)
            offset += len(separator)
        source.start = offset
        pieces.append(source.transformed)
        offset += len(source.transformed)
    return SourceBundle(b''.join(pieces), ordered)


def publish(artifact, output, executable):
    """Replace the destination only after writing and closing a complete file."""
    descriptor, temporary_name = tempfile.mkstemp(prefix=f'.{output.name}.', dir=output.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, 'wb') as target, artifact.open('rb') as source:
            shutil.copyfileobj(source, target)
        temporary.chmod(0o755 if executable else 0o644)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', type=Path, nargs='+', help='Kairote sources or existing .kro objects')
    parser.add_argument('-o', '--output', type=Path,
                        help='output path (default: first input basename, with .kro for -c, in the current directory)')
    parser.add_argument('-c', '--object', action='store_true', help='write a KRO object without linking')
    parser.add_argument('--check', action='store_true', help='compile and validate without publishing output')
    parser.add_argument('-I', '--include', type=Path, action='append', default=[], help='add a module search directory')
    parser.add_argument('-O', dest='optimization', choices=['0', '1', '2', '3'], default='0',
                        help='only -O0 is currently supported; there is no native optimization pipeline')
    parser.add_argument('--compiler', type=Path, default=Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')))
    linker = ROOT / 'build/ArkLink/ArkLink'
    parser.add_argument('--linker', type=Path, default=linker if linker.exists() else ROOT / 'ArkLink/build/ArkLink')
    # Re.KrtC's positional spelling is an alias, including paths with spaces.
    normalized = []
    preserve_next = False
    literal = False
    for word in argv:
        if literal or preserve_next:
            normalized.append(word)
            preserve_next = False
        elif word == '--':
            literal = True
            normalized.append(word)
        else:
            normalized.append('-o' if word == 'output' else word)
            preserve_next = word in ('-o', '--output', 'output', '-I', '--include', '--compiler', '--linker', '-O')
    args = parser.parse_intermixed_args(normalized)
    if args.optimization != '0':
        parser.error('E_OPTION: -O1/-O2/-O3 require a native optimizer, which is not implemented; use -O0')
    if args.object and any(path.suffix.lower() == '.kro' for path in args.sources):
        parser.error('E_OPTION: -c accepts source files; omit -c to link existing KRO objects')
    if args.output is None:
        first = Path(args.sources[0].name)
        args.output = first.with_suffix('.kro') if args.object else Path(first.stem)
    return args


def compile_files(argv=None, bundle=None):
    args = arguments(sys.argv[1:] if argv is None else argv)
    try:
        sources = [path for path in args.sources if path.suffix.lower() != '.kro']
        objects = [path.resolve() for path in args.sources if path.suffix.lower() == '.kro']
        if bundle is None:
            bundle = collect_sources(sources, args.include) if sources else None
        output = args.output.absolute()
        inputs = objects + ([source.path for source in bundle.files] if bundle else [])
        if output.resolve() in inputs:
            raise DriverError(f'E_OUTPUT: output would overwrite an input file: {output}')
        for path in objects:
            if not path.is_file():
                raise DriverError(f'E_INPUT: object file not found: {path}')
        with tempfile.TemporaryDirectory(prefix='kairote-selfhost-') as directory:
            work = Path(directory)
            empty_path = work / 'empty-path'
            empty_path.mkdir()
            env = dict(os.environ, PATH=str(empty_path))
            if bundle is not None:
                if not 0 < len(bundle.data) <= MAX_SOURCE_BYTES:
                    raise DriverError('E_SIZE: combined source must contain 1 to 1048576 bytes')
                compiler = args.compiler.resolve()
                if not compiler.is_file():
                    raise DriverError('E_COMPILER: Stage 2 is missing; run python3 Test/SelfHost/Bootstrap.py first')
                (work / 'program.krt').write_bytes(bundle.data)
                if args.object:
                    (work / 'library.mode').touch()
                compiled = subprocess.run([str(compiler)], cwd=work, env=env, capture_output=True, text=True,
                                          encoding='utf-8', errors='replace', timeout=120)
                if compiled.stdout:
                    print(bundle.diagnostic(compiled.stdout), end='')
                if compiled.stderr:
                    print(bundle.diagnostic(compiled.stderr), end='', file=sys.stderr)
                if compiled.returncode:
                    if not compiled.stderr or compiled.returncode < 0:
                        print(f'E_COMPILER: compiler exited with status {compiled.returncode}', file=sys.stderr)
                    return 1
                kro = work / 'stage1-probe.kro'
                if not kro.is_file():
                    raise DriverError('E_COMPILER: compiler succeeded but produced no KRO object')
                objects.insert(0, kro)
            if args.check:
                return 0
            if args.object:
                artifact = objects[0]
            else:
                artifact = work / 'program'
                linked = subprocess.run([str(args.linker.resolve()), *map(str, objects), '--target', 'elf',
                                         '-o', str(artifact)], cwd=work, env=env, capture_output=True,
                                        text=True, encoding='utf-8', errors='replace', timeout=30)
                if linked.returncode:
                    raise DriverError('E_LINK: ' + (linked.stdout + linked.stderr).strip())
                if not artifact.is_file():
                    raise DriverError('E_LINK: linker succeeded but produced no executable')
            publish(artifact, output, executable=not args.object)
        return 0
    except (DriverError, OSError, subprocess.TimeoutExpired) as error:
        print(str(error) if isinstance(error, DriverError) else f'E_DRIVER: {error}', file=sys.stderr)
        return 1


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    command_position = None
    options_with_value = {'-o', '--output', '-I', '--include', '--compiler', '--linker', '-O'}
    skip = False
    for index, word in enumerate(argv):
        if skip:
            skip = False
            continue
        if word in options_with_value:
            skip = True
        elif word in ('new', 'build', 'check', 'clean'):
            command_position = index
            break
        elif not word.startswith('-'):
            break
    if command_position is not None:
        argv = [argv[command_position], *argv[:command_position], *argv[command_position + 1:]]
        if __package__:
            from . import Project as project
        else:
            import Project as project
        return project.main(argv, sys.modules[__name__])
    return compile_files(argv)


if __name__ == '__main__':
    sys.exit(main())
