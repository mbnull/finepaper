#!/usr/bin/env python3
"""Verify the closed Gate 0 standalone JSON fixture catalog.

This stdlib-only authoring check validates the catalog envelope and semantic
links. It deliberately does not validate fixture contents against JSON Schema;
the later executable fixture test owns that responsibility.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import unicodedata
from pathlib import Path, PurePosixPath
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CONTRACTS = ROOT / "docs" / "contracts"
UNICODE_TABLE_PATH = Path("unicode/simple-case-folding-17.0.0.json")
UNICODE_TABLE_FIELDS = {
    "schema", "unicodeVersion", "source", "sourceUrl", "licenseName", "licenseUrl", "mappings"
}
ENTRY_FIELDS = {
    "path", "schemaId", "validationPhase", "expected", "errorCode", "behaviorEvidence"
}
WINDOWS_RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    *(f"COM{number}" for number in range(1, 10)),
    *(f"LPT{number}" for number in range(1, 10)),
}
EVIDENCE_RE = re.compile(r"^vectors/([^/]+\.json)#([A-Za-z0-9][A-Za-z0-9._-]*)$")
CODE_POINT_RE = re.compile(r"^[0-9A-F]{4,6}$")


class VerificationError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise VerificationError(message)


def load_json(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot load {path}: {error}")


def require_exact_object(value: Any, fields: set[str], location: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{location} must be an object")
    if set(value) != fields:
        missing = sorted(fields - set(value))
        extra = sorted(set(value) - fields)
        fail(f"{location} fields mismatch; missing={missing}, extra={extra}")
    return value


def code_point(value: Any, location: str) -> int:
    if not isinstance(value, str) or CODE_POINT_RE.fullmatch(value) is None:
        fail(f"{location} must be canonical uppercase code-point hex")
    result = int(value, 16)
    if result > 0x10FFFF or 0xD800 <= result <= 0xDFFF or f"{result:04X}" != value:
        fail(f"{location} must encode exactly one Unicode scalar value")
    return result


def load_simple_case_folding_table(contracts: Path) -> dict[int, int]:
    location = str(contracts / UNICODE_TABLE_PATH)
    document = require_exact_object(load_json(contracts / UNICODE_TABLE_PATH), UNICODE_TABLE_FIELDS, location)
    expected_metadata = {
        "schema": "ipcraft.unicode-simple-case-folding.v1",
        "unicodeVersion": "17.0.0",
        "source": "Unicode CaseFolding-17.0.0 C/S mappings",
        "sourceUrl": "https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt",
        "licenseName": "Unicode License V3",
        "licenseUrl": "https://www.unicode.org/license.txt",
    }
    for field, expected in expected_metadata.items():
        if document[field] != expected:
            fail(f"{location}.{field} must be exactly {expected!r}")
    mappings = document["mappings"]
    if not isinstance(mappings, list) or not mappings:
        fail(f"{location}.mappings must be a non-empty sorted array")
    result: dict[int, int] = {}
    previous_source = -1
    for index, raw in enumerate(mappings):
        entry_location = f"{location}.mappings[{index}]"
        entry = require_exact_object(raw, {"source", "target"}, entry_location)
        source = code_point(entry["source"], f"{entry_location}.source")
        target = code_point(entry["target"], f"{entry_location}.target")
        if source <= previous_source:
            fail(f"{location}.mappings must have unique sources sorted by code point")
        previous_source = source
        result[source] = target
    return result


def simple_case_fold(text: str, mappings: dict[int, int]) -> str:
    """Fold with the committed Unicode 17.0.0 C/S mapping table only.

    Gate 0 production implementations must use this version-pinned machine
    source, never host Unicode data or full-fold/lowercase heuristics.
    """
    return "".join(chr(mappings.get(ord(character), ord(character))) for character in text)


def run_simple_case_fold_witnesses(mappings: dict[int, int]) -> None:
    required = {
        0x0041: 0x0061,
        0x03A3: 0x03C3,
        0x03C2: 0x03C3,
        0x1E9E: 0x00DF,
        0xFB05: 0xFB06,
    }
    for source, target in required.items():
        if mappings.get(source) != target:
            fail(f"simple-fold table missing required U+{source:04X} -> U+{target:04X} mapping")
    if simple_case_fold("Straße", mappings) != simple_case_fold("straße", mappings):
        fail("simple-fold self-check: ordinary case variants must collide")
    if simple_case_fold("straße", mappings) == simple_case_fold("strasse", mappings):
        fail("simple-fold self-check: sharp-s must not expand to ss")
    if simple_case_fold("ẞ", mappings) != "ß" or simple_case_fold("ß", mappings) != "ß":
        fail("simple-fold self-check: capital/lower sharp-s mapping is incorrect")
    if simple_case_fold("Σ", mappings) != simple_case_fold("ς", mappings) or simple_case_fold("ς", mappings) != "σ":
        fail("simple-fold self-check: Greek sigma variants must collide")
    if simple_case_fold("ﬅ", mappings) != simple_case_fold("ﬆ", mappings):
        fail("simple-fold self-check: U+FB05/U+FB06 must collide")
    if simple_case_fold("ﬃ", mappings) == simple_case_fold("ffi", mappings):
        fail("simple-fold self-check: multi-code-point full-fold expansions are forbidden")


def portable_path(value: Any, location: str) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{location} must be a non-empty string")
    if unicodedata.normalize("NFC", value) != value:
        fail(f"{location} must be NFC-normalized")
    if value.startswith("/") or "\\" in value or ":" in value:
        fail(f"{location} must be a portable relative POSIX path")
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in value):
        fail(f"{location} contains a control character")
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        fail(f"{location} contains an empty or dot path segment")
    if PurePosixPath(value).as_posix() != value:
        fail(f"{location} is not normalized")
    for part in parts:
        if part.endswith((" ", ".")):
            fail(f"{location} has a segment ending in dot or space")
        stem = part.split(".", 1)[0].upper()
        if stem in WINDOWS_RESERVED:
            fail(f"{location} uses reserved Windows device name {part!r}")
    if not value.endswith(".json"):
        fail(f"{location} must name a JSON file")
    return value


def catalog_ids(contracts: Path) -> set[str]:
    document = require_exact_object(
        load_json(contracts / "schema-catalog.json"), {"schema", "items"}, "schema catalog"
    )
    if document["schema"] != "ipcraft.contract-schema-catalog.v1":
        fail("schema catalog has the wrong schema identity")
    items = document["items"]
    if not isinstance(items, list):
        fail("schema catalog items must be an array")
    ids: list[str] = []
    paths: set[str] = set()
    for index, raw in enumerate(items):
        entry = require_exact_object(raw, {"id", "path", "freezeGate"}, f"schema catalog items[{index}]")
        if not all(isinstance(entry[key], str) and entry[key] for key in entry):
            fail(f"schema catalog items[{index}] members must be non-empty strings")
        if entry["id"] in ids or entry["path"] in paths:
            fail(f"schema catalog duplicate id/path at items[{index}]")
        schema_path = contracts / entry["path"]
        schema = load_json(schema_path)
        if not isinstance(schema, dict) or schema.get("$id") != entry["id"]:
            fail(f"schema catalog items[{index}] does not resolve to its exact $id")
        ids.append(entry["id"])
        paths.add(entry["path"])
    if ids != sorted(ids):
        fail("schema catalog items must be sorted by id")
    return set(ids)


def error_codes(contracts: Path) -> set[str]:
    document = require_exact_object(
        load_json(contracts / "error-codes-v1.json"), {"schema", "version", "codes"}, "error catalog"
    )
    if document["schema"] != "ipcraft.error-catalog.v1" or document["version"] != "1":
        fail("error catalog has the wrong identity/version")
    if not isinstance(document["codes"], list):
        fail("error catalog codes must be an array")
    result: set[str] = set()
    for index, raw in enumerate(document["codes"]):
        entry = require_exact_object(raw, {"code", "category"}, f"error catalog codes[{index}]")
        if not all(isinstance(entry[key], str) and entry[key] for key in entry):
            fail(f"error catalog codes[{index}] members must be non-empty strings")
        if entry["code"] in result:
            fail(f"duplicate error code {entry['code']!r}")
        result.add(entry["code"])
    return result


def behavior_case_ids(vector: Any, location: str) -> set[str]:
    if not isinstance(vector, dict):
        fail(f"{location} root must be an object")
    result: set[str] = set()
    for member, values in vector.items():
        if not isinstance(values, list):
            continue
        for index, entry in enumerate(values):
            if not isinstance(entry, dict):
                continue
            case_id = entry.get("id", entry.get("caseId"))
            if case_id is None:
                continue
            if not isinstance(case_id, str) or not case_id:
                fail(f"{location} {member}[{index}] has an invalid case ID")
            if case_id in result:
                fail(f"{location} has duplicate case ID {case_id!r}")
            result.add(case_id)
    return result


def verify_evidence(value: str, contracts: Path, location: str) -> None:
    match = EVIDENCE_RE.fullmatch(value)
    if match is None:
        fail(f"{location} must be vectors/<file>.json#<case-id>")
    vector_path = contracts / "vectors" / match.group(1)
    if not vector_path.is_file() or vector_path.is_symlink():
        fail(f"{location} references an unknown committed vector file")
    if match.group(2) not in behavior_case_ids(load_json(vector_path), str(vector_path)):
        fail(f"{location} references unknown exact case ID {match.group(2)!r}")


def physical_fixtures(contracts: Path) -> set[str]:
    root = contracts / "fixtures"
    if not root.exists():
        return set()
    if not root.is_dir() or root.is_symlink():
        fail("fixtures must be a physical directory")
    result: set[str] = set()
    for directory, names, files in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        for name in names:
            child = directory_path / name
            if child.is_symlink():
                fail(f"fixture context directories/symlinks are forbidden: {child.relative_to(contracts)}")
        for name in files:
            child = directory_path / name
            relative = child.relative_to(contracts).as_posix()
            if child.is_symlink() or not child.is_file():
                fail(f"fixture must be a regular JSON file: {relative}")
            if child.suffix != ".json":
                fail(f"fixture directories may contain only JSON documents: {relative}")
            result.add(portable_path(relative, f"physical fixture {relative}"))
    return result


def verify(catalog_path: Path, contracts: Path, allow_empty: bool = False) -> tuple[int, int, int]:
    document = require_exact_object(load_json(catalog_path), {"schema", "items"}, "fixture catalog")
    if document["schema"] != "ipcraft.fixture-catalog.v1":
        fail("fixture catalog has the wrong schema identity")
    items = document["items"]
    if not isinstance(items, list):
        fail("fixture catalog items must be an array")
    if not items and not allow_empty:
        fail("fixture catalog is empty; use --allow-empty only during Task 4A authoring")

    known_schemas = catalog_ids(contracts)
    known_errors = error_codes(contracts)
    folding_mappings = load_simple_case_folding_table(contracts)
    run_simple_case_fold_witnesses(folding_mappings)
    paths: list[str] = []
    casefold_paths: set[str] = set()
    for index, raw in enumerate(items):
        location = f"fixture catalog items[{index}]"
        entry = require_exact_object(raw, ENTRY_FIELDS, location)
        path = portable_path(entry["path"], f"{location}.path")
        collision_key = simple_case_fold(unicodedata.normalize("NFC", path), folding_mappings)
        if path in paths or collision_key in casefold_paths:
            fail(f"{location}.path is duplicate or portable-case-colliding")
        paths.append(path)
        casefold_paths.add(collision_key)

        schema_id = entry["schemaId"]
        if not isinstance(schema_id, str) or schema_id not in known_schemas:
            fail(f"{location}.schemaId does not resolve in the schema catalog")
        if schema_id == "ipcraft.fixture-catalog.v1":
            fail(f"{location}.schemaId may not target the V1 fixture catalog")
        if entry["validationPhase"] not in {"schema", "core-semantic"}:
            fail(f"{location}.validationPhase must be schema or core-semantic")

        expected = entry["expected"]
        error_code = entry["errorCode"]
        evidence = entry["behaviorEvidence"]
        if expected == "accept":
            if not path.startswith("fixtures/valid/"):
                fail(f"{location}.path prefix disagrees with expected=accept")
            if error_code is not None:
                fail(f"{location}.errorCode must be null for expected=accept")
            if evidence is not None:
                if not isinstance(evidence, str):
                    fail(f"{location}.behaviorEvidence must be a string or null")
                verify_evidence(evidence, contracts, f"{location}.behaviorEvidence")
        elif expected == "reject":
            if not path.startswith("fixtures/invalid/"):
                fail(f"{location}.path prefix disagrees with expected=reject")
            if not isinstance(error_code, str) or not error_code:
                fail(f"{location}.errorCode must be non-null for expected=reject")
            if evidence is not None:
                fail(f"{location}.behaviorEvidence is forbidden for expected=reject")
        else:
            fail(f"{location}.expected must be accept or reject")
        if error_code is not None and error_code not in known_errors:
            fail(f"{location}.errorCode does not resolve in the error catalog")

    if paths != sorted(paths):
        fail("fixture catalog items must be sorted by path")
    physical = physical_fixtures(contracts)
    catalogued = set(paths)
    if physical != catalogued:
        fail(
            "fixture physical totality mismatch; "
            f"missing={sorted(physical - catalogued)}, extra={sorted(catalogued - physical)}"
        )
    return len(items), len(known_schemas), len(known_errors)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contracts-root", type=Path, default=DEFAULT_CONTRACTS)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--allow-empty", action="store_true")
    args = parser.parse_args()
    contracts = args.contracts_root.resolve()
    catalog = args.catalog.resolve() if args.catalog else contracts / "fixture-catalog.json"
    try:
        fixture_count, schema_count, error_count = verify(catalog, contracts, args.allow_empty)
    except VerificationError as error:
        print(f"fixture catalog verification failed: {error}", file=sys.stderr)
        return 1
    print(
        f"fixture catalog verification passed: {fixture_count} fixtures, "
        f"{schema_count} schemas, {error_count} error codes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
