#!/usr/bin/env python3
"""Verify the Gate 0 canonical collection rule index.

This authoring check is read-only and uses only the Python standard library.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
CONTRACTS = ROOT / "docs" / "contracts"
CATALOG_PATH = CONTRACTS / "schema-catalog.json"
VECTOR_PATH = CONTRACTS / "vectors" / "core-canonical-projection-v1.json"
CANONICAL_KEY = "x-ipcraft-canonical"
VALID_KINDS = {"set", "ordered", "derived-ordered"}
DIGEST_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
VECTOR_SCHEMA = "ipcraft.canonical-vector-catalog.v1"
CANONICALIZATION = "RFC8785-after-Appendix-F-set-projection"
REQUIRED_CANDIDATE_CASE_IDS = {
    "shared-authority-application-localref-namespace",
    "hypothetical-final-host-id-mappings-excluded",
    "localized-impact-presentation-excluded",
    "object-key-order-permutation",
    "undirected-link-endpoint-swap-normalizes-equally",
    "localref-rename-with-edges-updated-changes-digest",
    "localref-edge-change-changes-digest",
    "patch-operation-order-swap-changes-digest",
    "pipeline-step-order-swap-changes-digest",
    "update-set-reused-collection-permutations",
    "applicability-change-changes-digest",
    "transaction-id-change-changes-digest",
    "structured-impact-change-changes-digest",
    "candidate-localref-collision",
    "authority-uses-application-prefix",
    "application-uses-authority-prefix",
    "allocation-order-missing",
    "allocation-order-duplicate",
    "allocation-order-noncanonical",
    "final-host-id-mapping-injected",
    "published-host-id-injected",
}
EXPECTED_DEFERRED_EXTENSION_PATHS = frozenset({
    "providerManifest.command",
    "providerManifest.capabilities",
    "providerManifest.ownedEntityTypes",
    "providerManifest.ownedRelationTypes",
    "providerHello.requestedCapabilities",
    "providerHelloResult.capabilities",
    "reconcileRequest.dependencyLocks",
    "reconcileRequest.capabilities",
    "providerResult.diagnostics",
    "toolManifest.capabilities",
})


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
        if re.search(r"~(?:[^01]|$)", raw_token):
            fail(f"invalid RFC 6901 escape in pointer {pointer!r}")
        token = raw_token.replace("~1", "/").replace("~0", "~")
        try:
            if isinstance(current, list):
                if not re.fullmatch(r"0|[1-9][0-9]*", token):
                    fail(f"noncanonical array index {token!r} in pointer {pointer!r}")
                current = current[int(token)]
            else:
                current = current[token]
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


def validate_rule_members(rule: dict[str, Any], location: str) -> None:
    expected_members = {"schemaId", "schemaPointer", "kind"}
    if rule.get("kind") != "ordered":
        expected_members.add("sortKey")
    if set(rule) != expected_members:
        fail(f"{location}: members must be exactly {sorted(expected_members)}")


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
        validate_rule_members(rule, location_name)
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

    validate_deferred_extensions(deferred)
    return table, deferred


def validate_deferred_extensions(deferred: Any) -> None:
    if not isinstance(deferred, list):
        fail("deferredExtensionCollections must be an array")
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
    if seen_deferred != EXPECTED_DEFERRED_EXTENSION_PATHS:
        fail(
            "deferred Extension path set mismatch; "
            f"missing={sorted(EXPECTED_DEFERRED_EXTENSION_PATHS - seen_deferred)}, "
            f"extra={sorted(seen_deferred - EXPECTED_DEFERRED_EXTENSION_PATHS)}"
        )


def validate_digest(value: Any, location: str) -> None:
    if value is None:
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            validate_digest(item, f"{location}[{index}]")
        return
    if not isinstance(value, str) or DIGEST_RE.fullmatch(value) is None:
        fail(f"{location}: expected sha256: plus 64 lowercase hexadecimal characters")


def collect_local_ref_references(value: Any) -> set[str]:
    references: set[str] = set()
    if isinstance(value, dict):
        if set(value) == {"localRef"} and isinstance(value["localRef"], str):
            references.add(value["localRef"])
        for child in value.values():
            references.update(collect_local_ref_references(child))
    elif isinstance(value, list):
        for child in value:
            references.update(collect_local_ref_references(child))
    return references


def validate_candidate_negative_isolation(case: dict[str, Any], location: str) -> None:
    if case.get("expectedRelation") != "invalid":
        return
    variants = case.get("inputVariants")
    if not isinstance(variants, list) or len(variants) != 1 or not isinstance(variants[0], dict):
        fail(f"{location}: invalid candidate cases require exactly one object variant")
    candidate = variants[0]
    case_id = case["id"]
    authority_operations = candidate.get("authorityPatch", {}).get("operations", [])
    application_operations = candidate.get("applicationPatch", {}).get("operations", [])
    authority_creates = [operation.get("localRef") for operation in authority_operations if operation.get("op") in {"createEntity", "createRelation"}]
    application_creates = [operation.get("localRef") for operation in application_operations if operation.get("op") in {"createEntity", "createRelation"}]
    creates = authority_creates + application_creates
    references: set[str] = set()
    for operation in authority_operations + application_operations:
        references.update(collect_local_ref_references(operation.get("value")))
    if not references <= set(creates):
        fail(f"{location}: negative case contains an unknown localRef reference")

    allocation = candidate.get("allocationOrder")
    canonical_allocation = sorted(set(creates))
    if case_id == "candidate-localref-collision":
        if len(authority_creates) == len(set(authority_creates)) or any(not value.startswith("authority:") for value in authority_creates):
            fail(f"{location}: collision must be duplicate Authority localRefs with correct prefixes")
        if any(not value.startswith("application:") for value in application_creates) or allocation != canonical_allocation:
            fail(f"{location}: collision case must otherwise have exact prefixes and allocationOrder")
    elif case_id == "authority-uses-application-prefix":
        if sum(value.startswith("application:") for value in authority_creates) != 1:
            fail(f"{location}: exactly one Authority create must use the Application prefix")
        if any(not value.startswith("application:") for value in application_creates) or allocation != canonical_allocation:
            fail(f"{location}: Authority-prefix case must otherwise be isolated")
    elif case_id == "application-uses-authority-prefix":
        if sum(value.startswith("authority:") for value in application_creates) != 1:
            fail(f"{location}: exactly one Application create must use the Authority prefix")
        if any(not value.startswith("authority:") for value in authority_creates) or allocation != canonical_allocation:
            fail(f"{location}: Application-prefix case must otherwise be isolated")
    elif case_id == "allocation-order-missing":
        if "allocationOrder" in candidate or case.get("expectedErrorCode") != "patch.schema_violation":
            fail(f"{location}: missing allocationOrder must be a schema violation")
    elif case_id == "allocation-order-duplicate":
        if not isinstance(allocation, list) or len(allocation) == len(set(allocation)) or set(allocation) != set(creates):
            fail(f"{location}: duplicate allocationOrder must contain the exact create refs with one duplicate")
        if case.get("expectedErrorCode") != "patch.schema_violation":
            fail(f"{location}: duplicate allocationOrder is rejected by uniqueItems")
    elif case_id == "allocation-order-noncanonical":
        if not isinstance(allocation, list) or set(allocation) != set(creates) or len(allocation) != len(set(allocation)) or allocation == canonical_allocation:
            fail(f"{location}: noncanonical allocationOrder must differ only in order")
    elif case_id == "final-host-id-mapping-injected":
        if "finalHostIdMapping" not in candidate or allocation != canonical_allocation:
            fail(f"{location}: final Host-ID mapping injection must otherwise be isolated")
    elif case_id == "published-host-id-injected":
        if "publishedHostIds" not in candidate or allocation != canonical_allocation:
            fail(f"{location}: published Host-ID injection must otherwise be isolated")


def validate_vector_catalog(
    table: dict[tuple[str, str], dict[str, Any]], deferred: list[dict[str, Any]]
) -> tuple[int, int]:
    index = load_json(VECTOR_PATH)
    catalog = index.get("vectorCatalog")
    if not isinstance(catalog, list) or not catalog:
        fail("vector document requires a non-empty vectorCatalog")
    expected_files = {path.name for path in VECTOR_PATH.parent.glob("*.json")}
    catalog_files: set[str] = set()
    for item_index, item in enumerate(catalog):
        name = f"vectorCatalog[{item_index}]"
        if not isinstance(item, dict) or set(item) != {"path", "purpose"}:
            fail(f"{name} must contain exactly path and purpose")
        path_value = item["path"]
        purpose = item["purpose"]
        if not isinstance(path_value, str) or not path_value.startswith("vectors/"):
            fail(f"{name}.path must be a vectors/ relative path")
        if not isinstance(purpose, str) or not purpose:
            fail(f"{name}.purpose must be a non-empty string")
        path = CONTRACTS / path_value
        if not path.is_file():
            fail(f"{name}: missing vector file {path_value}")
        if path.name in catalog_files:
            fail(f"duplicate vector catalog path {path_value}")
        catalog_files.add(path.name)
    if catalog_files != expected_files:
        fail(f"vectorCatalog file coverage mismatch; missing={sorted(expected_files - catalog_files)}, extra={sorted(catalog_files - expected_files)}")

    collection_path = VECTOR_PATH.parent / "core-set-permutation-v1.json"
    candidate_path = VECTOR_PATH.parent / "candidate-local-ref-v1.json"
    collection = load_json(collection_path)
    candidate = load_json(candidate_path)
    for path, document, expected_kind in (
        (collection_path, collection, "collection-permutation"),
        (candidate_path, candidate, "candidate-causality"),
    ):
        if not isinstance(document, dict) or set(document) != {"schema", "kind", "canonicalization", "cases"}:
            fail(f"{path.relative_to(ROOT)}: vector envelope must contain exactly schema, kind, canonicalization, cases")
        if document["schema"] != VECTOR_SCHEMA or document["kind"] != expected_kind or document["canonicalization"] != CANONICALIZATION:
            fail(f"{path.relative_to(ROOT)}: invalid vector envelope identity")
        if not isinstance(document["cases"], list) or not document["cases"]:
            fail(f"{path.relative_to(ROOT)}: cases must be a non-empty array")

    ids: set[str] = set()
    covered: dict[tuple[str, str], int] = {}
    for case_index, case in enumerate(collection["cases"]):
        name = f"core-set-permutation-v1 cases[{case_index}]"
        if not isinstance(case, dict):
            fail(f"{name} must be an object")
        case_id = case.get("id")
        if not isinstance(case_id, str) or not case_id or case_id in ids:
            fail(f"{name}: id must be non-empty and globally unique")
        ids.add(case_id)
        schema_id = case.get("schemaId")
        pointer_value = case.get("schemaPointer")
        location = (schema_id, pointer_value)
        if location not in table:
            fail(f"{name}: location is not a Core canonical collection")
        covered[location] = covered.get(location, 0) + 1
        metadata = table[location]
        if case.get("collectionKind") != metadata["kind"]:
            fail(f"{name}: collectionKind does not match canonical table")
        if metadata["kind"] == "ordered":
            if "sortKey" in case or case.get("expectedRelation") != "different":
                fail(f"{name}: ordered cases omit sortKey and must expect different")
            if not isinstance(case.get("inputVariants"), list) or len(case["inputVariants"]) < 2:
                fail(f"{name}: ordered cases require at least two variants")
        else:
            if case.get("sortKey") != metadata["sortKey"]:
                fail(f"{name}: sortKey does not match canonical table")
        if metadata["kind"] == "set":
            variants = case.get("inputVariants")
            if not isinstance(variants, list) or len(variants) < 3 or any(not isinstance(value, list) or not value for value in variants):
                fail(f"{name}: set cases require at least three non-empty permutations")
            if case.get("expectedRelation") != "equal":
                fail(f"{name}: set cases must expect equal")
            canonical_members = [
                sorted(json.dumps(item, sort_keys=True, separators=(",", ":"), ensure_ascii=False) for item in value)
                for value in variants
            ]
            if any(members != canonical_members[0] for members in canonical_members[1:]):
                fail(f"{name}: set inputVariants are not permutations of identical values")
            encoded_variants = {json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) for value in variants}
            if len(encoded_variants) < 3:
                fail(f"{name}: set cases require three distinct source orders")
            if not isinstance(case.get("expectedDigest"), str):
                fail(f"{name}: equal set cases require one expectedDigest string")
        elif metadata["kind"] == "derived-ordered":
            if case.get("expectedRelation") != "invalid" or not isinstance(case.get("expectedErrorCode"), list):
                fail(f"{name}: derived-ordered case must include valid/invalid variants and stable errors")
        elif not isinstance(case.get("expectedDigest"), list) or len(set(case["expectedDigest"])) != len(case["expectedDigest"]):
            fail(f"{name}: ordered cases require aligned distinct expected digests")
        validate_digest(case.get("expectedDigest"), f"{name}.expectedDigest")
    duplicates = sorted(location for location, count in covered.items() if count != 1)
    missing = sorted(set(table) - set(covered))
    if duplicates or missing:
        fail(f"collection vector coverage mismatch; duplicate={duplicates}, missing={missing}")

    deferred_paths = {entry["displayPath"] for entry in deferred}
    candidate_ids: set[str] = set()
    for case_index, case in enumerate(candidate["cases"]):
        name = f"candidate-local-ref-v1 cases[{case_index}]"
        if not isinstance(case, dict):
            fail(f"{name} must be an object")
        case_id = case.get("id")
        if not isinstance(case_id, str) or not case_id or case_id in ids:
            fail(f"{name}: id must be non-empty and globally unique")
        ids.add(case_id)
        candidate_ids.add(case_id)
        if case.get("expectedRelation") not in {"equal", "different", "invalid"}:
            fail(f"{name}: invalid expectedRelation")
        if not isinstance(case.get("includedProjection"), list) or not isinstance(case.get("excludedProjection"), list):
            fail(f"{name}: includedProjection/excludedProjection must be arrays")
        if any(value in deferred_paths for value in case.get("includedProjection", [])):
            fail(f"{name}: deferred Extension path appears in Core vectors")
        relation = case["expectedRelation"]
        if relation == "equal" and not isinstance(case.get("expectedDigest"), str):
            fail(f"{name}: equal cases require one expectedDigest string")
        if relation == "different" and (not isinstance(case.get("expectedDigest"), list) or len(set(case["expectedDigest"])) != len(case["expectedDigest"])):
            fail(f"{name}: different cases require aligned distinct expected digests")
        if relation == "invalid" and (not isinstance(case.get("expectedErrorCode"), str) or not case["expectedErrorCode"]):
            fail(f"{name}: invalid cases require a stable expectedErrorCode")
        validate_candidate_negative_isolation(case, name)
        validate_digest(case.get("expectedDigest"), f"{name}.expectedDigest")
    if candidate_ids != REQUIRED_CANDIDATE_CASE_IDS:
        fail(
            "candidate vector coverage mismatch; "
            f"missing={sorted(REQUIRED_CANDIDATE_CASE_IDS - candidate_ids)}, "
            f"extra={sorted(candidate_ids - REQUIRED_CANDIDATE_CASE_IDS)}"
        )
    return len(collection["cases"]), len(candidate["cases"])


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
    assert_ref(
        "/$defs/patchPackageRelationUpdateSet/properties/targets",
        "/$defs/patchPackageRelationValue/properties/targets",
    )
    for definition_name, value_name in (
        ("patchComponentUpdateSet", "patchComponentValue"),
        ("patchInterfaceUpdateSet", "patchInterfaceValue"),
        ("patchPackageEntityUpdateSet", "patchPackageEntityValue"),
        ("patchPackageRelationUpdateSet", "patchPackageRelationValue"),
    ):
        assert_ref(
            f"/$defs/{definition_name}/properties/extensions",
            f"/$defs/{value_name}/properties/extensions",
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
        (core_id, "/$defs/patchPackageRelationValue/properties/targets"),
        (core_id, "/$defs/patchComponentValue/properties/extensions"),
        (core_id, "/$defs/patchInterfaceValue/properties/extensions"),
        (core_id, "/$defs/patchPackageEntityValue/properties/extensions"),
        (core_id, "/$defs/patchPackageRelationValue/properties/extensions"),
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

    mutated_rule = {
        "schemaId": core_id,
        "schemaPointer": "/$defs/patchOperations",
        "kind": "ordered",
        "displayPath": "not.allowed",
    }
    try:
        validate_rule_members(mutated_rule, "self-check canonical rule")
    except VerificationError:
        pass
    else:
        fail("self-check: canonical rule displayPath mutation was accepted")

    replaced_deferred = [dict(entry) for entry in deferred]
    replaced_deferred[0]["displayPath"] = "arbitrary.replacement"
    try:
        validate_deferred_extensions(replaced_deferred)
    except VerificationError:
        pass
    else:
        fail("self-check: arbitrary deferred Extension replacement was accepted")


def main() -> int:
    try:
        graph = SchemaGraph()
        graph.validate_all_custom_keywords()
        table, deferred = validate_rule_table(graph)
        run_witnesses(graph, table, deferred)
        collection_count, candidate_count = validate_vector_catalog(table, deferred)
    except VerificationError as error:
        print(f"canonical rule verification failed: {error}", file=sys.stderr)
        return 1
    print(
        f"canonical rule verification passed: {len(table)} Core array locations, "
        f"{len(deferred)} deferred extension display paths, "
        f"{collection_count} collection cases, {candidate_count} candidate cases"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
