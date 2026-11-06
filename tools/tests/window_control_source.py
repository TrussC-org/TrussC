"""Window control declarations for host probes, taken from the core headers.

The probes splice the production Window::control() body, so the WindowControl
enum and the Window-taking native operations it reads must match the headers.
Deriving them here means a new control or operation needs no probe edit.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def window_control_enum():
    """The `enum class WindowControl { ... };` declaration from tcWindow.h."""
    text = (ROOT / 'core/include/tc/app/tcWindow.h').read_text(encoding='utf-8')
    match = re.search(r'enum class WindowControl \{[^}]*\};', text)
    if not match:
        raise AssertionError('missing source marker: enum class WindowControl in tcWindow.h')
    return match.group(0)


def recording_operations():
    """`struct RecordingOperations` with the Window-taking members of WindowNativeOperations.

    Members that take sapp_window handles (metadata, swapchain, events) are left
    out: the probes stub the Window binding, not the native windowing layer.
    """
    text = (ROOT / 'core/include/tc/app/internal/tcWindowRuntime.h').read_text(encoding='utf-8')
    match = re.search(r'struct WindowNativeOperations \{\n(.*?)\n\};', text, re.S)
    if not match:
        raise AssertionError('missing source marker: struct WindowNativeOperations in tcWindowRuntime.h')
    members = [line for line in match.group(1).splitlines()
               if re.search(r'\(\*\w+\)\((const )?Window&', line)]
    if not members:
        raise AssertionError('WindowNativeOperations has no Window-taking members')
    return 'struct RecordingOperations {\n' + '\n'.join(members) + '\n};'


def with_window_declarations(source):
    """Fill the WINDOW_CONTROL_ENUM and RECORDING_OPERATIONS tokens of a probe."""
    for token in ('WINDOW_CONTROL_ENUM', 'RECORDING_OPERATIONS'):
        if token not in source:
            raise AssertionError(f'probe is missing the {token} token')
    return (source.replace('WINDOW_CONTROL_ENUM', window_control_enum())
                  .replace('RECORDING_OPERATIONS', recording_operations()))
