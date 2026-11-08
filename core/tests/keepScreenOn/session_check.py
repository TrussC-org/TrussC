#!/usr/bin/env python3
"""Exercise optional libdbus only on a new, disposable session bus.

Usage: /usr/bin/python3 session_check.py /path/to/keepScreenOn
The fake service requires the system Python's installed dbus and gi modules.
"""
import json
import os
from pathlib import Path
import select
import secrets
import subprocess
import sys
import tempfile


ISOLATION_ERROR = 'Refusing D-Bus access: use the runner-created private session bus.'


def require_private_bus(token):
    expected_token = os.environ.get('TC_KEEP_SCREEN_TEST_TOKEN')
    address = os.environ.get('DBUS_SESSION_BUS_ADDRESS')
    expected_address = os.environ.get('TC_KEEP_SCREEN_TEST_BUS')
    if (not token or not expected_token or
            not secrets.compare_digest(token, expected_token) or
            not address or address != expected_address):
        sys.exit(ISOLATION_ERROR)


def refusal_tests():
    # A stub prevents any D-Bus import/connection even if a guard regresses.
    with tempfile.TemporaryDirectory(prefix='keep-screen-refusal-') as directory:
        root = Path(directory)
        imported = root / 'dbus-imported'
        (root / 'dbus.py').write_text(
            'from pathlib import Path\n'
            f'Path({str(imported)!r}).touch()\n'
            'raise RuntimeError("D-Bus must not be imported by refusal tests")\n',
            encoding='utf-8')
        env = dict(os.environ, PYTHONPATH=directory,
                   DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent-trussc-refusal-bus')
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_STARTER_ADDRESS',
                    'DBUS_STARTER_BUS_TYPE', 'TC_KEEP_SCREEN_TEST_TOKEN',
                    'TC_KEEP_SCREEN_TEST_BUS'):
            env.pop(key, None)
        token = secrets.token_hex(32)
        credentials = dict(TC_KEEP_SCREEN_TEST_TOKEN=token,
                           TC_KEEP_SCREEN_TEST_BUS=env['DBUS_SESSION_BUS_ADDRESS'])
        for mode, args in (('--service', [str(root / 'calls'), token]),
                           ('--private', [token, str(root / 'must-not-run')])):
            cases = [([args[0]], env),  # Direct invocation without a token.
                     (args, env),  # No runner credentials.
                     (args, dict(env, **credentials, DBUS_SESSION_BUS_ADDRESS='')),
                     (args, dict(env, **credentials,
                                 DBUS_SESSION_BUS_ADDRESS='unix:path=/wrong-private-bus')),
                     (args, dict(env, TC_KEEP_SCREEN_TEST_TOKEN='wrong-token',
                                 TC_KEEP_SCREEN_TEST_BUS=env['DBUS_SESSION_BUS_ADDRESS']))]
            for arguments, child_env in cases:
                result = subprocess.run([sys.executable, __file__, mode, *arguments],
                                        env=child_env, capture_output=True, text=True,
                                        encoding='utf-8', timeout=5)
                if (result.returncode == 0 or result.stderr.strip() != ISOLATION_ERROR or
                        imported.exists()):
                    raise RuntimeError(f'{mode} did not refuse before D-Bus access: {result}')
        print('PASS direct/invalid service and private modes refuse before D-Bus access', flush=True)


def service(record, token):
    require_private_bus(token)
    import dbus
    import dbus.service
    from dbus.mainloop.glib import DBusGMainLoop
    from gi.repository import GLib

    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    name = dbus.service.BusName('org.freedesktop.ScreenSaver', bus=bus,
                               do_not_queue=True)

    class ScreenSaver(dbus.service.Object):
        def record(self, values):
            with open(record, 'a', encoding='utf-8') as output:
                output.write(json.dumps(values) + '\n')

        @dbus.service.method('org.freedesktop.ScreenSaver', in_signature='ss', out_signature='u')
        def Inhibit(self, app, reason):
            self.record(['Inhibit', str(app), str(reason), 1041])
            return dbus.UInt32(1041)

        @dbus.service.method('org.freedesktop.ScreenSaver', in_signature='u', out_signature='')
        def UnInhibit(self, cookie):
            self.record(['UnInhibit', int(cookie)])

    saver = ScreenSaver(name, '/org/freedesktop/ScreenSaver')
    print('READY', flush=True)
    GLib.MainLoop().run()
    del saver


def private_tests(binary, token):
    require_private_bus(token)
    subprocess.run([binary], check=True, timeout=30)
    with tempfile.TemporaryDirectory(prefix='keep-screen-session-') as directory:
        record = Path(directory) / 'calls.jsonl'
        with subprocess.Popen([sys.executable, __file__, '--service', str(record), token],
                              stdout=subprocess.PIPE, text=True, encoding='utf-8') as fake:
            try:
                if not select.select([fake.stdout], [], [], 10)[0] or fake.stdout.readline().strip() != 'READY':
                    raise RuntimeError('fake screen-saver service did not start')
                for mode in ['service', 'exit', 'production', 'headless', 'no-library']:
                    subprocess.run([binary, '--session-probe', mode], check=True, timeout=15)
                calls = [json.loads(line) for line in record.read_text(encoding='utf-8').splitlines()]
                assert calls == [item for _ in range(5) for item in (
                    ['Inhibit', 'TrussC', 'Keep the exhibition display on', 1041],
                    ['UnInhibit', 1041])], calls
                print(json.dumps(calls), flush=True)
                print('PASS fake service records exact Inhibit/UnInhibit pairs and exit cookie', flush=True)
            finally:
                fake.terminate()
                fake.wait(timeout=5)
        # The private bus is still running, but has no ScreenSaver owner.
        subprocess.run([binary, '--session-probe', 'no-service'], check=True, timeout=15)
        subprocess.run([binary, '--session-probe', 'production-no-service'], check=True, timeout=15)
        missing_bus = dict(os.environ, DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent-trussc-test-bus',
                           TC_KEEP_SCREEN_TEST_BUS='unix:path=/nonexistent-trussc-test-bus')
        subprocess.run([binary, '--session-probe', 'no-bus'], env=missing_bus, check=True, timeout=15)
    return 0


if __name__ == '__main__':
    if sys.argv[1:2] == ['--service']:
        if len(sys.argv) != 4:
            sys.exit(ISOLATION_ERROR)
        service(sys.argv[2], sys.argv[3])
    elif sys.argv[1:2] == ['--private']:
        if len(sys.argv) != 4:
            sys.exit(ISOLATION_ERROR)
        sys.exit(private_tests(sys.argv[3], sys.argv[2]))
    elif sys.argv[1:] == ['--self-test']:
        refusal_tests()
    elif len(sys.argv) == 2 and not sys.argv[1].startswith('--'):
        refusal_tests()
        env = dict(os.environ)
        for key in ('DBUS_SESSION_BUS_ADDRESS', 'DBUS_STARTER_ADDRESS',
                    'DBUS_STARTER_BUS_TYPE', 'DISPLAY', 'WAYLAND_DISPLAY',
                    'TC_KEEP_SCREEN_TEST_BUS', 'TC_KEEP_SCREEN_TEST_TOKEN'):
            env.pop(key, None)
        token = secrets.token_hex(32)
        env['TC_KEEP_SCREEN_TEST_TOKEN'] = token
        # Explicitly replace the address inside the freshly created bus session.
        command = ['dbus-run-session', '--', 'sh', '-c',
                   'export DBUS_SESSION_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"; '
                   'export TC_KEEP_SCREEN_TEST_BUS="$DBUS_SESSION_BUS_ADDRESS"; exec "$@"',
                   'private-keep-screen', sys.executable, __file__, '--private', token,
                   str(Path(sys.argv[1]).resolve())]
        sys.exit(subprocess.run(command, env=env, timeout=120).returncode)
    else:
        sys.exit('Usage: session_check.py /path/to/keepScreenOn')
