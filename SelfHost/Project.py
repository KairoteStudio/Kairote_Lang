"""Deterministic project builds for the native Kairote compiler.

Project configuration is data: Configure(Project p) supports assignments,
conditions, string expansion and the documented Project methods. It never runs
a shell command or calls a seed compiler.
"""
import argparse
import contextlib
import io
from dataclasses import dataclass
import glob
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import sys
import tempfile


class ProjectError(Exception):
    pass


TOKEN = re.compile(r'\s+|//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|==|!=|&&|\|\||[A-Za-z_][A-Za-z_0-9]*|[0-9]+|[{}().,;=+!]', re.S)
ALIASES = {
    'project': 'name', 'name': 'name', 'type': 'type', 'output': 'output',
    'sources': 'sources', 'source': 'sources', 'libraries': 'libraries',
    'using': 'libraries', 'includes': 'includes', 'include': 'includes',
    'includepaths': 'includes', 'optimization': 'optimization',
    'optimize': 'optimization', 'target': 'target',
    'version': 'version', 'description': 'description',
}
LISTS = {'sources', 'libraries', 'includes'}


def set_option(options, name, values, location):
    key = ALIASES.get(name.lower())
    if key is None:
        raise ProjectError(f'{location}: E_PROJECT: unknown project option {name!r}')
    if key in LISTS:
        if not all(isinstance(value, str) for value in values):
            raise ProjectError(f'{location}: E_PROJECT: {name} requires string paths')
        options.setdefault(key, []).extend(values)
    else:
        if len(values) != 1 or not isinstance(values[0], (str, int)) or isinstance(values[0], bool):
            raise ProjectError(f'{location}: E_PROJECT: {name} requires one string or integer')
        options[key] = str(values[0])


class ConfigurationParser:
    def __init__(self, path, source, configuration):
        self.path, self.source = path, source
        self.tokens, self.offsets, end = [], [], 0
        for match in TOKEN.finditer(source):
            if match.start() != end:
                self.fail('unrecognized configuration token', end)
            end = match.end()
            token = match.group()
            if token.isspace() or token.startswith(('//', '/*')):
                continue
            self.tokens.append(token)
            self.offsets.append(match.start())
        if end != len(source):
            self.fail('unrecognized configuration token', end)
        self.tokens.append('')
        self.offsets.append(len(source))
        self.position = 0
        self.values = {'os': sys.platform, 'platform': sys.platform, 'config': configuration,
                       'true': True, 'false': False}
        self.options = {}
        self.receiver = 'p'

    def fail(self, message, offset=None):
        if offset is None:
            offset = self.offsets[self.position]
        line = self.source.count('\n', 0, offset) + 1
        column = offset - self.source.rfind('\n', 0, offset)
        raise ProjectError(f'{self.path}:{line}:{column}: E_PROJECT: {message}')

    def peek(self):
        return self.tokens[self.position]

    def take(self, expected=None):
        word = self.peek()
        if expected is not None and word != expected:
            self.fail(f'expected {expected!r}, found {word!r}')
        if not word:
            self.fail('unexpected end of configuration')
        self.position += 1
        return word

    def expand(self, value):
        def replace(match):
            name = match.group(1)
            if name.startswith('env:'):
                return os.environ.get(name[4:], '')
            if name not in self.values:
                self.fail(f'unknown variable {name!r}')
            return str(self.values[name])
        return re.sub(r'\$\{([^}]+)\}', replace, value)

    def primary(self):
        word = self.take()
        if word == '(':
            value = self.expression()
            self.take(')')
            return value
        if word == '!':
            return not bool(self.primary())
        if word.startswith('"'):
            try:
                return self.expand(json.loads(word))
            except ValueError as error:
                self.fail(f'invalid string: {error}')
        if word.isdecimal():
            return int(word)
        if word not in self.values:
            self.fail(f'unknown variable {word!r}')
        return self.values[word]

    def expression(self, minimum=0):
        value = self.primary()
        precedence = {'||': 1, '&&': 2, '==': 3, '!=': 3, '+': 4}
        while self.peek() in precedence and precedence[self.peek()] >= minimum:
            operator = self.take()
            right = self.expression(precedence[operator] + 1)
            if operator == '+':
                if not isinstance(value, str) or not isinstance(right, str):
                    self.fail('configuration + accepts strings only')
                value += right
            elif operator == '==':
                value = value == right
            elif operator == '!=':
                value = value != right
            elif operator == '&&':
                value = bool(value) and bool(right)
            else:
                value = bool(value) or bool(right)
        return value

    def statement(self, active=True):
        word = self.peek()
        if word == '{':
            self.take()
            while self.peek() != '}':
                self.statement(active)
            self.take('}')
        elif word == 'if':
            self.take()
            self.take('(')
            condition = bool(self.expression())
            self.take(')')
            previous = dict(self.values)
            self.statement(active and condition)
            if not (active and condition):
                self.values = previous
            if self.peek() == 'else':
                self.take()
                previous = dict(self.values)
                self.statement(active and not condition)
                if not (active and not condition):
                    self.values = previous
        elif word in ('string', 'var', 'let'):
            self.take()
            name = self.take()
            if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name):
                self.fail('expected variable name')
            self.take('=')
            value = self.expression()
            self.take(';')
            self.values[name] = value
        elif word == self.receiver or (word.lower() in ALIASES and self.tokens[self.position + 1] == '('):
            if word == self.receiver:
                self.take()
                self.take('.')
                method = self.take()
            else:
                method = self.take()
            self.take('(')
            values = []
            if self.peek() != ')':
                values.append(self.expression())
                while self.peek() == ',':
                    self.take()
                    values.append(self.expression())
            self.take(')')
            self.take(';')
            # Validate inactive branches too, so typos never become latent.
            set_option(self.options if active else {}, method, values, str(self.path))
        elif word in self.values and self.tokens[self.position + 1] == '=':
            name = self.take()
            self.take('=')
            value = self.expression()
            self.take(';')
            self.values[name] = value
        else:
            self.fail(f'unsupported configuration statement {word!r}')

    def parse(self):
        found = False
        while self.peek():
            if self.peek() == 'function':
                if found:
                    self.fail('only one Configure function is allowed')
                found = True
                self.take('function')
                self.take('Configure')
                self.take('(')
                if self.peek() != ')':
                    self.take('Project')
                    self.receiver = self.take()
                self.take(')')
                if self.peek() != '{':
                    self.fail('Configure requires a body')
                self.statement()
            else:
                self.statement()
        if not found:
            self.fail('expected function Configure(Project p)')
        return self.options


@dataclass
class Project:
    path: Path
    options: dict
    sources: list
    includes: list
    objects: list
    output: Path
    configuration: str

    @property
    def cache(self):
        identifier = hashlib.sha256(str(self.path).encode()).hexdigest()[:12]
        return self.path.parent / '.krtcache' / identifier


def project_file(path):
    path = Path(path).resolve()
    if path.is_dir():
        for name in ('project.krt', 'project.json'):
            if (path / name).is_file():
                return path / name
        raise ProjectError(f'{path}: E_PROJECT: no project.krt or project.json found')
    if not path.is_file():
        raise ProjectError(f'{path}: E_PROJECT: project file not found')
    return path


def load(path, configuration, root):
    path = project_file(path)
    source = path.read_text(encoding='utf-8')
    if path.suffix.lower() == '.json' or source.lstrip().startswith('{'):
        try:
            raw = json.loads(source)
        except ValueError as error:
            raise ProjectError(f'{path}: E_PROJECT: {error}') from error
        if not isinstance(raw, dict):
            raise ProjectError(f'{path}: E_PROJECT: expected a JSON object')
        options = {}
        for name, value in raw.items():
            set_option(options, name, value if isinstance(value, list) else [value], str(path))
    elif re.search(r'^\s*(?:project|name|type|output|sources)\s*:', source, re.M):
        options = {}
        for number, line in enumerate(source.splitlines(), 1):
            line = line.strip()
            if not line or line.startswith(('#', '//')):
                continue
            if ':' not in line:
                raise ProjectError(f'{path}:{number}: E_PROJECT: expected key: value')
            name, value = line.split(':', 1)
            values = shlex.split(value, comments=True)
            set_option(options, name.strip(), values, f'{path}:{number}')
    else:
        options = ConfigurationParser(path, source, configuration).parse()
    options.setdefault('name', path.parent.name)
    options.setdefault('type', 'console')
    options.setdefault('optimization', '0')
    options.setdefault('target', 'elf')
    if options['type'] not in ('console', 'exe', 'system', 'library', 'lib'):
        raise ProjectError(f'{path}: E_PROJECT: project type {options["type"]!r} is unsupported; choose console, system, or library')
    if options['optimization'] not in ('0', 'O0', '-O0'):
        raise ProjectError(f'{path}: E_OPTION: native optimizer is not implemented; set Optimization("0")')
    if options['target'] not in ('elf', 'linux-x86_64'):
        raise ProjectError(f'{path}: E_OPTION: native project target must be elf or linux-x86_64')
    if not options.get('sources'):
        raise ProjectError(f'{path}: E_PROJECT: no Sources defined')
    includes = [(path.parent / item).resolve() for item in options.get('includes', [])]
    sources, objects = [], []

    def add(filename):
        filename = filename.resolve()
        if not filename.is_file():
            raise ProjectError(f'{path}: E_INPUT: source or library not found: {filename}')
        target = objects if filename.suffix.lower() == '.kro' else sources
        if filename not in target:
            target.append(filename)

    for pattern in options['sources']:
        matches = sorted(glob.glob(str(path.parent / pattern), recursive=True))
        matches = [Path(match) for match in matches if Path(match).is_file()]
        if not matches:
            raise ProjectError(f'{path}: E_INPUT: source pattern matched no files: {pattern}')
        for match in matches:
            if match.resolve() == path:
                continue
            add(match)
    for library in options.get('libraries', []):
        direct = path.parent / library
        candidates = [direct]
        if not Path(library).suffix or Path(library).suffix not in ('.krt', '.kro'):
            relative = Path(*library.removesuffix('.*').split('.'))
            for directory in [path.parent, path.parent / 'libs', *includes, root / 'libs', root / 'libs/System']:
                candidates.append(directory / relative)
                if not library.endswith('.*'):
                    candidates.append(directory / relative.with_suffix('.krt'))
        selected = next((candidate for candidate in candidates if candidate.is_file() or candidate.is_dir()), None)
        if selected is None:
            raise ProjectError(f'{path}: E_IMPORT: library not found: {library}')
        if selected.is_dir():
            matches = sorted(selected.glob('*.krt'))
            if not matches:
                raise ProjectError(f'{path}: E_IMPORT: library contains no source files: {library}')
            for match in matches:
                add(match)
        else:
            add(selected)
    if not sources:
        raise ProjectError(f'{path}: E_PROJECT: no source files selected')
    output_name = options.get('output', f'bin/{configuration}/{options["name"]}')
    # Re.KrtC places a bare Output name under bin/<platform>. An explicit path
    # is relative to the project, so build configurations can choose directories.
    if '/' not in output_name and '\\' not in output_name:
        output_name = f'bin/{sys.platform}/{output_name}'
    if options['type'] in ('library', 'lib') and not output_name.endswith('.kro'):
        output_name += '.kro'
    output = (path.parent / output_name).resolve()
    return Project(path, options, sources, includes, objects, output, configuration)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def atomic_json(path, value):
    descriptor, name = tempfile.mkstemp(prefix='.manifest-', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write('\n')
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def manifest(path):
    try:
        result = json.loads(path.read_text())
        return result if isinstance(result, dict) else {}
    except (OSError, ValueError):
        return {}


def build(project, args, driver):
    sources = project.sources
    library = project.options['type'] in ('library', 'lib')
    includes = [*project.includes, *(path.resolve() for path in args.include)]
    bundle = driver.collect_sources(sources, includes)
    output = args.output.resolve() if args.output else project.output
    inputs = [project.path, *project.objects, *(part.path for part in bundle.files)]
    compiler, linker = args.compiler.resolve(), args.linker.resolve()
    if output in inputs or output in (compiler, linker):
        raise ProjectError(f'E_OUTPUT: output would overwrite an input file or tool: {output}')
    if project.cache == output or project.cache in output.parents:
        raise ProjectError(f'E_OUTPUT: output must be outside the project cache: {output}')
    if not compiler.is_file():
        raise ProjectError('E_COMPILER: Stage 2 is missing; run python3 Test/SelfHost/Bootstrap.py first')
    if args.command != 'check' and not library and not linker.is_file():
        raise ProjectError(f'E_LINK: linker not found: {linker}')
    fingerprint = {
        'schema': 1, 'configuration': project.configuration, 'options': project.options,
        'output': str(output), 'includes': [str(item) for item in includes],
        'dependencies': {str(path): sha(path) for path in inputs},
        'bundle': hashlib.sha256(bundle.data).hexdigest(),
        'compiler': {'path': str(compiler), 'sha256': sha(compiler)},
        'driver': sha(Path(driver.__file__)), 'project_driver': sha(Path(__file__)),
    }
    if args.command != 'check' and not library:
        fingerprint['linker'] = {'path': str(linker), 'sha256': sha(linker)}
    if args.optimization != '0':
        raise ProjectError('E_OPTION: -O1/-O2/-O3 require a native optimizer; use -O0')
    key = hashlib.sha256(json.dumps(fingerprint, sort_keys=True).encode()).hexdigest()
    old = manifest(project.cache / 'manifest.json')
    artifact = project.cache / 'artifact'
    objects_valid = all((project.cache / item['cache']).is_file() and sha(project.cache / item['cache']) == item['sha256'] for item in old.get('objects', []))
    if args.command != 'check' and not args.rebuild and old.get('key') == key and objects_valid and artifact.is_file() and sha(artifact) == old.get('artifact_sha256'):
        output.parent.mkdir(parents=True, exist_ok=True)
        if not output.is_file() or sha(output) != old['artifact_sha256']:
            driver.publish(artifact, output, not library)
        for item in old.get('objects', []):
            cached = project.cache / item['cache']
            target = Path(item['output'])
            if cached.is_file() and sha(cached) == item['sha256']:
                if not target.is_file() or sha(target) != item['sha256']:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    driver.publish(cached, target, False)
        print(f'up-to-date: {output}')
        return 0
    command = [*map(str, sources), *map(str, project.objects), '--compiler', str(compiler), '--linker', str(linker)]
    for directory in includes:
        command.extend(['-I', str(directory)])
    if library:
        command.append('-c')
    if args.command == 'check':
        result = driver.compile_files([*command, '--check'], bundle=bundle)
        if result == 0:
            print(f'checked: {project.path}')
        return result
    project.cache.mkdir(parents=True, exist_ok=True)
    # A failed compiler/linker never replaces either the successful output or
    # the manifest describing it. Temporary builds live beside the cache.
    with tempfile.TemporaryDirectory(prefix='.build-', dir=project.cache) as temporary:
        candidate = Path(temporary) / 'artifact'
        result = driver.compile_files([*command, '-o', str(candidate)], bundle=bundle)
        if result:
            return result
        object_records = []
        object_notes = []
        for index, source in enumerate(sources):
            try:
                relative = source.relative_to(project.path.parent).with_suffix('.kro')
            except ValueError:
                relative = Path('_external') / (hashlib.sha256(str(source).encode()).hexdigest()[:12] + '-' + source.stem + '.kro')
            object_output = project.path.parent / 'obj' / relative
            if object_output.resolve() in inputs or object_output.resolve() == output:
                raise ProjectError(f'E_OUTPUT: object would overwrite an input or final output: {object_output}')
            object_candidate = Path(temporary) / f'unit-{index}.kro'
            logs = io.StringIO()
            with contextlib.redirect_stdout(logs), contextlib.redirect_stderr(logs):
                result = driver.compile_files([str(source), '--compiler', str(compiler), '-c', '-o', str(object_candidate),
                                               *[part for directory in includes for part in ('-I', str(directory))]])
            if result:
                object_notes.append({'source': str(source), 'reason': logs.getvalue().strip()})
                print(f'object deferred: {source} requires the combined project context', file=sys.stderr)
                continue
            cache_name = f'unit-{index}.kro'
            object_output.parent.mkdir(parents=True, exist_ok=True)
            driver.publish(object_candidate, object_output, False)
            driver.publish(object_candidate, project.cache / cache_name, False)
            object_records.append({'source': str(source), 'output': str(object_output), 'cache': cache_name, 'sha256': sha(object_candidate)})
        current_objects = {item['output'] for item in object_records}
        for previous in old.get('objects', []):
            target = Path(previous['output'])
            if previous['output'] not in current_objects and target.is_file() and sha(target) == previous['sha256']:
                target.unlink()
        output.parent.mkdir(parents=True, exist_ok=True)
        driver.publish(candidate, output, not library)
        driver.publish(candidate, artifact, not library)
        atomic_json(project.cache / 'manifest.json', {
            'key': key, 'inputs': fingerprint, 'artifact_sha256': sha(artifact),
            'output': str(output), 'project': str(project.path), 'objects': object_records, 'deferred_objects': object_notes,
        })
    print(f'built: {output}')
    return 0


def clean(project, args):
    cached = manifest(project.cache / 'manifest.json')
    output = Path(cached.get('output', str(args.output.resolve() if args.output else project.output))).resolve()
    # Only remove the artifact recorded by a successful build. Never remove a
    # replaced user file, project source, or recursively erase an output tree.
    if cached.get('project') == str(project.path) and output.is_file() and sha(output) == cached.get('artifact_sha256'):
        output.unlink()
        print(f'removed: {output}')
    if cached.get('project') == str(project.path):
        for item in cached.get('objects', []):
            target = Path(item['output'])
            if target.is_file() and sha(target) == item['sha256']:
                target.unlink()
    if project.cache.is_dir():
        shutil.rmtree(project.cache)
        print(f'cleaned: {project.cache}')
    return 0


def new(args):
    if args.kind not in ('console', 'exe', 'system', 'library', 'lib'):
        raise ProjectError(f'E_PROJECT: project type {args.kind!r} is unsupported; choose console, system, or library')
    destination = args.directory.resolve()
    if destination.exists() and any(destination.iterdir()):
        raise ProjectError(f'E_PROJECT: destination is not empty: {destination}')
    name = args.name or destination.name
    if not name or name in ('.', '..') or any(character in name for character in ('/', '\\', '\0', '\n', '\r')):
        raise ProjectError('E_PROJECT: project name must not contain a path separator or newline')
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'src').mkdir(exist_ok=True)
    if args.kind in ('library', 'lib'):
        (destination / 'src/library.krt').write_text('int64 Add(int64 left, int64 right) { return left + right; }\n')
    else:
        (destination / 'src/main.krt').write_text('using System.Console;\n\nint32 main() {\n    Console.WriteLine("Hello, Kairote!");\n    return 0;\n}\n')
    quoted = json.dumps(name, ensure_ascii=False)
    config = ('// Native Kairote project\nfunction Configure(Project p) {\n'
              f'    p.Name({quoted});\n    p.Type("{args.kind}");\n'
              '    p.Sources("src/**/*.krt");\n    p.Output("bin/${config}/' + name.replace('\\', '\\\\').replace('"', '\\"') + '");\n}\n')
    (destination / 'project.krt').write_text(config)
    (destination / '.gitignore').write_text('bin/\nobj/\n.krtcache/\n')
    print(f'created: {destination / "project.krt"}')
    return 0


def arguments(argv, driver):
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest='command', required=True)
    create = subparsers.add_parser('new', help='create a native executable project')
    create.add_argument('kind', help='console, exe, system, or library')
    create.add_argument('directory', type=Path)
    create.add_argument('--name')
    for command in ('build', 'check', 'clean'):
        sub = subparsers.add_parser(command)
        sub.add_argument('project', type=Path, nargs='?', default=Path('.'))
        sub.add_argument('--config', default=os.environ.get('KRT_CONFIG', 'release'))
        sub.add_argument('-o', '--output', type=Path)
        sub.add_argument('-I', '--include', action='append', type=Path, default=[])
        sub.add_argument('--compiler', type=Path, default=Path(os.environ.get('SELFHOST_COMPILER', driver.ROOT / 'build/selfhost/stage2/program')))
        linker = driver.ROOT / 'build/ArkLink/ArkLink'
        sub.add_argument('--linker', type=Path, default=linker if linker.exists() else driver.ROOT / 'ArkLink/build/ArkLink')
        sub.add_argument('--rebuild', action='store_true', help='ignore the successful-build cache')
        sub.add_argument('-O', dest='optimization', choices=['0', '1', '2', '3'], default='0')
    return parser.parse_args(argv)


def main(argv, driver):
    args = arguments(argv, driver)
    try:
        if args.command == 'new':
            return new(args)
        if args.command == 'clean':
            path = project_file(args.project)
            project = Project(path, {}, [], [], [], path.parent / 'unused', args.config)
            return clean(project, args)
        project = load(args.project, args.config, driver.ROOT)
        return build(project, args, driver)
    except (ProjectError, driver.DriverError, OSError, UnicodeError, ValueError) as error:
        print(str(error) if isinstance(error, (ProjectError, driver.DriverError)) else f'E_PROJECT: {error}', file=sys.stderr)
        return 1
