#!/usr/bin/env python3
"""Flag mutable header-inline state in TrussC's core headers.

Why: a hot reload guest on Windows is a DLL linked against the host EXE's
import library. It imports only the non-inline functions compiled into
TrussC.lib; every header-inline function or variable is compiled again into
the guest DLL, with its own copy of every function-local `static` and every
`inline` variable (PE has no symbol interposition and no weak coalescing
across modules). Linux (-rdynamic interposition) and macOS (dyld weak
coalescing) share one instance, so a split only shows on Windows, and only at
run time. State that must be one per process therefore lives non-inline in a
.cpp (docs/ARCHITECTURE.md, "One instance per process").

What is flagged, in core/include (vendored libraries excluded):
  - a `static` / `thread_local` local variable inside a function body
    (every function defined in a header is inline or a template)
  - an `inline` variable (namespace scope, or `static inline` in a class)
  - a namespace-scope `static` variable, or a variable in an anonymous
    namespace (one copy per translation unit, not even per module)
`constexpr` variables are not flagged: compile-time constants have no run-time
state to split. Everything else must be listed in the allowlist
(tools/header_state_allowlist.txt) with the reason a per-module copy is
harmless: a cache, immutable data, state only the host ever touches, ... An
allowlist entry that no longer matches anything fails too, so the list cannot
go stale.

The scan is textual (comments, strings and preprocessor lines blanked, braces
tracked to tell namespace, class and function scopes apart), so it reads both
sides of every #if -- the Windows branches too, which a compiler-based check
run on Linux or macOS would skip. It runs in well under a second.

Usage:
  python3 tools/check_header_state.py          # check; exit 1 on a finding
  python3 tools/check_header_state.py --list   # print every finding
"""

import argparse
import copy
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_ROOT = os.path.join(REPO, "core", "include")
ALLOWLIST = os.path.join(REPO, "tools", "header_state_allowlist.txt")

# Vendored third-party code under core/include: not TrussC's to police.
VENDORED_DIRS = {"sokol", "stb", "nlohmann", "pugixml", "earcut", "lz4", "impl"}
VENDORED_FILES = {"miniaudio.h", "stb_vorbis.c"}
HEADER_EXT = (".h", ".hpp", ".inl")

CLASS_KEYS = {"class", "struct", "union"}
RAW_PREFIXES = {"R", "u8R", "uR", "UR", "LR"}
TRAILING_QUALIFIERS = {"const", "volatile", "noexcept", "override", "final",
                       "mutable", "constexpr", "consteval", "&", "&&"}
IDENT = re.compile(r"[A-Za-z_]\w*$")
MACRO = re.compile(r"[A-Z][A-Z0-9]*_[A-Z0-9_]+$")   # TC_PLATFORMS, TC_LUA_BIND, ...


def is_ident(t):
    return bool(IDENT.match(t))


# ---------------------------------------------------------------------------
# Lexing
# ---------------------------------------------------------------------------

def blank(text):
    """Blank comments, string / char literals and preprocessor lines, keeping
    every newline so line numbers stay valid. A literal becomes a lone `"`, so
    `extern "C"` is still recognizable."""
    out = []
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start:
            j = i
            while j < n and text[j] in " \t":
                j += 1
            if j < n and text[j] == "#":
                # Directive, with backslash continuations. Conditionals leave
                # a marker token: the walker scans every branch from the same
                # starting scope, so braces opened differently per branch
                # (`#ifdef _WIN32  Foo() : a(x) {  #else ...`) stay balanced.
                m = re.match(r"#\s*(\w+)", text[j:j + 40])
                d = m.group(1) if m else ""
                if d in ("if", "ifdef", "ifndef"):
                    out.append(" __pp_if__ ")
                elif d in ("elif", "elifdef", "elifndef", "else"):
                    out.append(" __pp_else__ ")
                elif d == "endif":
                    out.append(" __pp_endif__ ")
                while j < n:
                    if text[j] == "\n":
                        if text[j - 1] == "\\":
                            out.append("\n")
                            j += 1
                            continue
                        break
                    j += 1
                i = j
                continue
            line_start = False
        if c == "\n":
            out.append(c)
            i += 1
            line_start = True
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:end]))
            i = end
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            word = text[i:j]
            if word in RAW_PREFIXES and j < n and text[j] == '"':
                # Raw string literal R"delim( ... )delim"
                paren = text.find("(", j)
                delim = text[j + 1:paren]
                end = text.find(")" + delim + '"', paren)
                end = n if end < 0 else end + len(delim) + 2
                out.append('"' + "".join(ch if ch == "\n" else " " for ch in text[j + 1:end]))
                i = end
                continue
            out.append(word)
            i = j
            continue
        if c.isdigit():
            # Numbers, with digit separators (1'000) that would otherwise
            # open a char literal
            j = i
            while j < n and (text[j].isalnum() or text[j] in "._'"):
                j += 1
            out.append(text[i:j])
            i = j
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                if text[j] == "\\":
                    j += 1
                j += 1
            out.append('"')
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


TOKEN_RE = re.compile(r"[A-Za-z_]\w*|\d[\w.']*|::|->|&&|\|\||==|!=|<=|>=|\.\.\.|\S")


def tokenize(text):
    tokens = []
    for lineno, line in enumerate(text.split("\n"), 1):
        for m in TOKEN_RE.finditer(line):
            tokens.append((m.group(0), lineno))
    return tokens


# ---------------------------------------------------------------------------
# Declaration helpers
# ---------------------------------------------------------------------------

def skip_group(toks, i, open_tok, close_tok):
    """toks[i] == open_tok: index just past its matching close_tok."""
    depth = 0
    while i < len(toks):
        if toks[i] == open_tok:
            depth += 1
        elif toks[i] == close_tok:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def strip_prefix(toks):
    """Drop leading `template <...>` and `[[attributes]]`, and annotation
    macro calls such as TC_PLATFORMS("macos,windows") wherever they sit
    (`class TC_PLATFORMS("...") Thread {`)."""
    i = 0
    while i < len(toks):
        if toks[i] == "template" and i + 1 < len(toks) and toks[i + 1] == "<":
            i = skip_group(toks, i + 1, "<", ">")
        elif toks[i] == "[" and i + 1 < len(toks) and toks[i + 1] == "[":
            i = skip_group(toks, i, "[", "]")
        else:
            break
    out = []
    toks = toks[i:]
    i = 0
    while i < len(toks):
        if MACRO.match(toks[i]) and i + 1 < len(toks) and toks[i + 1] == "(":
            i = skip_group(toks, i + 1, "(", ")")
            continue
        out.append(toks[i])
        i += 1
    return out


def top_level(toks):
    """Yield (index, token) for tokens outside (), <> and [] groups. `<` / `>`
    count as brackets only in a declaration head, which is all this sees
    (callers cut at the first top-level `=`)."""
    paren = angle = square = 0
    prev = None
    for i, t in enumerate(toks):
        if paren == angle == square == 0:
            yield i, t
        if t == "(":
            paren += 1
        elif t == ")":
            paren -= 1
        elif t == "[":
            square += 1
        elif t == "]":
            square -= 1
        elif t == "<" and prev != "operator" and paren == 0:
            angle += 1
        elif t == ">" and angle > 0 and paren == 0:
            angle -= 1
        prev = t


def declaration_head(toks):
    """Tokens of a declaration up to its initializer / body / end."""
    for i, t in top_level(toks):
        if t in ("{", ";"):
            return toks[:i]
        if t == "=" and (i == 0 or toks[i - 1] != "operator"):
            return toks[:i]
    return toks


def qualified_name_before(toks, i):
    """The (possibly qualified) name that ends at toks[i]."""
    parts = [toks[i]]
    k = i - 1
    if k >= 0 and toks[k] == "~":
        parts.insert(0, "~")
        k -= 1
    while k >= 1 and toks[k] == "::" and is_ident(toks[k - 1]):
        parts[0:0] = [toks[k - 1], "::"]
        k -= 2
    return "".join(parts)


def classify_head(head):
    """('function', name) | ('variable', name) | ('other', None)."""
    if not head:
        return "other", None
    if head[0] in ("namespace", "using", "typedef", "friend", "static_assert",
                   "concept", "requires", "enum", "return") or head[0] in CLASS_KEYS:
        return "other", None
    if "namespace" in head[:3]:
        return "other", None
    if "operator" in head:
        return "function", "operator"
    for i, t in top_level(head):
        if t != "(":
            continue
        nxt = head[i + 1] if i + 1 < len(head) else ""
        if nxt in ("*", "&", "^") or (is_ident(nxt) and head[i + 2:i + 4] == ["::", "*"]):
            # Pointer / reference to function (or member): a variable
            group_end = skip_group(head, i, "(", ")")
            idents = [x for x in head[i + 1:group_end] if is_ident(x) and x != "const"]
            return "variable", (idents[-1] if idents else None)
        if i > 0 and is_ident(head[i - 1]) and head[i - 1] not in ("decltype", "alignas", "sizeof"):
            return "function", qualified_name_before(head, i - 1)
        return "other", None
    cut = head
    for i, t in top_level(head):
        if t == "[":
            cut = head[:i]
            break
    idents = [t for t in cut if is_ident(t)]
    if not idents:
        return "other", None
    return "variable", idents[-1]


def is_immutable(head):
    """A const object -- not merely a pointer (or function pointer) to const."""
    for i, t in top_level(head):
        if t == "(" and i + 1 < len(head) and head[i + 1] in ("*", "&"):
            group = head[i:skip_group(head, i, "(", ")")]
            return "const" in group
    if "const" not in head:
        return False
    last_const = max(i for i, t in enumerate(head) if t == "const")
    stars = [i for i, t in enumerate(head) if t == "*"]
    return not stars or stars[-1] < last_const


def is_lambda_body(buf):
    """Does a `{` right after `buf` open a lambda body?"""
    i = len(buf) - 1
    # Trailing return type: `-> type`
    depth = 0
    for k in range(len(buf) - 1, -1, -1):
        t = buf[k]
        if t in (")", "]", ">"):
            depth += 1
        elif t in ("(", "[", "<"):
            depth -= 1
            if depth < 0:
                break
        elif t == "->" and depth == 0:
            i = k - 1
            break
        elif t in (";", "{", "}", "="):
            break
    while i >= 0 and buf[i] in TRAILING_QUALIFIERS:
        i -= 1
    if i >= 0 and buf[i] == ")":
        depth, k = 0, i
        while k >= 0:
            if buf[k] == ")":
                depth += 1
            elif buf[k] == "(":
                depth -= 1
                if depth == 0:
                    break
            k -= 1
        i = k - 1
        while i >= 0 and buf[i] in TRAILING_QUALIFIERS:
            i -= 1
    if i >= 0 and buf[i] == "]":
        depth, k = 0, i
        while k >= 0:
            if buf[k] == "]":
                depth += 1
            elif buf[k] == "[":
                depth -= 1
                if depth == 0:
                    break
            k -= 1
        before = buf[k - 1] if k >= 1 else ""
        if before in ("operator", "delete", "new"):
            return False
        # A subscript follows a name or a closing bracket; a lambda
        # introducer follows an operator, `(`, `,`, `{`, `return`, ...
        return before == "return" or not (is_ident(before) or before in (")", "]"))
    return False


# ---------------------------------------------------------------------------
# Scope walk
# ---------------------------------------------------------------------------

class Finding:
    def __init__(self, path, line, scope, name, kind, immutable):
        self.path, self.line, self.scope, self.name = path, line, scope, name
        self.kind, self.immutable = kind, immutable

    @property
    def key(self):
        return f"{self.path} {self.scope}::{self.name}" if self.scope else f"{self.path} {self.name}"


def scan_tokens(tokens, rel):
    findings = []
    # A frame: kind (ns / class / func / init), name, and the enclosing
    # statement's token buffer, restored when the frame closes.
    stack = [{"kind": "ns", "name": "", "anon": False}]
    buf, buf_line = [], 0

    def scope_name():
        return "::".join(fr["name"] for fr in stack if fr["name"])

    def in_anon_ns():
        return any(fr.get("anon") for fr in stack)

    def add(line, name, kind, immutable):
        findings.append(Finding(rel, line, scope_name(), name, kind, immutable))

    def analyze_decl(stmt, line):
        """A namespace- or class-scope declaration ended at `;`."""
        head = declaration_head(strip_prefix(stmt))
        what, name = classify_head(head)
        if what != "variable" or not name or "constexpr" in head:
            return
        specs = set(head)
        if stack[-1]["kind"] == "class":
            if "static" in specs and "inline" in specs:
                add(line, name, "inline variable", is_immutable(head))
            return
        if "extern" in specs:
            return
        if "inline" in specs:
            add(line, name, "inline variable", is_immutable(head))
        elif "static" in specs:
            if not is_immutable(head):
                add(line, name, "namespace static (one per translation unit)", False)
        elif in_anon_ns() and not is_immutable(head):
            add(line, name, "anonymous-namespace variable (one per translation unit)", False)

    def open_frame(kind):
        """Classify the `{` that ends `buf`."""
        b = strip_prefix(buf)
        if kind == "ns" and (b[:1] == ["namespace"] or b[:2] == ["inline", "namespace"]):
            names = [t for t in b if is_ident(t) and t not in ("namespace", "inline")]
            return {"kind": "ns", "name": "::".join(names), "anon": not names}
        if kind == "ns" and b[:1] == ["extern"]:
            return {"kind": "ns", "name": ""}                       # extern "C" { ... }
        if is_lambda_body(buf):
            return {"kind": "func", "name": "<lambda>", "keep": True}
        if kind in ("func", "init"):
            # Inside a function: a local class, or just a block / initializer
            tail = buf[-12:]
            keys = [k for k, t in enumerate(tail) if t in CLASS_KEYS]
            if keys and not any(t in ("(", "=") for t in tail[keys[-1]:]):
                nm = tail[keys[-1] + 1] if keys[-1] + 1 < len(tail) else ""
                return {"kind": "class", "name": nm if is_ident(nm) else "<local>"}
            return {"kind": kind, "name": "", "keep": kind == "init"}
        # Namespace or class scope
        if not b:
            return {"kind": "func", "name": ""}                     # ctor body after `a{1}`
        top = list(top_level(b))
        if any(t == "=" and (i == 0 or b[i - 1] != "operator") for i, t in top):
            return {"kind": "init", "name": "", "keep": True}      # `= { ... }`
        colon = [i for i, t in top if t == ":"]
        if (colon and ")" in b[:colon[0]]
                and b[-1] != ")" and b[-1] not in TRAILING_QUALIFIERS):
            # Brace-initialized member in a constructor's mem-initializer list:
            # `Foo() : a_{1}, b_(2) {` -- the body is the NEXT `{`
            return {"kind": "init", "name": "", "keep": True}
        if any(t == "enum" for _, t in top):
            return {"kind": "init", "name": "", "keep": True}
        keys = [i for i, t in top if t in CLASS_KEYS]
        if keys and not any(t == "(" for i, t in top if i > keys[0]):
            rest = [t for t in b[keys[0] + 1:] if is_ident(t) and t not in ("final", "alignas")]
            return {"kind": "class", "name": rest[0] if rest else "<anon>", "keep": True}
        what, nm = classify_head(b)
        if what == "function" or any(t == "(" for _, t in top):
            return {"kind": "func", "name": nm or ""}
        return {"kind": "init", "name": "", "keep": True}           # `name{ ... }`

    # Preprocessor conditionals: [snapshot at #if, state after the first branch]
    pp = []

    i, n = 0, len(tokens)
    while i < n:
        tok, line = tokens[i]
        kind = stack[-1]["kind"]

        if tok == "__pp_if__":
            pp.append([copy.deepcopy((stack, buf, buf_line)), None])
            i += 1
            continue
        if tok == "__pp_else__":
            if pp:
                if pp[-1][1] is None:
                    pp[-1][1] = copy.deepcopy((stack, buf, buf_line))
                stack, buf, buf_line = copy.deepcopy(pp[-1][0])
            i += 1
            continue
        if tok == "__pp_endif__":
            if pp:
                _snap, first = pp.pop()
                if first is not None:
                    # Continue from where the first branch left off (branches
                    # normally leave the same nesting behind).
                    stack, buf, buf_line = first
            i += 1
            continue

        if kind == "func" and tok in ("static", "thread_local"):
            # A `static` directly in a function body always declares a static
            # local (block-scope functions cannot be static).
            if not (buf and buf[-1] in ("static", "thread_local")):
                rest = [t for t, _ in tokens[i:i + 80]]
                head = declaration_head(rest)
                cut = head
                for k, t in top_level(head):
                    if t in ("(", "[", "{"):
                        cut = head[:k]
                        break
                if "constexpr" not in cut:
                    idents = [t for t in cut if is_ident(t) and t not in
                              ("static", "thread_local", "const", "inline", "mutable")]
                    add(line, idents[-1] if idents else "?", "function-local static",
                        is_immutable(cut))
            buf.append(tok)
            i += 1
            continue

        if tok == "{":
            frame = open_frame(kind)
            frame["buf"], frame["line"] = buf, buf_line
            stack.append(frame)
            buf, buf_line = [], line
            i += 1
            continue

        if tok == "}":
            if len(stack) > 1:
                closed = stack.pop()
                buf, buf_line = closed["buf"], closed["line"]
                # A declaration continues after a class body / initializer /
                # lambda (`struct {...} x;`, `x = {...};`, `f([]{...});`);
                # after a namespace, a function body or a block it is over.
                buf = buf + ["{", "}"] if closed.get("keep") else []
            i += 1
            continue

        if tok == ";":
            if kind in ("ns", "class"):
                analyze_decl(buf, buf_line)
            buf = []
            i += 1
            continue

        if kind == "class" and tok == ":" and buf in (["public"], ["private"], ["protected"]):
            buf = []
            i += 1
            continue

        if not buf:
            buf_line = line
        buf.append(tok)
        i += 1

    return findings


def scan_file(path, rel):
    with open(path, encoding="utf-8", errors="replace") as f:
        return scan_tokens(tokenize(blank(f.read())), rel)


def scan():
    findings = []
    for root, dirs, files in os.walk(SCAN_ROOT):
        if os.path.normpath(root) == os.path.normpath(SCAN_ROOT):
            dirs[:] = [d for d in dirs if d not in VENDORED_DIRS]
        dirs.sort()
        for fn in sorted(files):
            if fn in VENDORED_FILES or not fn.endswith(HEADER_EXT):
                continue
            if fn.endswith(".glsl.h"):
                continue   # sokol-shdc output, generated at build time (gitignored)
            path = os.path.join(root, fn)
            rel = os.path.relpath(path, REPO).replace(os.sep, "/")
            findings.extend(scan_file(path, rel))
    return findings


# ---------------------------------------------------------------------------
# Allowlist
# ---------------------------------------------------------------------------

def load_allowlist(path):
    """{key: reason}. Format, one entry per line:
         <path> <scope>::<name>   # <reason>
    `#` lines and blank lines are ignored. Every entry needs a reason."""
    entries, errors = {}, []
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            key, sep, reason = line.partition("#")
            key = " ".join(key.split())
            if not sep or not reason.strip():
                errors.append(f"{os.path.relpath(path, REPO)}:{lineno}: entry has no reason: {key}")
                continue
            if key in entries:
                errors.append(f"{os.path.relpath(path, REPO)}:{lineno}: duplicate entry: {key}")
            entries[key] = reason.strip()
    return entries, errors


def main():
    ap = argparse.ArgumentParser(description="Flag mutable header-inline state in core/include.")
    ap.add_argument("--list", action="store_true", help="print every finding and exit 0")
    args = ap.parse_args()

    findings = scan()
    if args.list:
        for f in findings:
            tag = "immutable" if f.immutable else "mutable"
            print(f"{f.path}:{f.line}: {f.kind} ({tag}): {f.key.split(' ', 1)[1]}")
        print(f"{len(findings)} finding(s)")
        return 0

    allow, errors = load_allowlist(ALLOWLIST)
    used = set()
    new = []
    for f in findings:
        if f.key in allow:
            used.add(f.key)
        else:
            new.append(f)

    for f in new:
        tag = "immutable" if f.immutable else "mutable"
        errors.append(f"{f.path}:{f.line}: {f.kind} ({tag}) not in the allowlist: {f.key}")
    for key in sorted(set(allow) - used):
        errors.append(f"allowlist entry matches nothing (moved or removed? drop it): {key}")

    if errors:
        for e in errors:
            print(e)
        print()
        print("Header-inline state is one instance PER MODULE on Windows: a hot reload")
        print("guest DLL gets its own copy, invisible to the host. State that must be one")
        print("per process (singletons, registries, flags) belongs non-inline in a .cpp")
        print("(see tcGlobal.cpp). If a per-module copy is harmless (a cache, immutable")
        print("data), add the entry to tools/header_state_allowlist.txt with the reason.")
        print("See docs/ARCHITECTURE.md, 'One instance per process'.")
        return 1
    print(f"header state check: OK ({len(findings)} finding(s), all allowlisted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
