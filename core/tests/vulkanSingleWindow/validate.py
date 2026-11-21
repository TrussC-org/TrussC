#!/usr/bin/env python3
"""Run Linux Vulkan tests on the caller's X session and ICD; never select a GPU.

Build probe/ with CMake, then supply --probe and the existing test/app binary.
Logs retain validation output through vkDestroyDevice and process exit.
"""
import argparse
import ctypes as c
import os
from pathlib import Path
import re
import subprocess
import time


def validation_layer_issues(log):
    issues = []
    for line in log.splitlines():
        if "VK_LAYER_KHRONOS_validation" in line and re.search(
                r"not found|not present|failed to load|cannot find|could not find", line, re.I):
            issues.append("Khronos validation layer unavailable (see log)")
            break
    for scope in ("instance", "device"):
        if not re.search(rf'Insert(?:ed)? {scope} layer "VK_LAYER_KHRONOS_validation"', log):
            issues.append(f"missing Khronos {scope} layer insertion evidence")
    return issues


def close_window(window):
    x = c.CDLL("libX11.so.6")
    x.XOpenDisplay.restype = c.c_void_p
    x.XInternAtom.argtypes = [c.c_void_p, c.c_char_p, c.c_int]
    x.XInternAtom.restype = c.c_ulong
    x.XSendEvent.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_long, c.c_void_p]
    x.XSync.argtypes = [c.c_void_p, c.c_int]
    x.XCloseDisplay.argtypes = [c.c_void_p]

    class Data(c.Union):
        _fields_ = [("b", c.c_char * 20), ("s", c.c_short * 10), ("l", c.c_long * 5)]

    class Client(c.Structure):
        _fields_ = [("type", c.c_int), ("serial", c.c_ulong), ("send_event", c.c_int),
                    ("display", c.c_void_p), ("window", c.c_ulong), ("message_type", c.c_ulong),
                    ("format", c.c_int), ("data", Data)]

    class Event(c.Union):
        _fields_ = [("client", Client), ("pad", c.c_long * 24)]

    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("cannot open caller's DISPLAY")
    try:
        event = Event()
        event.client.type = 33  # ClientMessage
        event.client.display = display
        event.client.window = window
        event.client.message_type = x.XInternAtom(display, b"WM_PROTOCOLS", 0)
        event.client.format = 32
        event.client.data.l[0] = x.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
        if not x.XSendEvent(display, window, 0, 0, c.byref(event)):
            raise RuntimeError("WM_DELETE_WINDOW failed")
        x.XSync(display, 0)
    finally:
        x.XCloseDisplay(display)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--combined", action="store_true")
    parser.add_argument("--empty", action="store_true")
    parser.add_argument("--inject", action="store_true")
    parser.add_argument("--wm-close", action="store_true")
    parser.add_argument("args", nargs="*")
    args, extra = parser.parse_known_args()
    options = args.args + extra
    env = dict(os.environ, VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation",
               VK_LOADER_DEBUG="layer",
               LD_PRELOAD=str(args.probe.resolve()))
    if args.inject:
        env["TC_VK_INJECT_WSI"] = "1"
    command = [str(args.binary.resolve())]
    if args.combined:
        command.append("vulkanSingleWindow")
    command += options
    if args.wm_close and not args.empty:
        command.append("--wm-close")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, env=env)
        # Bound (test duration): allow resize/validation work, then reject a stalled app after 45 seconds.
        deadline = time.monotonic() + 45
        sent = False
        try:
            while process.poll() is None:
                log = args.log.read_text(encoding="utf-8", errors="replace")
                ready = "VK_PROBE successful presents=16" if args.empty else "24 resizes completed; ready for close"
                if (args.wm_close or args.empty) and not sent and ready in log:
                    # The observed surface identifies this process's window;
                    # the example is free to use any title.
                    windows = re.findall(r"VK_PROBE window=(\d+)", log)
                    if not windows:
                        raise RuntimeError("missing native test window")
                    close_window(int(windows[-1]))
                    sent = True
                if time.monotonic() >= deadline:
                    raise RuntimeError("45 s process timeout")
                time.sleep(0.02)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
    log = args.log.read_text(encoding="utf-8", errors="replace")
    errors = len(re.findall(r"Validation Error", log))
    warnings = len(re.findall(r"Validation (?:Warning|Performance Warning)", log))
    issues = validation_layer_issues(log)
    if process.returncode:
        issues.append(f"exit {process.returncode}")
    if errors or warnings or "VUID-" in log or "UNASSIGNED-" in log:
        issues.append("validation diagnostics (see log)")
    if "VK_PROBE close swapchains=0 images=0 views=0 memory=0 semaphores=0" not in log:
        issues.append("missing zero-object teardown")
    if (args.wm_close or args.empty) and not sent:
        issues.append("WM_DELETE_WINDOW not sent")
    if args.empty and "VK_PROBE successful presents=16\n" not in log:
        issues.append("16 successful presentations not observed")
    if args.inject:
        for result in ("acquire OUT_OF_DATE", "acquire SUBOPTIMAL", "present SUBOPTIMAL"):
            if "VK_PROBE injected " + result not in log:
                issues.append("missing " + result)
    if "--resize" in options:
        if "24 resizes completed" not in log or len(re.findall(r"VK_PROBE swapchain=", log)) < 25:
            issues.append("resize/recreation check incomplete")
    if "--stencil" in options:
        for ref in (1, 73, 255):
            if f"VK_PROBE stencil faces=3 ref={ref}\n" not in log:
                issues.append(f"native stencil reference {ref} missing")
    if ": FAIL" in log:
        issues.append("app assertion failed")
    print(f"{args.log}: exit={process.returncode} validation errors={errors} warnings={warnings} "
          f"swapchains={len(re.findall(r'VK_PROBE swapchain=', log))} WM_DELETE={sent}")
    if issues:
        raise SystemExit("; ".join(issues))


if __name__ == "__main__":
    main()
