"""Compile a Kairote server and exercise real loopback TCP using Python clients."""
import os
from pathlib import Path
import re
import select
import socket
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()
LINKER = Path(os.environ.get('ARKLINK', ROOT / 'build/ArkLink/ArkLink')).resolve()
SERVER = ROOT / 'examples/http-server/Server.krt'


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), 'build the native compiler and ArkLink first')
class HttpServerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='krt-http-')
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.env = dict(os.environ, PATH='')

    def compile(self, level, connection_limit, source=None):
        main = self.work / f'main-O{level}-{connection_limit}.krt'
        main.write_text(source if source is not None else
                        f'int32 main(){{return HttpServer.Run(0,{connection_limit});}}')
        output = main.with_suffix('')
        result = subprocess.run([str(COMPILER), str(SERVER), str(main), f'-O{level}',
                                 '--linker', str(LINKER), '-o', str(output)],
                                cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(output.is_file())
        self.assertEqual(output.read_bytes()[:4], b'\x7fELF')
        return output

    def start(self, executable):
        process = subprocess.Popen([str(executable)], cwd=self.work, env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        def cleanup():
            if process.poll() is None:
                process.terminate()
            try:
                process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=3)
        self.addCleanup(cleanup)

        deadline = time.monotonic() + 10
        output = b''
        while b'\n' not in output and time.monotonic() < deadline:
            ready, _, _ = select.select([process.stdout], [], [], 0.1)
            if ready:
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    break
                output += chunk
            if process.poll() is not None:
                break
        match = re.fullmatch(rb'PORT=(\d+)\n', output)
        if not match:
            if process.poll() is None:
                process.terminate()
            stdout, stderr = process.communicate(timeout=3)
            self.fail(f'server failed to bind port 0: status={process.returncode}, output={output + stdout!r}, error={stderr!r}')
        port = int(match.group(1))
        self.assertGreater(port, 0)
        self.assertLessEqual(port, 65535)
        self.assertIsNone(process.poll(), 'server exited before receiving clients')
        return process, port

    def receive(self, client, expected_status, expected_body):
        response = b''
        while True:
            chunk = client.recv(4096)
            if not chunk:
                break
            response += chunk
            self.assertLessEqual(len(response), 8192)
        self.assertIn(b'\r\n\r\n', response)
        header, body = response.split(b'\r\n\r\n', 1)
        lines = header.split(b'\r\n')
        self.assertEqual(int(lines[0].split(b' ')[1]), expected_status, response)
        fields = dict(line.split(b': ', 1) for line in lines[1:])
        self.assertEqual(fields[b'Connection'], b'close')
        if expected_status in (204, 304):
            self.assertNotIn(b'Content-Length', fields)
            self.assertEqual(body, b'')
        else:
            self.assertEqual(int(fields[b'Content-Length']), len(body), response)
        self.assertEqual(body, expected_body)
        return response

    def request(self, port, path, status, body):
        with socket.create_connection(('127.0.0.1', port), timeout=3) as client:
            client.sendall(f'GET {path} HTTP/1.1\r\nHost: loopback\r\n\r\n'.encode())
            return self.receive(client, status, body)

    def fragmented(self, port, path, status, body):
        with socket.create_connection(('127.0.0.1', port), timeout=3) as client:
            request = f'GET {path} HTTP/1.1\r\nHost: loopback\r\n\r\n'.encode()
            for part in (request[:2], request[2:7], request[7:-1]):
                client.sendall(part)
                client.settimeout(0.1)
                with self.assertRaises(socket.timeout, msg='server responded before complete request headers'):
                    client.recv(1)
            client.settimeout(3)
            client.sendall(request[-1:])
            return self.receive(client, status, body)

    def finish(self, process, port):
        stdout, stderr = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, stdout + stderr)
        self.assertEqual(stdout, b'')
        self.assertEqual(stderr, b'')
        with self.assertRaises(OSError, msg='listener survived normal server exit'):
            with socket.create_connection(('127.0.0.1', port), timeout=0.5):
                pass

    def test_real_get_not_found_fragmentation_and_connection_cleanup(self):
        for level in (0, 2):
            with self.subTest(optimization=level):
                process, port = self.start(self.compile(level, 8))
                self.request(port, '/', 200, b'Hello, Kairote!\n')
                self.request(port, '/missing', 404, b'Not Found\n')
                self.fragmented(port, '/', 200, b'Hello, Kairote!\n')
                with socket.create_connection(('127.0.0.1', port), timeout=3) as abandoned:
                    abandoned.sendall(b'GET / HT')
                self.request(port, '/', 200, b'Hello, Kairote!\n')
                self.fragmented(port, '/missing', 404, b'Not Found\n')
                self.request(port, '/', 200, b'Hello, Kairote!\n')
                self.request(port, '/missing', 404, b'Not Found\n')
                self.finish(process, port)

    def test_invalid_requests_close_the_connection_without_stopping_server(self):
        for level in (0, 2):
            with self.subTest(optimization=level):
                process, port = self.start(self.compile(level, 3))
                with socket.create_connection(('127.0.0.1', port), timeout=3) as client:
                    client.sendall(b'GET / HTTP/9.9\r\nHost: loopback\r\n\r\n')
                    self.receive(client, 400, b'Bad Request\n')
                with socket.create_connection(('127.0.0.1', port), timeout=3) as client:
                    client.sendall(b'POST / HTTP/1.1\r\nHost: loopback\r\nContent-Length: 0\r\n\r\n')
                    self.receive(client, 405, b'Method Not Allowed\n')
                self.request(port, '/', 200, b'Hello, Kairote!\n')
                self.finish(process, port)

    def test_optional_service_configuration_and_response_struct_snapshots(self):
        source = '''
struct Listener{int32 port;int32 connections;byte tag[2];}
class Settings{public Listener listener;public string name;}
static int32 loads=0;
Settings Load(){loads++;return null;}
Settings Defaults(){Settings value=new Settings();value.name="loopback";
    value.listener.connections=2;value.listener.tag[0]=7;value.listener.tag[1]=11;return value;}
int32 main(){Settings requested=Load();Settings selected=requested??Defaults();
    Listener missing=requested?.listener;Listener snapshot=selected?.listener;
    string name=requested?.name??selected.name;
    if(loads!=1 || name!="loopback" || missing.connections!=0 || missing.tag[1]!=0){return 90;}
    selected.listener.connections=99;selected.listener.tag[1]=19;
    if(snapshot.connections!=2 || snapshot.tag[1]!=11){return 91;}
    delete selected;return HttpServer.Run(snapshot.port,snapshot.connections);}
'''
        for level in (0, 2):
            with self.subTest(optimization=level):
                process, port = self.start(self.compile(level, 2, source))
                self.fragmented(port, '/', 200, b'Hello, Kairote!\n')
                self.request(port, '/missing', 404, b'Not Found\n')
                self.finish(process, port)

    def test_escaping_closure_handler_shared_state_utf8_and_exception_cleanup(self):
        source = '''
struct Settings{int32 success;string greeting;byte tag[2];}
struct Handlers{fn(string)->HttpResponse respond;fn()->int32 count;}
Handlers Make(Settings options){
    int32 requests=0;Handlers handlers=default(Handlers);
    handlers.respond=function(string path)=>{
        requests++;
        if(options.tag[0]!=7 || options.tag[1]!=11){throw 91;}
        if(path=="/fail"){throw 23;}
        HttpResponse response=default(HttpResponse);
        if(path=="/"){response.status=options.success;response.body=options.greeting;}
        else{response.status=404;response.body="Closure route missing\\n";}
        return response;
    };
    handlers.count=function()=>requests;return handlers;
}
int32 main(){
    Settings settings=default(Settings);settings.success=200;
    settings.greeting="你好，闭包！\\n";settings.tag[0]=7;settings.tag[1]=11;
    Handlers handlers=Make(settings);settings.success=503;settings.tag[0]=99;
    int32 status=HttpServer.Run(0,4,handlers.respond);
    int32 count=handlers.count();delete handlers.respond;delete handlers.count;
    if(status!=0){return status;}return count==4?0:92;
}
'''
        for level in (0, 2):
            with self.subTest(optimization=level):
                process, port = self.start(self.compile(level, 4, source))
                self.request(port, '/', 200, '你好，闭包！\n'.encode())
                self.request(port, '/missing', 404, b'Closure route missing\n')
                self.request(port, '/fail', 500, b'Internal Server Error\n')
                self.fragmented(port, '/', 200, '你好，闭包！\n'.encode())
                self.finish(process, port)

    def test_handler_empty_body_bodyless_statuses_and_invalid_terminal_status(self):
        source = '''
HttpResponse Reply(string path){
    HttpResponse response=default(HttpResponse);response.status=200;
    if(path=="/empty"){return response;}
    response.body="ignored body";
    if(path=="/no-content"){response.status=204;}
    else if(path=="/not-modified"){response.status=304;}
    else if(path=="/interim"){response.status=199;}
    else{response.status=600;}
    return response;
}
int32 main(){return HttpServer.Run(0,5,&Reply);}
'''
        for level in (0, 2):
            with self.subTest(optimization=level):
                process, port = self.start(self.compile(level, 5, source))
                self.request(port, '/empty', 200, b'')
                self.request(port, '/no-content', 204, b'')
                self.request(port, '/not-modified', 304, b'')
                self.request(port, '/interim', 500, b'Internal Server Error\n')
                self.request(port, '/invalid', 500, b'Internal Server Error\n')
                self.finish(process, port)


if __name__ == '__main__':
    unittest.main()
