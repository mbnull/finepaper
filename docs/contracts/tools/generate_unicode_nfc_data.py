#!/usr/bin/env python3
"""Generate the pinned Unicode 17.0.0 NFC tables and conformance source."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

from rfc8785 import sha256_digest as rfc_sha256_digest


UNICODE_VERSION = "17.0.0"
SOURCE_SHA256 = {
    "UnicodeData.txt": "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c",
    "CompositionExclusions.txt": "2f239196ef3b5b61db5cc476e9bd80f534d15aa1b74e1be1dea5d042a344c85f",
    "NormalizationTest.txt": "5019ffd530751a741900c849c0e010332f142a3612234639bd200b82138a87db",
}
HEX_RE = re.compile(r"^[0-9A-F]{4,6}$")


def digest_json(value: object) -> str:
    return rfc_sha256_digest(value).removeprefix("sha256:")


def checked_source(path: Path, name: str) -> bytes:
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256[name]:
        raise ValueError(f"{name} does not match the pinned Unicode 17.0.0 digest")
    return data


def scalar(value: str) -> int:
    if HEX_RE.fullmatch(value) is None:
        raise ValueError(f"noncanonical code point {value!r}")
    result = int(value, 16)
    if result > 0x10FFFF or 0xD800 <= result <= 0xDFFF:
        raise ValueError(f"invalid scalar {value!r}")
    return result


def exclusions(path: Path) -> set[int]:
    result: set[int] = set()
    for line in checked_source(path, "CompositionExclusions.txt").decode().splitlines():
        data = line.split("#", 1)[0].strip()
        if data:
            result.add(scalar(data))
    return result


def generate(unicode_data: Path, composition_exclusions: Path) -> dict[str, object]:
    excluded = exclusions(composition_exclusions)
    decompositions: list[dict[str, object]] = []
    combining_classes: list[dict[str, object]] = []
    raw: dict[int, tuple[int, list[int]]] = {}
    for line_number, line in enumerate(checked_source(unicode_data, "UnicodeData.txt").decode().splitlines(), 1):
        fields = line.split(";")
        if len(fields) != 15:
            raise ValueError(f"UnicodeData line {line_number} does not have 15 fields")
        raw_point = int(fields[0], 16)
        if 0xD800 <= raw_point <= 0xDFFF:
            continue
        point = scalar(fields[0])
        ccc = int(fields[3])
        if ccc:
            combining_classes.append({"codePoint": f"{point:04X}", "class": ccc})
        decomposition = fields[5].split()
        if decomposition and not decomposition[0].startswith("<"):
            mapping = [scalar(value) for value in decomposition]
            decompositions.append({"codePoint": f"{point:04X}", "mapping": [f"{value:04X}" for value in mapping]})
            raw[point] = (ccc, mapping)
    classes = {int(entry["codePoint"], 16): int(entry["class"]) for entry in combining_classes}
    compositions = []
    for composite, (_, mapping) in sorted(raw.items()):
        if len(mapping) == 2 and classes.get(mapping[0], 0) == 0 and composite not in excluded:
            compositions.append({
                "starter": f"{mapping[0]:04X}", "combining": f"{mapping[1]:04X}",
                "composite": f"{composite:04X}",
            })
    return {
        "schema": "ipcraft.unicode-nfc-data.v1",
        "unicodeVersion": UNICODE_VERSION,
        "sources": [
            {"name": name, "url": f"https://www.unicode.org/Public/17.0.0/ucd/{name}", "sha256": digest}
            for name, digest in SOURCE_SHA256.items()
        ],
        "licenseName": "Unicode License V3",
        "licenseUrl": "https://www.unicode.org/license.txt",
        "licenseFile": "UNICODE-LICENSE.txt",
        "decompositionCount": len(decompositions),
        "decompositionsSha256": digest_json(decompositions),
        "combiningClassCount": len(combining_classes),
        "combiningClassesSha256": digest_json(combining_classes),
        "compositionCount": len(compositions),
        "compositionsSha256": digest_json(compositions),
        "decompositions": decompositions,
        "combiningClasses": combining_classes,
        "compositions": compositions,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unicode-data", type=Path, required=True)
    parser.add_argument("--composition-exclusions", type=Path, required=True)
    parser.add_argument("--normalization-test", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--conformance-output", type=Path, required=True)
    args = parser.parse_args()
    document = generate(args.unicode_data, args.composition_exclusions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    conformance = checked_source(args.normalization_test, "NormalizationTest.txt")
    args.conformance_output.write_bytes(conformance)
    print(
        f"generated Unicode {UNICODE_VERSION} NFC data: {document['decompositionCount']} decompositions, "
        f"{document['combiningClassCount']} combining classes, {document['compositionCount']} compositions"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
