#!/usr/bin/env python3
"""Exercise the production MCP Stream adapter and real HTTP parser without sockets."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    compiler = os.environ.get("CXX") or shutil.which("c++")
    if not compiler:
        raise SystemExit("A C++ compiler is required for the MCP ingress test")
    source = (ROOT / "core/include/tc/utils/tcMCP.cpp").read_text(encoding="utf-8")
    begin = source.index("class McpIngressStream final")
    end = source.index("static thread_local McpIngressStream* activeIngressStream", begin)
    fixture = (ROOT / "tools/tests/mcp_ingress_stream.cpp").read_text(encoding="utf-8")
    code = fixture.replace("// PRODUCTION_INGRESS_STREAM", source[begin:end])
    with tempfile.TemporaryDirectory(prefix="tc-mcp-ingress-") as directory:
        cpp = Path(directory) / "main.cpp"
        binary = Path(directory) / "test"
        cpp.write_text(code, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-pthread", "-I", str(ROOT / "core/include"),
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
