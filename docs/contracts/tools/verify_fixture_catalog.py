#!/usr/bin/env python3
"""Verify the closed Gate 0 standalone JSON fixture catalog.

This stdlib-only authoring check validates the catalog envelope and semantic
links. It deliberately does not validate fixture contents against JSON Schema;
the later executable fixture test owns that responsibility.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CONTRACTS = ROOT / "docs" / "contracts"
UNICODE_CASE_FOLD_PATH = Path("unicode/simple-case-folding-17.0.0.json")
UNICODE_NFC_PATH = Path("unicode/nfc-normalization-17.0.0.json")
UNICODE_NORMALIZATION_TEST_PATH = Path("unicode/NormalizationTest-17.0.0.txt")
ERROR_POLICY_PATH = Path("fixture-error-policy-v1.json")
ERROR_POLICY_BOUNDARIES_SHA256 = "e9d3101858f0cc6459b243e0c6a1ce6ae161de23516f4c017b016b67cf205565"
ERROR_POLICY_RULES_SHA256 = "f42e7f6cfa3caedfb67f3b08d885fd24c2d18e35c6f3ddbd4e987ec8c3def7d2"
UNICODE_TABLE_FIELDS = {
    "schema", "unicodeVersion", "source", "sourceUrl", "sourceSha256", "licenseName", "licenseUrl",
    "licenseFile", "mappingCount", "mappingsSha256", "mappings"
}
ENTRY_FIELDS = {
    "path", "schemaId", "validationPhase", "failureBoundary", "expected", "errorCode", "behaviorEvidence"
}
WINDOWS_RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    *(f"COM{number}" for number in range(1, 10)),
    *(f"LPT{number}" for number in range(1, 10)),
}
EVIDENCE_RE = re.compile(r"^vectors/([^/]+\.json)#([A-Za-z0-9][A-Za-z0-9._-]*)$")
CODE_POINT_RE = re.compile(r"^[0-9A-F]{4,6}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
CASE_FOLD_SOURCE_SHA256 = "ff8d8fefbf123574205085d6714c36149eb946d717a0c585c27f0f4ef58c4183"
CASE_FOLD_MAPPING_COUNT = 1512
CASE_FOLD_MAPPINGS_SHA256 = "9696b3c0460d3cbab57dcd9baadc26ba24cf289f2e14ae2585063ca11eea3095"
NORMALIZATION_TEST_SHA256 = "5019ffd530751a741900c849c0e010332f142a3612234639bd200b82138a87db"
UNICODE_LICENSE_SHA256 = "e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96"
NFC_ARRAY_IDENTITIES = {
    "decompositions": (2081, "85fc2687c3fabab6bfe7f711374fdd35f92165d411b7e8da079ada27e590dc08"),
    "combiningClasses": (968, "e96a0ef8091026c67939f5037e1e8e3e4be2ec0ea5c7dae3db252fe06343c652"),
    "compositions": (961, "03965ad919c780b3b2d877b240d7f4148d23608fe13f47fe1a19726b7cde0f31"),
}
RECOGNIZED_VECTORS = {
    "core-canonical-projection-v1.json": ("ipcraft.core-canonical-vectors.v1", None, ("vectors",), "bf570f7a18cc99f58fb3d50a94ccc2598c9285a85976da0235ec3c82c95e4e44"),
    "core-set-permutation-v1.json": ("ipcraft.canonical-vector-catalog.v1", "collection-permutation", ("cases",), "0ae46f1968eb03807e013be14acc3340b5434d71b3dd31828ff22847ac799fb7"),
    "candidate-local-ref-v1.json": ("ipcraft.canonical-vector-catalog.v1", "candidate-causality", ("cases",), "8de0ba093da10867d8864bec4526c10db3474d354b70cb7dfbd5292777d712ef"),
    "default-engine-lock-v1.json": (
        "ipcraft.default-engine-behavior-vectors.v1", None,
        ("resolutionCases", "migrationCases", "freshnessCases"), "24fa76edfdbf48c26be68ae0b31247879f846c87d82479aa3bc01a4c6a5fffb9",
    ),
    "host-side-effects-v1.json": ("ipcraft.host-side-effect-behavior-vectors.v1", None, ("cases",), "6ca6cbbe2daff2589f86c13ed877e7accee39a1aa95390996ce7d7877fbb7c79"),
}


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


def canonical_digest(value: object) -> str:
    return hashlib.sha256(json.dumps(value, ensure_ascii=True, separators=(",", ":")).encode()).hexdigest()


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
    location = str(contracts / UNICODE_CASE_FOLD_PATH)
    document = require_exact_object(load_json(contracts / UNICODE_CASE_FOLD_PATH), UNICODE_TABLE_FIELDS, location)
    expected_metadata = {
        "schema": "ipcraft.unicode-simple-case-folding.v1",
        "unicodeVersion": "17.0.0",
        "source": "Unicode CaseFolding-17.0.0 C/S mappings",
        "sourceUrl": "https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt",
        "sourceSha256": CASE_FOLD_SOURCE_SHA256,
        "licenseName": "Unicode License V3",
        "licenseUrl": "https://www.unicode.org/license.txt",
        "licenseFile": "UNICODE-LICENSE.txt",
        "mappingCount": CASE_FOLD_MAPPING_COUNT,
        "mappingsSha256": CASE_FOLD_MAPPINGS_SHA256,
    }
    for field, expected in expected_metadata.items():
        if document[field] != expected:
            fail(f"{location}.{field} must be exactly {expected!r}")
    mappings = document["mappings"]
    if not isinstance(mappings, list) or len(mappings) != CASE_FOLD_MAPPING_COUNT:
        fail(f"{location}.mappings must contain exactly {CASE_FOLD_MAPPING_COUNT} sorted entries")
    if not isinstance(document["mappingsSha256"], str) or not SHA256_RE.fullmatch(document["mappingsSha256"]):
        fail(f"{location}.mappingsSha256 must be lowercase SHA-256")
    if canonical_digest(mappings) != CASE_FOLD_MAPPINGS_SHA256:
        fail(f"{location}.mappings does not match its pinned full-table digest")
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


def load_unicode_normalization_data(contracts: Path) -> tuple[dict[int, tuple[int, ...]], dict[int, int], dict[tuple[int, int], int]]:
    location = str(contracts / UNICODE_NFC_PATH)
    fields = {
        "schema", "unicodeVersion", "sources", "licenseName", "licenseUrl", "licenseFile",
        "decompositionCount", "decompositionsSha256", "combiningClassCount", "combiningClassesSha256",
        "compositionCount", "compositionsSha256", "decompositions", "combiningClasses", "compositions",
    }
    document = require_exact_object(load_json(contracts / UNICODE_NFC_PATH), fields, location)
    expected_sources = [
        ("UnicodeData.txt", "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c"),
        ("CompositionExclusions.txt", "2f239196ef3b5b61db5cc476e9bd80f534d15aa1b74e1be1dea5d042a344c85f"),
        ("NormalizationTest.txt", NORMALIZATION_TEST_SHA256),
    ]
    if document["schema"] != "ipcraft.unicode-nfc-data.v1" or document["unicodeVersion"] != "17.0.0":
        fail(f"{location} has the wrong identity/version")
    if (document["licenseName"] != "Unicode License V3" or document["licenseUrl"] != "https://www.unicode.org/license.txt"
            or document["licenseFile"] != "UNICODE-LICENSE.txt"):
        fail(f"{location} must reference the committed Unicode License V3 notice")
    license_path = contracts / "unicode" / document["licenseFile"]
    if not license_path.is_file() or hashlib.sha256(license_path.read_bytes()).hexdigest() != UNICODE_LICENSE_SHA256:
        fail(f"{location} license file is missing")
    sources = document["sources"]
    if not isinstance(sources, list) or len(sources) != len(expected_sources):
        fail(f"{location}.sources is not the pinned source set")
    for index, (name, digest) in enumerate(expected_sources):
        source = require_exact_object(sources[index], {"name", "url", "sha256"}, f"{location}.sources[{index}]")
        if source["name"] != name or source["sha256"] != digest or source["url"] != f"https://www.unicode.org/Public/17.0.0/ucd/{name}":
            fail(f"{location}.sources[{index}] is not the pinned {name} identity")
    arrays = (
        ("decompositions", "decompositionCount", "decompositionsSha256"),
        ("combiningClasses", "combiningClassCount", "combiningClassesSha256"),
        ("compositions", "compositionCount", "compositionsSha256"),
    )
    for member, count_member, digest_member in arrays:
        values = document[member]
        expected_count, expected_digest = NFC_ARRAY_IDENTITIES[member]
        if not isinstance(values, list) or document[count_member] != expected_count or len(values) != expected_count:
            fail(f"{location}.{member} count mismatch")
        if document[digest_member] != expected_digest or canonical_digest(values) != expected_digest:
            fail(f"{location}.{member} full-table digest mismatch")
    decompositions: dict[int, tuple[int, ...]] = {}
    for index, raw in enumerate(document["decompositions"]):
        entry = require_exact_object(raw, {"codePoint", "mapping"}, f"{location}.decompositions[{index}]")
        point = code_point(entry["codePoint"], f"{location}.decompositions[{index}].codePoint")
        if point in decompositions or not isinstance(entry["mapping"], list) or not entry["mapping"]:
            fail(f"{location}.decompositions must be unique and non-empty")
        decompositions[point] = tuple(code_point(value, f"{location}.decompositions[{index}].mapping") for value in entry["mapping"])
    combining: dict[int, int] = {}
    for index, raw in enumerate(document["combiningClasses"]):
        entry = require_exact_object(raw, {"codePoint", "class"}, f"{location}.combiningClasses[{index}]")
        point = code_point(entry["codePoint"], f"{location}.combiningClasses[{index}].codePoint")
        if point in combining or not isinstance(entry["class"], int) or not 1 <= entry["class"] <= 255:
            fail(f"{location}.combiningClasses has invalid or duplicate entry")
        combining[point] = entry["class"]
    compositions: dict[tuple[int, int], int] = {}
    for index, raw in enumerate(document["compositions"]):
        entry = require_exact_object(raw, {"starter", "combining", "composite"}, f"{location}.compositions[{index}]")
        key = (code_point(entry["starter"], f"{location}.compositions[{index}].starter"),
               code_point(entry["combining"], f"{location}.compositions[{index}].combining"))
        if key in compositions:
            fail(f"{location}.compositions has a duplicate pair")
        compositions[key] = code_point(entry["composite"], f"{location}.compositions[{index}].composite")
    return decompositions, combining, compositions


def nfc_normalize(text: str, tables: tuple[dict[int, tuple[int, ...]], dict[int, int], dict[tuple[int, int], int]]) -> str:
    decompositions, combining_classes, compositions = tables
    s_base, l_base, v_base, t_base = 0xAC00, 0x1100, 0x1161, 0x11A7
    l_count, v_count, t_count = 19, 21, 28
    n_count, s_count = v_count * t_count, l_count * v_count * t_count

    def decompose(point: int, out: list[int]) -> None:
        s_index = point - s_base
        if 0 <= s_index < s_count:
            out.extend((l_base + s_index // n_count, v_base + (s_index % n_count) // t_count))
            tail = s_index % t_count
            if tail:
                out.append(t_base + tail)
            return
        mapping = decompositions.get(point)
        if mapping is None:
            out.append(point)
        else:
            for child in mapping:
                decompose(child, out)

    ordered: list[int] = []
    for character in text:
        expanded: list[int] = []
        decompose(ord(character), expanded)
        for value in expanded:
            ordered.append(value)
            index = len(ordered) - 1
            ccc = combining_classes.get(value, 0)
            cursor = index
            while cursor > 0 and ccc and combining_classes.get(ordered[cursor - 1], 0) > ccc:
                ordered[cursor] = ordered[cursor - 1]
                cursor -= 1
            ordered[cursor] = value
    if not ordered:
        return ""
    result = [ordered[0]]
    starter_pos = 0
    starter = ordered[0]
    last_ccc = 0
    for point in ordered[1:]:
        composite = None
        l_index, v_index = starter - l_base, point - v_base
        if 0 <= l_index < l_count and 0 <= v_index < v_count:
            composite = s_base + (l_index * v_count + v_index) * t_count
        else:
            s_index, t_index = starter - s_base, point - t_base
            if 0 <= s_index < s_count and s_index % t_count == 0 and 1 <= t_index < t_count:
                composite = starter + t_index
            else:
                composite = compositions.get((starter, point))
        ccc = combining_classes.get(point, 0)
        if composite is not None and (last_ccc < ccc or last_ccc == 0):
            result[starter_pos] = composite
            starter = composite
        else:
            if ccc == 0:
                starter_pos = len(result)
                starter = point
            result.append(point)
            last_ccc = ccc
    return "".join(chr(point) for point in result)


def verify_normalization_conformance(contracts: Path, tables: tuple[dict[int, tuple[int, ...]], dict[int, int], dict[tuple[int, int], int]]) -> None:
    path = contracts / UNICODE_NORMALIZATION_TEST_PATH
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != NORMALIZATION_TEST_SHA256:
        fail(f"{path} does not match the official Unicode 17.0.0 normalization conformance source")
    for line_number, line in enumerate(data.decode("utf-8").splitlines(), 1):
        body = line.split("#", 1)[0].strip()
        if not body or body.startswith("@"): continue
        fields = [part.strip() for part in body.split(";")]
        if len(fields) < 5: fail(f"{path}:{line_number} malformed conformance row")
        columns = ["".join(chr(int(value, 16)) for value in field.split()) for field in fields[:5]]
        c1, c2, c3, c4, c5 = columns
        for source, expected in ((c1,c2),(c2,c2),(c3,c2),(c4,c4),(c5,c4)):
            if nfc_normalize(source, tables) != expected:
                fail(f"Unicode 17 NFC conformance failed at {path}:{line_number}")


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


def portable_path(value: Any, location: str, normalization_tables: tuple[dict[int, tuple[int, ...]], dict[int, int], dict[tuple[int, int], int]]) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{location} must be a non-empty string")
    if nfc_normalize(value, normalization_tables) != value:
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
        stem = "".join(chr(ord(character) - 32) if "a" <= character <= "z" else character
                       for character in part.split(".", 1)[0])
        if stem in WINDOWS_RESERVED:
            fail(f"{location} uses reserved Windows device name {part!r}")
    if not value.endswith(".json"):
        fail(f"{location} must name a JSON file")
    return value


def catalog_ids(contracts: Path, normalization_tables=None, folding_mappings=None) -> set[str]:
    if normalization_tables is None:
        normalization_tables = load_unicode_normalization_data(contracts)
    if folding_mappings is None:
        folding_mappings = load_simple_case_folding_table(contracts)
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
    casefold_paths: set[str] = set()
    for index, raw in enumerate(items):
        entry = require_exact_object(raw, {"id", "path", "freezeGate"}, f"schema catalog items[{index}]")
        if not all(isinstance(entry[key], str) and entry[key] for key in entry):
            fail(f"schema catalog items[{index}] members must be non-empty strings")
        if entry["id"] in ids or entry["path"] in paths:
            fail(f"schema catalog duplicate id/path at items[{index}]")
        catalog_path = portable_path(entry["path"], f"schema catalog items[{index}].path", normalization_tables)
        collision_key = simple_case_fold(nfc_normalize(catalog_path, normalization_tables), folding_mappings)
        if collision_key in casefold_paths:
            fail(f"schema catalog items[{index}].path is portable-case-colliding")
        if (len(PurePosixPath(catalog_path).parts) != 2 or not catalog_path.startswith("schemas/")
                or not catalog_path.endswith(".schema.json")):
            fail(f"schema catalog items[{index}].path must match schemas/*.schema.json")
        schema_path = contracts / catalog_path
        schema_root_path = contracts / "schemas"
        schema_root = schema_root_path.resolve()
        relative_parts = Path(catalog_path).parts[1:]
        traversed = schema_root_path
        has_symlink_component = schema_root_path.is_symlink()
        for part in relative_parts:
            traversed /= part
            has_symlink_component = has_symlink_component or traversed.is_symlink()
        if has_symlink_component or not schema_root_path.is_dir() or not schema_path.is_file() or not schema_path.resolve().is_relative_to(schema_root):
            fail(f"schema catalog items[{index}].path must resolve to a regular non-symlink inside schemas/")
        schema = load_json(schema_path)
        if not isinstance(schema, dict) or schema.get("$id") != entry["id"]:
            fail(f"schema catalog items[{index}] does not resolve to its exact $id")
        ids.append(entry["id"])
        paths.add(entry["path"])
        casefold_paths.add(collision_key)
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


def behavior_case_ids(vector: Any, filename: str, location: str) -> set[str]:
    expected = RECOGNIZED_VECTORS.get(filename)
    if expected is None:
        fail(f"{location} is not in the closed recognized vector catalog")
    if not isinstance(vector, dict) or vector.get("schema") != expected[0]:
        fail(f"{location} has the wrong recognized vector envelope")
    if expected[1] is not None and vector.get("kind") != expected[1]:
        fail(f"{location} has the wrong recognized vector kind")
    result: set[str] = set()
    for member in expected[2]:
        values = vector.get(member)
        if not isinstance(values, list): fail(f"{location}.{member} must be the recognized case array")
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
    filename = match.group(1)
    if filename not in RECOGNIZED_VECTORS:
        fail(f"{location} references a file outside the closed vector catalog")
    vector_path = contracts / "vectors" / filename
    if not vector_path.is_file() or vector_path.is_symlink():
        fail(f"{location} references an unknown committed vector file")
    if hashlib.sha256(vector_path.read_bytes()).hexdigest() != RECOGNIZED_VECTORS[filename][3]:
        fail(f"{location} references a recognized vector file whose frozen content digest does not match")
    if match.group(2) not in behavior_case_ids(load_json(vector_path), filename, str(vector_path)):
        fail(f"{location} references unknown exact case ID {match.group(2)!r}")


def physical_fixtures(contracts: Path, normalization_tables) -> set[str]:
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
            result.add(portable_path(relative, f"physical fixture {relative}", normalization_tables))
    return result


def load_error_policy(contracts: Path) -> dict[tuple[str, str, str], set[str]]:
    document = require_exact_object(
        load_json(contracts / ERROR_POLICY_PATH),
        {"schema", "version", "boundariesSha256", "rulesSha256", "boundaries", "rules"}, "error policy"
    )
    if document["schema"] != "ipcraft.fixture-error-policy.v1" or document["version"] != "1":
        fail("error policy has the wrong identity/version")
    if (document["boundariesSha256"] != ERROR_POLICY_BOUNDARIES_SHA256
            or canonical_digest(document["boundaries"]) != ERROR_POLICY_BOUNDARIES_SHA256):
        fail("error policy boundaries do not match the frozen Gate 0 mapping")
    if document["rulesSha256"] != ERROR_POLICY_RULES_SHA256 or canonical_digest(document["rules"]) != ERROR_POLICY_RULES_SHA256:
        fail("error policy rules do not match the frozen Gate 0 mapping")
    boundary_codes = {}
    for index, raw in enumerate(document["boundaries"]):
        entry = require_exact_object(raw, {"failureBoundary", "errorCode"}, f"error policy boundaries[{index}]")
        boundary = entry["failureBoundary"]
        code = entry["errorCode"]
        if not isinstance(boundary, str) or not boundary or boundary in boundary_codes or not isinstance(code, str) or not code:
            fail(f"error policy boundaries[{index}] must be a unique non-empty boundary/code pair")
        boundary_codes[boundary] = code
    if list(boundary_codes) != sorted(boundary_codes):
        fail("error policy boundaries must be sorted by failureBoundary")
    fixture_schema = load_json(contracts / "schemas/ipcraft.fixture-catalog.v1.schema.json")
    try:
        schema_boundaries = fixture_schema["$defs"]["failureBoundary"]["enum"]
    except (KeyError, TypeError):
        fail("fixture schema does not expose the closed failureBoundary enum")
    if not isinstance(schema_boundaries, list) or schema_boundaries != sorted(boundary_codes):
        fail("fixture schema failureBoundary enum differs from the frozen error policy")
    known_errors = error_codes(contracts)
    unknown_codes = sorted(set(boundary_codes.values()) - known_errors)
    if unknown_codes:
        fail(f"error policy boundaries reference unknown stable error codes {unknown_codes}")
    result: dict[tuple[str, str, str], set[str]] = {}
    for index, raw in enumerate(document["rules"]):
        entry = require_exact_object(raw, {"schemaId", "validationPhase", "allowedFailureBoundaries"}, f"error policy rules[{index}]")
        pair = (entry["schemaId"], entry["validationPhase"])
        boundaries = entry["allowedFailureBoundaries"]
        if not isinstance(boundaries, list) or not boundaries or boundaries != sorted(set(boundaries)):
            fail(f"error policy rules[{index}] must contain sorted unique failure boundaries")
        if any(key[:2] == pair for key in result):
            fail(f"error policy rules[{index}] duplicates schema/phase")
        for boundary in boundaries:
            if boundary not in boundary_codes:
                fail(f"error policy rules[{index}] references unknown failure boundary {boundary!r}")
            result[(pair[0], pair[1], boundary)] = {boundary_codes[boundary]}
    schema_catalog = load_json(contracts / "schema-catalog.json")
    catalog_ids_for_policy = {
        entry["id"] for entry in schema_catalog.get("items", [])
        if isinstance(entry, dict) and isinstance(entry.get("id"), str) and entry["id"] != "ipcraft.fixture-catalog.v1"
    }
    expected_pairs = {(schema_id, phase) for schema_id in catalog_ids_for_policy for phase in ("schema", "core-semantic")}
    actual_pairs = {key[:2] for key in result}
    if actual_pairs != expected_pairs:
        fail(f"error policy schema/phase coverage mismatch; missing={sorted(expected_pairs-actual_pairs)}, extra={sorted(actual_pairs-expected_pairs)}")
    return result


def verify_error_policy(schema_id: str, phase: str, boundary: str, code: str,
                        policy: dict[tuple[str, str, str], set[str]], location: str) -> None:
    allowed = policy.get((schema_id, phase, boundary))
    if allowed is None or code not in allowed:
        fail(f"{location}.errorCode {code!r} is not allowed for ({schema_id}, {phase}, {boundary})")


def verify(catalog_path: Path, contracts: Path, allow_empty: bool = False) -> tuple[int, int, int]:
    document = require_exact_object(load_json(catalog_path), {"schema", "items"}, "fixture catalog")
    if document["schema"] != "ipcraft.fixture-catalog.v1":
        fail("fixture catalog has the wrong schema identity")
    items = document["items"]
    if not isinstance(items, list):
        fail("fixture catalog items must be an array")
    if not items and not allow_empty:
        fail("fixture catalog is empty; use --allow-empty only during Task 4A authoring")

    normalization_tables = load_unicode_normalization_data(contracts)
    verify_normalization_conformance(contracts, normalization_tables)
    folding_mappings = load_simple_case_folding_table(contracts)
    run_simple_case_fold_witnesses(folding_mappings)
    known_schemas = catalog_ids(contracts, normalization_tables, folding_mappings)
    known_errors = error_codes(contracts)
    error_policy = load_error_policy(contracts)
    paths: list[str] = []
    casefold_paths: set[str] = set()
    for index, raw in enumerate(items):
        location = f"fixture catalog items[{index}]"
        entry = require_exact_object(raw, ENTRY_FIELDS, location)
        path = portable_path(entry["path"], f"{location}.path", normalization_tables)
        collision_key = simple_case_fold(nfc_normalize(path, normalization_tables), folding_mappings)
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
        failure_boundary = entry["failureBoundary"]
        error_code = entry["errorCode"]
        evidence = entry["behaviorEvidence"]
        if expected == "accept":
            if not path.startswith("fixtures/valid/"):
                fail(f"{location}.path prefix disagrees with expected=accept")
            if error_code is not None:
                fail(f"{location}.errorCode must be null for expected=accept")
            if failure_boundary is not None:
                fail(f"{location}.failureBoundary must be null for expected=accept")
            if evidence is not None:
                if not isinstance(evidence, str):
                    fail(f"{location}.behaviorEvidence must be a string or null")
                verify_evidence(evidence, contracts, f"{location}.behaviorEvidence")
        elif expected == "reject":
            if not path.startswith("fixtures/invalid/"):
                fail(f"{location}.path prefix disagrees with expected=reject")
            if not isinstance(error_code, str) or not error_code:
                fail(f"{location}.errorCode must be non-null for expected=reject")
            if not isinstance(failure_boundary, str) or not failure_boundary:
                fail(f"{location}.failureBoundary must be non-null for expected=reject")
            if evidence is not None:
                fail(f"{location}.behaviorEvidence is forbidden for expected=reject")
        else:
            fail(f"{location}.expected must be accept or reject")
        if error_code is not None and error_code not in known_errors:
            fail(f"{location}.errorCode does not resolve in the error catalog")
        if error_code is not None:
            verify_error_policy(schema_id, entry["validationPhase"], failure_boundary, error_code, error_policy, location)

    if paths != sorted(paths):
        fail("fixture catalog items must be sorted by path")
    physical = physical_fixtures(contracts, normalization_tables)
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
