#!/usr/bin/env python3
"""Flag mutable header-inline state in TrussC's core headers.

Why: a hot reload guest on Windows is a DLL linked against the host EXE's
import library. It imports only the non-inline functions compiled into
TrussC.lib; every header-inline function or variable is compiled again into
the guest DLL, with its own copy of every function-local `static` and every
`inline` variable (PE has no symbol interposition and no weak coalescing
across modules). Linux (-rdynamic interposition) and macOS (dyld weak
coalescing) give the guest the host's instance, but only of state the host
contains too, i.e. state host code uses; state only guest code touches is the
guest's own there as well (on Linux, a fresh copy per reloaded generation).
So a split between host and guest shows only on Windows, and only at run
time. State that must be one per process therefore lives non-inline in a .cpp
(docs/ARCHITECTURE.md, "One instance per process").

What is flagged, in core/include (vendored libraries excluded):
  - a `static` / `thread_local` local variable inside a function body
    (every function defined in a header is inline or a template)
  - an `inline` variable (namespace scope, or `static inline` in a class)
  - a mutable namespace-scope `static` variable, or a mutable variable in an
    anonymous namespace (one copy per translation unit, not even per module)
  - a class template's static data member defined in the header
    (`template <class T> int Reg<T>::count = 0;`) and a variable template
    (`template <class T> T zero = T();`): one instance per module, like an
    inline variable
Every declarator of a declaration counts (`static int a = 0, b = 1;` is two
findings). `constexpr` variables are not flagged: compile-time constants have
no run-time state to split. Nor are `const` namespace-scope `static` and
anonymous-namespace variables, any more than a plain namespace-scope `const`:
all three have internal linkage, so every translation unit on every platform
already has its own copy of the same constant, and hot reload changes nothing
about that (tools/header_state_selftest.h pins it: notFlaggedNsStaticConst).
Everything else must be listed in the allowlist
(tools/header_state_allowlist.txt) with the reason a per-module copy is
harmless, under one of the categories in CATEGORIES below. An allowlist entry
that no longer matches anything fails too, so the list cannot go stale.

The scan is textual (comments, strings and preprocessor lines blanked, braces
tracked to tell namespace, class and function scopes apart), so it reads both
sides of every #if -- the Windows branches too, which a compiler-based check
run on Linux or macOS would skip. It runs in well under a second. Being
textual, it can still be fooled by unusual code; tools/header_state_selftest.h
pins the constructs it must see through (compound-assignment operators, braced
default arguments and mem-initializers, template members, several declarators,
...), and every run checks the scanner against it first.

An entry `<path> <scope>::*` covers every IMMUTABLE finding in that scope (a
table of constants); an entry in the immutable category must match an
immutable finding.

Usage:
  python3 tools/check_header_state.py              # self-test, then check; exit 1 on a failure
  python3 tools/check_header_state.py --self-test  # the self-tests only (scanner, failure message)
  python3 tools/check_header_state.py --list       # print every finding
"""

import argparse
import copy
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_ROOT = os.path.join(REPO, "core", "include")
ALLOWLIST = os.path.join(REPO, "tools", "header_state_allowlist.txt")
SELFTEST = os.path.join(REPO, "tools", "header_state_selftest.h")
GLOBAL_CPP = os.path.join(REPO, "core", "include", "tc", "app", "tcGlobal.cpp")

# Allowlist categories (see the allowlist's header). Anything else is rejected,
# so state that a Windows guest would really split cannot be parked there
# under a new label: it has to move into a .cpp.
CATEGORIES = ("harmless", "immutable", "host-only", "no-hot-reload")

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


# Multi-character punctuators the walker must not split: `a += b` lexed as `+`,
# `=` would read as an initializer (`operator+=` then hides every later body in
# its class). Longest first, as the C++ lexer does.
TOKEN_RE = re.compile(r"[A-Za-z_]\w*|\d[\w.']*|<<=|>>=|::|->|&&|\|\||==|!=|<=|>=|"
                      r"\+=|-=|\*=|/=|%=|&=|\|=|\^=|\.\.\.|\S")


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
            if nxt[:1].isdigit() or nxt in ('"', "-", "+", "{"):
                # A parameter list never starts with a literal: this is a
                # direct-initialized variable (`inline std::vector<int> v(3);`)
                return "variable", head[i - 1]
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


def first_declarator(head):
    """`head` cut before its second declarator (`int a, b` -> `int a`)."""
    for i, t in top_level(head):
        if t == ",":
            return head[:i]
    return head


DECL_PREFIX = {"*", "&", "&&", "const", "volatile"}
DECL_FOLLOW = {"=", ",", "[", "{", "(", ":", None}   # None: end of statement


def later_declarators(toks, start):
    """Names declared after the first declarator of a declaration: `toks` is
    the whole statement, toks[start:] what follows the first declarator's
    name. A comma separates declarators only outside (), [] and {}, and only
    when a declarator follows it, so a comma between template arguments in
    an initializer (`= std::pair<int, int>(1, 2), b`) does not."""
    names = []
    depth = 0
    for i in range(start, len(toks)):
        t = toks[i]
        if t in ("(", "[", "{"):
            depth += 1
        elif t in (")", "]", "}"):
            depth -= 1
        elif t == "," and depth == 0:
            k = i + 1
            while k < len(toks) and toks[k] in DECL_PREFIX:
                k += 1
            if k + 1 < len(toks) and toks[k] == "(" and toks[k + 1] in ("*", "&"):
                # `(*fp)(args)`: a pointer to function
                end = skip_group(toks, k, "(", ")")
                idents = [x for x in toks[k + 1:end] if is_ident(x) and x != "const"]
                if idents:
                    names.append(idents[-1])
            elif k < len(toks) and is_ident(toks[k]):
                if (toks[k + 1] if k + 1 < len(toks) else None) in DECL_FOLLOW:
                    names.append(toks[k])
    return names


def class_qualified_name(head, name):
    """`Reg<T>::items` in a declaration head -> "Reg::items" (template
    arguments dropped); None when `name` is not qualified."""
    idx = max(i for i, t in enumerate(head) if t == name)
    parts = [name]
    k = idx - 1
    while k >= 1 and head[k] == "::":
        j = k - 1
        if head[j] == ">":
            depth = 0
            while j >= 0:
                if head[j] == ">":
                    depth += 1
                elif head[j] == "<":
                    depth -= 1
                    if depth == 0:
                        break
                j -= 1
            j -= 1
        if j < 0 or not is_ident(head[j]):
            break
        parts.insert(0, head[j])
        k = j - 1
    return "::".join(parts) if len(parts) > 1 else None


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
        is_template = stmt[:2] == ["template", "<"]
        toks = strip_prefix(stmt)
        head = first_declarator(declaration_head(toks))
        what, name = classify_head(head)
        if what != "variable" or not name or "constexpr" in head:
            return
        names = [name] + later_declarators(toks, len(head))
        specs = set(head)
        immutable = is_immutable(head)

        def add_all(kind):
            for nm in names:
                add(line, nm, kind, immutable)

        if stack[-1]["kind"] == "class":
            if "static" in specs and "inline" in specs:
                add_all("inline variable")
            return
        if "extern" in specs:
            return
        if "inline" in specs:
            add_all("inline variable")
        elif is_template:
            member = class_qualified_name(head, name)
            if member:
                add(line, member, "template static data member (one per module)", immutable)
            else:
                add_all("variable template (one per module)")
        elif "static" in specs:
            if not immutable:
                add_all("namespace static (one per translation unit)")
        elif in_anon_ns() and not immutable:
            add_all("anonymous-namespace variable (one per translation unit)")

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
        if b.count("(") > b.count(")"):
            # Inside a parameter list still open: a braced default argument
            # (`const Settings& s = {}`, `int v = int{1}`), not a body. The
            # declaration goes on after the `}`.
            return {"kind": "init", "name": "", "keep": True}
        top = list(top_level(b))
        if any(t == "=" and (i == 0 or b[i - 1] != "operator") for i, t in top):
            return {"kind": "init", "name": "", "keep": True}      # `= { ... }`
        colon = [i for i, t in top if t == ":"]
        if (colon and ")" in b[:colon[0]]
                and b[-1] not in (")", "}") and b[-1] not in TRAILING_QUALIFIERS):
            # Brace-initialized member in a constructor's mem-initializer list:
            # `Foo() : a_{1}, b_(2) {` -- the body is the `{` after a complete
            # initializer, i.e. after its `)` or `}`
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
                # The statement, up to its `;` (a lambda in the initializer
                # has `;`s of its own, one level down)
                rest, depth = [], 0
                for t, _ in tokens[i:i + 2000]:
                    if t == ";" and depth == 0:
                        break
                    if t in ("(", "[", "{"):
                        depth += 1
                    elif t in (")", "]", "}"):
                        depth -= 1
                    rest.append(t)
                head = first_declarator(declaration_head(rest))
                cut = head
                for k, t in top_level(head):
                    if t in ("(", "[", "{"):
                        cut = head[:k]
                        break
                if "constexpr" not in cut:
                    idents = [t for t in cut if is_ident(t) and t not in
                              ("static", "thread_local", "const", "inline", "mutable")]
                    for nm in [idents[-1] if idents else "?"] + later_declarators(rest, len(head)):
                        add(line, nm, "function-local static", is_immutable(cut))
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

def parse_allowlist(lines, source):
    """{key: reason} from allowlist lines. Format, one entry per line:
         <path> <scope>::<name>   # <category>: <reason>
    `#` lines and blank lines are ignored. Every entry needs a reason, under
    one of CATEGORIES."""
    entries, errors = {}, []
    for lineno, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        where = f"{source}:{lineno}"
        key, sep, reason = line.partition("#")
        key = " ".join(key.split())
        if not sep or not reason.strip():
            errors.append(f"{where}: entry has no reason: {key}")
            continue
        category, colon, detail = reason.partition(":")
        if category.strip() == "<category>" or detail.strip() == "<reason>":
            errors.append(f"{where}: fill in the category and the reason (the '{PLACEHOLDER}' "
                          f"placeholder is still there): {key}")
            continue
        if not colon or category.strip() not in CATEGORIES or not detail.strip():
            errors.append(f"{where}: the reason must be '<category>: <why>', with a category "
                          f"from {', '.join(CATEGORIES)}: {key}")
            continue
        if key in entries:
            errors.append(f"{where}: duplicate entry: {key}")
        entries[key] = reason.strip()
    return entries, errors


def load_allowlist(path):
    with open(path, encoding="utf-8") as f:
        return parse_allowlist(f.read().split("\n"), os.path.relpath(path, REPO))


# ---------------------------------------------------------------------------
# The failure message
# ---------------------------------------------------------------------------
# Written for someone who has never used hot reload: what goes wrong first, in
# one plain sentence, then the fix that is always right, and only then the
# allowlist, as the exception. message_self_test() pins that order.

WHAT_GOES_WRONG = ("A static or inline variable in a header gets a separate copy inside a "
                   "Windows hot-reload app, so the app and TrussC would see different values.")

# The accessor example: a real one, internal::touchAsMouse() in tcGlobal.cpp
# (message_self_test() checks it is still there).
EXAMPLE_NAME = "touchAsMouse"
EXAMPLE_CPP = "bool& touchAsMouse() {"
EXAMPLE = [
    "    header, before:  inline bool touchAsMouse = true;",
    "    header, after:   bool& touchAsMouse();",
    "    .cpp, after:     bool& touchAsMouse() { static bool enabled = true; return enabled; }",
]

# When each allowlist category applies, one plain line each.
CATEGORY_HELP = {
    "harmless": "a separate copy breaks nothing: a warn-once flag, a small cache (not of GPU objects)",
    "immutable": "constant data that never changes, so every copy is the same",
    "host-only": "only TrussC's own main loop and .cpp files use it, never app or addon code",
    "no-hot-reload": "only used where hot reload never runs (Android, headless apps)",
}
PLACEHOLDER = "<category>: <reason>"


def finding_line(f):
    tag = "immutable" if f.immutable else "mutable"
    return f"{f.path}:{f.line}: new {f.kind} ({tag}): {f.key.split(' ', 1)[1]}"


def allowlist_line(f):
    """The allowlist entry for finding f, ready to paste; the placeholder
    must be filled in (the loader rejects it as is)."""
    return f"{f.key}   # {PLACEHOLDER}"


def failure_message(new, problems):
    """The lines a failing run prints: each new finding, what goes wrong and
    how to fix it, then the other allowlist problems (stale or malformed
    entries), which explain themselves."""
    out = [finding_line(f) for f in new]
    if new:
        width = max(len(c) for c in CATEGORIES)
        out += [
            "",
            WHAT_GOES_WRONG,
            "",
            "When unsure, move it to a .cpp and reach it through a function. That is",
            f"always correct. For example, internal::{EXAMPLE_NAME}() in tcGlobal.cpp:",
            "",
            *EXAMPLE,
            "",
            f"Callers then write {EXAMPLE_NAME}() where they wrote {EXAMPLE_NAME}. For a",
            "static inside a function, move the whole function into the .cpp and keep",
            "only its declaration in the header.",
            "",
            "Only if you are sure a separate copy is harmless, allowlist it instead:",
            f"add {'this line' if len(new) == 1 else 'these lines'} to tools/header_state_allowlist.txt, with",
            f"'{PLACEHOLDER}' replaced by one of the categories below and why:",
            "",
            *[allowlist_line(f) for f in new],
            "",
            *[f"  {c.ljust(width)}  {CATEGORY_HELP[c]}" for c in CATEGORIES],
            "",
            "Picking a category without being sure is the wrong move: the bug it",
            "would hide shows up only at run time, and only on Windows. Move the",
            "variable to a .cpp instead.",
        ]
    if problems:
        if new:
            out.append("")
        out += problems
    out += ["", "More: docs/ARCHITECTURE.md, \"One instance per process\"."]
    return out


def message_self_test():
    """Check failure_message() on a made-up finding: the plain sentence comes
    first, the .cpp fix before the allowlist route, the pasted line parses
    (once filled in) to the finding's key, and every category is explained.
    Returns a list of problems."""
    errors = []
    f = Finding("core/include/tcExample.h", 12, "trussc::internal", "exampleState",
                "inline variable", False)
    lines = failure_message([f], [])
    if lines[:3] != [finding_line(f), "", WHAT_GOES_WRONG]:
        errors.append("failure message: the first line after the findings must be WHAT_GOES_WRONG")
    paste = [i for i, ln in enumerate(lines) if ln == allowlist_line(f)]
    fix = [i for i, ln in enumerate(lines) if ln.startswith("When unsure, move it to a .cpp")]
    if len(paste) != 1 or not fix or fix[0] > paste[0]:
        errors.append("failure message: needs the .cpp fix first, then one allowlist line per finding")
    else:
        for c in CATEGORIES:
            filled = lines[paste[0]].replace(PLACEHOLDER, f"{c}: why it is fine")
            entries, errs = parse_allowlist([filled], "<message>")
            if errs or list(entries) != [f.key]:
                errors.append(f"failure message: the allowlist line, filled in as {c}, does not "
                              f"parse to the finding's key: {filled}")
        _entries, errs = parse_allowlist([lines[paste[0]]], "<message>")
        if not errs:
            errors.append("failure message: the allowlist line is accepted with the placeholder unfilled")
    if set(CATEGORY_HELP) != set(CATEGORIES):
        errors.append("failure message: CATEGORY_HELP must explain every category in CATEGORIES")
    for c in CATEGORIES:
        if not any(ln.split()[:1] == [c] for ln in lines):
            errors.append(f"failure message: category '{c}' is not listed")
    if not any("wrong move" in ln for ln in lines):
        errors.append("failure message: must say that picking a category without being sure is wrong")
    if "docs/ARCHITECTURE.md" not in lines[-1]:
        errors.append("failure message: must end with the pointer to docs/ARCHITECTURE.md")
    with open(GLOBAL_CPP, encoding="utf-8") as g:
        if EXAMPLE_CPP not in g.read():
            errors.append(f"failure message: its example, {EXAMPLE_CPP} in tcGlobal.cpp, is gone: "
                          f"point EXAMPLE at another accessor there")
    return errors


def self_test():
    """Scan tools/header_state_selftest.h and compare with the `// expect:`
    markers in it: each marks the keys (<scope>::<name>) the declaration on
    its line must produce. Returns a list of mismatches."""
    with open(SELFTEST, encoding="utf-8") as f:
        text = f.read()
    expected = set()
    for lineno, raw in enumerate(text.split("\n"), 1):
        m = re.search(r"//\s*expect:\s*(.+)$", raw)
        if m:
            expected.update((lineno, k) for k in m.group(1).split())
    rel = os.path.relpath(SELFTEST, REPO).replace(os.sep, "/")
    got = {(f.line, f.key.split(" ", 1)[1]) for f in scan_tokens(tokenize(blank(text)), rel)}
    errors = [f"{rel}:{ln}: expected finding not reported: {k}" for ln, k in sorted(expected - got)]
    errors += [f"{rel}:{ln}: unexpected finding: {k}" for ln, k in sorted(got - expected)]
    return errors


def main():
    ap = argparse.ArgumentParser(description="Flag mutable header-inline state in core/include.")
    ap.add_argument("--list", action="store_true", help="print every finding and exit 0")
    ap.add_argument("--self-test", action="store_true",
                    help="only check the scanner against tools/header_state_selftest.h, "
                         "and the failure message")
    args = ap.parse_args()

    # The scanner first: a blind spot would make the check below pass on
    # state it never saw.
    problems = self_test()
    if problems:
        for p in problems:
            print(p)
        print()
        print("check_header_state.py no longer reads tools/header_state_selftest.h as")
        print("expected: fix the scanner (or the fixture's expect: markers) first.")
        return 1
    problems = message_self_test()
    if problems:
        for p in problems:
            print(p)
        return 1
    if args.self_test:
        print("header state check: self-test OK")
        return 0

    findings = scan()
    if args.list:
        for f in findings:
            tag = "immutable" if f.immutable else "mutable"
            print(f"{f.path}:{f.line}: {f.kind} ({tag}): {f.key.split(' ', 1)[1]}")
        print(f"{len(findings)} finding(s)")
        return 0

    allow, errors = load_allowlist(ALLOWLIST)
    category = {k: r.split(":", 1)[0].strip() for k, r in allow.items()}
    # `<path> <scope>::*` covers every finding in that scope, for tables of
    # constants; only immutable ones, so a mutable variable added to the same
    # scope later is still reported.
    wildcards = {k[:-1]: k for k in allow if k.endswith("::*")}
    for k in wildcards.values():
        if category[k] != "immutable":
            errors.append(f"a wildcard entry must be in the immutable category: {k}")
    used = set()
    new = []
    for f in findings:
        if f.key in allow:
            used.add(f.key)
            if category[f.key] == "immutable" and not f.immutable:
                errors.append(f"{f.path}:{f.line}: listed as immutable, but it is not: {f.key}")
            continue
        wild = next((k for prefix, k in wildcards.items() if f.key.startswith(prefix)), None)
        if wild and f.immutable and category[wild] == "immutable":
            used.add(wild)
        else:
            new.append(f)

    for key in sorted(set(allow) - used):
        errors.append(f"allowlist entry matches nothing (moved or removed? drop it): {key}")

    if new or errors:
        for line in failure_message(new, errors):
            print(line)
        return 1
    print(f"header state check: OK ({len(findings)} finding(s), all allowlisted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
