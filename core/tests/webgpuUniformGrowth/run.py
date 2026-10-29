#!/usr/bin/env python3
"""Build the Release WebGPU harness and capture Chrome's verdict (stdlib only).

python3 core/tests/webgpuUniformGrowth/run.py --build
Use --build-dir to run a baseline build, or --url/--screenshot to capture an
existing Web example served on localhost. --timeout is a hang guard, not a test.
"""
import argparse
import base64
from functools import partial
import http.server
import json
import os
from pathlib import Path
import shlex
import socket
import struct
import subprocess
import tempfile
import threading
import time

TEST = Path(__file__).resolve().parent
# Bound (security): the DevTools WebSocket upgrade response (external bytes). Overflow fails the run.
MAX_UPGRADE_BYTES = 16 * 1024
# Bound (security): one CDP message, checked before reading (screenshots are a few MiB). Overflow fails the run.
MAX_FRAME_BYTES = 64 * 1024 * 1024
# Bound (security): all DevTools WebSocket bytes in one run, including page-controlled console output. Overflow fails the run.
MAX_TOTAL_BYTES = 256 * 1024 * 1024
# Bound (security): the /json target list (overflow fails the run) and the DevToolsActivePort read
# (only this many bytes are parsed; the port is on its first line).
MAX_SMALL_RESPONSE_BYTES = 1024 * 1024
# Bound (platform): only this tail of Chrome's output is kept and printed; earlier output is dropped.
LOG_TAIL_BYTES = 12000
# Bound (security): Chrome's output is drained from a pipe while it runs, never written to disk.
# Past this many bytes Chrome is stopped and the run fails.
MAX_LOG_BYTES = 256 * 1024 * 1024
# Bound (platform): waiting for the output pipe to close after Chrome stops. The tail printed so far is kept.
LOG_DRAIN_JOIN_SECONDS = 10
# Bound (security): asset requests handled at once (any local peer can connect). A connection past
# the cap gets an immediate 503 and is closed; the count of rejections is printed when the run ends.
MAX_ACTIVE_REQUESTS = 16
# Bound (security): each accepted connection's total lifetime, however slowly the peer sends or reads.
# At the deadline its socket is shut down, which ends that handler.
REQUEST_DEADLINE_SECONDS = 30


def remaining(deadline):
    # Bound (platform): a stalled browser must not hang the run. Overflow fails the run.
    left = deadline - time.monotonic()
    if left <= 0:
        raise RuntimeError('Timed out waiting for Chrome')
    return left


class WebSocket:
    def __init__(self, url, deadline):
        from urllib.parse import urlsplit
        parts = urlsplit(url)
        self.deadline = deadline
        self.received = 0
        self.pending = bytearray()
        self.socket = socket.create_connection((parts.hostname, parts.port), timeout=remaining(deadline))
        key = base64.b64encode(os.urandom(16)).decode()
        request = (f'GET {parts.path} HTTP/1.1\r\nHost: {parts.netloc}\r\n'
                   f'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                   f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n')
        self.socket.settimeout(remaining(deadline))
        self.socket.sendall(request.encode())
        response = bytearray()
        while b'\r\n\r\n' not in response:
            if len(response) > MAX_UPGRADE_BYTES:
                raise RuntimeError('DevTools upgrade response too large')
            response.extend(self.chunk(4096))
        end = response.index(b'\r\n\r\n') + 4
        self.pending = response[end:]
        if not response.startswith(b'HTTP/1.1 101'):
            raise RuntimeError(bytes(response[:min(end, 512)]).decode('latin-1'))
        self.sequence = 0

    def chunk(self, size):
        # Every read honours the absolute deadline; EOF fails immediately.
        self.socket.settimeout(remaining(self.deadline))
        data = self.socket.recv(size)
        if not data:
            raise RuntimeError('Chrome closed the DevTools connection')
        self.received += len(data)
        if self.received > MAX_TOTAL_BYTES:
            raise RuntimeError('DevTools traffic exceeded the byte budget')
        return data

    def exact(self, length):
        result = bytearray(self.pending[:length])
        del self.pending[:length]
        while len(result) < length:
            result.extend(self.chunk(min(length - len(result), 1 << 20)))
        return bytes(result)

    def receive(self):
        header = self.exact(2)
        length = header[1] & 127
        if length == 126:
            length = struct.unpack('!H', self.exact(2))[0]
        elif length == 127:
            length = struct.unpack('!Q', self.exact(8))[0]
        if length > MAX_FRAME_BYTES:
            raise RuntimeError(f'DevTools message of {length} bytes exceeds the frame budget')
        if header[0] & 15 == 8:
            raise RuntimeError('Chrome closed the DevTools connection')
        data = self.exact(length)
        if header[0] & 15 != 1:
            raise RuntimeError(f'unexpected WebSocket opcode {header[0] & 15}')
        return json.loads(data)

    def call(self, method, params=None):
        remaining(self.deadline)
        if os.environ.get("TC_WEBGPU_CDP_TRACE"):
            print("CDP", method, flush=True)
        self.sequence += 1
        payload = json.dumps({'id': self.sequence, 'method': method, 'params': params or {}}).encode()
        mask = os.urandom(4)
        length = len(payload)
        header = bytes([0x81, 0x80 | length]) if length < 126 else bytes([0x81, 0x80 | 126]) + struct.pack('!H', length)
        self.socket.sendall(header + mask + bytes(value ^ mask[index % 4] for index, value in enumerate(payload)))
        while True:
            message = self.receive()
            if message.get('method') == 'Runtime.consoleAPICalled':
                print('console:', ' '.join(str(arg.get('value', arg.get('description', ''))) for arg in message['params']['args']), flush=True)
            elif message.get('method') == 'Runtime.exceptionThrown':
                print('exception:', message['params'], flush=True)
            if message.get('id') == self.sequence:
                if 'error' in message:
                    raise RuntimeError(message['error'])
                return message.get('result', {})


def devtools_targets(port, deadline):
    """GET /json from DevTools. Every read honours the absolute deadline."""
    with socket.create_connection(('127.0.0.1', port), timeout=remaining(deadline)) as connection:
        connection.settimeout(remaining(deadline))
        connection.sendall(f'GET /json HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nConnection: close\r\n\r\n'.encode())
        response = bytearray()
        expected = None
        while expected is None or len(response) < expected:
            # The timeout is recomputed before each read, so a peer that keeps
            # sending slowly still cannot extend the run past the deadline.
            connection.settimeout(remaining(deadline))
            data = connection.recv(65536)
            if not data:
                break
            response.extend(data)
            if len(response) > MAX_SMALL_RESPONSE_BYTES:
                raise RuntimeError('DevTools target list too large')
            if expected is None and b'\r\n\r\n' in response:
                end = response.index(b'\r\n\r\n') + 4
                for line in bytes(response[:end]).split(b'\r\n')[1:]:
                    name, _, value = line.partition(b':')
                    if name.strip().lower() == b'content-length':
                        expected = end + int(value.strip())
    head, separator, body = bytes(response).partition(b'\r\n\r\n')
    status = head.split(b'\r\n', 1)[0].split()
    if not separator or len(status) < 2 or status[1] != b'200':
        raise RuntimeError('unexpected DevTools /json response: ' + head[:200].decode('latin-1'))
    return json.loads(body)


class LogDrain:
    """Reads Chrome's combined output from a pipe while Chrome runs (bounded tail)."""

    def __init__(self, stream, stop):
        self.tail = bytearray()
        self.total = 0
        self.overflow = False
        self.thread = threading.Thread(target=self.run, args=(stream, stop), daemon=True)
        self.thread.start()

    def run(self, stream, stop):
        while True:
            data = stream.read1(65536)
            if not data:
                return
            self.total += len(data)
            self.tail.extend(data)
            del self.tail[:-LOG_TAIL_BYTES]
            if self.total > MAX_LOG_BYTES and not self.overflow:
                self.overflow = True
                stop()


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, pattern, *args):
        if os.environ.get("TC_WEBGPU_CDP_TRACE"):
            print("HTTP", pattern % args, flush=True)


class AssetServer(http.server.ThreadingHTTPServer):
    """Serves the build directory with a cap on active handlers and a per-connection deadline."""

    # Handlers are daemon threads bounded by the deadline; closing does not wait for them.
    block_on_close = False

    def __init__(self, address, handler):
        super().__init__(address, handler)
        self.slots = threading.BoundedSemaphore(MAX_ACTIVE_REQUESTS)
        self.rejected = 0

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            self.rejected += 1
            try:
                request.settimeout(1)
                request.sendall(b'HTTP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
            except OSError:
                pass
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        timer = threading.Timer(REQUEST_DEADLINE_SECONDS, self.expire, (request,))
        timer.daemon = True
        timer.start()
        try:
            super().process_request_thread(request, client_address)
        finally:
            timer.cancel()
            self.slots.release()

    @staticmethod
    def expire(request):
        try:
            request.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


def build(directory):
    wrapper = shlex.split(os.environ.get('TC_BUILD_WRAPPER', str(Path.home() / 'TrussC-dev/trussc-ai-tools/tcbuild.sh')))
    emcmake = Path(os.environ.get('EMSDK', Path.home() / 'emsdk')) / 'upstream/emscripten/emcmake'
    subprocess.run(wrapper + [str(emcmake), 'cmake', '-S', str(TEST), '-B', str(directory), '-DCMAKE_BUILD_TYPE=Release'], check=True)
    subprocess.run(wrapper + ['cmake', '--build', str(directory), '--parallel', '4'], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--build-dir', type=Path, default=TEST / 'build-web')
    parser.add_argument('--chrome', default='/usr/bin/google-chrome')
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--url', help='Capture an already served example instead of the harness')
    parser.add_argument('--screenshot', type=Path)
    parser.add_argument('--frames', type=int, help='For examples: freeze clocks and capture this requestAnimationFrame count')
    parser.add_argument('--ready', default='document.querySelector("#verdict")?.textContent', help='JS expression returning a completed verdict; for examples use a frame-count condition')
    args = parser.parse_args()
    if args.build:
        build(args.build_dir.resolve())
    handler = partial(Handler, directory=str(args.build_dir.resolve()))
    server = AssetServer(('127.0.0.1', 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = args.url or f'http://127.0.0.1:{server.server_port}/index.html'
    print('URL:', url, flush=True)
    try:
        with tempfile.TemporaryDirectory(prefix='webgpu-chrome-') as profile:
            command = [args.chrome, '--headless=new', '--no-sandbox', '--disable-dev-shm-usage',
                       '--enable-unsafe-webgpu', '--use-angle=swiftshader',
                       '--use-webgpu-adapter=swiftshader', '--remote-debugging-port=0',
                       '--disable-background-networking', '--disable-component-update', '--disable-sync',
                       '--no-proxy-server', '--disable-features=NetworkServiceSandbox', '--disable-extensions', '--no-first-run', '--no-default-browser-check', '--disable-breakpad',
                       '--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE localhost, EXCLUDE 127.0.0.1',
                       '--window-size=1280,1024', f'--user-data-dir={profile}', 'about:blank']
            chrome_env = dict(os.environ, XDG_CONFIG_HOME=str(Path(profile) / "config"), XDG_CACHE_HOME=str(Path(profile) / "cache"))
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=chrome_env)
            drain = LogDrain(process.stdout, process.terminate)
            connection = None
            try:
                deadline = time.monotonic() + args.timeout
                portfile = Path(profile) / 'DevToolsActivePort'
                while not portfile.exists():
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Chrome failed to start DevTools')
                    time.sleep(0.1)
                with open(portfile, 'rb') as active:
                    port = int(active.read(MAX_SMALL_RESPONSE_BYTES).decode('ascii').splitlines()[0])
                page = next(item for item in devtools_targets(port, deadline) if item['type'] == 'page')
                connection = WebSocket(page['webSocketDebuggerUrl'], deadline)
                connection.call('Runtime.enable')
                connection.call('Page.enable')
                if args.frames:
                    script = """
                    (() => {
                        const raf = window.requestAnimationFrame.bind(window);
                        let frame = 0;
                        Object.defineProperty(performance, 'now', {value: () => frame * 1000 / 60});
                        Date.now = () => 1700000000000 + frame * 1000 / 60;
                        let seed = 863;
                        Math.random = () => ((seed = (1664525 * seed + 1013904223) >>> 0) / 4294967296);
                        window.requestAnimationFrame = callback => {
                            if (frame >= FRAME_LIMIT) return 0;
                            return raf(() => {
                                ++frame; callback(frame * 1000 / 60);
                                if (frame === FRAME_LIMIT) window.tcCaptureVerdict = 'PASS fixed frame ' + frame;
                            });
                        };
                    })();
                    """.replace('FRAME_LIMIT', str(args.frames))
                    connection.call('Page.addScriptToEvaluateOnNewDocument', {'source': script})
                connection.call('Page.navigate', {'url': url})
                while time.monotonic() < deadline:
                    if drain.overflow:
                        raise RuntimeError('Chrome log exceeded the byte budget')
                    result = connection.call('Runtime.evaluate', {'expression': 'window.tcCaptureVerdict' if args.frames else args.ready, 'returnByValue': True})
                    verdict = result.get('result', {}).get('value')
                    if verdict and verdict != 'RUNNING':
                        print(verdict, flush=True)
                        if args.screenshot:
                            data = connection.call('Page.captureScreenshot', {'format': 'png'})['data']
                            args.screenshot.write_bytes(base64.b64decode(data))
                        return 0 if 'PASS' in str(verdict) else 1
                    time.sleep(0.1)
                raise RuntimeError('Timed out waiting for browser verdict')
            except (OSError, RuntimeError, StopIteration, ValueError) as error:
                # The drain stops Chrome on overflow; report that rather than the
                # broken connection or failed start it causes.
                if drain.overflow:
                    raise RuntimeError('Chrome log exceeded the byte budget') from error
                raise
            finally:
                if connection:
                    connection.socket.close()
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
                drain.thread.join(LOG_DRAIN_JOIN_SECONDS)
                print(bytes(drain.tail).decode('utf-8', 'replace'), flush=True)
    finally:
        server.shutdown(); server.server_close()
        if server.rejected:
            print(f'asset server: rejected {server.rejected} connection(s) over the {MAX_ACTIVE_REQUESTS}-request cap', flush=True)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'WEBGPU_UNIFORM_GROWTH: FAIL {error}', flush=True)
        raise SystemExit(1)
