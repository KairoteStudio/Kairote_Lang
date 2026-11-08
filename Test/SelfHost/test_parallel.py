"""Observe actual concurrent native workers and preserve successful outputs on failure."""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()


@unittest.skipUnless(COMPILER.is_file(), 'build the native self-hosted compiler first')
class NativeParallelTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='kairote parallel ')
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        (self.work / 'src').mkdir()
        self.project = self.work / 'project.krt'
        self.project.write_text('function Configure(Project p){p.Name("Parallel");p.Sources("src/*.krt");p.Target("vm");p.Output("program");}')
        self.environment = dict(os.environ, PATH='', KAIROTE_ROOT=str(ROOT))

    def command(self, *arguments, expected=0):
        result = subprocess.run([str(COMPILER), *map(str, arguments)], cwd=self.work,
                                env=self.environment, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def fixture(self, heavy=False):
        (self.work / 'src/main.krt').write_text('int32 main(){return helper();}')
        (self.work / 'src/helper.krt').write_text('int32 helper(){return 17;}')
        if heavy:
            for file_index in range(24):
                functions = []
                for function_index in range(6):
                    functions.append(f'int32 unit{file_index}_{function_index}(){{int32 n=0;'
                                     + 'n=n+1;' * 100 + 'return n;}')
                (self.work / f'src/unit{file_index:02}.krt').write_text('\n'.join(functions))

    def monitor(self, jobs, kill_worker=False, deny_worker_output=False):
        process = subprocess.Popen([str(COMPILER), 'build', str(self.project), '--rebuild', '-O0', '-j', str(jobs)],
                                   cwd=self.work, env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        peak = 0; killed = False; deadline = time.monotonic() + 60; changed_directory = None
        try:
            while process.poll() is None:
                if time.monotonic() > deadline:
                    raise AssertionError('parallel build exceeded 60 seconds')
                try:
                    children = [int(pid) for pid in Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()]
                except FileNotFoundError:
                    children = []
                peak = max(peak, len(children))
                if deny_worker_output and children and changed_directory is None:
                    directories = [path for path in self.work.rglob('.build-*') if path.is_dir()]
                    if directories:
                        changed_directory = directories[0]; changed_directory.chmod(0o500)
                if kill_worker and children and not killed:
                    for child in reversed(children):
                        try:
                            # /proc lists unreaped zombies as children too. Stop
                            # a live worker first so an already completed worker
                            # cannot make a successful build look like a failure.
                            if Path(f'/proc/{child}/stat').read_text().split()[2] == 'Z':
                                continue
                            os.kill(child, signal.SIGSTOP)
                            stopped = False
                            stop_deadline = time.monotonic() + 0.02
                            while time.monotonic() < stop_deadline:
                                state = Path(f'/proc/{child}/stat').read_text().split()[2]
                                if state == 'T':
                                    stopped = True; break
                                if state == 'Z':
                                    break
                                time.sleep(0.0002)
                            if stopped:
                                os.kill(child, signal.SIGKILL); killed = True; break
                            os.kill(child, signal.SIGCONT)
                        except (ProcessLookupError, FileNotFoundError):
                            pass
                time.sleep(0.001)
            stdout, stderr = process.communicate(timeout=5)
        finally:
            if changed_directory is not None and changed_directory.exists():
                changed_directory.chmod(0o700)
            if process.poll() is None:
                process.kill(); process.communicate()
        return process.returncode, stdout, stderr, peak, killed

    def test_jobs_options_are_validated(self):
        for arguments in (('-j0',), ('-j257',), ('-j', '-1'), ('--jobs', 'x'), ('-j',)):
            result = self.command(*arguments, expected=1)
            self.assertIn(b'E_OPTION', result.stderr)
        self.assertIn(b'--jobs', self.command('--help').stdout)

    def test_real_worker_overlap_serial_equivalence_and_deferred_context(self):
        self.fixture(heavy=True)
        status, stdout, stderr, peak, _ = self.monitor(4)
        self.assertEqual(status, 0, stdout + stderr)
        self.assertGreaterEqual(peak, 2, 'native compiler workers never overlapped')
        self.assertLessEqual(peak, 4)
        self.assertIn(b'up to 4 jobs', stdout)
        self.assertIn(b'object deferred:', stdout)
        output = self.work / 'bin/linux/program'; parallel = output.read_bytes()
        self.command('run-vm', output, expected=17)
        self.command('build', self.project, '--rebuild', '-O0', '-j1')
        self.assertEqual(output.read_bytes(), parallel)

    def test_failed_worker_reports_signal_and_preserves_previous_build(self):
        self.fixture()
        self.command('build', self.project, '-j2')
        output = self.work / 'bin/linux/program'; previous = output.read_bytes()
        manifests = list((self.work / '.krtcache').rglob('manifest.krtcache'))
        if not manifests:
            manifests = list(self.work.rglob('manifest.krtcache'))
        self.assertEqual(len(manifests), 1)
        manifest = manifests[0]; previous_manifest = manifest.read_bytes()
        self.fixture(heavy=True)
        status, stdout, stderr, _, killed = self.monitor(4, kill_worker=True)
        self.assertTrue(killed, 'could not observe a native worker to terminate')
        self.assertEqual(status, 1, stdout + stderr)
        self.assertIn(b'E_WORKER', stderr)
        self.assertIn(b'signal 9', stderr)
        self.assertEqual(output.read_bytes(), previous)
        self.assertEqual(manifest.read_bytes(), previous_manifest)
        self.command('run-vm', output, expected=17)

    def test_worker_output_error_is_fatal_and_not_deferred(self):
        self.fixture()
        self.command('build', self.project, '-j2')
        output = self.work / 'bin/linux/program'; previous = output.read_bytes()
        self.fixture(heavy=True)
        status, stdout, stderr, peak, _ = self.monitor(4, deny_worker_output=True)
        self.assertGreaterEqual(peak, 1)
        self.assertEqual(status, 1, stdout + stderr)
        self.assertIn(b'E_WORKER', stderr)
        self.assertIn(b'E_OUTPUT', stderr)
        self.assertEqual(output.read_bytes(), previous)
        self.command('run-vm', output, expected=17)


if __name__ == '__main__':
    unittest.main()
