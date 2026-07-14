#!/usr/bin/env python3
"""Independently recompute and verify committed canonical golden vectors.

This stdlib-only verifier intentionally imports no generator code. It supports
the integer/string/boolean/null JSON domain used by the committed vectors.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


VECTOR_SCHEMA = "ipcraft.canonical-vector-catalog.v1"
CANONICALIZATION = "RFC8785-after-Appendix-F-set-projection"
DIGEST_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
COLLECTION_COMMON = {
    "id", "schemaId", "schemaPointer", "collectionKind", "inputVariants",
    "expectedRelation", "expectedNormalized", "expectedCanonicalJson", "expectedDigest",
}
VALID_CANDIDATE_FIELDS = {
    "id", "inputVariants", "expectedRelation", "includedProjection", "excludedProjection",
    "expectedNormalized", "expectedCanonicalJson", "expectedDigest",
}
INVALID_CANDIDATE_FIELDS = {
    "id", "baselineId", "mutation", "inputVariants", "expectedRelation",
    "includedProjection", "excludedProjection", "expectedErrorCode", "violatedRule",
}
MUTATION_VALUE_OPERATIONS = {"replace", "add", "append", "rename-local-ref"}
SCHEMA_KEYWORDS = {
    "$ref", "$comment", "$defs", "definitions", "title", "description", "$id", "$schema",
    "type", "required", "properties", "additionalProperties", "enum", "const", "oneOf",
    "anyOf", "allOf", "if", "then", "else", "not", "items", "minItems", "maxItems",
    "uniqueItems", "pattern", "minimum", "maximum", "minLength", "maxLength", "format",
    "x-ipcraft-canonical",
}


class VerificationError(RuntimeError):
    pass


class SchemaViolation(RuntimeError):
    pass


def fail(message: str) -> None:
    raise VerificationError(message)


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot load {path}: {error}")


def canonical_json(value: Any) -> str:
    def check(item: Any, location: str) -> None:
        if item is None or isinstance(item, (str, bool, int)):
            return
        if isinstance(item, float):
            fail(f"{location}: floating-point values are outside the supported vector domain")
        if isinstance(item, list):
            for index, child in enumerate(item):
                check(child, f"{location}[{index}]")
            return
        if isinstance(item, dict):
            if not all(isinstance(key, str) for key in item):
                fail(f"{location}: object keys must be strings")
            for key, child in item.items():
                check(child, f"{location}.{key}")
            return
        fail(f"{location}: unsupported JSON value type {type(item).__name__}")
    check(value, "canonical input")
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def sha256_digest(value: Any) -> str:
    return "sha256:" + hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def decode_pointer_token(raw: str, pointer: str) -> str:
    if re.search(r"~(?:[^01]|$)", raw):
        fail(f"invalid RFC 6901 escape in {pointer!r}")
    return raw.replace("~1", "/").replace("~0", "~")


def resolve_pointer(document: Any, pointer: str) -> Any:
    if pointer == "":
        return document
    if not isinstance(pointer, str) or not pointer.startswith("/"):
        fail(f"invalid RFC 6901 pointer {pointer!r}")
    current = document
    for raw in pointer[1:].split("/"):
        token = decode_pointer_token(raw, pointer)
        try:
            if isinstance(current, list):
                if not re.fullmatch(r"0|[1-9][0-9]*", token):
                    fail(f"noncanonical list index {token!r} in {pointer!r}")
                current = current[int(token)]
            elif isinstance(current, dict):
                current = current[token]
            else:
                fail(f"pointer {pointer!r} traverses non-container value")
        except (KeyError, IndexError):
            fail(f"pointer {pointer!r} does not resolve")
    return current


class SchemaWorld:
    def __init__(self, contracts: Path) -> None:
        self.contracts = contracts
        catalog = load_json(contracts / "schema-catalog.json")
        if not isinstance(catalog, dict) or not isinstance(catalog.get("items"), list):
            fail("schema catalog must contain an items array")
        self.documents: dict[str, dict[str, Any]] = {}
        self.files: dict[str, str] = {}
        for index, entry in enumerate(catalog["items"]):
            if not isinstance(entry, dict) or not isinstance(entry.get("id"), str) or not isinstance(entry.get("path"), str):
                fail(f"schema catalog items[{index}] requires string id/path")
            document = load_json(contracts / entry["path"])
            if not isinstance(document, dict) or document.get("$id") != entry["id"]:
                fail(f"schema catalog item {entry['id']} has mismatched document")
            self.documents[entry["id"]] = document
            self.files[Path(entry["path"]).name] = entry["id"]

    def resolve(self, schema_id: str, schema: Any) -> tuple[str, Any]:
        seen: set[tuple[str, str]] = set()
        while isinstance(schema, dict) and "$ref" in schema:
            ref = schema["$ref"]
            if not isinstance(ref, str):
                fail(f"{schema_id}: $ref must be a string")
            key = (schema_id, ref)
            if key in seen:
                fail(f"cyclic direct $ref {schema_id} {ref}")
            seen.add(key)
            file_part, separator, fragment = ref.partition("#")
            if not separator:
                fragment = ""
            if file_part:
                filename = Path(file_part).name
                if filename not in self.files:
                    fail(f"{schema_id}: unresolved external $ref {ref!r}")
                schema_id = self.files[filename]
            schema = resolve_pointer(self.documents[schema_id], fragment)
        return schema_id, schema

    def validate(self, schema_id: str, schema: Any, value: Any, location: str) -> None:
        schema_id, schema = self.resolve(schema_id, schema)
        if not isinstance(schema, dict):
            fail(f"{location}: schema node must be an object")
        unsupported = set(schema) - SCHEMA_KEYWORDS
        if unsupported:
            fail(f"{location}: unsupported schema keywords affect vector validation: {sorted(unsupported)}")
        if "const" in schema and value != schema["const"]:
            raise SchemaViolation(f"{location}: value does not equal const")
        if "enum" in schema:
            if not isinstance(schema["enum"], list):
                fail(f"{location}: enum must be an array")
            if value not in schema["enum"]:
                raise SchemaViolation(f"{location}: value is not in enum")
        types = schema.get("type")
        if types is not None:
            if isinstance(types, str):
                types = [types]
            if not isinstance(types, list) or not all(isinstance(item, str) for item in types):
                fail(f"{location}: type must be a string or string array")
            if not any(self.type_matches(item, value) for item in types):
                raise SchemaViolation(f"{location}: expected type {types}, got {type(value).__name__}")
        if "oneOf" in schema:
            branches = schema["oneOf"]
            if not isinstance(branches, list):
                fail(f"{location}: oneOf must be an array")
            matches = sum(self.is_valid(schema_id, branch, value, location) for branch in branches)
            if matches != 1:
                raise SchemaViolation(f"{location}: oneOf matched {matches} branches")
        if "anyOf" in schema:
            branches = schema["anyOf"]
            if not isinstance(branches, list):
                fail(f"{location}: anyOf must be an array")
            if not any(self.is_valid(schema_id, branch, value, location) for branch in branches):
                raise SchemaViolation(f"{location}: no anyOf branch matched")
        if "not" in schema and self.is_valid(schema_id, schema["not"], value, location):
            raise SchemaViolation(f"{location}: forbidden by not")
        if isinstance(value, dict):
            required = schema.get("required", [])
            properties = schema.get("properties", {})
            if not isinstance(required, list) or not all(isinstance(item, str) for item in required):
                fail(f"{location}: required must be a string array")
            if not isinstance(properties, dict):
                fail(f"{location}: properties must be an object")
            missing = [item for item in required if item not in value]
            if missing:
                raise SchemaViolation(f"{location}: missing required properties {missing}")
            if schema.get("additionalProperties") is False:
                extra = set(value) - set(properties)
                if extra:
                    raise SchemaViolation(f"{location}: additional properties {sorted(extra)}")
            for key, child in value.items():
                if key in properties:
                    self.validate(schema_id, properties[key], child, f"{location}/{key}")
        if isinstance(value, list):
            minimum = schema.get("minItems", 0)
            maximum = schema.get("maxItems")
            if not isinstance(minimum, int) or (maximum is not None and not isinstance(maximum, int)):
                fail(f"{location}: minItems/maxItems must be integers")
            if len(value) < minimum or (maximum is not None and len(value) > maximum):
                raise SchemaViolation(f"{location}: array cardinality is outside bounds")
            if schema.get("uniqueItems") is True and len({canonical_json(item) for item in value}) != len(value):
                raise SchemaViolation(f"{location}: uniqueItems violated")
            if "items" in schema:
                for index, item in enumerate(value):
                    self.validate(schema_id, schema["items"], item, f"{location}/{index}")
        if isinstance(value, str):
            minimum = schema.get("minLength", 0)
            maximum = schema.get("maxLength")
            if len(value) < minimum or (maximum is not None and len(value) > maximum):
                raise SchemaViolation(f"{location}: string length is outside bounds")
            if "pattern" in schema:
                try:
                    matched = re.search(schema["pattern"], value)
                except (TypeError, re.error) as error:
                    fail(f"{location}: invalid schema pattern: {error}")
                if matched is None:
                    raise SchemaViolation(f"{location}: string does not match pattern")
            if schema.get("format") == "date-time" and re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", value) is None:
                raise SchemaViolation(f"{location}: unsupported date-time spelling")
            if "format" in schema and schema["format"] != "date-time":
                fail(f"{location}: unsupported schema format {schema['format']!r}")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if "minimum" in schema and value < schema["minimum"]:
                raise SchemaViolation(f"{location}: below minimum")
            if "maximum" in schema and value > schema["maximum"]:
                raise SchemaViolation(f"{location}: above maximum")
        for index, branch in enumerate(schema.get("allOf", [])):
            if not isinstance(branch, dict):
                fail(f"{location}: allOf[{index}] must be an object")
            if "if" in branch:
                chosen = branch.get("then", {}) if self.is_valid(schema_id, branch["if"], value, location) else branch.get("else", {})
                self.validate(schema_id, chosen, value, f"{location}/allOf/{index}")
            else:
                self.validate(schema_id, branch, value, f"{location}/allOf/{index}")

    def is_valid(self, schema_id: str, schema: Any, value: Any, location: str) -> bool:
        try:
            self.validate(schema_id, schema, value, location)
            return True
        except SchemaViolation:
            return False

    @staticmethod
    def type_matches(expected: str, value: Any) -> bool:
        return {
            "object": isinstance(value, dict), "array": isinstance(value, list),
            "string": isinstance(value, str), "integer": isinstance(value, int) and not isinstance(value, bool),
            "number": isinstance(value, (int, float)) and not isinstance(value, bool),
            "boolean": isinstance(value, bool), "null": value is None,
        }.get(expected, False)


def object_ref_token(value: Any, location: str) -> str:
    if not isinstance(value, dict) or set(value) not in ({"id"}, {"localRef"}):
        fail(f"{location}: object reference must contain exactly id or localRef")
    key = "id" if "id" in value else "localRef"
    if not isinstance(value[key], str):
        fail(f"{location}: reference value must be a string")
    return f"{key}:{value[key]}"


def endpoint_key(item: Any, patch: bool, location: str) -> tuple[Any, ...]:
    if not isinstance(item, dict) or item.get("state") not in {"resolved", "unresolved"}:
        fail(f"{location}: endpoint requires resolved/unresolved state")
    subject_key = "subject" if item["state"] == "resolved" else "intendedSubject"
    subject = item.get(subject_key)
    if not isinstance(subject, dict) or not isinstance(subject.get("kind"), str):
        fail(f"{location}: endpoint subject requires string kind")
    if patch:
        token = object_ref_token(subject.get("ref"), location)
    else:
        if not isinstance(subject.get("id"), str):
            fail(f"{location}: persisted endpoint subject requires string id")
        token = "id:" + subject["id"]
    key: tuple[Any, ...] = (0 if item["state"] == "resolved" else 1, subject["kind"], token)
    if item["state"] == "unresolved":
        if not isinstance(item.get("reasonCode"), str):
            fail(f"{location}: unresolved endpoint requires string reasonCode")
        key += (item["reasonCode"],)
    return key


def sort_components(keys: Any, item: Any, location: str) -> tuple[Any, ...]:
    if not isinstance(keys, list) or not keys or not all(isinstance(key, str) and key for key in keys):
        fail(f"{location}: sortKey must be a non-empty string array")
    result: list[Any] = []
    for key in keys:
        if key == "unicodeScalarValue":
            if not isinstance(item, str):
                fail(f"{location}: unicodeScalarValue item must be a string")
            result.append(item)
        elif key == "canonicalJson":
            result.append(canonical_json(item).encode("utf-8"))
        elif key == "subjectsCanonicalJson":
            result.append(canonical_json(item.get("subjects")).encode("utf-8"))
        elif key == "detailsCanonicalJson":
            result.append(canonical_json(item.get("details")).encode("utf-8"))
        elif key == "persistedEndpointCanonicalKey":
            result.extend(endpoint_key(item, False, location))
        elif key == "patchEndpointCanonicalKey":
            result.extend(endpoint_key(item, True, location))
        else:
            if not isinstance(item, dict) or key not in item:
                fail(f"{location}: item lacks sort component {key!r}")
            result.append(item[key])
    return tuple(result)


def normalize(world: SchemaWorld, schema_id: str, schema: Any, value: Any, location: str) -> Any:
    schema_id, schema = world.resolve(schema_id, schema)
    if "oneOf" in schema:
        matches = [branch for branch in schema["oneOf"] if world.is_valid(schema_id, branch, value, location)]
        if len(matches) != 1:
            fail(f"{location}: cannot select oneOf normalization branch")
        return normalize(world, schema_id, matches[0], value, location)
    if "anyOf" in schema:
        matches = [branch for branch in schema["anyOf"] if world.is_valid(schema_id, branch, value, location)]
        if not matches:
            fail(f"{location}: cannot select anyOf normalization branch")
        return normalize(world, schema_id, matches[0], value, location)
    if isinstance(value, dict):
        properties = schema.get("properties", {})
        return {key: normalize(world, schema_id, properties.get(key, {}), child, f"{location}/{key}") for key, child in value.items()}
    if isinstance(value, list):
        values = [normalize(world, schema_id, schema.get("items", {}), item, f"{location}/{index}") for index, item in enumerate(value)]
        metadata = schema.get("x-ipcraft-canonical")
        if metadata is not None:
            if not isinstance(metadata, dict) or metadata.get("kind") not in {"set", "ordered", "derived-ordered"}:
                fail(f"{location}: malformed x-ipcraft-canonical")
            if metadata["kind"] == "set":
                values.sort(key=lambda item: sort_components(metadata.get("sortKey"), item, location))
        return values
    return copy.deepcopy(value)


def normalize_candidate(world: SchemaWorld, variant: Any, excluded: list[str], location: str) -> Any:
    if not isinstance(variant, dict):
        fail(f"{location}: candidate variant must be an object")
    value = copy.deepcopy(variant)
    if "candidate" in value:
        allowed = {"candidate"} | set(excluded)
        if set(value) != allowed or any("." in name or "/" in name for name in excluded):
            fail(f"{location}: wrapper fields must exactly match top-level excludedProjection")
        candidate = value["candidate"]
    else:
        if excluded:
            fail(f"{location}: excludedProjection requires an explicit candidate wrapper")
        candidate = value
    if not isinstance(candidate, dict):
        fail(f"{location}: candidate must be an object")
    candidate = copy.deepcopy(candidate)
    candidate.pop("candidateDigest", None)
    core_id = "ipcraft.core-canonical-models.v1"
    normalized = normalize(world, core_id, world.documents[core_id]["$defs"]["candidateTransaction"], candidate, location)
    for patch_name in ("authorityPatch", "applicationPatch"):
        operations = normalized[patch_name]["operations"]
        for operation in operations:
            if operation.get("op") == "createEntity" and operation.get("entityKind") == "structural-link":
                link = operation["value"]
                if object_ref_token(link["endpointB"], location) < object_ref_token(link["endpointA"], location):
                    link["endpointA"], link["endpointB"] = link["endpointB"], link["endpointA"]
            if operation.get("op") == "updateEntity":
                updates = operation.get("set", {})
                if operation.get("entityKind") == "access-slot" and "allowedContracts" in updates:
                    schema = world.documents[core_id]["$defs"]["accessSlot"]["properties"]["allowedContracts"]
                    updates["allowedContracts"] = normalize(world, core_id, schema, updates["allowedContracts"], location)
                definitions = {"component":"patchComponentValue", "interface":"patchInterfaceValue", "package-entity":"patchPackageEntityValue"}
                if operation.get("entityKind") in definitions and "extensions" in updates:
                    schema = world.documents[core_id]["$defs"][definitions[operation["entityKind"]]]["properties"]["extensions"]
                    updates["extensions"] = normalize(world, core_id, schema, updates["extensions"], location)
            if operation.get("op") == "updateRelation" and operation.get("relationKind") == "package-relation":
                updates = operation.get("set", {})
                properties = world.documents[core_id]["$defs"]["patchPackageRelationValue"]["properties"]
                for key in ("sources", "targets", "extensions"):
                    if key in updates:
                        updates[key] = normalize(world, core_id, properties[key], updates[key], location)
    return normalized


def exact_fields(value: Any, expected: set[str], location: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{location}: expected object")
    if set(value) != expected:
        fail(f"{location}: fields must be exactly {sorted(expected)}; got {sorted(value)}")
    return value


def string_list(value: Any, location: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
        fail(f"{location}: expected non-empty-string array")
    return value


def validate_digest(value: Any, location: str) -> None:
    if not isinstance(value, str) or DIGEST_RE.fullmatch(value) is None:
        fail(f"{location}: invalid digest")


def apply_mutation(baseline: Any, mutation: Any, location: str) -> Any:
    if not isinstance(mutation, dict) or not isinstance(mutation.get("operation"), str) or not isinstance(mutation.get("path"), str):
        fail(f"{location}: mutation requires string operation/path")
    operation = mutation["operation"]
    expected = {"operation", "path"} if operation == "remove" else {"operation", "path", "value"}
    if operation not in MUTATION_VALUE_OPERATIONS | {"remove"} or set(mutation) != expected:
        fail(f"{location}: mutation has invalid operation or fields")
    if not mutation["path"].startswith("/"):
        fail(f"{location}: mutation path must be a non-root RFC 6901 pointer")
    result = copy.deepcopy(baseline)
    tokens = [decode_pointer_token(raw, mutation["path"]) for raw in mutation["path"][1:].split("/")]
    parent = result
    for token in tokens[:-1]:
        if isinstance(parent, list):
            if not re.fullmatch(r"0|[1-9][0-9]*", token): fail(f"{location}: invalid list index")
            parent = parent[int(token)]
        elif isinstance(parent, dict) and token in parent:
            parent = parent[token]
        else:
            fail(f"{location}: mutation path does not resolve")
    token = tokens[-1]
    if operation == "remove":
        if not isinstance(parent, dict) or token not in parent: fail(f"{location}: remove target does not exist")
        del parent[token]
    elif operation in {"replace", "add"}:
        if isinstance(parent, list): parent[int(token)] = copy.deepcopy(mutation["value"])
        elif isinstance(parent, dict): parent[token] = copy.deepcopy(mutation["value"])
        else: fail(f"{location}: mutation parent is not a container")
    elif operation == "append":
        target = parent[int(token)] if isinstance(parent, list) else parent.get(token) if isinstance(parent, dict) else None
        if not isinstance(target, list): fail(f"{location}: append target must be an array")
        target.append(copy.deepcopy(mutation["value"]))
    else:
        if isinstance(parent, list): old = parent[int(token)]; parent[int(token)] = mutation["value"]
        elif isinstance(parent, dict) and token in parent: old = parent[token]; parent[token] = mutation["value"]
        else: fail(f"{location}: rename target does not exist")
        new = mutation["value"]
        if not isinstance(old, str) or not isinstance(new, str): fail(f"{location}: rename-local-ref requires strings")
        def rewrite(value: Any) -> None:
            if isinstance(value, dict):
                for key, child in value.items():
                    if key == "localRef" and child == old: value[key] = new
                    else: rewrite(child)
            elif isinstance(value, list):
                for index, child in enumerate(value):
                    if child == old: value[index] = new
                    else: rewrite(child)
        rewrite(result)
        if not isinstance(result.get("allocationOrder"), list): fail(f"{location}: rename baseline lacks allocationOrder")
        result["allocationOrder"] = sorted(set(result["allocationOrder"]))
    return result


def semantic_predicates(candidate: dict[str, Any]) -> dict[str, bool]:
    authority = [op for op in candidate["authorityPatch"]["operations"] if op.get("op") in {"createEntity", "createRelation"}]
    application = [op for op in candidate["applicationPatch"]["operations"] if op.get("op") in {"createEntity", "createRelation"}]
    authority_refs = [op.get("localRef") for op in authority]
    application_refs = [op.get("localRef") for op in application]
    creates = authority_refs + application_refs
    referenced: set[str] = set()
    def visit(value: Any) -> None:
        if isinstance(value, dict):
            if set(value) == {"localRef"} and isinstance(value["localRef"], str): referenced.add(value["localRef"])
            for child in value.values(): visit(child)
        elif isinstance(value, list):
            for child in value: visit(child)
    for operation in candidate["authorityPatch"]["operations"] + candidate["applicationPatch"]["operations"]:
        visit(operation.get("value"))
    allocation = candidate.get("allocationOrder")
    return {
        "candidate.localRef.unique": len(creates) == len(set(creates)),
        "candidate.localRef.authority-prefix": all(isinstance(item, str) and item.startswith("authority:") for item in authority_refs),
        "candidate.localRef.application-prefix": all(isinstance(item, str) and item.startswith("application:") for item in application_refs),
        "candidate.localRef.references": referenced <= set(creates),
        "candidate.allocationOrder.canonical": isinstance(allocation, list) and allocation == sorted(set(creates)),
    }


def verify_collection_cases(world: SchemaWorld, document: Any, rules: dict[tuple[str, str], dict[str, Any]]) -> tuple[int, int]:
    exact_fields(document, {"schema", "kind", "canonicalization", "cases"}, "collection envelope")
    if document["schema"] != VECTOR_SCHEMA or document["kind"] != "collection-permutation" or document["canonicalization"] != CANONICALIZATION:
        fail("collection envelope identity mismatch")
    if not isinstance(document["cases"], list): fail("collection cases must be an array")
    checked_items = 0; digests = 0; seen: set[tuple[str, str]] = set()
    for index, case in enumerate(document["cases"]):
        location = f"collection cases[{index}]"
        if not isinstance(case, dict): fail(f"{location}: expected object")
        kind = case.get("collectionKind")
        expected = COLLECTION_COMMON | ({"sortKey"} if kind != "ordered" else set()) | ({"expectedErrorCode"} if kind == "derived-ordered" else set())
        exact_fields(case, expected, location)
        if not isinstance(case["id"], str) or not case["id"]: fail(f"{location}: invalid id")
        key = (case["schemaId"], case["schemaPointer"])
        if key not in rules or key in seen: fail(f"{location}: unknown or duplicate collection location")
        seen.add(key); rule = rules[key]
        if kind != rule["kind"]: fail(f"{location}: collectionKind mismatch")
        variants = case["inputVariants"]
        if not isinstance(variants, list): fail(f"{location}: inputVariants must be an array")
        node = resolve_pointer(world.documents[key[0]], key[1])
        if "items" in node:
            for variant_index, variant in enumerate(variants):
                if not isinstance(variant, list): fail(f"{location}: variants must be arrays")
                for item_index, item in enumerate(variant):
                    try: world.validate(key[0], node["items"], item, f"{location}/inputVariants/{variant_index}/{item_index}")
                    except SchemaViolation as error: fail(str(error))
                    checked_items += 1
        normalized = [normalize(world, key[0], node, variant, f"{location}/inputVariants") for variant in variants]
        if kind == "set":
            if case["sortKey"] != rule["sortKey"] or len(variants) < 3: fail(f"{location}: invalid set coverage")
            if not all(value == normalized[0] for value in normalized): fail(f"{location}: set variants normalize differently")
            if case["expectedNormalized"] != normalized[0]: fail(f"{location}: expectedNormalized mismatch")
            if case["expectedCanonicalJson"] != canonical_json(normalized[0]): fail(f"{location}: expectedCanonicalJson mismatch")
            validate_digest(case["expectedDigest"], location)
            if case["expectedDigest"] != sha256_digest(normalized[0]): fail(f"{location}: expectedDigest mismatch")
            verify_comparator_depth(case, normalized[0], location)
            digests += 1
        elif kind == "ordered":
            count = len(variants)
            if count < 2 or not all(isinstance(value, list) and len(value) == count for value in (case["expectedNormalized"], case["expectedCanonicalJson"], case["expectedDigest"])):
                fail(f"{location}: ordered expectations must align with variants")
            if case["expectedNormalized"] != normalized: fail(f"{location}: ordered expectedNormalized mismatch")
            canonical = [canonical_json(value) for value in normalized]; digested = [sha256_digest(value) for value in normalized]
            if case["expectedCanonicalJson"] != canonical or case["expectedDigest"] != digested or len(set(digested)) != count:
                fail(f"{location}: ordered canonical/digest mismatch")
            digests += count
        elif kind == "derived-ordered":
            if len(variants) != 2: fail(f"{location}: derived case requires exactly valid and invalid variants")
            fields = (case["expectedNormalized"], case["expectedCanonicalJson"], case["expectedDigest"], case["expectedErrorCode"])
            if not all(isinstance(value, list) and len(value) == 2 for value in fields): fail(f"{location}: derived expectations must align")
            if fields[0][0] != variants[0] or fields[0][1] is not None or fields[1][0] != canonical_json(variants[0]) or fields[1][1] is not None:
                fail(f"{location}: derived normalized/canonical null placement mismatch")
            validate_digest(fields[2][0], location)
            if fields[2][0] != sha256_digest(variants[0]) or fields[2][1] is not None or fields[3][0] is not None or not isinstance(fields[3][1], str):
                fail(f"{location}: derived digest/error null placement mismatch")
            if variants[0] != sorted(variants[0]) or variants[1] == sorted(variants[1]): fail(f"{location}: derived order witness mismatch")
            digests += 1
        else:
            fail(f"{location}: invalid collection kind")
    if seen != set(rules): fail("collection case coverage does not equal canonical rule table")
    return checked_items, digests


def verify_comparator_depth(case: dict[str, Any], normalized: list[Any], location: str) -> None:
    keys = case["sortKey"]
    dependent_components = set()
    if (case.get("schemaId"), case.get("schemaPointer")) == (
        "ipcraft.core-canonical-models.v1", "/$defs/impactReport/properties/impacts"
    ):
        # The closed impact code contract functionally determines these fields.
        # They remain in the frozen total key for audit stability but cannot be
        # independently decisive without constructing a schema-invalid impact.
        dependent_components = {"severity", "dataLoss", "resolution"}
    if len(keys) > 1:
        components = [sort_components(keys, item, location) for item in normalized]
        for index in range(len(keys)):
            if keys[index] in dependent_components:
                continue
            if not any(left[:index] == right[:index] and left[index] != right[index] for pos, left in enumerate(components) for right in components[pos + 1:]):
                fail(f"{location}: comparator component {keys[index]!r} is never decisive")
    if keys == ["persistedEndpointCanonicalKey"]:
        endpoint_depth(normalized, False, location)
    if keys == ["patchEndpointCanonicalKey"]:
        endpoint_depth(normalized, True, location)


def endpoint_depth(items: list[Any], patch: bool, location: str) -> None:
    keys = [endpoint_key(item, patch, location) for item in items]
    if {key[0] for key in keys} != {0, 1}:
        fail(f"{location}: endpoint resolved/unresolved branch is incomplete")
    if not any(left[0] == right[0] and left[1] != right[1] for pos, left in enumerate(keys) for right in keys[pos + 1:]):
        fail(f"{location}: endpoint kind ordering has no decisive pair")
    if not any(left[:2] == right[:2] and left[2] != right[2] for pos, left in enumerate(keys) for right in keys[pos + 1:]):
        fail(f"{location}: endpoint ID/token ordering has no decisive pair")
    unresolved = [key for key in keys if key[0] == 1]
    if not any(left[:3] == right[:3] and left[3] != right[3] for pos, left in enumerate(unresolved) for right in unresolved[pos + 1:]):
        fail(f"{location}: unresolved endpoint reason ordering has no decisive pair")
    if patch and not any(
        left[:2] == right[:2] and str(left[2]).startswith("id:") and str(right[2]).startswith("localRef:")
        or left[:2] == right[:2] and str(right[2]).startswith("id:") and str(left[2]).startswith("localRef:")
        for pos, left in enumerate(keys) for right in keys[pos + 1:]
    ):
        fail(f"{location}: patch endpoint Host/localRef token ordering has no decisive pair")


def verify_candidate_cases(world: SchemaWorld, document: Any, error_codes: set[str]) -> tuple[int, int, int, int]:
    exact_fields(document, {"schema", "kind", "canonicalization", "cases"}, "candidate envelope")
    if document["schema"] != VECTOR_SCHEMA or document["kind"] != "candidate-causality" or document["canonicalization"] != CANONICALIZATION:
        fail("candidate envelope identity mismatch")
    if not isinstance(document["cases"], list): fail("candidate cases must be an array")
    by_id: dict[str, dict[str, Any]] = {}
    for index, case in enumerate(document["cases"]):
        if not isinstance(case, dict) or not isinstance(case.get("id"), str) or not case["id"] or case["id"] in by_id:
            fail(f"candidate cases[{index}]: invalid or duplicate id")
        by_id[case["id"]] = case
    digests = valid_inputs = schema_invalid = semantic_invalid = 0
    core_id = "ipcraft.core-canonical-models.v1"; core = world.documents[core_id]
    for index, case in enumerate(document["cases"]):
        location = f"candidate cases[{index}]"
        relation = case.get("expectedRelation")
        expected = INVALID_CANDIDATE_FIELDS if relation == "invalid" else VALID_CANDIDATE_FIELDS | ({"modelSchema"} if "modelSchema" in case else set())
        exact_fields(case, expected, location)
        included = string_list(case["includedProjection"], f"{location}.includedProjection")
        excluded = case["excludedProjection"]
        if not isinstance(excluded, list) or not all(isinstance(item, str) and item for item in excluded): fail(f"{location}: excludedProjection must be a string array")
        variants = case["inputVariants"]
        if not isinstance(variants, list) or not variants: fail(f"{location}: inputVariants must be non-empty")
        if relation == "invalid":
            if case["expectedErrorCode"] not in error_codes: fail(f"{location}: unknown expectedErrorCode {case['expectedErrorCode']!r}")
            if not isinstance(case["violatedRule"], str) or not case["violatedRule"]: fail(f"{location}: violatedRule must be non-empty")
            baseline_case = by_id.get(case["baselineId"])
            if baseline_case is None or baseline_case.get("expectedRelation") != "equal": fail(f"{location}: baselineId must name an equal case")
            baseline_variant = baseline_case["inputVariants"][0]
            baseline = baseline_variant.get("candidate", baseline_variant) if isinstance(baseline_variant, dict) else baseline_variant
            if not isinstance(baseline, dict): fail(f"{location}: baseline candidate must be an object")
            try: world.validate(core_id, core["$defs"]["candidateTransaction"], baseline, f"{location}/baseline")
            except SchemaViolation as error: fail(f"{location}: baseline is not schema-valid: {error}")
            mutant = apply_mutation(baseline, case["mutation"], f"{location}/mutation")
            if variants != [mutant]: fail(f"{location}: committed mutant differs outside declared mutation")
            mutant_valid = world.is_valid(core_id, core["$defs"]["candidateTransaction"], mutant, f"{location}/mutant")
            if case["expectedErrorCode"] == "patch.schema_violation":
                if mutant_valid: fail(f"{location}: schema-violation mutant passes schema")
                schema_invalid += 1
            else:
                if not mutant_valid: fail(f"{location}: semantic mutant fails schema first")
                baseline_predicates = semantic_predicates(baseline); mutant_predicates = semantic_predicates(mutant)
                if not all(baseline_predicates.values()): fail(f"{location}: baseline fails semantic predicates")
                if case["violatedRule"] not in mutant_predicates: fail(f"{location}: unknown semantic violatedRule")
                failed = {name for name, passed in mutant_predicates.items() if not passed}
                if failed != {case["violatedRule"]}: fail(f"{location}: semantic mutant failures are {sorted(failed)}")
                semantic_invalid += 1
            continue
        if relation not in {"equal", "different"}: fail(f"{location}: invalid expectedRelation")
        if "modelSchema" in case:
            if case["modelSchema"] != "ipcraft.pipeline-plan.v1" or excluded: fail(f"{location}: invalid modelSchema metadata")
            schema = core["$defs"]["pipelinePlan"]
            normalized = []
            for variant_index, variant in enumerate(variants):
                try: world.validate(core_id, schema, variant, f"{location}/inputVariants/{variant_index}")
                except SchemaViolation as error: fail(str(error))
                normalized.append(normalize(world, core_id, schema, variant, location)); valid_inputs += 1
        else:
            normalized = []
            for variant_index, variant in enumerate(variants):
                candidate = variant.get("candidate", variant) if isinstance(variant, dict) else variant
                try: world.validate(core_id, core["$defs"]["candidateTransaction"], candidate, f"{location}/inputVariants/{variant_index}")
                except SchemaViolation as error: fail(str(error))
                normalized.append(normalize_candidate(world, variant, excluded, location)); valid_inputs += 1
        canonical = [canonical_json(value) for value in normalized]; digested = [sha256_digest(value) for value in normalized]
        if relation == "equal":
            if not all(value == normalized[0] for value in normalized) or case["expectedNormalized"] != normalized[0] or case["expectedCanonicalJson"] != canonical[0] or case["expectedDigest"] != digested[0]:
                fail(f"{location}: equal projection/canonical/digest mismatch")
            validate_digest(case["expectedDigest"], location); digests += 1
        else:
            count = len(variants)
            if not all(isinstance(value, list) and len(value) == count for value in (case["expectedNormalized"], case["expectedCanonicalJson"], case["expectedDigest"])):
                fail(f"{location}: different expectations must align")
            if case["expectedNormalized"] != normalized or case["expectedCanonicalJson"] != canonical or case["expectedDigest"] != digested or len(set(digested)) != count:
                fail(f"{location}: different projection/canonical/digest mismatch")
            for value in case["expectedDigest"]: validate_digest(value, location)
            digests += count
    return digests, valid_inputs, schema_invalid, semantic_invalid


def load_rules(index: Any) -> dict[tuple[str, str], dict[str, Any]]:
    if not isinstance(index, dict) or not isinstance(index.get("canonicalCollections"), list): fail("canonical index lacks canonicalCollections")
    rules = {}
    for position, rule in enumerate(index["canonicalCollections"]):
        if not isinstance(rule, dict) or not isinstance(rule.get("schemaId"), str) or not isinstance(rule.get("schemaPointer"), str) or rule.get("kind") not in {"set", "ordered", "derived-ordered"}:
            fail(f"canonicalCollections[{position}] malformed")
        key = (rule["schemaId"], rule["schemaPointer"])
        if key in rules: fail(f"duplicate canonical rule {key}")
        rules[key] = rule
    return rules


def run(contracts: Path) -> None:
    world = SchemaWorld(contracts); vectors = contracts / "vectors"
    rules = load_rules(load_json(vectors / "core-canonical-projection-v1.json"))
    error_catalog = load_json(contracts / "error-codes-v1.json")
    if not isinstance(error_catalog, dict) or not isinstance(error_catalog.get("codes"), list): fail("error catalog lacks codes")
    error_codes = {entry.get("code") for entry in error_catalog["codes"] if isinstance(entry, dict) and isinstance(entry.get("code"), str)}
    checked, collection_digests = verify_collection_cases(world, load_json(vectors / "core-set-permutation-v1.json"), rules)
    candidate_digests, valid_inputs, schema_invalid, semantic_invalid = verify_candidate_cases(world, load_json(vectors / "candidate-local-ref-v1.json"), error_codes)
    print(f"canonical vector verification passed: {collection_digests + candidate_digests} digests, {checked} collection items, {valid_inputs} valid candidate/model inputs, {schema_invalid} schema negatives, {semantic_invalid} semantic negatives")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contracts-dir", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        run(args.contracts_dir.resolve())
    except (VerificationError, SchemaViolation) as error:
        print(f"canonical vector verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
