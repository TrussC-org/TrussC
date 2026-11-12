#!/usr/bin/env python3
"""Private Xvfb only; no network clients/downloads. Evidence stays in this clone."""
import ctypes
import os
from pathlib import Path
import subprocess
import time

root = Path(__file__).resolve().parent
# resolve() is for locating the binary; logs are copied out to the clone by the caller.
env = dict(os.environ, DISPLAY=':91422', NETWORK='1', LIBGL_ALWAYS_SOFTWARE='1',
           __GLX_VENDOR_LIBRARY_NAME='mesa',
           __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json')
x11 = ctypes.CDLL('libX11.so.6')
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
with open(root / 'xvfb.log', 'w') as log:
    server = subprocess.Popen(['Xvfb', ':91422', '-screen', '0', '128x128x24', '-nolisten', 'tcp'], env=env, stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 20
        while True:
            if server.poll() is not None:
                raise RuntimeError('Xvfb exited; see xvfb.log')
            display = x11.XOpenDisplay(b':91422')
            if display:
                x11.XCloseDisplay(display)
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('Xvfb readiness timeout')
            time.sleep(.05)
        for api, args in [('gl', []), ('gles', ['--gles'])]:
            with open(f'MEASUREMENT_{api.upper()}.log', 'w') as output:
                subprocess.run([str(root / 'fbo_measure'), *args], env=env, stdout=output, stderr=subprocess.STDOUT, check=True)
    finally:
        server.terminate()
        server.wait(timeout=10)
