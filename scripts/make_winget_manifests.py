#!/usr/bin/env python3
"""Generates winget manifests (schema 1.6) for a wjson release.

Usage:
  make_winget_manifests.py --version 1.0.0 --output manifests \\
      --installer x64=<url>=<zip file> --installer x86=<url>=<zip file> ...

Each installer is a zip that contains wjson.exe at its root; the SHA-256 is
computed from the local file. The output follows the layout of the
microsoft/winget-pkgs repository:
  manifests/i/imontene/wjson/<version>/imontene.wjson*.yaml
Submit it with `wingetcreate submit <folder>` or a pull request to
https://github.com/microsoft/winget-pkgs.
"""

import argparse
import hashlib
import sys
from pathlib import Path

IDENTIFIER = "imontene.wjson"
MANIFEST_VERSION = "1.6.0"
REPO = "https://github.com/imontene/win-json-parser"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--installer", action="append", required=True, metavar="ARCH=URL=FILE")
    args = parser.parse_args()

    installers = []
    for spec in args.installer:
        arch, url, file = spec.split("=", 2)
        if arch not in ("x64", "x86", "arm64"):
            parser.error(f"unknown architecture: {arch}")
        installers.append((arch, url, sha256(Path(file))))

    header = "# Created by scripts/make_winget_manifests.py\n"
    version_yaml = f"""{header}# yaml-language-server: $schema=https://aka.ms/winget-manifest.version.{MANIFEST_VERSION}.schema.json
PackageIdentifier: {IDENTIFIER}
PackageVersion: {args.version}
DefaultLocale: en-US
ManifestType: version
ManifestVersion: {MANIFEST_VERSION}
"""
    installer_entries = "".join(
        f"""  - Architecture: {arch}
    InstallerUrl: {url}
    InstallerSha256: {digest}
""" for arch, url, digest in installers)
    installer_yaml = f"""{header}# yaml-language-server: $schema=https://aka.ms/winget-manifest.installer.{MANIFEST_VERSION}.schema.json
PackageIdentifier: {IDENTIFIER}
PackageVersion: {args.version}
InstallerType: zip
NestedInstallerType: portable
NestedInstallerFiles:
  - RelativeFilePath: wjson.exe
    PortableCommandAlias: wjson
Commands:
  - wjson
Installers:
{installer_entries}ManifestType: installer
ManifestVersion: {MANIFEST_VERSION}
"""
    locale_yaml = f"""{header}# yaml-language-server: $schema=https://aka.ms/winget-manifest.defaultLocale.{MANIFEST_VERSION}.schema.json
PackageIdentifier: {IDENTIFIER}
PackageVersion: {args.version}
PackageLocale: en-US
Publisher: imontene
PublisherUrl: https://github.com/imontene
PackageName: wjson
PackageUrl: {REPO}
License: MIT
LicenseUrl: {REPO}/blob/main/LICENSE
ShortDescription: Fast, strict JSON validator, formatter and query tool.
Description: |-
  wjson validates, formats, minifies and queries JSON files from the command line.
  It reports errors with line and column, streams files of any size with constant memory,
  reads UTF-8 and UTF-16 files and supports JSONC (comments and trailing commas) and JSON Pointer.
Moniker: wjson
Tags:
  - json
  - jsonc
  - validator
  - formatter
  - cli
ReleaseNotesUrl: {REPO}/releases/tag/v{args.version}
ManifestType: defaultLocale
ManifestVersion: {MANIFEST_VERSION}
"""
    target = args.output / "manifests" / IDENTIFIER[0] / "imontene" / "wjson" / args.version
    target.mkdir(parents=True, exist_ok=True)
    (target / f"{IDENTIFIER}.yaml").write_text(version_yaml, encoding="utf-8")
    (target / f"{IDENTIFIER}.installer.yaml").write_text(installer_yaml, encoding="utf-8")
    (target / f"{IDENTIFIER}.locale.en-US.yaml").write_text(locale_yaml, encoding="utf-8")
    print(f"winget manifests written to {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
