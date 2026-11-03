#!/usr/bin/env python3
"""Downloads the standard JSON benchmark files into a directory.

The files come from a pinned commit of
https://github.com/miloyip/nativejson-benchmark and are checked by SHA-256:

  twitter.json       social-network data, lots of Unicode strings
  canada.json        GeoJSON, dominated by floating-point numbers
  citm_catalog.json  deeply nested objects, many integers and keys

Usage: fetch_benchmark_data.py <output-directory>
"""

import hashlib
import sys
import urllib.request
from pathlib import Path

COMMIT = "478d5727c2a4048e835a29c65adecc7d795360d5"
BASE_URL = f"https://raw.githubusercontent.com/miloyip/nativejson-benchmark/{COMMIT}/data/"
FILES = {
    "twitter.json": "a08b769f32b95f426cbc3abafcec65c1a19d3eb544d4ddf320eae142c99efc5d",
    "canada.json": "f83b3b354030d5dd58740c68ac4fecef64cb730a0d12a90362a7f23077f50d78",
    "citm_catalog.json": "a73e7a883f6ea8de113dff59702975e60119b4b58d451d518a929f31c92e2059",
}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, expected in FILES.items():
        target = out / name
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == expected:
            print(f"{name}: already present")
            continue
        with urllib.request.urlopen(BASE_URL + name, timeout=60) as response:
            data = response.read()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            print(f"{name}: SHA-256 mismatch (got {actual})", file=sys.stderr)
            return 1
        target.write_bytes(data)
        print(f"{name}: downloaded ({len(data) / 1e6:.2f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
