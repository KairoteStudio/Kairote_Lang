"""Run the existing real standard-library contract suite using SelfHost.

The tests import repository library sources through the normal driver. They are
the same tests used for Re.KrtC, with independently asserted runtime behavior.
The default run uses -O0; --levels can exercise all optimization levels.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import traceback
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RecordedResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.records = {}

    def startTest(self, test):
        self.records[test.id()] = {'status': 'running', 'start': time.perf_counter()}
        super().startTest(test)

    def stopTest(self, test):
        record = self.records[test.id()]
        record['seconds'] = time.perf_counter() - record.pop('start')
        super().stopTest(test)

    def addSuccess(self, test):
        self.records[test.id()]['status'] = 'pass'
        super().addSuccess(test)

    def addFailure(self, test, error):
        self.records[test.id()].update(status='failure', error=''.join(traceback.format_exception(*error)))
        super().addFailure(test, error)

    def addError(self, test, error):
        self.records[test.id()].update(status='error', error=''.join(traceback.format_exception(*error)))
        super().addError(test, error)

    def addSubTest(self, test, subtest, error):
        if error is not None:
            record = self.records[test.id()]
            record['status'] = 'failure'
            record.setdefault('subtest_failures', []).append({
                'subtest': str(subtest), 'error': ''.join(traceback.format_exception(*error))})
        super().addSubTest(test, subtest, error)

    def addSkip(self, test, reason):
        self.records[test.id()].update(status='skip', reason=reason)
        super().addSkip(test, reason)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=ROOT / 'build/selfhost/stage2/program')
    parser.add_argument('--linker', type=Path, help='explicit native linker used by the selected compiler')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/selfhost-standard-library')
    parser.add_argument('--pattern', default='test_*.py')
    parser.add_argument('--levels', default='0', help='comma-separated optimization levels (0,1,2,3)')
    parser.add_argument('--failfast', action='store_true')
    args = parser.parse_args()
    try:
        levels = [int(level) for level in args.levels.split(',')]
    except ValueError:
        parser.error('--levels requires comma-separated integers from 0 through 3')
    if not levels or len(set(levels)) != len(levels) or any(level not in range(4) for level in levels):
        parser.error('--levels requires distinct optimization levels from 0 through 3')
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        parser.error(f'compiler not found: {compiler}')
    args.work.mkdir(parents=True, exist_ok=True)
    os.environ['SELFHOST_COMPILER'] = str(compiler)
    os.environ['KRTC'] = str(ROOT / 'SelfHost/krtc')
    os.environ['KRT_STDLIB_TEST_LEVELS'] = ','.join(str(level) for level in levels)
    if args.linker is not None:
        if not args.linker.resolve().is_file():
            parser.error(f'linker not found: {args.linker}')
        os.environ['KRT_STDLIB_LINKER'] = str(args.linker.resolve())
    tracked = [compiler, ROOT / 'SelfHost/krtc', *sorted((ROOT / 'SelfHost').rglob('*.krt')),
               *sorted((ROOT / 'libs').rglob('*.krt'))]
    if os.environ.get('KRT_STDLIB_LINKER'):
        tracked.append(Path(os.environ['KRT_STDLIB_LINKER']).resolve())
    before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}
    suite = unittest.defaultTestLoader.discover(str(ROOT / 'Test/StandardLibrary'), pattern=args.pattern)
    with (args.work / 'tests.log').open('w') as stream:
        result = unittest.TextTestRunner(stream=stream, verbosity=2, failfast=args.failfast,
                                        resultclass=RecordedResult).run(suite)
    report = {
        'schema': 1, 'compiler': str(compiler),
        'compiler_sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
        'launcher_sha256': hashlib.sha256((ROOT / 'SelfHost/krtc').read_bytes()).hexdigest(),
        'compiler_sources': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in sorted((ROOT / 'SelfHost').rglob('*.krt'))},
        'optimization_levels': levels, 'tests_run': result.testsRun,
        'linker': os.environ.get('KRT_STDLIB_LINKER'),
        'library_sources': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                            for path in sorted((ROOT / 'libs').rglob('*.krt'))},
        'passed': result.wasSuccessful(), 'cases': result.records,
    }
    report['inputs_before'] = before
    report['inputs_after'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}
    report['complete'] = before == report['inputs_after']
    report['passed'] = report['passed'] and report['complete']
    (args.work / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, record in result.records.items():
        print(f'{record["status"]}: {name}')
    print(f'{result.testsRun} tests; report: {args.work / "report.json"}')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
