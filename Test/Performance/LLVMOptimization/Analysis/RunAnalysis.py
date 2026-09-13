#!/usr/bin/env python3
"""Preserve compiler output and optimization logs; never run timed benchmarks."""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SOURCE = ROOT / 'Test/Performance/O3Optimization/Artifacts'
FLAGS = ['-O3', '-std=gnu11', '-fwrapv', '-fno-lto', '-fno-pie']
VARIANTS = {
    'GccDefault': ('gcc', []),
    'GccNoRecursive': ('gcc', ['--param=max-inline-recursive-depth-auto=0']),
    'GccRecursive2': ('gcc', ['--param=max-inline-recursive-depth-auto=2']),
    'GccRecursive4': ('gcc', ['--param=max-inline-recursive-depth-auto=4']),
    'GccNoInline': ('gcc', ['-fno-inline']),
    'GccNoTail': ('gcc', ['-fno-optimize-sibling-calls']),
    'ClangDefault': ('clang', []),
}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

manifest = {'purpose': 'untimed compiler optimization investigation', 'versions': {}, 'compiler_sha256': {}, 'runs': []}
for compiler in ['gcc', 'clang']:
    manifest['versions'][compiler] = subprocess.check_output([compiler, '--version'], text=True)
    manifest['compiler_sha256'][compiler] = digest(pathlib.Path(shutil.which(compiler)).resolve())
params = subprocess.run(['gcc', '-O3', '-Q', '--help=params'], capture_output=True, text=True, check=True)
(HERE / 'GccParameters.txt').write_text(params.stdout + params.stderr)
for case in ['Fib40', 'Branching', 'BinaryTree']:
    case_dir = HERE / case
    case_dir.mkdir(exist_ok=True)
    source = case_dir / (case + '.c')
    shutil.copyfile(SOURCE / source.name, source)
    for name, (compiler, extra) in VARIANTS.items():
        directory = case_dir / name
        directory.mkdir(exist_ok=True)
        flags = FLAGS + extra
        log_options = (['-fdump-ipa-inline-details', '-fdump-tree-tailr1-details', '-fdump-tree-optimized', '-fopt-info-inline-optimized=Inline.txt']
                       if compiler == 'gcc' else ['-Rpass=inline', '-Rpass-missed=inline', '-Rpass=tailcallelim'])
        command = [compiler] + flags + log_options + ['-c', str(source), '-o', 'Kernel.o']
        if compiler == 'gcc':
            (directory / 'Inline.txt').write_text('')
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True)
        (directory / 'Diagnostics.txt').write_text(result.stdout + result.stderr)
        result.check_returncode()
        assembly = subprocess.run(['objdump', '-drwC', '-Mintel', 'Kernel.o'], cwd=directory, capture_output=True, text=True, check=True)
        (directory / 'Kernel.asm').write_text(assembly.stdout)
        if compiler == 'clang':
            ircommand = [compiler] + flags + ['-S', '-emit-llvm', str(source), '-o', 'Kernel.ll']
            subprocess.run(ircommand, cwd=directory, capture_output=True, text=True, check=True)
        sizes = subprocess.check_output(['nm', '-S', '--size-sort', 'Kernel.o'], cwd=directory, text=True)
        (directory / 'Sizes.txt').write_text(sizes)
        manifest['runs'].append({'case': case, 'variant': name, 'command': command, 'source_sha256': digest(source),
                                 'object_sha256': digest(directory / 'Kernel.o'), 'files': {file.name: digest(file) for file in sorted(directory.iterdir()) if file.is_file()}, 'directory': str(directory.relative_to(HERE)),
                                 'sizes': sizes})
summary = []
for run in manifest['runs']:
    directory = HERE / run['directory']
    function = {'Fib40': 'Recur', 'Branching': 'Paths', 'BinaryTree': 'Tree'}[run['case']]
    size = next(int(line.split()[1], 16) for line in run['sizes'].splitlines() if line.endswith(' ' + function))
    logs = list(directory.glob('*inline'))
    log_text = logs[0].read_text() if logs else ''
    depths = [int(depth) for depth in re.findall(r'Inlining call of depth (\d+)', log_text)]
    summary.append({'case': run['case'], 'variant': run['variant'], 'function_bytes': size,
                    'recursive_inline_depths': depths})
(HERE / 'Summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(HERE / 'Manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Prepared', len(manifest['runs']), 'untimed compiler analyses.')
