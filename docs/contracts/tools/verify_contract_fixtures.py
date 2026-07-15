#!/usr/bin/env python3
"""Execute the frozen Gate 0 standalone fixture contract.

The validator is intentionally stdlib-only.  It implements the closed Draft
2020-12 keyword subset used by the 19 Gate 0 schemas, then applies deterministic
Core-semantic checks whose stable boundary/code pairs are catalogued by Task 4A.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

import verify_fixture_catalog


class FixtureVerificationError(RuntimeError):
    pass


@dataclass(frozen=True)
class Classification:
    phase: str | None
    failure_boundary: str | None
    error_code: str | None


@dataclass(frozen=True)
class Summary:
    valid: int
    invalid: int
    schema_phase: int
    core_semantic_phase: int
    schema_roots: int


class SchemaFailure(RuntimeError):
    def __init__(self, path: str, message: str):
        super().__init__(f"{path}: {message}")
        self.path = path
        self.message = message


def load_strict_json(path: Path) -> Any:
    def object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise FixtureVerificationError(f"{path}: duplicate JSON object member {key!r}")
            result[key] = value
        return result

    def non_json_constant(value: str) -> Any:
        raise FixtureVerificationError(f"{path}: non-JSON numeric constant {value}")

    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=object_pairs, parse_constant=non_json_constant)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FixtureVerificationError(f"cannot load strict JSON {path}: {error}") from error


def _json_equal(left: Any, right: Any) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return left == right
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(_json_equal(a, b) for a, b in zip(left, right))
    if isinstance(left, dict) and isinstance(right, dict):
        return set(left) == set(right) and all(_json_equal(left[key], right[key]) for key in left)
    return type(left) is type(right) and left == right


class Draft202012Subset:
    def __init__(self, contracts: Path):
        self.contracts = contracts
        self.schemas: dict[str, tuple[Any, Path]] = {}
        for item in load_strict_json(contracts / "schema-catalog.json")["items"]:
            path = contracts / item["path"]
            self.schemas[item["id"]] = (load_strict_json(path), path)

    def validate(self, schema_id: str, instance: Any) -> None:
        root, path = self.schemas[schema_id]
        self._validate(root, instance, root, path, "$")

    def _resolve(self, reference: str, root: Any, root_path: Path) -> tuple[Any, Any, Path]:
        file_part, _, fragment = reference.partition("#")
        if file_part:
            target_path = (root_path.parent / file_part).resolve()
            target = load_strict_json(target_path)
        else:
            target_path, target = root_path, root
        node: Any = target
        if fragment:
            if not fragment.startswith("/"):
                raise FixtureVerificationError(f"unsupported schema anchor {reference!r}")
            for raw in fragment[1:].split("/"):
                token = raw.replace("~1", "/").replace("~0", "~")
                node = node[int(token)] if isinstance(node, list) else node[token]
        if not isinstance(node, (bool, dict)):
            raise FixtureVerificationError(f"schema reference {reference!r} does not resolve to a schema")
        return node, target, target_path

    @staticmethod
    def _matches_type(instance: Any, declared: str) -> bool:
        return {
            "null": instance is None,
            "boolean": isinstance(instance, bool),
            "integer": isinstance(instance, int) and not isinstance(instance, bool),
            "number": isinstance(instance, (int, float)) and not isinstance(instance, bool),
            "string": isinstance(instance, str),
            "array": isinstance(instance, list),
            "object": isinstance(instance, dict),
        }[declared]

    def _is_valid(self, schema: Any, instance: Any, root: Any, root_path: Path) -> bool:
        try:
            self._validate(schema, instance, root, root_path, "$")
            return True
        except SchemaFailure:
            return False

    def _validate(self, schema: Any, instance: Any, root: Any, root_path: Path, path: str) -> None:
        if schema is True:
            return
        if schema is False:
            raise SchemaFailure(path, "false schema rejected instance")
        if not isinstance(schema, dict):
            raise FixtureVerificationError(f"{root_path}: schema at {path} is neither an object nor a boolean")
        if "$ref" in schema:
            target, target_root, target_path = self._resolve(schema["$ref"], root, root_path)
            self._validate(target, instance, target_root, target_path, path)
        if "type" in schema:
            types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
            if not any(self._matches_type(instance, declared) for declared in types):
                raise SchemaFailure(path, f"expected type {types}")
        if "const" in schema and not _json_equal(instance, schema["const"]):
            raise SchemaFailure(path, "const mismatch")
        if "enum" in schema and not any(_json_equal(instance, value) for value in schema["enum"]):
            raise SchemaFailure(path, "enum mismatch")
        if isinstance(instance, str):
            if len(instance) < schema.get("minLength", 0):
                raise SchemaFailure(path, "string shorter than minLength")
            if "pattern" in schema and re.search(schema["pattern"], instance) is None:
                raise SchemaFailure(path, "pattern mismatch")
            if schema.get("format") == "date-time":
                if re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})", instance) is None:
                    raise SchemaFailure(path, "invalid RFC 3339 date-time")
                try:
                    datetime.fromisoformat(instance.replace("Z", "+00:00"))
                except ValueError as error:
                    raise SchemaFailure(path, "invalid date-time") from error
        if isinstance(instance, (int, float)) and not isinstance(instance, bool):
            if "minimum" in schema and instance < schema["minimum"]:
                raise SchemaFailure(path, "below minimum")
            if "maximum" in schema and instance > schema["maximum"]:
                raise SchemaFailure(path, "above maximum")
            if "exclusiveMinimum" in schema and instance <= schema["exclusiveMinimum"]:
                raise SchemaFailure(path, "not above exclusiveMinimum")
        if isinstance(instance, list):
            if len(instance) < schema.get("minItems", 0):
                raise SchemaFailure(path, "too few items")
            if "maxItems" in schema and len(instance) > schema["maxItems"]:
                raise SchemaFailure(path, "too many items")
            if schema.get("uniqueItems"):
                if any(_json_equal(instance[left], instance[right]) for left in range(len(instance)) for right in range(left + 1, len(instance))):
                    raise SchemaFailure(path, "duplicate array item")
            if "items" in schema:
                for index, value in enumerate(instance):
                    self._validate(schema["items"], value, root, root_path, f"{path}[{index}]")
            if "contains" in schema:
                count = sum(self._is_valid(schema["contains"], value, root, root_path) for value in instance)
                if count < schema.get("minContains", 1):
                    raise SchemaFailure(path, "contains minimum not met")
                if "maxContains" in schema and count > schema["maxContains"]:
                    raise SchemaFailure(path, "contains maximum exceeded")
        if isinstance(instance, dict):
            for member in schema.get("required", []):
                if member not in instance:
                    raise SchemaFailure(path, f"missing required property {member}")
            properties = schema.get("properties", {})
            for member, value in instance.items():
                if member in properties:
                    self._validate(properties[member], value, root, root_path, f"{path}.{member}")
                elif "additionalProperties" in schema:
                    self._validate(schema["additionalProperties"], value, root, root_path, f"{path}.{member}")
        for branch in schema.get("allOf", []):
            self._validate(branch, instance, root, root_path, path)
        if "anyOf" in schema and not any(self._is_valid(branch, instance, root, root_path) for branch in schema["anyOf"]):
            raise SchemaFailure(path, "no anyOf branch matched")
        if "oneOf" in schema:
            matches = sum(self._is_valid(branch, instance, root, root_path) for branch in schema["oneOf"])
            if matches != 1:
                raise SchemaFailure(path, f"expected exactly one oneOf branch, got {matches}")
        if "not" in schema and self._is_valid(schema["not"], instance, root, root_path):
            raise SchemaFailure(path, "not schema matched")
        if "if" in schema:
            branch_key = "then" if self._is_valid(schema["if"], instance, root, root_path) else "else"
            if branch_key in schema:
                self._validate(schema[branch_key], instance, root, root_path, path)


SUPPORTED_SCHEMA_KEYWORDS = {
    "$schema", "$id", "$ref", "$defs", "$comment", "title", "description", "default",
    "type", "const", "enum", "required", "properties", "additionalProperties",
    "items", "minItems", "maxItems", "uniqueItems", "contains", "minContains", "maxContains",
    "minLength", "pattern", "format", "minimum", "maximum", "exclusiveMinimum",
    "allOf", "anyOf", "oneOf", "not", "if", "then", "else", "x-ipcraft-canonical",
}


def audit_schema_keywords(contracts: Path) -> set[str]:
    """Return unknown keywords and malformed supported-keyword locations."""
    unknown: set[str] = set()

    schema_types = {"array", "boolean", "integer", "null", "number", "object", "string"}

    def malformed(location: str, key: str) -> None:
        unknown.add(f"{location}/{key}:invalid-shape")

    def is_non_negative_integer(value: Any) -> bool:
        return isinstance(value, int) and not isinstance(value, bool) and value >= 0

    def is_number(value: Any) -> bool:
        return isinstance(value, (int, float)) and not isinstance(value, bool)

    def is_schema(value: Any) -> bool:
        return isinstance(value, (bool, dict))

    def json_values_are_unique(values: list[Any]) -> bool:
        return not any(_json_equal(values[left], values[right]) for left in range(len(values)) for right in range(left + 1, len(values)))

    def walk(schema: Any, location: str) -> None:
        if isinstance(schema, bool):
            return
        if not isinstance(schema, dict):
            unknown.add(f"{location}:invalid-schema")
            return
        for key in schema:
            if key not in SUPPORTED_SCHEMA_KEYWORDS:
                unknown.add(f"{location}/{key}")

        for key in ("$schema", "$id", "$ref", "$comment", "title", "description"):
            if key in schema and not isinstance(schema[key], str):
                malformed(location, key)
        if "$schema" in schema and schema["$schema"] != "https://json-schema.org/draft/2020-12/schema":
            malformed(location, "$schema")
        if "$ref" in schema and schema["$ref"] == "":
            malformed(location, "$ref")

        if "type" in schema:
            declared = schema["type"]
            if isinstance(declared, str):
                valid_type = declared in schema_types
            elif isinstance(declared, list):
                valid_type = (
                    bool(declared)
                    and all(isinstance(item, str) and item in schema_types for item in declared)
                    and len(declared) == len(set(declared))
                )
            else:
                valid_type = False
            if not valid_type:
                malformed(location, "type")
        if "enum" in schema:
            values = schema["enum"]
            if not isinstance(values, list) or not values or not json_values_are_unique(values):
                malformed(location, "enum")
        if "required" in schema:
            required = schema["required"]
            if not isinstance(required, list) or not all(isinstance(item, str) for item in required) or len(required) != len(set(required)):
                malformed(location, "required")

        for key in ("minItems", "maxItems", "minContains", "maxContains", "minLength"):
            if key in schema and not is_non_negative_integer(schema[key]):
                malformed(location, key)
        if "uniqueItems" in schema and not isinstance(schema["uniqueItems"], bool):
            malformed(location, "uniqueItems")
        for key in ("minimum", "maximum", "exclusiveMinimum"):
            if key in schema and not is_number(schema[key]):
                malformed(location, key)
        if "pattern" in schema:
            pattern = schema["pattern"]
            if not isinstance(pattern, str):
                malformed(location, "pattern")
            else:
                try:
                    re.compile(pattern)
                except re.error:
                    malformed(location, "pattern")
        if "format" in schema and schema["format"] != "date-time":
            malformed(location, "format")

        canonical = schema.get("x-ipcraft-canonical")
        if "x-ipcraft-canonical" in schema:
            if not isinstance(canonical, dict) or set(canonical) - {"kind", "sortKey"}:
                malformed(location, "x-ipcraft-canonical")
            elif canonical.get("kind") == "ordered":
                if set(canonical) != {"kind"}:
                    malformed(location, "x-ipcraft-canonical")
            elif canonical.get("kind") in {"set", "derived-ordered"}:
                sort_key = canonical.get("sortKey")
                if not isinstance(sort_key, list) or not sort_key or not all(isinstance(item, str) and item for item in sort_key) or len(sort_key) != len(set(sort_key)):
                    malformed(location, "x-ipcraft-canonical")
            else:
                malformed(location, "x-ipcraft-canonical")

        for member in ("properties", "$defs"):
            if member not in schema:
                continue
            values = schema[member]
            if not isinstance(values, dict):
                malformed(location, member)
            else:
                for key, child in values.items():
                    if not is_schema(child):
                        malformed(f"{location}/{member}", key)
                    walk(child, f"{location}/{member}/{key}")
        for member in ("items", "additionalProperties", "contains", "not", "if", "then", "else"):
            if member in schema:
                child = schema[member]
                if not is_schema(child):
                    malformed(location, member)
                walk(child, f"{location}/{member}")
        for member in ("allOf", "anyOf", "oneOf"):
            if member not in schema:
                continue
            children = schema[member]
            if not isinstance(children, list) or not children:
                malformed(location, member)
            else:
                for index, child in enumerate(children):
                    if not is_schema(child):
                        malformed(f"{location}/{member}", str(index))
                    walk(child, f"{location}/{member}/{index}")

    for item in load_strict_json(contracts / "schema-catalog.json")["items"]:
        walk(load_strict_json(contracts / item["path"]), item["id"])
    return unknown


SCHEMA_BOUNDARY = {
    "ipcraft.artifact-manifest.v1": ("tool-artifact", "tool.artifact_invalid"),
    "ipcraft.bundle-manifest.v1": ("bundle-manifest", "dependency.manifest_invalid"),
    "ipcraft.command-result.v1": ("command-result", "command.result_invalid"),
    "ipcraft.diagnostic-report.v1": ("diagnostic-report", "diagnostic.report_invalid"),
    "ipcraft.noc-side-effects.v1": ("host-side-effect-result", "host.side_effect_result_invalid"),
    "ipcraft.output-manifest.v1": ("output-manifest", "output.manifest_invalid"),
    "ipcraft.patch.v1": ("patch-envelope", "patch.schema_invalid"),
    "ipcraft.pipeline-result.v1": ("pipeline-result", "pipeline.result_invalid"),
    "ipcraft.tool-input.v1": ("tool-input", "tool.input_invalid"),
    "ipcraft.tool-result.v1": ("tool-result", "tool.result_invalid"),
}


def _duplicates(values: list[Any], key) -> bool:
    keys = [key(value) for value in values]
    return len(keys) != len(set(keys))


def _portable_collisions(entries: list[dict[str, Any]]) -> bool:
    paths = [entry.get("path") for entry in entries]
    return len(paths) != len(set(paths))


def _declared_scalar_matches(kind: str, value: Any) -> bool:
    if value is None:
        return True
    if kind == "bool":
        return isinstance(value, bool)
    if kind == "int":
        return isinstance(value, int) and not isinstance(value, bool)
    if kind == "double":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if kind in {"string", "enum"}:
        return isinstance(value, str) if kind == "string" else True
    return False


def semantic_failure(schema_id: str, document: Any) -> tuple[str, str] | None:
    if schema_id == "ipcraft.project-design.v1":
        groups = [document.get(name, []) for name in ("components", "interfaces", "topologies")]
        topology = document.get("topologies", [{}])[0] if document.get("topologies") else {}
        groups += [topology.get(name, []) for name in ("routers", "structuralLinks", "accessSlots", "attachments", "domains", "domainMemberships", "packageEntities", "packageRelations")]
        ids = [document.get("id")]
        ids += [item.get("id") for group in groups for item in group if isinstance(item, dict)]
        ids += [item.get("lockId") for item in document.get("dependencies", []) if isinstance(item, dict)]
        if len(ids) != len(set(ids)):
            return "project-duplicate-id", "project.duplicate_id"
        known = set(ids)
        components = {item["id"]: item for item in document.get("components", [])}
        dependencies = {item["lockId"]: item for item in document.get("dependencies", [])}
        interfaces = {item["id"]: item for item in document.get("interfaces", [])}
        routers_by_id = {item["id"]: item for item in topology.get("routers", [])}
        slots_by_id = {item["id"]: item for item in topology.get("accessSlots", [])}
        if topology.get("ownerComponentId") not in components:
            return "project-reference", "project.unknown_reference"
        for component in components.values():
            dependency = dependencies.get(component.get("packageLockId"))
            if dependency is None or dependency.get("kind") != "noc-package":
                return "project-reference", "project.unknown_reference"
        for interface in interfaces.values():
            if interface.get("ownerComponentId") not in components:
                return "project-reference", "project.unknown_reference"
            dependency = dependencies.get(interface.get("contract", {}).get("lockId"))
            if dependency is None or dependency.get("kind") != "interface-contract":
                return "project-reference", "project.unknown_reference"
        derivation = topology.get("derivation", {})
        engine_lock = dependencies.get(derivation.get("defaultEngineLockId"))
        authority_lock = dependencies.get(derivation.get("structureAuthority", {}).get("lockId"))
        if engine_lock is None or engine_lock.get("kind") != "default-engine" or authority_lock is not engine_lock:
            return "project-reference", "project.unknown_reference"
        package_lock = dependencies.get(next(iter(components.values())).get("packageLockId")) if components else None
        authority = derivation.get("structureAuthority", {})
        if (
            package_lock is None
            or derivation.get("packageBundleDigest") != package_lock.get("bundleManifestDigest")
            or derivation.get("defaultEngineBundleDigest") != engine_lock.get("bundleManifestDigest")
            or authority.get("bundleDigest") != engine_lock.get("bundleManifestDigest")
            or authority.get("identity") != engine_lock.get("id")
            or authority.get("version") != engine_lock.get("version")
            or derivation.get("engineHostContractVersion") != engine_lock.get("engineHostContractVersion")
            or derivation.get("hostSideEffectContractVersion") != engine_lock.get("hostSideEffectContractVersion")
            or derivation.get("engineCompatibilityVersion") != engine_lock.get("engineCompatibilityVersion")
        ):
            return "project-invariant", "project.invariant_violation"
        extension_owners = document.get("extensions", []) + topology.get("extensions", [])
        extension_owners += [extension for item in components.values() for extension in item.get("extensions", [])]
        extension_owners += [extension for item in interfaces.values() for extension in item.get("extensions", [])]
        extension_owners += [extension for item in topology.get("packageEntities", []) for extension in item.get("extensions", [])]
        extension_owners += [extension for item in topology.get("packageRelations", []) for extension in item.get("extensions", [])]
        for extension in extension_owners:
            if extension.get("ownerLockId") not in dependencies:
                return "project-reference", "project.unknown_reference"
        for dependency in dependencies.values():
            if dependency.get("kind") == "runtime":
                closure = dependency.get("runtimeClosure", {})
                if dependency.get("bundleManifestDigest") != closure.get("runtimeDistributionBundleDigest"):
                    return "project-invariant", "project.invariant_violation"
            if dependency.get("kind") in {"extension-provider", "drc-tool", "generator-tool"}:
                runtime = dependencies.get(dependency.get("runtimeLockId"))
                if runtime is None or runtime.get("kind") != "runtime":
                    return "project-reference", "project.unknown_reference"

        coordinates = [(item["coordinate"]["row"], item["coordinate"]["column"]) for item in routers_by_id.values()]
        if len(coordinates) != len(set(coordinates)):
            return "project-invariant", "project.invariant_violation"
        unordered_links: set[tuple[str, str]] = set()
        for link in topology.get("structuralLinks", []):
            endpoint_a, endpoint_b = link.get("endpointA"), link.get("endpointB")
            if endpoint_a not in routers_by_id or endpoint_b not in routers_by_id:
                return "project-reference", "project.unknown_reference"
            if endpoint_a == endpoint_b:
                return "project-invariant", "project.invariant_violation"
            pair = tuple(sorted((endpoint_a, endpoint_b)))
            if pair in unordered_links:
                return "project-invariant", "project.invariant_violation"
            unordered_links.add(pair)
            left, right = routers_by_id[endpoint_a]["coordinate"], routers_by_id[endpoint_b]["coordinate"]
            row_delta, column_delta = abs(left["row"] - right["row"]), abs(left["column"] - right["column"])
            if row_delta + column_delta != 1:
                return "project-invariant", "project.invariant_violation"
            expected_axis = "vertical" if row_delta else "horizontal"
            if link.get("axis") != expected_axis:
                return "project-invariant", "project.invariant_violation"

        slot_keys: set[tuple[str, str]] = set()
        for slot in slots_by_id.values():
            if slot.get("routerId") not in routers_by_id:
                return "project-reference", "project.unknown_reference"
            key = (slot["routerId"], slot["templateKey"])
            if key in slot_keys:
                return "project-invariant", "project.invariant_violation"
            slot_keys.add(key)
            allowed_contract_ids = [item["contractLockId"] for item in slot["allowedContracts"]]
            if len(allowed_contract_ids) != len(set(allowed_contract_ids)):
                return "project-invariant", "project.invariant_violation"
            if any(dependencies.get(lock_id, {}).get("kind") != "interface-contract" for lock_id in allowed_contract_ids):
                return "project-reference", "project.unknown_reference"

        interface_attachments: set[str] = set()
        occupied_slots: set[str] = set()
        for attachment in topology.get("attachments", []):
            interface_id = attachment.get("interfaceId")
            if interface_id not in interfaces:
                return "project-reference", "project.unknown_reference"
            if interface_id in interface_attachments:
                return "project-invariant", "project.invariant_violation"
            interface_attachments.add(interface_id)
            if attachment.get("state") == "resolved":
                slot = slots_by_id.get(attachment.get("slotId"))
                if slot is None or attachment.get("routerId") not in routers_by_id:
                    return "project-reference", "project.unknown_reference"
                if slot["routerId"] != attachment.get("routerId"):
                    return "project-invariant", "project.invariant_violation"
                if slot["id"] in occupied_slots:
                    return "project-invariant", "project.invariant_violation"
                occupied_slots.add(slot["id"])
                interface = interfaces[interface_id]
                contract_lock = interface["contract"]["lockId"]
                allowance = next((item for item in slot["allowedContracts"] if item["contractLockId"] == contract_lock), None)
                if allowance is None or interface["contract"]["role"] not in allowance["roles"]:
                    return "project-invariant", "project.invariant_violation"
                capabilities = interface.get("capabilities", {})
                if any(capabilities.get(key) != value for key, value in allowance["capabilityConstraints"].items()):
                    return "project-invariant", "project.invariant_violation"
        for membership in topology.get("domainMemberships", []):
            if membership.get("routerId") not in known or membership.get("domainId") not in known:
                return "project-reference", "project.unknown_reference"
        subject_kinds = {
            document["id"]: "project", **{item["id"]: "component" for item in components.values()},
            **{item["id"]: "interface" for item in interfaces.values()},
            **{item["id"]: "router" for item in routers_by_id.values()},
            **{item["id"]: "structural-link" for item in topology.get("structuralLinks", [])},
            **{item["id"]: "access-slot" for item in slots_by_id.values()},
            **{item["id"]: "attachment" for item in topology.get("attachments", [])},
            **{item["id"]: "domain" for item in topology.get("domains", [])},
            **{item["id"]: "domain-membership" for item in topology.get("domainMemberships", [])},
            **{item["id"]: "package-entity" for item in topology.get("packageEntities", [])},
            **{item["id"]: "package-relation" for item in topology.get("packageRelations", [])},
        }
        for relation in topology.get("packageRelations", []):
            for endpoint in relation.get("sources", []) + relation.get("targets", []):
                if endpoint.get("state") == "resolved":
                    subject = endpoint["subject"]
                    if subject_kinds.get(subject["id"]) != subject["kind"]:
                        return "project-reference", "project.unknown_reference"

        routers = set(routers_by_id)
        domains = {item.get("id"): item.get("typeKey") for item in topology.get("domains", [])}
        domain_types = set(domains.values())
        for domain_type in domain_types:
            defaults = [item for item in topology.get("domains", []) if item["typeKey"] == domain_type and item["isDefault"]]
            if len(defaults) != 1:
                return "project-invariant", "project.invariant_violation"
        counts = {(router, domain_type): 0 for router in routers for domain_type in domain_types}
        for membership in topology.get("domainMemberships", []):
            key = (membership.get("routerId"), domains.get(membership.get("domainId")))
            if key in counts: counts[key] += 1
        if routers and (not domain_types or any(count != 1 for count in counts.values())):
            return "project-invariant", "project.invariant_violation"
        adjacency = {router: set() for router in routers}
        for link in topology.get("structuralLinks", []):
            adjacency[link["endpointA"]].add(link["endpointB"])
            adjacency[link["endpointB"]].add(link["endpointA"])
        for domain_id in domains:
            members = {item["routerId"] for item in topology.get("domainMemberships", []) if item["domainId"] == domain_id}
            domain = next(item for item in topology.get("domains", []) if item["id"] == domain_id)
            if not members and not domain["isDefault"]:
                return "project-invariant", "project.invariant_violation"
            if len(members) > 1:
                reached: set[str] = set()
                frontier = [next(iter(members))]
                while frontier:
                    router = frontier.pop()
                    if router in reached:
                        continue
                    reached.add(router)
                    frontier.extend((adjacency[router] & members) - reached)
                if reached != members:
                    return "project-invariant", "project.invariant_violation"
    elif schema_id in {"ipcraft.bundle-manifest.v1", "ipcraft.artifact-manifest.v1"}:
        member = "files" if "files" in document else "artifacts"
        if _portable_collisions(document.get(member, [])):
            return ("bundle-manifest", "dependency.manifest_invalid") if member == "files" else ("tool-artifact", "tool.artifact_invalid")
        if member == "files":
            digest_input = {key: value for key, value in document.items() if key != "manifestDigest"}
            canonical = json.dumps(digest_input, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            if document.get("manifestDigest") != "sha256:" + hashlib.sha256(canonical.encode()).hexdigest():
                return "bundle-manifest", "dependency.manifest_invalid"
    elif schema_id == "ipcraft.interface-contract.v1":
        for member in ("roles", "capabilities", "fields"):
            if _duplicates(document.get(member, []), lambda item: item.get("key")):
                return "contract-declaration", "contract.invariant_violation"
        field_keys = {item.get("key") for item in document.get("fields", [])}
        for capability in document.get("capabilities", []):
            default = capability.get("default")
            values = capability.get("values")
            if not _declared_scalar_matches(capability.get("type"), default):
                return "contract-declaration", "contract.invariant_violation"
            if capability.get("required") and default is None:
                return "contract-declaration", "contract.invariant_violation"
            if values is not None and (any(not _declared_scalar_matches(capability.get("type"), value) for value in values) or default not in values):
                return "contract-declaration", "contract.invariant_violation"
        for field in document.get("fields", []):
            default = field.get("default")
            if not _declared_scalar_matches(field.get("type"), default):
                return "contract-declaration", "contract.invariant_violation"
            if field.get("required") and default is None:
                return "contract-declaration", "contract.invariant_violation"
            minimum, maximum = field.get("minimum"), field.get("maximum")
            if minimum is not None and maximum is not None and minimum > maximum:
                return "contract-declaration", "contract.invariant_violation"
            if isinstance(default, (int, float)) and not isinstance(default, bool):
                if minimum is not None and default < minimum or maximum is not None and default > maximum:
                    return "contract-declaration", "contract.invariant_violation"
            values = field.get("values")
            if values is not None and (any(not _declared_scalar_matches(field.get("type"), value) for value in values) or default not in values):
                return "contract-declaration", "contract.invariant_violation"
            for condition_name in ("visibleWhen", "enabledWhen"):
                condition = field.get(condition_name)
                if condition and condition.get("field") not in field_keys:
                    return "contract-declaration", "contract.invariant_violation"
    elif schema_id == "ipcraft.noc-package.v1":
        for member, key in (("interfaceTemplates", "key"), ("domainTypes", "key"), ("packageEntityTypes", "typeKey"), ("packageRelationTypes", "typeKey")):
            if _duplicates(document.get(member, []), lambda item: item.get(key)):
                return "package-declaration", "package.invariant_violation"
        global_fields = {item.get("key"): item for item in document.get("configuration", {}).get("global", {}).get("fields", [])}
        for member in ("rowField", "columnField"):
            field = global_fields.get(document.get("topology", {}).get(member))
            if not field or field.get("type") != "int" or field.get("topologyDriving") is not True:
                return "package-declaration", "package.invariant_violation"
    elif schema_id == "ipcraft.engine-bundle.v1":
        if document.get("engineHostContractVersion") != "ipcraft.engine-host.v1":
            return "engine-host-contract", "engine.host_contract_unsupported"
        if not any(value == "linux-x86_64-gnu-v1" for value in document.get("supportedPlatformAbis", [])):
            return "engine-platform", "engine.platform_unsupported"
    elif schema_id == "ipcraft.recovery.v1":
        if document.get("projectId") != document.get("authoritativeDesign", {}).get("id"):
            return "recovery-binding", "recovery.binding_mismatch"
    elif schema_id == "ipcraft.tool-input.v1":
        if document.get("kind") == "generator" and document.get("snapshotDigest") != document.get("formallySavedProjectDigest"):
            return "tool-input", "tool.input_invalid"
        if _duplicates(document.get("dependencies", []), lambda item: item.get("lockId")):
            return "tool-input", "tool.input_invalid"
    elif schema_id == "ipcraft.tool-result.v1":
        if document.get("expectedInvocationId") and document.get("expectedInvocationId") != document.get("invocationId"):
            return "runtime-result-binding", "tool.result_mismatch"
    elif schema_id == "ipcraft.pipeline-result.v1":
        step_ids = {item.get("stepId") for item in document.get("steps", [])}
        if document.get("failedStepId") is not None and document.get("failedStepId") not in step_ids:
            return "pipeline-result", "pipeline.result_invalid"
    elif schema_id == "ipcraft.diagnostic-report.v1":
        if _duplicates(document.get("diagnostics", []), lambda item: json.dumps(item, sort_keys=True)):
            return "diagnostic-report", "diagnostic.report_invalid"
    elif schema_id == "ipcraft.patch.v1":
        source = document.get("source", {}).get("kind")
        operations = document.get("operations", [])
        local_refs: list[str] = []
        ids: list[str] = []
        for operation in operations:
            if "localRef" in operation: local_refs.append(operation["localRef"])
            if "id" in operation: ids.append(operation["id"])
        if len(local_refs) != len(set(local_refs)):
            return "local-reference", "patch.local_ref_invalid"
        if len(ids) != len(set(ids)):
            return "duplicate-id", "patch.duplicate_id"
        required_properties = {
            "component": {"kind", "name", "packageLockId", "typeKey", "config", "extensions"},
            "interface": {"ownerComponentId", "templateKey", "name", "contract", "capabilities", "contractConfig", "nocConfig", "extensions"},
            "router": {"templateKey", "identityCompatibilityVersion", "coordinate", "properties"},
            "structural-link": {"templateKey", "identityCompatibilityVersion", "endpointA", "endpointB", "axis", "properties"},
            "access-slot": {"routerId", "templateKey", "identityCompatibilityVersion", "displayOrder", "label", "allowedContracts", "properties"},
            "domain": {"typeKey", "name", "isDefault", "config"},
            "package-entity": {"typeKey", "data", "extensions"},
        }
        for operation in operations:
            if operation.get("op") in {"updateEntity", "updateRelation"} and set(operation.get("set", {})) & set(operation.get("unset", [])):
                return "patch-invariant", "patch.invariant_violation"
            if operation.get("op") == "updateEntity" and set(operation.get("unset", [])) & required_properties.get(operation.get("entityKind"), set()):
                return "patched-subject-schema", "patch.schema_violation"
        if source == "user-command" and any(operation.get("entityKind") in {"router", "structural-link", "access-slot"} for operation in operations):
            return "ownership", "patch.ownership_violation"
        if source in {"default-engine", "extension-provider"} and any(operation.get("entityKind") in {"project", "interface", "domain"} for operation in operations):
            return "ownership", "patch.ownership_violation"
        existence = {(item.get("entityKind"), item.get("id")): item.get("kind") for item in document.get("preconditions", []) if item.get("kind") in {"entity-exists", "entity-absent"}}
        if any(
            {item.get("kind") for item in document.get("preconditions", []) if (item.get("entityKind"), item.get("id")) == key}
            == {"entity-exists", "entity-absent"}
            for key in existence
        ):
            return "precondition", "patch.precondition_failed"
    elif schema_id == "ipcraft.core-canonical-models.v1":
        if document.get("schema") == "ipcraft.candidate-transaction.v1":
            if document.get("kind") == "default-engine-migration":
                migration = document.get("migration", {})
                current = migration.get("currentDefaultEngineLock", {}).get("bundleManifestDigest")
                target = migration.get("targetDefaultEngineLock", {}).get("bundleManifestDigest")
                if not current or not target or current == target or current != document.get("applicability", {}).get("defaultEngineBundleDigest"):
                    return "engine-migration-binding", "engine.migration_invalid"
            refs = document.get("allocationOrder", [])
            if len(refs) != len(set(refs)):
                return "generic-structure", "contract.schema_invalid"
    elif schema_id == "ipcraft.noc-side-effects.v1":
        expected = document.get("expected", {})
        impacts = expected.get("impactReport", {}).get("impacts", [])
        destructive = any(item.get("code") == "domain.non_default_deleted" for item in impacts)
        if destructive != bool(expected.get("requiresConfirmation")):
            return "host-side-effect-result", "host.side_effect_result_invalid"
    return None


def classify_document(contracts: Path, schema_id: str, document: Any) -> Classification:
    if schema_id == "ipcraft.project-design.v1" and isinstance(document, dict) and "project" in document and "schema" not in document:
        return Classification("schema", "legacy-project-root", "project.legacy_format_unsupported")
    validator = Draft202012Subset(contracts)
    try:
        validator.validate(schema_id, document)
    except SchemaFailure as failure:
        if schema_id == "ipcraft.patch.v1" and ".operations" in failure.path:
            boundary, code = "operation-shape", "patch.operation_invalid"
        else:
            boundary, code = SCHEMA_BOUNDARY.get(schema_id, ("generic-structure", "contract.schema_invalid"))
        return Classification("schema", boundary, code)
    semantic = semantic_failure(schema_id, document)
    if semantic:
        return Classification("core-semantic", semantic[0], semantic[1])
    return Classification(None, None, None)


def load_catalog(contracts: Path) -> list[dict[str, Any]]:
    return load_strict_json(contracts / "fixture-catalog.json")["items"]


def copy_contract_tree(source: Path, destination: Path) -> None:
    shutil.copytree(source, destination)


def verify_all(contracts: Path) -> Summary:
    unsupported = audit_schema_keywords(contracts)
    if unsupported:
        raise FixtureVerificationError(f"unsupported JSON Schema keywords: {sorted(unsupported)}")
    try:
        fixture_count, schema_count, _ = verify_fixture_catalog.verify(contracts / "fixture-catalog.json", contracts)
    except verify_fixture_catalog.VerificationError as error:
        raise FixtureVerificationError(str(error)) from error
    items = load_catalog(contracts)
    valid = invalid = schema_phase = semantic_phase = 0
    for entry in items:
        document = load_strict_json(contracts / entry["path"])
        observed = classify_document(contracts, entry["schemaId"], document)
        if entry["expected"] == "accept":
            valid += 1
            if observed.phase is not None:
                raise FixtureVerificationError(f"{entry['path']} expected accept, observed {observed}")
        else:
            invalid += 1
            if observed.phase == "schema": schema_phase += 1
            if observed.phase == "core-semantic": semantic_phase += 1
            expected = Classification(entry["validationPhase"], entry["failureBoundary"], entry["errorCode"])
            if observed != expected:
                raise FixtureVerificationError(f"{entry['path']} expected {expected}, observed {observed}")
    if fixture_count != len(items):
        raise FixtureVerificationError("fixture catalog count changed during verification")
    return Summary(valid, invalid, schema_phase, semantic_phase, schema_count - 1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contracts-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        result = verify_all(args.contracts_root.resolve())
    except (FixtureVerificationError, OSError, json.JSONDecodeError) as error:
        print(f"contract fixture verification failed: {error}", file=sys.stderr)
        return 1
    print(f"contract fixture verification passed: {result.valid} valid, {result.invalid} invalid; "
          f"{result.schema_phase} schema-phase, {result.core_semantic_phase} core-semantic; "
          f"{result.schema_roots} standalone schema roots")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
