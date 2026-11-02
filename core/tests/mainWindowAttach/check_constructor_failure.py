"""The setup C callback must contain construction failure and exit with code 1."""
import subprocess
import sys
result = subprocess.run([*sys.argv[1:], "--constructor-failure"], check=False)
print("mainWindowAttach: constructor failure:", "PASS" if result.returncode == 1 else "FAIL")
sys.exit(0 if result.returncode == 1 else 1)
