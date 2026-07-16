#!/usr/bin/env python3
"""Regenerate pinned Unicode artifacts from official sources and byte-compare."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from generate_unicode_nfc_data import checked_source, generate as generate_nfc
from generate_unicode_simple_case_folding import generate as generate_case_folding


ROOT = Path(__file__).resolve().parents[1]


def encoded(document: object) -> bytes:
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def require_equal(actual: bytes, expected_path: Path, label: str) -> None:
    if actual != expected_path.read_bytes():
        raise ValueError(f"{label} regeneration differs byte-for-byte from {expected_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-folding", type=Path, required=True)
    parser.add_argument("--unicode-data", type=Path, required=True)
    parser.add_argument("--composition-exclusions", type=Path, required=True)
    parser.add_argument("--normalization-test", type=Path, required=True)
    parser.add_argument("--contracts-root", type=Path, default=ROOT)
    args = parser.parse_args()
    require_equal(
        encoded(generate_case_folding(args.case_folding)),
        args.contracts_root / "unicode/simple-case-folding-17.0.0.json",
        "simple case-fold table",
    )
    require_equal(
        encoded(generate_nfc(args.unicode_data, args.composition_exclusions)),
        args.contracts_root / "unicode/nfc-normalization-17.0.0.json",
        "NFC data",
    )
    require_equal(
        checked_source(args.normalization_test, "NormalizationTest.txt"),
        args.contracts_root / "unicode/NormalizationTest-17.0.0.txt",
        "normalization conformance source",
    )
    print("Unicode 17.0.0 regeneration verification passed byte-for-byte")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
