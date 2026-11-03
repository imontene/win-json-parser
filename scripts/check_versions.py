#!/usr/bin/env python3
"""Checks that every place declaring the wjson version agrees.

Usage: check_versions.py [expected-version]

Prints the version and exits with 1 if any file disagrees (or if it differs
from expected-version, e.g. the release tag without the leading "v").
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def find(pattern: str, path: str) -> str:
    match = re.search(pattern, read(path))
    if not match:
        raise SystemExit(f"{path}: version not found")
    return match.group(1)


def main() -> int:
    versions = {
        "CMakeLists.txt": find(r"project\(wjson\s+VERSION\s+([0-9.]+)", "CMakeLists.txt"),
        "ports/wjson/vcpkg.json": json.loads(read("ports/wjson/vcpkg.json"))["version"],
        "bindings/dotnet/src/Wjson/Wjson.csproj": find(r"<Version>([^<]+)</Version>",
                                                         "bindings/dotnet/src/Wjson/Wjson.csproj"),
        "bindings/powershell/Wjson/Wjson.psd1": find(r"ModuleVersion\s*=\s*'([^']+)'",
                                                       "bindings/powershell/Wjson/Wjson.psd1"),
    }
    expected = sys.argv[1] if len(sys.argv) > 1 else versions["CMakeLists.txt"]
    ok = True
    for path, version in versions.items():
        mark = "ok" if version == expected else "MISMATCH"
        ok &= version == expected
        print(f"{mark:8} {version:10} {path}")
    if not ok:
        print(f"expected version {expected} everywhere", file=sys.stderr)
        return 1
    print(expected)
    return 0


if __name__ == "__main__":
    sys.exit(main())
