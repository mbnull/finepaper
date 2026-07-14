#!/usr/bin/env python3
"""Verify the Gate 0 canonical collection rule index.

This authoring check is read-only and uses only the Python standard library.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
CONTRACTS = ROOT / "docs" / "contracts"
CATALOG_PATH = CONTRACTS / "schema-catalog.json"
VECTOR_PATH = CONTRACTS / "vectors" / "core-canonical-projection-v1.json"
CANONICAL_KEY = "x-ipcraft-canonical"
VALID_KINDS = {"set", "ordered", "derived-ordered"}


class VerificationError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise VerificationError(message)


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot load {path.relative_to(ROOT)}: {error}")


def escape_pointer_token(token: str) -> str:
    return token.replace("~", "~0").replace("/", "~1")


def resolve_pointer(document: Any, pointer: str) -> Any:
    if pointer == "":
        return document
    if not pointer.startswith("/"):
        fail(f"invalid RFC 6901 pointer {pointer!r}")
    current = document
    for raw_token in pointer[1:].split("/"):
        token = raw_token.replace("~1", "/").replace("~0", "~")
        try:
            current = current[int(token)] if isinstance(current, list) else current[token]
        except (KeyError, IndexError, ValueError, TypeError):
            fail(f"JSON Pointer {pointer!r} does not resolve")
    return current


def validate_metadata(metadata: Any, location: str) -> dict[str, Any]:
    if not isinstance(metadata, dict):
        fail(f"{location}: {CANONICAL_KEY} must be an object")
    if set(metadata) - {"kind", "sortKey"}:
        fail(f"{location}: unknown {CANONICAL_KEY} members {sorted(set(metadata) - {'kind', 'sortKey'})}")
    kind = metadata.get("kind")
    if kind not in VALID_KINDS:
        fail(f"{location}: invalid canonical kind {kind!r}")
    sort_key = metadata.get("sortKey")
    if kind == "ordered":
        if "sortKey" in metadata:
            fail(f"{location}: ordered collections must omit sortKey")
    else:
        if not isinstance(sort_key, list) or not sort_key or not all(isinstance(item, str) and item for item in sort_key):
            fail(f"{location}: {kind} collections require a non-empty string sortKey")
    return metadata


class SchemaGraph:
    def __init__(self) -> None:
        catalog = load_json(CATALOG_PATH)
        items = catalog.get("items")
        if not isinstance(items, list):
            fail("schema catalog items must be an array")
        self.documents: dict[str, dict[str, Any]] = {}
        self.file_to_id: dict[str, str] = {}
        self.root_ids: list[str] = []
        for item in items:
            schema_id = item.get("id")
            relative_path = item.get("path")
            if not isinstance(schema_id, str) or not isinstance(relative_path, str):
                fail("schema catalog entries require string id/path")
            path = CONTRACTS / relative_path
            document = load_json(path)
            if document.get("$id") != schema_id:
                fail(f"{relative_path}: $id does not match catalog id {schema_id}")
            if schema_id in self.documents or path.name in self.file_to_id:
                fail(f"duplicate schema catalog id or filename for {schema_id}")
            self.documents[schema_id] = document
            self.file_to_id[path.name] = schema_id
            self.root_ids.append(schema_id)

    def validate_all_custom_keywords(self) -> None:
        def walk(schema_id: str, value: Any, pointer: str) -> None:
            if isinstance(value, dict):
                if CANONICAL_KEY in value:
                    if value.get("type") != "array":
                        fail(f"{schema_id}#{pointer}: {CANONICAL_KEY} is only valid on explicit array schemas")
                    validate_metadata(value[CANONICAL_KEY], f"{schema_id}#{pointer}")
                for key, child in value.items():
                    walk(schema_id, child, f"{pointer}/{escape_pointer_token(key)}")
            elif isinstance(value, list):
                for index, child in enumerate(value):
                    walk(schema_id, child, f"{pointer}/{index}")

        for schema_id, document in self.documents.items():
            walk(schema_id, document, "")

    def resolve_ref(self, current_schema_id: str, ref: str) -> tuple[str, str, Any]:
        file_part, separator, fragment = ref.partition("#")
        if not separator:
            fragment = ""
        if file_part:
            filename = Path(file_part).name
            if filename not in self.file_to_id:
                fail(f"{current_schema_id}: unresolved external $ref {ref!r}")
            target_schema_id = self.file_to_id[filename]
        else:
            target_schema_id = current_schema_id
        target = resolve_pointer(self.documents[target_schema_id], fragment)
        return target_schema_id, fragment, target

    def reachable_arrays(self) -> dict[tuple[str, str], dict[str, Any]]:
        visited: set[tuple[str, str]] = set()
        arrays: dict[tuple[str, str], dict[str, Any]] = {}

        def walk(schema_id: str, node: Any, pointer: str) -> None:
            location = (schema_id, pointer)
            if location in visited or not isinstance(node, dict):
                return
            visited.add(location)
            if node.get("type") == "array":
                if CANONICAL_KEY not in node:
                    fail(f"{schema_id}#{pointer}: reachable explicit array lacks {CANONICAL_KEY}")
                arrays[location] = validate_metadata(node[CANONICAL_KEY], f"{schema_id}#{pointer}")
            elif CANONICAL_KEY in node:
                fail(f"{schema_id}#{pointer}: {CANONICAL_KEY} is only valid on explicit array schemas")

            ref = node.get("$ref")
            if ref is not None:
                if not isinstance(ref, str):
                    fail(f"{schema_id}#{pointer}: $ref must be a string")
                target_id, target_pointer, target = self.resolve_ref(schema_id, ref)
                walk(target_id, target, target_pointer)

            for key, child in node.items():
                if key in {"$ref", "$defs", "definitions", CANONICAL_KEY}:
                    continue
                child_pointer = f"{pointer}/{escape_pointer_token(key)}"
                if isinstance(child, dict):
                    walk(schema_id, child, child_pointer)
                elif isinstance(child, list):
                    for index, item in enumerate(child):
                        if isinstance(item, dict):
                            walk(schema_id, item, f"{child_pointer}/{index}")

        for schema_id in self.root_ids:
            walk(schema_id, self.documents[schema_id], "")
        return arrays


def validate_rule_table(graph: SchemaGraph) -> tuple[dict[tuple[str, str], dict[str, Any]], list[dict[str, Any]]]:
    vectors = load_json(VECTOR_PATH)
    rules = vectors.get("canonicalCollections")
    deferred = vectors.get("deferredExtensionCollections")
    if not isinstance(rules, list) or not isinstance(deferred, list):
        fail("vector document requires canonicalCollections and deferredExtensionCollections arrays")

    table: dict[tuple[str, str], dict[str, Any]] = {}
    for index, rule in enumerate(rules):
        location_name = f"canonicalCollections[{index}]"
        if not isinstance(rule, dict):
            fail(f"{location_name} must be an object")
        allowed = {"schemaId", "schemaPointer", "kind", "sortKey", "displayPath"}
        if set(rule) - allowed:
            fail(f"{location_name}: unknown members {sorted(set(rule) - allowed)}")
        schema_id = rule.get("schemaId")
        pointer = rule.get("schemaPointer")
        if not isinstance(schema_id, str) or schema_id not in graph.documents:
            fail(f"{location_name}: schemaId must name a catalogued schema")
        if not isinstance(pointer, str):
            fail(f"{location_name}: schemaPointer must be a string")
        node = resolve_pointer(graph.documents[schema_id], pointer)
        if not isinstance(node, dict) or node.get("type") != "array":
            fail(f"{location_name}: {schema_id}#{pointer} does not point to an explicit array schema")
        metadata = {key: rule[key] for key in ("kind", "sortKey") if key in rule}
        validate_metadata(metadata, location_name)
        key = (schema_id, pointer)
        if key in table:
            fail(f"duplicate canonical rule location {schema_id}#{pointer}")
        table[key] = metadata

    reachable = graph.reachable_arrays()
    missing = sorted(set(reachable) - set(table))
    extra = sorted(set(table) - set(reachable))
    if missing or extra:
        fail(f"canonical rule coverage mismatch; missing={missing}, extra={extra}")
    for location, metadata in reachable.items():
        if table[location] != metadata:
            fail(f"{location[0]}#{location[1]}: table metadata {table[location]} != schema metadata {metadata}")

    seen_deferred: set[str] = set()
    for index, entry in enumerate(deferred):
        name = f"deferredExtensionCollections[{index}]"
        if not isinstance(entry, dict) or set(entry) != {"freezeGate", "displayPath"}:
            fail(f"{name} must contain exactly freezeGate and displayPath")
        if entry["freezeGate"] != "extension" or not isinstance(entry["displayPath"], str) or not entry["displayPath"]:
            fail(f"{name} must be a non-empty extension display path")
        if entry["displayPath"] in seen_deferred:
            fail(f"duplicate deferred extension display path {entry['displayPath']}")
        seen_deferred.add(entry["displayPath"])
    return table, deferred


def run_witnesses(graph: SchemaGraph, table: dict[tuple[str, str], dict[str, Any]], deferred: list[dict[str, Any]]) -> None:
    core_id = "ipcraft.core-canonical-models.v1"
    core = graph.documents[core_id]

    def assert_ref(pointer: str, expected_pointer: str) -> None:
        node = resolve_pointer(core, pointer)
        if not isinstance(node, dict) or node.get("$ref") != f"#{expected_pointer}":
            fail(f"self-check: {pointer} must reuse {expected_pointer}")

    assert_ref(
        "/$defs/patchAccessSlotUpdateSet/properties/allowedContracts",
        "/$defs/patchAccessSlotValue/properties/allowedContracts",
    )
    assert_ref(
        "/$defs/patchPackageRelationUpdateSet/properties/sources",
        "/$defs/patchPackageRelationValue/properties/sources",
    )
    for definition_name in (
        "patchProjectUpdateSet", "patchComponentUpdateSet", "patchInterfaceUpdateSet",
        "patchRouterUpdateSet", "patchStructuralLinkUpdateSet", "patchAccessSlotUpdateSet",
        "patchDomainUpdateSet", "patchPackageEntityUpdateSet", "patchAttachmentUpdateSet",
        "patchDomainMembershipUpdateSet", "patchPackageRelationUpdateSet",
    ):
        definition = core["$defs"][definition_name]
        if definition.get("additionalProperties") is not False or "required" in definition:
            fail(f"self-check: {definition_name} must be closed with every field optional")
    if core["$defs"]["updateEntityOperation"]["properties"]["set"].get("$ref") != "#/$defs/patchEntityUpdateSet":
        fail("self-check: updateEntity.set is not closed by the entity update-set union")
    if core["$defs"]["updateRelationOperation"]["properties"]["set"].get("$ref") != "#/$defs/patchRelationUpdateSet":
        fail("self-check: updateRelation.set is not closed by the relation update-set union")
    expected_update_locations = {
        (core_id, "/$defs/accessSlot/properties/allowedContracts"),
        (core_id, "/$defs/accessSlot/properties/allowedContracts/items/properties/roles"),
        (core_id, "/$defs/patchPackageRelationValue/properties/sources"),
    }
    if not expected_update_locations <= set(table):
        fail("self-check: update-set allowedContracts/roles/sources locations are not covered")

    impact_location = (core_id, "/$defs/impactReport/properties/impacts")
    expected_impact_key = [
        "code", "severity", "dataLoss", "subjectsCanonicalJson", "detailsCanonicalJson", "resolution"
    ]
    if table.get(impact_location, {}).get("sortKey") != expected_impact_key:
        fail("self-check: impact comparator is not total")

    recovery_location = ("ipcraft.recovery.v1", "/$defs/draftEntry/properties/diagnostics")
    if list(table).count(recovery_location) != 1:
        fail("self-check: reused recovery draftEntry diagnostics must resolve once by defining pointer")

    if any("schemaId" in entry or "schemaPointer" in entry for entry in deferred):
        fail("self-check: Gate D deferred entries must not count as Core-addressed rules")


def main() -> int:
    try:
        graph = SchemaGraph()
        graph.validate_all_custom_keywords()
        table, deferred = validate_rule_table(graph)
        run_witnesses(graph, table, deferred)
    except VerificationError as error:
        print(f"canonical rule verification failed: {error}", file=sys.stderr)
        return 1
    print(
        f"canonical rule verification passed: {len(table)} Core array locations, "
        f"{len(deferred)} deferred extension display paths"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
