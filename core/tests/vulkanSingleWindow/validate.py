#!/usr/bin/env python3
"""Validate Vulkan smoke logs on Linux and Windows; never select a GPU.

Linux live runs require --probe and retain native object/WSI/stencil evidence.
Windows uses host recreation/teardown logs and app resource/stencil draw counts;
--check-log --windows --exit-code N checks logs captured with Start-Process.
Native object counts, injected WSI results and native stencil refs are Linux-only.
"""
import argparse
import ctypes as c
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def validation_environment(inherited, settings):
    env = {key: value for key, value in inherited.items()
           if key.upper() not in ("VK_LAYER_DISABLES", "VK_LAYER_ENABLES", "VK_LAYER_SETTINGS_PATH")
           and not key.upper().startswith("VK_KHRONOS_VALIDATION_")}
    env.update(VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation",
               VK_LOADER_DEBUG="layer", VK_LAYER_SETTINGS_PATH=str(settings))
    return env


def validation_environment_log(env):
    return "Effective validation environment:\n" + "".join(
        f"{key}={value}\n" for key, value in sorted(env.items())
        if key.upper().startswith(("VK_LAYER_", "VK_KHRONOS_VALIDATION_"))
        or key.upper() in ("VK_INSTANCE_LAYERS", "VK_LOADER_DEBUG"))


def validation_layer_issues(log):
    issues = []
    if re.search(r"\bVK_LAYER_DISABLES\b|\bVK_VALIDATION_FEATURE_DISABLE_\w+|"
                 r"\bVK_KHRONOS_VALIDATION_\w+", log, re.I):
        issues.append("validation disable or uncontrolled Khronos setting (see log)")
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


def log_verdict(log, options, exit_code, *, windows=False, wm_close=False,
                empty=False, inject=False, sent=False):
    errors = len(re.findall(r"Validation Error", log))
    warnings = len(re.findall(r"Validation (?:Warning|Performance Warning)", log))
    issues = validation_layer_issues(log)
    if exit_code:
        issues.append(f"exit {exit_code}")
    if errors or warnings or "VUID-" in log or "UNASSIGNED-" in log:
        issues.append("validation diagnostics (see log)")
    if windows:
        if "Vulkan host: teardown complete" not in log:
            issues.append("missing host teardown marker")
        if "vulkanSingleWindow: app checks complete" not in log:
            issues.append("missing app completion marker")
        if not re.search(r"Vulkan host: device=.+", log):
            issues.append("missing selected GPU name")
    elif "VK_PROBE close swapchains=0 images=0 views=0 memory=0 semaphores=0" not in log:
        issues.append("missing zero-object teardown")
    if wm_close or empty:
        if windows:
            if "vulkanSingleWindow: WM_CLOSE posted: PASS" not in log:
                issues.append("WM_CLOSE not posted")
        elif not sent:
            issues.append("WM_DELETE_WINDOW not sent")
    if empty and "VK_PROBE successful presents=16\n" not in log:
        issues.append("16 successful presentations not observed")
    if inject:
        for result in ("acquire OUT_OF_DATE", "acquire SUBOPTIMAL", "present SUBOPTIMAL"):
            if "VK_PROBE injected " + result not in log:
                issues.append("missing " + result)
    swapchains = log.count("Vulkan host: swapchain created") if windows else len(re.findall(r"VK_PROBE swapchain=", log))
    if "--resize" in options:
        if "24 resizes completed" not in log or swapchains < 25:
            issues.append("resize/recreation check incomplete")
        if windows:
            for resource in ("buffers", "images", "views", "samplers", "shaders", "pipelines"):
                counts = re.search(rf"vulkanSingleWindow: live {resource} before=(\d+) after=(\d+)", log)
                if not counts or counts[1] != counts[2]:
                    issues.append(f"live {resource} count missing or grew")
    if "--stencil" in options:
        for ref in (1, 73, 255):
            marker = f"vulkanSingleWindow: stencil draw ref={ref}\n" if windows else f"VK_PROBE stencil faces=3 ref={ref}\n"
            if marker not in log:
                issues.append(f"stencil {'draw' if windows else 'native'} reference {ref} missing")
    if ": FAIL" in log:
        issues.append("app assertion failed")
    return issues, errors, warnings, swapchains


def report(log, options, args, exit_code, sent=False):
    issues, errors, warnings, swapchains = log_verdict(
        log, options, exit_code, windows=args.windows, wm_close=args.wm_close,
        empty=args.empty, inject=args.inject, sent=sent)
    print(f"{args.log}: exit={exit_code} validation errors={errors} warnings={warnings} "
          f"swapchains={swapchains}")
    if args.windows:
        print("Linux-only checks: native object counts, injected WSI results, native stencil refs; "
              "Windows checks app stencil draws without pixel readback.")
    if issues:
        raise SystemExit("; ".join(issues))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--windows", action="store_true", default=sys.platform == "win32")
    parser.add_argument("--check-log", action="store_true")
    parser.add_argument("--exit-code", type=int)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--combined", action="store_true")
    parser.add_argument("--empty", action="store_true")
    parser.add_argument("--inject", action="store_true")
    parser.add_argument("--wm-close", action="store_true")
    parser.add_argument("args", nargs="*")
    args, extra = parser.parse_known_args()
    options = args.args + extra
    if args.windows and (args.inject or args.empty):
        parser.error("--inject and --empty require the Linux native probe")
    if args.check_log:
        if args.exit_code is None:
            parser.error("--check-log requires the captured --exit-code")
        return report(args.log.read_text(encoding="utf-8-sig", errors="replace"), options,
                      args, args.exit_code)
    if not args.binary or (not args.windows and not args.probe):
        parser.error("live runs require --binary and, on Linux, --probe")
    # Keep layer settings independent of the caller's environment and working directory.
    with tempfile.TemporaryDirectory(prefix="tc-vk-validation-") as directory:
        settings = Path(directory) / "vk_layer_settings.txt"
        settings.write_text("", encoding="utf-8")
        return run_live(args, options, validation_environment(os.environ, settings))


def run_live(args, options, env):
    if not args.windows:
        env["LD_PRELOAD"] = str(args.probe.resolve())
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
        output.write(validation_environment_log(env))
        output.flush()
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, env=env)
        # Bound (test duration): allow resize/validation work, then reject a stalled app after 45 seconds.
        deadline = time.monotonic() + 45
        sent = False
        try:
            while process.poll() is None:
                log = args.log.read_text(encoding="utf-8", errors="replace")
                ready = "VK_PROBE successful presents=16" if args.empty else "24 resizes completed; ready for close"
                if not args.windows and (args.wm_close or args.empty) and not sent and ready in log:
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
    return report(log, options, args, process.returncode, sent)


if __name__ == "__main__":
    main()
