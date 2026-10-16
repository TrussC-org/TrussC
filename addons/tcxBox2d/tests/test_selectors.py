"""Check selected runs fail on typos instead of silently running zero checks."""
import pathlib
import subprocess
import sys

binary = str(pathlib.Path(sys.argv[1]).resolve())
for group, known in (("--delivery", "router-gates"),
                     ("--review-round-1", "clear-update"),
                     ("--review-round-2", "owner-teardown")):
    for selector, expected in (("no-such-case", 1), (known, 0)):
        result = subprocess.run([binary, group, selector], capture_output=True, text=True)
        output = result.stdout + result.stderr
        assert result.returncode == expected, (group, selector, result.returncode, output)
        if expected:
            assert "unknown case: no-such-case" in output and "0 checks" in output, output
        else:
            assert "checks, 0 failures" in output, output
        print(f"{group} {selector}: exit {expected}")
result = subprocess.run([binary, "--no-such-group"], capture_output=True, text=True)
assert result.returncode == 1 and "unknown case" in result.stderr, result
print("unknown group: exit 1")
