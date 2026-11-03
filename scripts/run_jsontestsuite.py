#!/usr/bin/env python3
"""Runs the wjson CLI against JSONTestSuite (https://github.com/nst/JSONTestSuite).

Every y_*.json file must be accepted (exit code 0) and every n_*.json file must
be rejected cleanly (exit code 1). i_*.json files are implementation-defined:
they are reported but never fail the run, as long as wjson does not crash.

Usage: run_jsontestsuite.py <path-to-wjson-executable> <JSONTestSuite/test_parsing>
"""

import subprocess
import sys
from pathlib import Path

TIMEOUT_SECONDS = 10


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    wjson = Path(sys.argv[1])
    suite = Path(sys.argv[2])
    files = sorted(suite.glob("*.json"))
    if not files:
        print(f"no test files found in {suite}", file=sys.stderr)
        return 2

    failures = []
    implementation_defined = {"accepted": 0, "rejected": 0}
    for path in files:
        try:
            proc = subprocess.run(
                [str(wjson), "validate", str(path)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=TIMEOUT_SECONDS,
            )
            code = proc.returncode
        except subprocess.TimeoutExpired:
            failures.append((path.name, "timeout"))
            continue

        kind = path.name[0]
        if code not in (0, 1):
            failures.append((path.name, f"crashed or I/O error (exit code {code})"))
        elif kind == "y" and code != 0:
            failures.append((path.name, "valid JSON was rejected"))
        elif kind == "n" and code != 1:
            failures.append((path.name, "invalid JSON was accepted"))
        elif kind == "i":
            implementation_defined["accepted" if code == 0 else "rejected"] += 1

    print(f"JSONTestSuite: {len(files)} files, {len(failures)} failures")
    print(
        "implementation-defined (i_): "
        f"{implementation_defined['accepted']} accepted, "
        f"{implementation_defined['rejected']} rejected"
    )
    for name, reason in failures:
        print(f"  FAIL {name}: {reason}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
