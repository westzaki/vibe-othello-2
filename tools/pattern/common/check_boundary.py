#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


PRODUCTION_FILES = (
    "index_mode.h",
    "index_mode.cc",
    "schema_validation.h",
    "schema_validation.cc",
)

SMOKE_ONLY_TOKENS = (
    "tiny_fixture",
    "fixed_pattern_set_fixture",
    "symmetry_aware_fixed_pattern_set_fixture",
    "smoke_fixture",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--common-dir", required=True, type=Path)
    args = parser.parse_args()

    ok = True
    for relative in PRODUCTION_FILES:
        path = args.common_dir / relative
        text = path.read_text(encoding="utf-8")
        for token in SMOKE_ONLY_TOKENS:
            if token in text:
                print(f"{relative} must not depend on smoke-only token: {token}", file=sys.stderr)
                ok = False

    cmake = (args.common_dir / "CMakeLists.txt").read_text(encoding="utf-8")
    for target in ("vibe_othello_pattern_index_mode", "vibe_othello_pattern_schema_validation"):
        link_call = re.search(rf"target_link_libraries\({target}\s+([^)]+)\)", cmake, re.S)
        if link_call and "vibe_othello_pattern_smoke_fixture" in link_call.group(1):
            print(f"{target} must not link the smoke fixture target", file=sys.stderr)
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
