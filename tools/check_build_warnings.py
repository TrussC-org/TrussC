#!/usr/bin/env python3
"""Fail normal CI builds on compiler, linker and CMake warnings.

--capture keeps stdout/stderr visible and saves them, preserving command status.
Inherited output pipes get a bounded grace period after command exit.
Scan that log in a following step, even when the build command failed.
For mixed build/test drivers, --capture-builds collects only run_build calls
and C++ hot-reload builds via TC_BUILD_WARNING_LOG; runtime stdout stays visible.
--run-build checks a single command immediately (also usable from shell scripts).
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import queue
import time
from contextlib import nullcontext

ALLOWLIST = Path(__file__).with_name('build_warning_allowlist.json')
ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
# Only build output belongs here. Runtime output must never reach this scanner.
WARNING = re.compile(
    r'(?:^\s*|:\s*)warning\s*:'
    r'|\bwarning\s+[A-Z]+\d+\b'
    r'|\bCMake (?:Deprecation )?Warning\b'
    r'|\b(?:emcc|em\+\+):\s*warning\b'
    r'|\b(?:ld(?:64)?(?:\.lld)?|lld(?:-link)?|link(?:\.exe)?)\s*:\s*'
    r'(?:warning\b|fatal warning\(s\))'
    # Apple's libtool/ranlib omit the word "warning" for these diagnostics,
    # including universal archives' "for architecture: ..." prefix.
    r'|\b(?:libtool|ranlib)(?:\.exe)?:\s*(?:for architecture:\s*\S+\s+)?'
    r'(?:warning\b|file:.*\bhas no symbols\b|duplicate member(?: name)?\b)', re.I)

BUILD_LOG_ENV = 'TC_BUILD_WARNING_LOG'


# Bound (memory): 8 queued lines + 2 reader lines, tail and warning sample; each <= 64K Unicode characters (256 KiB).
OUTPUT_LIMIT = 64 * 1024
CAPTURE_EXIT_GRACE = 1.0


def run_build(command, *, echo=True, on_line=None, **kwargs):
    """Stream merged build output; return only its bounded tail in stdout.

    on_line observes each decoded line for callers checking specific build events.
    Oversized lines fail closed because scanning fragments cannot certify them.
    Use only for builds, never for test executables.
    """
    check = kwargs.pop('check', False)
    timeout = kwargs.pop('timeout', None)
    kwargs.pop('capture_output', None)
    kwargs.pop('stdout', None)
    kwargs.pop('stderr', None)
    as_text = kwargs.pop('text', False)
    as_text = kwargs.pop('universal_newlines', False) or as_text
    encoding = kwargs.pop('encoding', None)
    errors = kwargs.pop('errors', None)
    as_text = as_text or encoding is not None or errors is not None
    entries = load_allowlist(ALLOWLIST)
    log = (kwargs.get('env') or os.environ).get(BUILD_LOG_ENV)
    tail = sample = ''
    count = 0
    oversized = False
    expired = threading.Event()
    with (Path(log).open('a', encoding='utf-8') if log else nullcontext()) as stream:
        if stream:
            stream.write('Build output:\n')
            stream.flush()
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding=encoding or 'utf-8', errors=errors or 'replace',
                              **kwargs) as process:
            def expire():
                expired.set()
                process.kill()
            timer = threading.Timer(timeout, expire) if timeout is not None else None
            if timer:
                timer.start()
            pending = queue.Queue(maxsize=8)
            stopped = threading.Event()

            def enqueue(item):
                while not stopped.is_set():
                    try:
                        pending.put(item, timeout=.1)
                        return
                    except queue.Full:
                        pass

            def read_lines(pipe):
                try:
                    while not stopped.is_set():
                        line = pipe.readline(OUTPUT_LIMIT)
                        if not line:
                            break
                        enqueue(line)
                except Exception as error:
                    enqueue(error)
                finally:
                    enqueue(None)

            # Merge incrementally, but keep stream boundaries when one ends in
            # a partial line so stdout cannot conceal a stderr diagnostic.
            readers = [threading.Thread(target=read_lines, args=(pipe,))
                       for pipe in (process.stdout, process.stderr)]
            for reader in readers:
                reader.start()
            try:
                finished = 0
                while finished < len(readers):
                    line = pending.get()
                    if line is None:
                        finished += 1
                        continue
                    if isinstance(line, Exception):
                        raise line
                    if len(line) == OUTPUT_LIMIT and not line.endswith('\n'):
                        oversized = True
                    if len(line) < OUTPUT_LIMIT and not line.endswith('\n'):
                        line += '\n'
                    if stream:
                        stream.write(line)
                        stream.flush()
                    if echo:
                        print(line, end='', flush=True)
                    tail = (tail + line)[-OUTPUT_LIMIT:]
                    if on_line:
                        on_line(line)
                    if scan((line,), entries):
                        count += 1
                        sample = (sample + line)[:OUTPUT_LIMIT]
                status = process.wait()
            except BaseException:
                process.kill()
                process.wait()
                raise
            finally:
                stopped.set()
                for reader in readers:
                    reader.join()
                if timer:
                    timer.cancel()
                    timer.join()
    output = tail if as_text else tail.encode(encoding or 'utf-8', errors=errors or 'replace')
    if expired.is_set():
        raise subprocess.TimeoutExpired(command, timeout, output=output)
    if oversized:
        raise RuntimeError('Build warning gate: line exceeds 64K characters; cannot certify complete diagnostics')
    if count:
        raise RuntimeError(f'Build warning gate: {count} unexpected warning(s)\n' + sample)
    result = subprocess.CompletedProcess(command, status, output, '' if as_text else b'')
    if check:
        result.check_returncode()
    return result


def load_allowlist(path):
    entries = json.loads(path.read_text(encoding='utf-8'))
    for entry in entries:
        if not all(entry.get(key) for key in ('file', 'pattern', 'reason')):
            raise ValueError('allowlist entries require file, pattern and reason')
        re.compile(entry['pattern'])
    return entries


def scan(lines, entries):
    offenders = []
    for number, raw in enumerate(lines, 1):
        line = ANSI.sub('', raw.rstrip()).replace('\\', '/')
        if not WARNING.search(line):
            continue
        # Require the diagnostic's own file location, not a nearby include or
        # command line. In particular, never exempt all warnings from a TU.
        allowed = any(re.search(r'(?:^|[\s/])' + re.escape(e['file']) + r'(?=[:(])', line)
                      and re.search(e['pattern'], line) for e in entries)
        if not allowed:
            offenders.append((number, raw.rstrip()))
    return offenders


def capture(path, command):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('wb') as output:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        pending = queue.Queue(maxsize=8)
        stopped = threading.Event()

        def enqueue(item):
            while not stopped.is_set():
                try:
                    pending.put(item, timeout=.1)
                    return
                except queue.Full:
                    pass

        def read_output():
            # A descendant can retain the pipe after the command exits. This
            # daemon owns/ultimately closes it; closing a blocked reader from
            # the main thread can itself hang on Windows/Python's stream lock.
            try:
                with process.stdout as pipe:
                    while not stopped.is_set():
                        chunk = os.read(pipe.fileno(), OUTPUT_LIMIT)
                        if not chunk:
                            break
                        enqueue(chunk)
            except OSError as error:
                enqueue(error)
            finally:
                enqueue(None)

        reader = threading.Thread(target=read_output, daemon=True)
        reader.start()
        deadline = None

        def write_chunk(chunk):
            if isinstance(chunk, Exception):
                raise chunk
            output.write(chunk)
            output.flush()
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()

        try:
            while True:
                if process.poll() is not None and deadline is None:
                    deadline = time.monotonic() + CAPTURE_EXIT_GRACE
                if deadline is not None and time.monotonic() >= deadline:
                    stopped.set()
                    # Preserve bytes already read. The queue plus a racing
                    # producer put contains at most nine bounded chunks.
                    for _ in range(9):
                        try:
                            chunk = pending.get_nowait()
                        except queue.Empty:
                            break
                        if chunk is not None:
                            write_chunk(chunk)
                    notice = (b'\nBuild capture: output may be incomplete; inherited output pipe '
                              b'remained open after command exit.\n')
                    output.write(notice)
                    sys.stderr.buffer.write(notice)
                    sys.stderr.buffer.flush()
                    break
                try:
                    chunk = pending.get(timeout=.05)
                except queue.Empty:
                    continue
                if chunk is None:
                    break
                write_chunk(chunk)
            status = process.wait()
        except BaseException:
            process.kill()
            process.wait()
            raise
        finally:
            stopped.set()
            reader.join(timeout=.1)
            output.flush()
    return status if status >= 0 else 128 - status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allowlist', type=Path, default=ALLOWLIST)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--capture-builds', type=Path,
                        help='collect only run_build output from a mixed build/test command')
    parser.add_argument('--run-build', action='store_true')
    parser.add_argument('logs', nargs='*', type=Path)
    argv = sys.argv[1:]
    command = []
    if '--' in argv:
        split = argv.index('--')
        command, argv = argv[split + 1:], argv[:split]
    args = parser.parse_args(argv)
    if args.capture_builds:
        if not command or args.capture or args.run_build or args.logs:
            parser.error('--capture-builds requires only -- command ...')
        path = args.capture_builds.resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('', encoding='utf-8')
        return subprocess.call(command, env=dict(os.environ, **{BUILD_LOG_ENV: str(path)}))
    if args.run_build:
        if not command or args.capture or args.logs:
            parser.error('--run-build requires only -- command ...')
        try:
            return run_build(command).returncode
        except (OSError, ValueError, RuntimeError) as error:
            print(error, file=sys.stderr)
            return 1
    if args.capture:
        if not command:
            parser.error('--capture requires -- command ...')
        return capture(args.capture, command)
    if command or not args.logs:
        parser.error('supply build logs, or --capture LOG -- command ...')
    try:
        entries = load_allowlist(args.allowlist)
        count = 0
        for path in args.logs:
            with path.open(encoding='utf-8', errors='replace') as stream:
                offenders = scan(stream, entries)
            for number, line in offenders:
                print(f'{path}:{number}: {line}')
            count += len(offenders)
        print(f'Build warning gate: {count} unexpected warning(s)')
        return int(count != 0)
    except (OSError, ValueError) as error:
        print(f'Build warning gate: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
