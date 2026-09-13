"""Run native compiler regressions and retain compilation/execution counts and test results."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', type=Path, default=HERE)
    parser.add_argument('--output', type=Path, default=HERE / 'Results.json')
    parser.add_argument('--compiler-flag', action='append', default=[])
    args = parser.parse_args()
    compiler = Path(os.environ.get('KRTC', ROOT / 'Re.KrtC/build/KrtC')).resolve()
    suite = unittest.defaultTestLoader.discover(str(args.suite.resolve()), pattern='test_*.py')
    counts = {'compiler_exits': Counter(), 'program_exits': Counter()}
    original_run = subprocess.run

    def recorded_run(command, *positional, **kwargs):
        if Path(command[0]).resolve() == compiler:
            command = [command[0], *args.compiler_flag, *command[1:]]
        result = original_run(command, *positional, **kwargs)
        category = 'compiler_exits' if Path(command[0]).resolve() == compiler else 'program_exits'
        counts[category][str(result.returncode)] += 1
        return result

    subprocess.run = recorded_run
    started = time.monotonic()
    try:
        result = unittest.TextTestRunner(verbosity=1).run(suite)
    finally:
        subprocess.run = original_run
    report = {
        'suite_directory': str(args.suite.resolve()),
        'date_utc': datetime.now(timezone.utc).isoformat(),
        'compiler': str(compiler), 'compiler_sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
        'compiler_flags': args.compiler_flag,
        'asan_options': os.environ.get('ASAN_OPTIONS'), 'ubsan_options': os.environ.get('UBSAN_OPTIONS'),
        'test_methods': result.testsRun, 'passed': result.wasSuccessful(),
        'failures': [str(test) for test, _ in result.failures],
        'errors': [str(test) for test, _ in result.errors],
        'skipped': [str(test) for test, _ in result.skipped],
        'duration_seconds': time.monotonic() - started, **counts,
        'notes': 'Negative compiler exits are crashes; expected runtime -4/-11 cases run in isolated children with core dumps disabled.',
    }
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Saved {args.output}; {dict(counts)}', flush=True)
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
