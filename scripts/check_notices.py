#!/usr/bin/env python3
"""Fail if a pinned third-party dependency is missing from THIRD_PARTY_NOTICES.md.

Every `FetchContent_Declare(<Name> ...)` in cmake/ZyronDependencies.cmake must be mentioned (case-insensitive,
whole word) in THIRD_PARTY_NOTICES.md. Exit codes: 0 ok, 1 missing notices, 2 input files not found.
Output is plain ASCII on purpose (Windows consoles may use a legacy code page).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

DEPENDENCIES_FILE = Path("cmake") / "ZyronDependencies.cmake"
NOTICES_FILE = Path("THIRD_PARTY_NOTICES.md")

_COMMENT = re.compile(r"#[^\n]*")
_DECLARE = re.compile(r"FetchContent_Declare\(\s*([A-Za-z0-9_+.-]+)")


def declared_dependencies(cmake_text: str) -> list[str]:
    """Names passed to FetchContent_Declare, in file order, ignoring CMake comments."""
    return _DECLARE.findall(_COMMENT.sub("", cmake_text))


def missing_notices(dependencies: list[str], notices_text: str) -> list[str]:
    """Dependencies that the notices file never mentions, each reported once, in input order."""
    missing: list[str] = []
    for name in dependencies:
        if name in missing:
            continue
        if not re.search(rf"(?<![A-Za-z0-9]){re.escape(name)}(?![A-Za-z0-9])", notices_text, re.IGNORECASE):
            missing.append(name)
    return missing


def main(root: Path) -> int:
    try:
        cmake_text = (root / DEPENDENCIES_FILE).read_text(encoding="utf-8")
        notices_text = (root / NOTICES_FILE).read_text(encoding="utf-8")
    except OSError as exc:
        print(f"check_notices: cannot read input: {exc}", file=sys.stderr)
        return 2

    dependencies = declared_dependencies(cmake_text)
    missing = missing_notices(dependencies, notices_text)
    if missing:
        print(f"{NOTICES_FILE} does not mention: {', '.join(missing)}. Add an entry in the same change that adds "
              "the dependency (licence, version, what it is used for, whether it ships in binaries).")
        return 1
    print(f"check_notices: {len(dependencies)} pinned dependenc{'y' if len(dependencies) == 1 else 'ies'} "
          f"covered by {NOTICES_FILE}")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(__file__).resolve().parent.parent))
