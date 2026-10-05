#!/usr/bin/env python3
"""Verify every i18n key the C++ sources ask for actually exists.

The generator (gen_i18n.py) bakes locales/en.json plus the per-locale overrides
into TranslationData.cpp. A typo in a `tr("...")` call therefore ships silently:
I18n::lookup() echoes the key back, so the UI shows `foo.bar` instead of text.
This script is the guard against that — run it from native/ (or via
`make check-keys`).

  python3 tools/check_i18n_keys.py            # check against ../../locales
  python3 tools/check_i18n_keys.py --locales /path/to/locales
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

KEY_RE = re.compile(r'\btr(?:_count)?\(\s*"([^"]+)"')
KEY_PARAM_RE = re.compile(r'"([^"]+)"\s*,\s*\{')


def flatten(prefix: str, value, out: dict) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            flatten(f"{prefix}{key}." if prefix else f"{key}.", child, out)
    else:
        out[prefix.rstrip(".")] = value


def load_locale(path: str) -> dict:
    table: dict = {}
    with open(path, "r", encoding="utf-8") as handle:
        flatten("", json.load(handle), table)
    return table


def source_files(root: str):
    for base in ("src", "include"):
        for directory, _, names in os.walk(os.path.join(root, base)):
            for name in names:
                if name.endswith((".cpp", ".h", ".hpp")):
                    yield os.path.join(directory, name)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    here = os.path.dirname(os.path.abspath(__file__))
    parser.add_argument("--locales", default=os.path.join(here, "..", "..", "locales"))
    parser.add_argument("--root", default=os.path.join(here, ".."))
    args = parser.parse_args()

    base_path = os.path.join(args.locales, "en.json")
    if not os.path.isfile(base_path):
        print(f"error: {base_path} not found (pass --locales)", file=sys.stderr)
        return 2
    base = load_locale(base_path)

    missing: dict[str, set[str]] = {}
    used: set[str] = set()
    for path in source_files(args.root):
        text = open(path, encoding="utf-8", errors="replace").read()
        for match in KEY_RE.finditer(text):
            key = match.group(1)
            used.add(key)
            if key not in base:
                missing.setdefault(key, set()).add(os.path.relpath(path, args.root))

    for key in sorted(missing):
        where = ", ".join(sorted(missing[key]))
        print(f"missing key: {key}   ({where})")

    # Also report keys the sources ask for that only exist in some locales.
    partial: list[str] = []
    for name in sorted(os.listdir(args.locales)):
        if not name.endswith(".json") or name == "en.json":
            continue
        table = load_locale(os.path.join(args.locales, name))
        for key in used:
            if key not in table:
                partial.append(f"{os.path.splitext(name)[0]}:{key}")

    print(f"{len(used)} keys referenced, {len(missing)} missing from en.json")
    if partial:
        print(f"note: {len(partial)} keys are not translated in every locale "
              f"(they fall back to English)")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
