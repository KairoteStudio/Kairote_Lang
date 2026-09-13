"""Benchmark handwritten x86-64 ASM and C naive recursive Fibonacci on this host."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCES = HERE / 'NativeFib'
VARIANTS = {'asm': 'fib_asm', 'c_recursive': 'fib_c', 'c_o2': 'fib_c_o2'}


def checked(command, **kwargs):
    result = subprocess.run([str(v) for v in command], capture_output=True,
                            text=True, timeout=60, **kwargs)
    if result.returncode:
        raise RuntimeError(f'{command}\n{result.stdout}\n{result.stderr}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rounds', type=int, default=10)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('--output', type=Path, default=HERE / 'NativeFibResults.json')
    args = parser.parse_args()
    if args.rounds < 1 or args.cpu not in os.sched_getaffinity(0):
        parser.error('positive rounds and an available logical CPU are required')
    if platform.machine() != 'x86_64' or platform.system() != 'Linux':
        parser.error('requires Linux x86-64')
    cc = os.environ.get('CC', 'cc')
    flags = ['-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fno-lto']
    report = {
        'date_utc': datetime.now(timezone.utc).isoformat(),
        'cpu': next(line.split(':', 1)[1].strip() for line in
                    Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')),
        'platform': platform.platform(), 'logical_cpu': args.cpu,
        'compiler': checked([cc, '--version']).splitlines()[0],
        'assembler': checked(['as', '--version']).splitlines()[0],
        'rounds': args.rounds, 'flags': flags,
        'c_recursive_extra_flags': ['-fno-optimize-sibling-calls'],
        'clock': 'CLOCK_MONOTONIC_RAW',
        'timed_region': 'one fib(n) call; excludes startup, compilation and output',
        'run_policy': 'serial, fixed CPU; rotate variant order each round; no warmup or discarded samples',
        'sources_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in sorted(SOURCES.iterdir()) if p.suffix in ('.c', '.S')},
        'cases': {},
    }
    with tempfile.TemporaryDirectory(prefix='kairote-native-fib-') as temporary:
        directory = Path(temporary)
        checked([cc, *flags, '-fno-optimize-sibling-calls', '-c', SOURCES / 'Fib.c', '-o', directory / 'strict.o'])
        checked([cc, *flags, '-DFIB_NAME=fib_c_o2', '-c', SOURCES / 'Fib.c', '-o', directory / 'o2.o'])
        binary = directory / 'fib'
        checked([cc, *flags, SOURCES / 'Driver.c', SOURCES / 'Fib.S',
                 directory / 'strict.o', directory / 'o2.o', '-o', binary])
        report['machine_code'] = {}
        for variant, symbol in VARIANTS.items():
            assembly = checked(['objdump', '-d', '-Mintel', '--disassemble=' + symbol, binary])
            calls = len(re.findall(r'\bcall\s+\w+\s+<' + symbol + r'>', assembly))
            if calls != (1 if variant == 'c_o2' else 2):
                raise RuntimeError(f'Unexpected recursive call count for {variant}: {calls}\n{assembly}')
            report['machine_code'][variant] = {'recursive_call_sites': calls, 'disassembly': assembly}
        def run(variant, n):
            return json.loads(checked(['taskset', '-c', args.cpu, binary, variant, n]))
        for variant in VARIANTS:
            for n, value in ((0, 0), (1, 1), (2, 1), (10, 55)):
                if run(variant, n)['result'] != value:
                    raise RuntimeError(f'{variant}: failed base-case check n={n}')
        for n, expected in ((35, 9227465), (40, 102334155)):
            samples = {v: [] for v in VARIANTS}
            order = list(VARIANTS)
            for round_index in range(args.rounds):
                rotated = order[round_index % len(order):] + order[:round_index % len(order)]
                for variant in rotated:
                    sample = run(variant, n)
                    if sample['result'] != expected or sample['duration_ns'] <= 0:
                        raise RuntimeError(f'Invalid sample: {variant} {n} {sample}')
                    samples[variant].append({'run': round_index + 1, **sample})
                    print(f'{variant} fib{n} {round_index+1}/{args.rounds}: '
                          f'{sample["duration_ns"]/1e6:.6f} ms', flush=True)
            for variant in VARIANTS:
                durations = [s['duration_ns'] / 1e6 for s in samples[variant]]
                report['cases'][f'{variant}_fib{n}'] = {
                    'samples': samples[variant], 'mean_ms': statistics.mean(durations),
                    'min_ms': min(durations), 'max_ms': max(durations),
                    'stdev_ms': statistics.stdev(durations) if len(durations) > 1 else 0,
                }
            args.output.write_text(json.dumps(report, indent=2) + '\n')
        for name, case in report['cases'].items():
            print(f'{name}: mean {case["mean_ms"]:.6f} ms', flush=True)
    print(f'Saved {args.output}', flush=True)


if __name__ == '__main__':
    main()
