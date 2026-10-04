"""Real loopback HTTP with private boxed routes and lazy ABI3 providers."""
import subprocess

from Test.SelfHost import test_http_server as http


FIXTURES = http.ROOT / 'Test/SelfHost/fixtures/runtime-http'


class RuntimeHttpTests(http.unittest.TestCase):
    setUp = http.HttpServerTests.setUp
    receive = http.HttpServerTests.receive
    request = http.HttpServerTests.request
    fragmented = http.HttpServerTests.fragmented
    finish = http.HttpServerTests.finish

    def start(self, argv):
        arguments = [str(value) for value in argv] if isinstance(argv, (list, tuple)) else [str(argv)]
        process = subprocess.Popen(arguments, cwd=self.work, env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        def cleanup():
            if process.poll() is None:
                process.terminate()
            try:
                process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill(); process.communicate(timeout=3)
        self.addCleanup(cleanup)
        deadline = http.time.monotonic() + 10
        output = b''
        while b'\n' not in output and http.time.monotonic() < deadline:
            ready, _, _ = http.select.select([process.stdout], [], [], 0.1)
            if ready:
                chunk = http.os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    break
                output += chunk
            if process.poll() is not None:
                break
        match = http.re.fullmatch(rb'PORT=(\d+)\n', output)
        if not match:
            if process.poll() is None:
                process.terminate()
            stdout, stderr = process.communicate(timeout=3)
            self.fail(f'server failed to bind port 0: status={process.returncode}, '
                      f'output={output + stdout!r}, error={stderr!r}')
        port = int(match.group(1))
        self.assertGreater(port, 0); self.assertLessEqual(port, 65535)
        self.assertIsNone(process.poll(), 'server exited before receiving clients')
        return process, port

    def command(self, *arguments):
        result = subprocess.run([str(http.COMPILER), '--linker', str(http.LINKER),
                                 *map(str, arguments)], cwd=self.work, env=self.env,
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def exercise(self, argv):
        process, port = self.start(argv)
        self.request(port, '/', 200, '你好，模块！\n'.encode())
        self.request(port, '/missing', 404, b'Runtime route missing\n')
        self.post(port, '/', 405, b'Method Not Allowed\n')
        self.request(port, '/fail', 500, b'Internal Server Error\n')
        self.fragmented(port, '/', 200, '你好，模块！\n'.encode())
        self.request(port, '/missing', 404, b'Runtime route missing\n')
        self.post(port, '/missing', 405, b'Method Not Allowed\n')
        self.request(port, '/', 200, '你好，模块！\n'.encode())
        self.finish(process, port)

    def post(self, port, path, status, body):
        with http.socket.create_connection(('127.0.0.1', port), timeout=3) as client:
            client.sendall(f'POST {path} HTTP/1.1\r\nHost: loopback\r\nContent-Length: 0\r\n\r\n'.encode())
            self.receive(client, status, body)

    def test_independently_compiled_private_routes_and_exception_resource_cleanup(self):
        for level in range(4):
            folder = self.work / f'modules-o{level}'; folder.mkdir()
            provider = folder / 'Provider.krt'
            provider.write_bytes((FIXTURES / 'Provider.krt').read_bytes())
            library = provider.with_suffix('.kro')
            self.command(provider, f'-O{level}', '-c', '-o', library)
            provider.unlink()
            transport = folder / 'Transport.kro'
            self.command(http.SERVER, f'-O{level}', '-c', '-o', transport)
            consumer = folder / 'Consumer.krt'
            consumer.write_bytes((FIXTURES / 'Consumer.krt').read_bytes())
            client = consumer.with_suffix('.kro')
            self.command(consumer, library, transport, f'-O{level}', '-c', '-o', client)
            for reverse in (False, True):
                with self.subTest(optimization=level, reverse=reverse):
                    binary = folder / f'server-{reverse}'
                    objects = (client, transport, library) if reverse else (library, transport, client)
                    self.command(*objects, '-o', binary)
                    self.exercise(binary)

    def test_vm_private_route_generator_struct_boxes_and_real_http(self):
        for level in range(4):
            with self.subTest(optimization=level):
                output = self.work / f'server-vm-o{level}'
                self.command(http.SERVER, FIXTURES / 'Provider.krt', FIXTURES / 'Consumer.krt',
                             f'-O{level}', 'target', 'vm', '-o', output)
                self.exercise([http.COMPILER, 'run-vm', output])
