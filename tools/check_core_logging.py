#!/usr/bin/env python3
"""Flag diagnostics in TrussC core that bypass the Logger (#231, #311).

Why: core's own diagnostics go through tc::Logger (logError() / logWarning()
/ logNotice() / logVerbose()), so they reach the log file (setLogFile(),
TRUSSC_LOG_FILE), onLog listeners and the platform log (logcat, os_log,
OutputDebugStringA), and honor the console level. A raw printf / cerr / cout
/ NSLog reaches none of them, and on Windows Release (GUI subsystem) stdout
and stderr go nowhere.

What is flagged, in core/include/tc/** and core/platform/** (.h .hpp .c .cpp
.m .mm): a call or use of printf, fprintf, vprintf, vfprintf, puts, fputs,
perror, std::cout, std::cerr, std::clog (and the wide wcout / wcerr / wclog),
NSLog and NSLogv. snprintf / vsnprintf (formatting into a buffer) are fine.

The scan is textual: comments and string / character literals (including
Objective-C @"..." and raw strings) are blanked first, so the words in prose
or in a message do not count. It reads both sides of every #if, so one Linux
run covers every platform's code.

Exemptions:
  - A line that carries the marker `log-check: allow` in a comment, with the
    reason next to it. For the Logger's own sinks (tcLog.h writes the console
    with cout / cerr) and the few raw lines kept on purpose (the MCP port
    line on stderr, until #414). A marker on a line with nothing to exempt
    fails too, so markers cannot go stale.
  - Vendored files: none live under the scanned folders today (miniaudio,
    lz4, nlohmann/json, sokol, ... sit directly in core/include). List one in
    VENDORED below if that changes.

Run: python3 tools/check_core_logging.py   (exit 1 on a finding)
It first checks its own scanner against SELFTEST below. Well under a second.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCAN_DIRS = ["core/include/tc", "core/platform"]
EXTENSIONS = {".h", ".hpp", ".c", ".cpp", ".m", ".mm"}
# Paths relative to ROOT, posix style.
VENDORED = set()

MARKER = "log-check: allow"
FORBIDDEN = re.compile(
    r"(?<![\w.>])(?:std::)?\b("
    r"printf|fprintf|vprintf|vfprintf|puts|fputs|perror|"
    r"cout|cerr|clog|wcout|wcerr|wclog|"
    r"NSLog|NSLogv"
    r")\b"
)


def blank(text):
    """Replace comments and string / char literals with spaces, keeping line
    breaks so line numbers stay right."""
    out = []
    i, n = 0, len(text)

    def keep_newlines(s):
        return "".join("\n" if c == "\n" else " " for c in s)

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(keep_newlines(text[i:j]))
            i = j
        elif c == "R" and nxt == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            # Raw string R"delim( ... )delim"
            m = re.match(r'R"([^ ()\\\t\n]{0,16})\(', text[i:])
            if not m:
                out.append(c)
                i += 1
                continue
            end = ")" + m.group(1) + '"'
            j = text.find(end, i + m.end())
            j = n if j < 0 else j + len(end)
            out.append(keep_newlines(text[i:j]))
            i = j
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum())):
            # String or character literal (a ' after a digit is a C++14 digit
            # separator, as in 1'000'000).
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(keep_newlines(text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scan(text):
    """Return (findings, stale_markers): findings are (line, word) pairs not
    covered by a marker; stale markers are line numbers of markers with
    nothing to exempt."""
    raw_lines = text.split("\n")
    code_lines = blank(text).split("\n")
    findings, stale = [], []
    for idx, code in enumerate(code_lines):
        words = [m.group(1) for m in FORBIDDEN.finditer(code)]
        marked = MARKER in raw_lines[idx]
        if words and not marked:
            findings.extend((idx + 1, w) for w in words)
        elif marked and not words:
            stale.append(idx + 1)
    return findings, stale


SELFTEST = r'''
// printf("comment") and std::cerr in a comment
/* NSLog(@"block comment");
   std::cout << "x"; */
const char* s = "printf cerr cout NSLog";
NSString* t = @"NSLog(%@)";
const char* r = R"x(std::cerr << ")x";
char q = '"';
int big = 1'000'000;
snprintf(buf, sizeof(buf), "%d", 1);
logError("X") << "printf failed";
obj.printf_count = 0;
FLAG printf("a");
FLAG std::cerr << "b";
FLAG NSLog(@"c");
FLAG cout << 1;
std::cerr << "kept"; // log-check: allow (reason)
// log-check: allow (STALE: nothing to exempt)
'''


def selftest():
    findings, stale = scan(SELFTEST)
    lines = SELFTEST.split("\n")
    want = {i + 1 for i, l in enumerate(lines) if l.startswith("FLAG ")}
    got = {ln for ln, _ in findings}
    want_stale = {i + 1 for i, l in enumerate(lines) if "STALE" in l}
    if got != want or set(stale) != want_stale:
        print("check_core_logging.py: scanner self-test failed")
        print(f"  flagged lines {sorted(got)}, expected {sorted(want)}")
        print(f"  stale markers {sorted(stale)}, expected {sorted(want_stale)}")
        sys.exit(2)


def main():
    selftest()
    problems = 0
    files = 0
    for d in SCAN_DIRS:
        for path in sorted((ROOT / d).rglob("*")):
            if path.suffix not in EXTENSIONS or not path.is_file():
                continue
            rel = path.relative_to(ROOT).as_posix()
            if rel in VENDORED:
                continue
            files += 1
            text = path.read_text(encoding="utf-8", errors="replace")
            findings, stale = scan(text)
            for line, word in findings:
                print(f"{rel}:{line}: '{word}' bypasses the Logger; use "
                      f"logError()/logWarning()/logNotice()/logVerbose() "
                      f"(or mark a Logger sink with '// {MARKER} (reason)')")
                problems += 1
            for line in stale:
                print(f"{rel}:{line}: stale '{MARKER}' marker (nothing to exempt on this line)")
                problems += 1
    if problems:
        print(f"check_core_logging.py: {problems} problem(s) in {files} files")
        return 1
    print(f"check_core_logging.py: OK ({files} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
