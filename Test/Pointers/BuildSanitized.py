"""Build the compiler with ASan and UBSan for the pointer regression suite."""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('/tmp/KrtC-pointer-sanitized'))
    args = parser.parse_args()
    project = ROOT / 'Re.KrtC'
    sources = re.findall(r'"([^"]+\.c)"', (project / 'build.zig').read_text())
    includes = ['src', 'src/Core', 'src/Tools', 'src/Bytecode', 'Shared', 'stub_include', 'vm']
    command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-w',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    command += ['-I' + str(project / path) for path in includes]
    command += [str(project / path) for path in sources]
    command += ['-I' + str(ROOT / 'ArkLink/include')]
    command += [str(path) for path in sorted((ROOT / 'ArkLink/src').rglob('*.c')) if path.name != 'Main.c']
    command += ['-lm', '-lpthread', '-o', str(args.output)]
    subprocess.run(command, check=True, timeout=180)
    print(f'Built {args.output}')


if __name__ == '__main__':
    main()
