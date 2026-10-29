#!/usr/bin/env python3
"""Check XSS on a disposable X server, never on the user's desktop."""
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile


def main():
    xvfb = shutil.which("Xvfb")
    if not xvfb:
        print("SKIP XSS integration: Xvfb unavailable")
        return 0
    env = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1", GALLIUM_DRIVER="llvmpipe",
               MESA_LOADER_DRIVER_OVERRIDE="llvmpipe", __GLX_VENDOR_LIBRARY_NAME="mesa")
    env.pop("WAYLAND_DISPLAY", None)
    reader, writer = os.pipe()
    with tempfile.TemporaryFile() as log:
        server = subprocess.Popen(
            [xvfb, "-displayfd", str(writer), "-screen", "0", "640x480x24",
             "-nolisten", "tcp", "-extension", "GLX"],
            env=env, pass_fds=(writer,), stdout=log, stderr=log)
        os.close(writer)
        try:
            if not select.select([reader], [], [], 5)[0]:
                raise RuntimeError("Xvfb did not report readiness")
            display = os.read(reader, 32).decode("ascii").strip()
            if not display.isdigit():
                raise RuntimeError("Xvfb did not return a display number")
            env["DISPLAY"] = ":" + display
            binary = Path(__file__).resolve().parent / "bin" / "keepScreenOn"
            result = subprocess.run([str(binary), "--x11-probe"], env=env, timeout=20)
            return 0 if result.returncode == 77 else result.returncode
        finally:
            os.close(reader)
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            log.seek(0)
            print(log.read().decode("utf-8", errors="replace"), end="")


if __name__ == "__main__":
    raise SystemExit(main())
