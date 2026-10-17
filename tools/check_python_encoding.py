#!/usr/bin/env python3
"""Require explicit encodings for first-party Python file I/O and text subprocesses.

A deliberately small AST check, not type inference: recognizes built-in/io/codecs
open, Path-style open/read_text/write_text and subprocess text keywords. Binary
opens and archive open APIs are exempt. Import aliases and positional encodings
are supported; dynamically dispatched wrappers are outside this check's scope.
"""
import ast
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
# Dependency source trees, downloaded builds and interpreter caches are not ours.
SKIP = {'.git', '.fetchcontent', '__pycache__', 'third_party', 'third-party',
        'vendor', 'vendored', 'libs', 'node_modules', '.venv'}


def python_files(root):
    for directory, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in SKIP and
                         d != 'build' and not d.startswith('build-'))
        for name in sorted(files):
            if name.endswith('.py'):
                yield Path(directory) / name


def violations(source):
    tree = ast.parse(source)
    aliases = {}
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for item in node.names:
                aliases[item.asname or item.name] = item.name
        elif isinstance(node, ast.ImportFrom):
            for item in node.names:
                aliases[item.asname or item.name] = f'{node.module}.{item.name}'

    def qualified(node):
        if isinstance(node, ast.Name):
            return aliases.get(node.id, node.id)
        if isinstance(node, ast.Attribute):
            return f'{qualified(node.value)}.{node.attr}'
        return ''

    # These factories return binary archive objects even when open() uses r/w.
    # Keep the list explicit: arbitrary objects with an open method may be Paths.
    archive_factories = {
        'zipfile.ZipFile', 'zipfile.PyZipFile', 'tarfile.open',
        'tarfile.TarFile', 'tarfile.TarFile.open',
    }

    def archive_value(node):
        return isinstance(node, ast.Call) and qualified(node.func) in archive_factories

    archive_targets = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and archive_value(node.value):
            archive_targets.update(id(target) for target in node.targets
                                   if isinstance(target, ast.Name))
        elif isinstance(node, ast.AnnAssign) and archive_value(node.value):
            archive_targets.add(id(node.target))
        elif isinstance(node, (ast.With, ast.AsyncWith)):
            for item in node.items:
                if archive_value(item.context_expr) and isinstance(item.optional_vars, ast.Name):
                    archive_targets.add(id(item.optional_vars))

    # Conservatively require every binding of a name to be a known archive.
    # Parameters always disqualify a name, including across function scopes.
    # This small check does not resolve scopes or follow arbitrary assignments.
    bindings = {}
    for node in ast.walk(tree):
        if isinstance(node, ast.Name) and isinstance(node.ctx, (ast.Store, ast.Del)):
            bindings.setdefault(node.id, []).append(id(node) in archive_targets)
        elif isinstance(node, ast.arg):
            bindings.setdefault(node.arg, []).append(False)
    archive_names = {name for name, values in bindings.items() if all(values)}

    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        name = qualified(node.func)
        method = name.rsplit('.', 1)[-1]
        kwargs = {k.arg: k.value for k in node.keywords}

        def argument(keyword, position, default=None):
            return kwargs.get(keyword, node.args[position] if len(node.args) > position else default)

        encoding_position = None
        if method in {'read_text', 'write_text'}:
            encoding_position = 0 if method == 'read_text' else 1
        elif method == 'open':
            receiver = node.func.value if isinstance(node.func, ast.Attribute) else None
            if (name.startswith(('tarfile.', 'zipfile.')) or archive_value(receiver) or
                    isinstance(receiver, ast.Name) and receiver.id in archive_names):
                continue
            # Path.open(mode, buffering, encoding), versus open(file, mode,
            # buffering, encoding) and codecs.open(filename, mode, encoding).
            file_open = name in {'open', 'builtins.open', 'io.open', 'codecs.open'}
            mode = argument('mode', 1 if file_open else 0, ast.Constant('r'))
            if isinstance(mode, ast.Constant) and isinstance(mode.value, str) and 'b' in mode.value:
                continue
            encoding_position = 2 if name == 'codecs.open' or not file_open else 3
        elif name.startswith('subprocess.') and any(
                isinstance(kwargs.get(k), ast.Constant) and kwargs[k].value is True
                for k in ('text', 'universal_newlines')):
            encoding_position = len(node.args)  # keyword-only
        if encoding_position is not None:
            encoding = argument('encoding', encoding_position)
            if encoding is None or isinstance(encoding, ast.Constant) and encoding.value is None:
                yield node.lineno, f'{name}: text I/O requires an explicit encoding'


def main():
    problems = []
    count = 0
    for path in python_files(ROOT):
        count += 1
        for line, message in violations(path.read_text(encoding='utf-8')):
            problems.append(f'{path.relative_to(ROOT)}:{line}: {message}')
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print(f'Python encoding check: OK ({count} files)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
