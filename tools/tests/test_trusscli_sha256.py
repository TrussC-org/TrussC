"""Check the release verifier against standard SHA-256 vectors and block boundaries."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Sha256Tests(unittest.TestCase):
    def test_vectors(self):
        with tempfile.TemporaryDirectory(prefix="trusscli-sha256-") as temp:
            base = Path(temp)
            source = base / "main.cpp"
            source.write_text('#include "Sha256.h"\n#include <iostream>\n'
                              'int main() { std::cout << sha256(std::cin); }\n', encoding="utf-8")
            binary = base / ("hash.exe" if os.name == "nt" else "hash")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-I", str(ROOT / "tools/src"),
                            str(source), str(ROOT / "tools/src/Sha256.cpp"), "-o", str(binary)], check=True)
            vectors = [(b"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
                       (b"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")]
            vectors += [(b"a" * n, hashlib.sha256(b"a" * n).hexdigest()) for n in (55, 56, 63, 64, 65, 1000000)]
            for data, expected in vectors:
                with self.subTest(length=len(data)):
                    result = subprocess.run([str(binary)], input=data, capture_output=True, check=True)
                    self.assertEqual(result.stdout.decode(), expected)


if __name__ == "__main__":
    unittest.main()
