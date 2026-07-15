#!/usr/bin/env python3
"""Generate the pinned Unicode 17.0.0 simple case-fold mapping artifact.

This is a non-normative authoring utility. Gate verification consumes only the
committed JSON table and never depends on a host Unicode data installation.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


UNICODE_VERSION = "17.0.0"
SOURCE_HEADER = f"# CaseFolding-{UNICODE_VERSION}.txt"
HEX_RE = re.compile(r"^[0-9A-F]{4,6}$")
STATUS_PRIORITY = {"C": 0, "S": 1}


def scalar(hex_value: str, location: str) -> int:
    if HEX_RE.fullmatch(hex_value) is None:
        raise ValueError(f"{location}: noncanonical code-point hex {hex_value!r}")
    value = int(hex_value, 16)
    if value > 0x10FFFF or 0xD800 <= value <= 0xDFFF:
        raise ValueError(f"{location}: not a Unicode scalar value")
    return value


def generate(input_path: Path) -> dict[str, object]:
    lines = input_path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != SOURCE_HEADER:
        raise ValueError(f"input must begin with exact {SOURCE_HEADER!r}")
    selected: dict[int, tuple[int, int]] = {}
    for line_number, line in enumerate(lines, 1):
        data = line.split("#", 1)[0].strip()
        if not data:
            continue
        fields = [field.strip() for field in data.split(";")]
        if fields and fields[-1] == "":
            fields.pop()
        if len(fields) != 3:
            raise ValueError(f"line {line_number}: expected three semicolon fields")
        source_hex, status, mapping_text = fields
        if status not in STATUS_PRIORITY:
            continue
        mapping = mapping_text.split()
        if len(mapping) != 1:
            raise ValueError(f"line {line_number}: C/S mapping must contain exactly one code point")
        source = scalar(source_hex, f"line {line_number} source")
        target = scalar(mapping[0], f"line {line_number} target")
        previous = selected.get(source)
        priority = STATUS_PRIORITY[status]
        if previous is None or priority > previous[0]:
            selected[source] = (priority, target)
        elif priority == previous[0] and target != previous[1]:
            raise ValueError(f"line {line_number}: conflicting {status} mapping for U+{source:04X}")
    mappings = [
        {"source": f"{source:04X}", "target": f"{selected[source][1]:04X}"}
        for source in sorted(selected)
    ]
    return {
        "schema": "ipcraft.unicode-simple-case-folding.v1",
        "unicodeVersion": UNICODE_VERSION,
        "source": "Unicode CaseFolding-17.0.0 C/S mappings",
        "sourceUrl": "https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt",
        "licenseName": "Unicode License V3",
        "licenseUrl": "https://www.unicode.org/license.txt",
        "mappings": mappings,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    document = generate(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"generated {len(document['mappings'])} Unicode {UNICODE_VERSION} simple case-fold mappings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
