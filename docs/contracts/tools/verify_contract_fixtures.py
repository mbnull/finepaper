#!/usr/bin/env python3
"""Execute the frozen Gate 0 standalone fixture contract.

The validator is intentionally stdlib-only.  It implements the closed Draft
2020-12 keyword subset used by the 19 Gate 0 schemas, then applies deterministic
Core-semantic checks whose stable boundary/code pairs are catalogued by Task 4A.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import re
import shutil
import sys
from dataclasses import dataclass
from datetime import datetime
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any

import verify_fixture_catalog


FROZEN_COVERAGE_REQUIREMENTS_DIGEST = "sha256:6128d13d3bb6bf26da98921743074d2731f91e12e447b6675d898d0cc0233edc"
FROZEN_COVERAGE_CONTRACT_DIGEST = "sha256:a2d5e2b68ca4c0788dedbe1ae08a609383d04e1e3fcfd3a37701237ac5fbc1b3"
FROZEN_PATCH_CONTEXT_DIGEST = "sha256:8f7c03e73dc634220222442202b9cb7650c7251fd2edca69deb976230e3fa9a2"


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
        return json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=object_pairs,
            parse_float=Decimal, parse_constant=non_json_constant,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FixtureVerificationError(f"cannot load strict JSON {path}: {error}") from error


def _json_equal(left: Any, right: Any) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left == right
    if (
        isinstance(left, (int, float, Decimal)) and not isinstance(left, bool)
        and isinstance(right, (int, float, Decimal)) and not isinstance(right, bool)
    ):
        return left == right
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(_json_equal(a, b) for a, b in zip(left, right))
    if isinstance(left, dict) and isinstance(right, dict):
        return set(left) == set(right) and all(_json_equal(left[key], right[key]) for key in left)
    return type(left) is type(right) and left == right


def _resolve_json_pointer(document: Any, pointer: str) -> Any:
    if pointer == "":
        return document
    if not pointer.startswith("/"):
        raise FixtureVerificationError(f"invalid RFC 6901 pointer {pointer!r}")
    node = document
    for raw in pointer[1:].split("/"):
        if re.search(r"~(?:[^01]|$)", raw):
            raise FixtureVerificationError(f"invalid RFC 6901 escape in {pointer!r}")
        token = raw.replace("~1", "/").replace("~0", "~")
        if isinstance(node, list):
            if re.fullmatch(r"0|[1-9][0-9]*", token) is None:
                raise FixtureVerificationError(f"invalid RFC 6901 array index {token!r}")
            index = int(token)
            if index >= len(node):
                raise FixtureVerificationError(f"RFC 6901 array index out of range: {token!r}")
            node = node[index]
        elif isinstance(node, dict):
            if token not in node:
                raise FixtureVerificationError(f"RFC 6901 object member not found: {token!r}")
            node = node[token]
        else:
            raise FixtureVerificationError(f"RFC 6901 pointer traverses a scalar at {token!r}")
    return node


class Draft202012Subset:
    def __init__(self, contracts: Path):
        self.contracts = contracts
        self.schemas: dict[str, tuple[Any, Path]] = {}
        self.schema_ids_by_filename: dict[str, str] = {}
        for item in load_strict_json(contracts / "schema-catalog.json")["items"]:
            path = contracts / item["path"]
            self.schemas[item["id"]] = (load_strict_json(path), path)
            self.schema_ids_by_filename[path.name] = item["id"]

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
            node = _resolve_json_pointer(target, fragment)
        if not isinstance(node, (bool, dict)):
            raise FixtureVerificationError(f"schema reference {reference!r} does not resolve to a schema")
        return node, target, target_path

    @staticmethod
    def _matches_type(instance: Any, declared: str) -> bool:
        is_number = (
            isinstance(instance, (int, float, Decimal))
            and not isinstance(instance, bool)
            and (
                (not isinstance(instance, float) or math.isfinite(instance))
                and (not isinstance(instance, Decimal) or instance.is_finite())
            )
        )
        return {
            "null": instance is None,
            "boolean": isinstance(instance, bool),
            "integer": is_number and (
                isinstance(instance, int)
                or (isinstance(instance, float) and instance.is_integer())
                or (isinstance(instance, Decimal) and instance == instance.to_integral_value())
            ),
            "number": is_number,
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
        if (
            isinstance(instance, float) and not math.isfinite(instance)
            or isinstance(instance, Decimal) and not instance.is_finite()
        ):
            raise SchemaFailure(path, "non-finite value is not a JSON number")
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
            if "pattern" in schema and re.search(_translate_portable_pattern(schema["pattern"]), instance) is None:
                raise SchemaFailure(path, "pattern mismatch")
            if schema.get("format") == "date-time":
                if re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})", instance) is None:
                    raise SchemaFailure(path, "invalid RFC 3339 date-time")
                try:
                    datetime.fromisoformat(instance.replace("Z", "+00:00"))
                except ValueError as error:
                    raise SchemaFailure(path, "invalid date-time") from error
        if isinstance(instance, (int, float, Decimal)) and not isinstance(instance, bool):
            if "minimum" in schema and instance < schema["minimum"]:
                raise SchemaFailure(path, "below minimum")
            if "maximum" in schema and instance > schema["maximum"]:
                raise SchemaFailure(path, "above maximum")
            if "exclusiveMinimum" in schema and instance <= schema["exclusiveMinimum"]:
                raise SchemaFailure(path, "not above exclusiveMinimum")
            if "multipleOf" in schema:
                try:
                    quotient = Decimal(str(instance)) / Decimal(str(schema["multipleOf"]))
                except (InvalidOperation, ZeroDivisionError):
                    raise SchemaFailure(path, "invalid multipleOf comparison")
                if not quotient.is_finite() or quotient != quotient.to_integral_value():
                    raise SchemaFailure(path, "not a multipleOf")
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
    "minLength", "pattern", "format", "minimum", "maximum", "exclusiveMinimum", "multipleOf",
    "allOf", "anyOf", "oneOf", "not", "if", "then", "else", "x-ipcraft-canonical",
}


_LOCAL_SCHEMA_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._~-]*$")
_SCHEMA_FILENAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._~-]*\.schema\.json$")
_FRAGMENT_CHARACTERS = re.compile(r"^[A-Za-z0-9._~!$&'()*+,;=:@/\-]*$")


def _is_supported_schema_id(value: str) -> bool:
    return value == "" or _LOCAL_SCHEMA_ID.fullmatch(value) is not None


def _is_supported_schema_ref(value: str) -> bool:
    if not value or any(ord(character) <= 0x20 or ord(character) == 0x7F for character in value):
        return False
    if "%" in value or value.count("#") > 1:
        return False
    file_part, separator, fragment = value.partition("#")
    if file_part and _SCHEMA_FILENAME.fullmatch(file_part) is None:
        return False
    if not file_part and not separator:
        return False
    if not separator:
        return True
    if fragment == "":
        return bool(file_part)
    if not fragment.startswith("/") or _FRAGMENT_CHARACTERS.fullmatch(fragment) is None:
        return False
    return all(re.search(r"~(?:[^01]|$)", token) is None for token in fragment[1:].split("/"))


def _is_supported_portable_pattern(value: str) -> bool:
    """Closed ECMA-262/Python intersection used by the committed V1 schemas."""
    for match in re.finditer(r"\(\?", value):
        if not (value.startswith("(?:", match.start()) or value.startswith("(?!", match.start())):
            return False
    if re.search(r"\\(?:[bBdDsSwW]|[1-9]|0[0-9]|g[<{]|k[<{]|A|Z|z|N\{|U[0-9A-Fa-f]{8})", value):
        return False
    if re.search(r"(?:[*+?]|\{[0-9]+(?:,[0-9]*)?\})\+", value):
        return False
    if any(operator in value for operator in ("&&", "--", "~~", "||")):
        return False
    try:
        re.compile(_translate_portable_pattern(value))
    except re.error:
        return False
    return True


def _translate_portable_pattern(value: str) -> str:
    """Give wildcard `.` its fixed ECMA-262 line-terminator semantics."""
    result: list[str] = []
    in_class = False
    index = 0
    while index < len(value):
        character = value[index]
        if character == "\\" and index + 1 < len(value):
            result.extend((character, value[index + 1]))
            index += 2
            continue
        if character == "[":
            in_class = True
        elif character == "]" and in_class:
            in_class = False
        if character == "." and not in_class:
            result.append("[^\\n\\r\\u2028\\u2029]")
        elif character == "$" and not in_class:
            result.append("\\Z")
        else:
            result.append(character)
        index += 1
    return "".join(result)


def audit_schema_keywords(contracts: Path) -> set[str]:
    """Return unknown keywords and malformed supported-keyword locations."""
    unknown: set[str] = set()
    catalog = load_strict_json(contracts / "schema-catalog.json")["items"]
    documents = {item["id"]: load_strict_json(contracts / item["path"]) for item in catalog}
    filenames = {Path(item["path"]).name: item["id"] for item in catalog}

    schema_types = {"array", "boolean", "integer", "null", "number", "object", "string"}

    def malformed(location: str, key: str) -> None:
        unknown.add(f"{location}/{key}:invalid-shape")

    def is_non_negative_integer(value: Any) -> bool:
        return isinstance(value, int) and not isinstance(value, bool) and value >= 0

    def is_number(value: Any) -> bool:
        return (
            isinstance(value, (int, float, Decimal))
            and not isinstance(value, bool)
            and (
                (not isinstance(value, float) or math.isfinite(value))
                and (not isinstance(value, Decimal) or value.is_finite())
            )
        )

    def is_schema(value: Any) -> bool:
        return isinstance(value, (bool, dict))

    def json_values_are_unique(values: list[Any]) -> bool:
        return not any(_json_equal(values[left], values[right]) for left in range(len(values)) for right in range(left + 1, len(values)))

    def reference_resolves(current_schema_id: str, reference: str) -> bool:
        file_part, separator, fragment = reference.partition("#")
        target_schema_id = filenames.get(file_part) if file_part else current_schema_id
        if target_schema_id not in documents:
            return False
        node: Any = documents[target_schema_id]
        if separator and fragment:
            try:
                node = _resolve_json_pointer(node, fragment)
            except FixtureVerificationError:
                return False
        return isinstance(node, (bool, dict))

    def walk(schema: Any, location: str, current_schema_id: str) -> None:
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
        if "$id" in schema and isinstance(schema["$id"], str) and not _is_supported_schema_id(schema["$id"]):
            malformed(location, "$id")
        if "$ref" in schema and isinstance(schema["$ref"], str):
            if not _is_supported_schema_ref(schema["$ref"]) or not reference_resolves(current_schema_id, schema["$ref"]):
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
        if "multipleOf" in schema and (not is_number(schema["multipleOf"]) or schema["multipleOf"] <= 0):
            malformed(location, "multipleOf")
        if "pattern" in schema:
            pattern = schema["pattern"]
            if not isinstance(pattern, str):
                malformed(location, "pattern")
            elif not _is_supported_portable_pattern(pattern):
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
                    walk(child, f"{location}/{member}/{key}", current_schema_id)
        for member in ("items", "additionalProperties", "contains", "not", "if", "then", "else"):
            if member in schema:
                child = schema[member]
                if not is_schema(child):
                    malformed(location, member)
                walk(child, f"{location}/{member}", current_schema_id)
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
                    walk(child, f"{location}/{member}/{index}", current_schema_id)

    for item in catalog:
        walk(documents[item["id"]], item["id"], item["id"])
    return unknown


def _pointer(document: Any, pointer: str) -> Any:
    return _resolve_json_pointer(document, pointer)


def _pointer_child(pointer: str, token: str) -> str:
    return f"{pointer}/{token.replace('~', '~0').replace('/', '~1')}"


class ProjectCanonicalSetVerifier:
    """Execute every frozen canonical collection reachable from ProjectDesign."""

    _SPECIAL_KEYS = {
        "unicodeScalarValue", "canonicalJson", "subjectsCanonicalJson", "detailsCanonicalJson",
        "persistedEndpointCanonicalKey", "patchEndpointCanonicalKey",
    }

    def __init__(self, contracts: Path, validator: Draft202012Subset | None = None):
        self.contracts = contracts
        self.validator = validator or Draft202012Subset(contracts)
        index = load_strict_json(contracts / "vectors/core-canonical-projection-v1.json")
        if not isinstance(index, dict) or not isinstance(index.get("canonicalCollections"), list):
            raise FixtureVerificationError("canonical vector index lacks canonicalCollections")
        self.rules: dict[tuple[str, str], dict[str, Any]] = {}
        for rule in index["canonicalCollections"]:
            if not isinstance(rule, dict) or rule.get("kind") not in {"set", "ordered", "derived-ordered"}:
                raise FixtureVerificationError("malformed canonical collection rule")
            if not isinstance(rule.get("schemaId"), str) or not isinstance(rule.get("schemaPointer"), str):
                raise FixtureVerificationError("malformed canonical collection rule location")
            if rule["kind"] != "ordered" and (
                not isinstance(rule.get("sortKey"), list) or not rule["sortKey"]
            ):
                raise FixtureVerificationError("malformed canonical collection sortKey")
            key = (rule["schemaId"], rule["schemaPointer"])
            if key in self.rules:
                raise FixtureVerificationError(f"duplicate canonical set rule {key}")
            self.rules[key] = rule

    def _resolve(self, schema_id: str, reference: str) -> tuple[str, str, Any]:
        file_part, separator, fragment = reference.partition("#")
        if file_part:
            try:
                schema_id = self.validator.schema_ids_by_filename[file_part]
            except KeyError as error:
                raise FixtureVerificationError(f"unresolved canonical schema reference {reference!r}") from error
        pointer = fragment if separator else ""
        document = self.validator.schemas[schema_id][0]
        try:
            return schema_id, pointer, _pointer(document, pointer)
        except (KeyError, IndexError, TypeError, ValueError) as error:
            raise FixtureVerificationError(f"unresolved canonical schema pointer {reference!r}") from error

    @staticmethod
    def _metadata(rule: dict[str, Any]) -> dict[str, Any]:
        result = {"kind": rule["kind"]}
        if "sortKey" in rule:
            result["sortKey"] = rule["sortKey"]
        return result

    def _item_schema_has_property(
        self, schema_id: str, schema: Any, property_name: str,
        visited: set[tuple[str, int]] | None = None,
    ) -> bool:
        if isinstance(schema, bool):
            return False
        visited = visited or set()
        marker = (schema_id, id(schema))
        if marker in visited:
            return False
        visited.add(marker)
        if "$ref" in schema:
            target_id, _, target = self._resolve(schema_id, schema["$ref"])
            if self._item_schema_has_property(target_id, target, property_name, visited):
                return True
        if property_name in schema.get("properties", {}) and property_name in schema.get("required", []):
            return True
        for member in ("oneOf", "anyOf"):
            branches = schema.get(member, [])
            if branches and all(self._item_schema_has_property(schema_id, branch, property_name, set(visited)) for branch in branches):
                return True
        return any(
            self._item_schema_has_property(schema_id, branch, property_name, set(visited))
            for branch in schema.get("allOf", [])
        )

    def _schema_variants(
        self, schema_id: str, schema: Any, visited: set[tuple[str, int]] | None = None,
    ) -> list[tuple[str, dict[str, Any]]]:
        if not isinstance(schema, dict):
            return []
        visited = visited or set()
        marker = (schema_id, id(schema))
        if marker in visited:
            return []
        visited.add(marker)
        if "$ref" in schema:
            target_id, _, target = self._resolve(schema_id, schema["$ref"])
            return self._schema_variants(target_id, target, visited)
        for member in ("oneOf", "anyOf"):
            if member in schema:
                return [
                    variant
                    for branch in schema[member]
                    for variant in self._schema_variants(schema_id, branch, set(visited))
                ]
        return [(schema_id, schema)]

    def _required_names(self, schema_id: str, schema: Any) -> set[str]:
        variants = self._schema_variants(schema_id, schema)
        if len(variants) > 1:
            required_sets = [self._required_names(variant_id, variant) for variant_id, variant in variants]
            return set.intersection(*required_sets) if required_sets else set()
        if not variants:
            return set()
        variant_id, variant = variants[0]
        required = set(variant.get("required", []))
        for branch in variant.get("allOf", []):
            required.update(self._required_names(variant_id, branch))
        return required

    def _property_schema(self, schema_id: str, schema: Any, property_name: str) -> tuple[str, Any] | None:
        variants = self._schema_variants(schema_id, schema)
        if len(variants) != 1:
            return None
        variant_id, variant = variants[0]
        if property_name in variant.get("properties", {}):
            return variant_id, variant["properties"][property_name]
        for branch in variant.get("allOf", []):
            result = self._property_schema(variant_id, branch, property_name)
            if result is not None:
                return result
        return None

    def _endpoint_key_is_total(self, schema_id: str, schema: Any, patch: bool) -> bool:
        variants = self._schema_variants(schema_id, schema)
        if not variants:
            return False
        for variant_id, variant in variants:
            required = self._required_names(variant_id, variant)
            state_schema = self._property_schema(variant_id, variant, "state")
            if state_schema is None or not isinstance(state_schema[1], dict):
                return False
            state = state_schema[1].get("const")
            if state == "resolved":
                subject_name = "subject"
                if not {"state", subject_name} <= required:
                    return False
            elif state == "unresolved":
                subject_name = "intendedSubject"
                if not {"state", subject_name, "reasonCode"} <= required:
                    return False
            else:
                return False
            subject_schema = self._property_schema(variant_id, variant, subject_name)
            if subject_schema is None:
                return False
            subject_id, subject = subject_schema
            subject_required = self._required_names(subject_id, subject)
            if patch:
                if not {"kind", "ref"} <= subject_required:
                    return False
                reference_schema = self._property_schema(subject_id, subject, "ref")
                if reference_schema is None:
                    return False
                reference_variants = self._schema_variants(*reference_schema)
                if not reference_variants or any(
                    not ({"id"} <= self._required_names(ref_id, ref_schema) or {"localRef"} <= self._required_names(ref_id, ref_schema))
                    for ref_id, ref_schema in reference_variants
                ):
                    return False
            elif not {"kind", "id"} <= subject_required:
                return False
        return True

    def _special_key_is_executable(self, schema_id: str, schema: Any, key: str) -> bool:
        item_schema = schema.get("items", False)
        if key == "unicodeScalarValue":
            variants = self._schema_variants(schema_id, item_schema)
            return bool(variants) and all(variant.get("type") == "string" for _, variant in variants)
        if key == "persistedEndpointCanonicalKey":
            return self._endpoint_key_is_total(schema_id, item_schema, False)
        if key == "patchEndpointCanonicalKey":
            return self._endpoint_key_is_total(schema_id, item_schema, True)
        if key == "canonicalJson":
            return True
        if key == "subjectsCanonicalJson":
            return self._item_schema_has_property(schema_id, item_schema, "subjects")
        if key == "detailsCanonicalJson":
            return self._item_schema_has_property(schema_id, item_schema, "details")
        return False

    def audit_coverage(self) -> None:
        visited: set[tuple[str, str]] = set()

        def walk(schema_id: str, pointer: str, schema: Any) -> None:
            location = (schema_id, pointer)
            if location in visited or isinstance(schema, bool):
                return
            if not isinstance(schema, dict):
                raise FixtureVerificationError(f"{schema_id}#{pointer}: non-schema in Project canonical graph")
            visited.add(location)
            if schema.get("type") == "array":
                metadata = schema.get("x-ipcraft-canonical")
                rule = self.rules.get(location)
                if rule is None:
                    raise FixtureVerificationError(f"canonical set rule missing for {schema_id}#{pointer}")
                if metadata != self._metadata(rule):
                    raise FixtureVerificationError(f"canonical set rule drift for {schema_id}#{pointer}")
                if rule["kind"] == "set":
                    for key in rule["sortKey"]:
                        if not isinstance(key, str) or not key or (
                            key not in self._SPECIAL_KEYS and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]*", key) is None
                        ):
                            raise FixtureVerificationError(f"unsupported canonical set key for {schema_id}#{pointer}")
                        if schema.get("maxItems") != 0 and key not in self._SPECIAL_KEYS and not self._item_schema_has_property(
                            schema_id, schema.get("items", False), key
                        ):
                            raise FixtureVerificationError(
                                f"canonical set key {key!r} has no executable item property for {schema_id}#{pointer}"
                            )
                        if schema.get("maxItems") != 0 and key in self._SPECIAL_KEYS and not self._special_key_is_executable(
                            schema_id, schema, key
                        ):
                            label = {
                                "persistedEndpointCanonicalKey": "persisted endpoint",
                                "patchEndpointCanonicalKey": "patch endpoint",
                            }.get(key, key)
                            raise FixtureVerificationError(
                                f"{label} canonical key is not total/executable for {schema_id}#{pointer}"
                            )
            if "$ref" in schema:
                target_id, target_pointer, target = self._resolve(schema_id, schema["$ref"])
                walk(target_id, target_pointer, target)
            for member in ("properties", "$defs"):
                for key, child in schema.get(member, {}).items():
                    walk(schema_id, _pointer_child(_pointer_child(pointer, member), key), child)
            for member in ("items", "additionalProperties", "contains", "not", "if", "then", "else"):
                if member in schema:
                    walk(schema_id, _pointer_child(pointer, member), schema[member])
            for member in ("allOf", "anyOf", "oneOf"):
                for index, child in enumerate(schema.get(member, [])):
                    walk(schema_id, _pointer_child(_pointer_child(pointer, member), str(index)), child)

        schema, _ = self.validator.schemas["ipcraft.project-design.v1"]
        walk("ipcraft.project-design.v1", "", schema)

    @staticmethod
    def _endpoint_key(item: Any, patch: bool) -> tuple[Any, ...]:
        if not isinstance(item, dict) or item.get("state") not in {"resolved", "unresolved"}:
            raise FixtureVerificationError("canonical endpoint lacks resolved/unresolved state")
        state = item["state"]
        subject_name = "subject" if state == "resolved" else "intendedSubject"
        subject = item.get(subject_name)
        if not isinstance(subject, dict) or not isinstance(subject.get("kind"), str):
            raise FixtureVerificationError("canonical endpoint subject lacks required kind")
        if patch:
            reference = subject.get("ref")
            if not isinstance(reference, dict) or set(reference) not in ({"id"}, {"localRef"}):
                raise FixtureVerificationError("patch endpoint subject lacks exactly one id/localRef")
            reference_name = "id" if "id" in reference else "localRef"
            if not isinstance(reference[reference_name], str):
                raise FixtureVerificationError("patch endpoint reference is not a string")
            token = f"{reference_name}:" + reference[reference_name]
        else:
            if not isinstance(subject.get("id"), str):
                raise FixtureVerificationError("persisted endpoint subject lacks required id")
            token = "id:" + subject["id"]
        key: tuple[Any, ...] = (0 if state == "resolved" else 1, subject["kind"], token)
        if state == "unresolved":
            if not isinstance(item.get("reasonCode"), str):
                raise FixtureVerificationError("unresolved endpoint lacks required reasonCode")
            key += (item["reasonCode"],)
        return key

    @staticmethod
    def _canonical_json(value: Any) -> str:
        return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))

    def _key(self, sort_keys: list[str], item: Any) -> tuple[Any, ...]:
        result: list[Any] = []
        for key in sort_keys:
            if key == "unicodeScalarValue":
                result.append(item)
            elif key == "canonicalJson":
                result.append(self._canonical_json(item))
            elif key == "subjectsCanonicalJson":
                result.append(self._canonical_json(item["subjects"]))
            elif key == "detailsCanonicalJson":
                result.append(self._canonical_json(item["details"]))
            elif key == "persistedEndpointCanonicalKey":
                result.extend(self._endpoint_key(item, False))
            elif key == "patchEndpointCanonicalKey":
                result.extend(self._endpoint_key(item, True))
            else:
                if not isinstance(item, dict) or key not in item:
                    raise FixtureVerificationError(f"canonical set item lacks required sort key {key!r}")
                result.append(self._canonical_json(item[key]))
        return tuple(result)

    def has_duplicate(self, document: Any) -> bool:
        self.audit_coverage()
        active: set[tuple[str, str, int]] = set()

        def walk(schema_id: str, pointer: str, schema: Any, instance: Any) -> bool:
            if isinstance(schema, bool):
                return False
            marker = (schema_id, pointer, id(instance))
            if marker in active:
                return False
            active.add(marker)
            try:
                root, root_path = self.validator.schemas[schema_id]
                if "$ref" in schema:
                    target_id, target_pointer, target = self._resolve(schema_id, schema["$ref"])
                    if walk(target_id, target_pointer, target, instance):
                        return True
                for member in ("oneOf", "anyOf"):
                    if member in schema:
                        matches = [branch for branch in schema[member] if self.validator._is_valid(branch, instance, root, root_path)]
                        selected = matches if member == "oneOf" else matches[:1]
                        if any(walk(schema_id, _pointer_child(_pointer_child(pointer, member), str(schema[member].index(branch))), branch, instance) for branch in selected):
                            return True
                for index, branch in enumerate(schema.get("allOf", [])):
                    if walk(schema_id, _pointer_child(_pointer_child(pointer, "allOf"), str(index)), branch, instance):
                        return True
                if "if" in schema:
                    branch_name = "then" if self.validator._is_valid(schema["if"], instance, root, root_path) else "else"
                    if branch_name in schema and walk(schema_id, _pointer_child(pointer, branch_name), schema[branch_name], instance):
                        return True
                if isinstance(instance, list):
                    metadata = schema.get("x-ipcraft-canonical")
                    if isinstance(metadata, dict) and metadata.get("kind") == "set":
                        keys = [self._key(metadata["sortKey"], item) for item in instance]
                        if len(keys) != len(set(keys)):
                            return True
                    if "items" in schema:
                        return any(walk(schema_id, _pointer_child(pointer, "items"), schema["items"], item) for item in instance)
                if isinstance(instance, dict):
                    properties = schema.get("properties", {})
                    return any(
                        member in properties and walk(
                            schema_id, _pointer_child(_pointer_child(pointer, "properties"), member),
                            properties[member], value,
                        )
                        for member, value in instance.items()
                    )
                return False
            finally:
                active.remove(marker)

        schema, _ = self.validator.schemas["ipcraft.project-design.v1"]
        return walk("ipcraft.project-design.v1", "", schema, document)


def audit_project_canonical_set_coverage(contracts: Path) -> None:
    ProjectCanonicalSetVerifier(contracts).audit_coverage()


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


def _portable_paths_invalid(contracts: Path, paths: list[Any]) -> bool:
    try:
        normalization = verify_fixture_catalog.load_unicode_normalization_data(contracts)
        folding = verify_fixture_catalog.load_simple_case_folding_table(contracts)
        keys = [
            verify_fixture_catalog.portable_collision_key(
                path, f"manifest path[{index}]", normalization, folding
            )
            for index, path in enumerate(paths)
        ]
    except verify_fixture_catalog.VerificationError:
        return True
    return len(keys) != len(set(keys))


def bundle_manifest_digest(contracts: Path, document: dict[str, Any]) -> str:
    schema = load_strict_json(contracts / "schemas/ipcraft.bundle-manifest.v1.schema.json")
    metadata = schema.get("properties", {}).get("files", {}).get("x-ipcraft-canonical")
    if metadata != {"kind": "set", "sortKey": ["path"]}:
        raise FixtureVerificationError("Bundle files canonical set rule drifted")
    rule_index = load_strict_json(contracts / "vectors/core-canonical-projection-v1.json")
    matching_rules = [
        rule for rule in rule_index.get("canonicalCollections", [])
        if rule.get("schemaId") == "ipcraft.bundle-manifest.v1"
        and rule.get("schemaPointer") == "/properties/files"
        and rule.get("kind") == "set"
        and rule.get("sortKey") == ["path"]
    ]
    if len(matching_rules) != 1:
        raise FixtureVerificationError("Bundle files canonical set rule missing from frozen index")
    projected = copy.deepcopy(document)
    projected.pop("manifestDigest", None)
    projected["files"] = sorted(projected.get("files", []), key=lambda item: item["path"])
    canonical = json.dumps(projected, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _declared_scalar_matches(kind: str, value: Any) -> bool:
    if value is None:
        return True
    if kind == "bool":
        return isinstance(value, bool)
    if kind == "int":
        return Draft202012Subset._matches_type(value, "integer")
    if kind == "double":
        return Draft202012Subset._matches_type(value, "number")
    if kind in {"string", "enum"}:
        return isinstance(value, str) if kind == "string" else True
    return False


def _field_declarations_invalid(fields: list[dict[str, Any]]) -> bool:
    if _duplicates(fields, lambda item: item.get("key")):
        return True
    by_key = {item.get("key"): item for item in fields}
    for field in fields:
        kind = field.get("type")
        default = field.get("default")
        values = field.get("values")
        minimum, maximum = field.get("minimum"), field.get("maximum")
        if not _declared_scalar_matches(kind, default):
            return True
        if field.get("required") and default is None:
            return True
        if kind == "enum" and not values:
            return True
        if kind != "enum" and values is not None:
            return True
        if values is not None and (
            any(not _declared_scalar_matches(kind, value) for value in values)
            or not any(_json_equal(default, value) for value in values)
        ):
            return True
        if minimum is not None and maximum is not None and minimum > maximum:
            return True
        if isinstance(default, (int, float, Decimal)) and not isinstance(default, bool):
            if minimum is not None and default < minimum:
                return True
            if maximum is not None and default > maximum:
                return True
        for condition_name in ("visibleWhen", "enabledWhen"):
            condition = field.get(condition_name)
            if condition is None:
                continue
            dependency = by_key.get(condition.get("field"))
            if dependency is None or not _declared_scalar_matches(dependency.get("type"), condition.get("equals")):
                return True
    return False


def _package_declaration_invalid(contracts: Path, document: dict[str, Any]) -> bool:
    provider = document.get("extensionProvider")
    if provider is not None and _portable_paths_invalid(contracts, [provider.get("manifestPath")]):
        return True

    canonical_sets: list[tuple[list[dict[str, Any]], Any]] = [
        (document.get("interfaceTemplates", []), lambda item: item.get("key")),
        (document.get("domainTypes", []), lambda item: item.get("key")),
        (document.get("packageEntityTypes", []), lambda item: item.get("typeKey")),
        (document.get("packageRelationTypes", []), lambda item: item.get("typeKey")),
        (document.get("extensions", []), lambda item: (item.get("ownerLockId"), item.get("schema"), item.get("version"))),
        (document.get("topology", {}).get("slotTemplates", []), lambda item: item.get("stableKey")),
    ]
    for items, key in canonical_sets:
        if _duplicates(items, key):
            return True

    field_scopes = [document.get("configuration", {}).get("global", {}).get("fields", [])]
    field_scopes.extend(item.get("nocConfig", {}).get("fields", []) for item in document.get("interfaceTemplates", []))
    field_scopes.extend(item.get("configuration", {}).get("fields", []) for item in document.get("domainTypes", []))
    if any(_field_declarations_invalid(fields) for fields in field_scopes):
        return True

    global_fields = {
        item.get("key"): item
        for item in document.get("configuration", {}).get("global", {}).get("fields", [])
    }
    for member in ("rowField", "columnField"):
        field = global_fields.get(document.get("topology", {}).get(member))
        if not field or field.get("type") != "int" or field.get("topologyDriving") is not True:
            return True

    for slot in document.get("topology", {}).get("slotTemplates", []):
        allowed = slot.get("allowedContracts", [])
        if _duplicates(
            allowed,
            lambda item: (item.get("contractId"), item.get("version"), item.get("bundleManifestDigest")),
        ):
            return True

    for template in document.get("interfaceTemplates", []):
        fields = {item.get("key"): item for item in template.get("nocConfig", {}).get("fields", [])}
        defaults = template.get("nocConfig", {}).get("defaults", {})
        if set(defaults) - set(fields):
            return True
        if any(not _declared_scalar_matches(fields[key].get("type"), value) for key, value in defaults.items()):
            return True

    for relation in document.get("packageRelationTypes", []):
        for endpoint_name in ("sources", "targets"):
            endpoint = relation.get(endpoint_name, {})
            if endpoint.get("minimum", 0) > endpoint.get("maximum", 0):
                return True
        if relation.get("ownership") == "engine" and relation.get("topologyDriving"):
            return True
    if any(item.get("ownership") == "engine" and item.get("topologyDriving") for item in document.get("packageEntityTypes", [])):
        return True

    return False


PATCH_ENTITY_DEFS = {
    "component": "patchComponentValue", "interface": "patchInterfaceValue", "router": "patchRouterValue",
    "structural-link": "patchStructuralLinkValue", "access-slot": "patchAccessSlotValue",
    "domain": "patchDomainValue", "package-entity": "patchPackageEntityValue",
}
PATCH_RELATION_DEFS = {
    "attachment": "patchAttachmentValue", "domain-membership": "patchDomainMembershipValue",
    "package-relation": "patchPackageRelationValue",
}


def _patch_context_digest(document: dict[str, Any]) -> str:
    projected = copy.deepcopy(document)
    projected.pop("contextDigest", None)
    canonical = json.dumps(projected, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _digest_json(value: Any) -> str:
    canonical = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _validate_patch_subject_schema(
    validator: Draft202012Subset, kind: str, value: dict[str, Any], relation: bool,
) -> bool:
    core, core_path = validator.schemas["ipcraft.core-canonical-models.v1"]
    if not relation and kind == "project":
        if set(value) != {"name", "dependencies"}:
            return False
        schema = core["$defs"]["patchProjectUpdateSet"]
    elif not relation and kind == "topology":
        if set(value) != {"derivation"}:
            return False
        schema = core["$defs"]["patchTopologyUpdateSet"]
    else:
        definition = (PATCH_RELATION_DEFS if relation else PATCH_ENTITY_DEFS).get(kind)
        if definition is None:
            return False
        schema = core["$defs"][definition]
    return validator._is_valid(schema, value, core, core_path)


def _patch_state_reference_failure(
    entities: dict[tuple[str, str], dict[str, Any]],
    relations: dict[tuple[str, str], dict[str, Any]],
    local_bindings: dict[str, tuple[str, str, bool]],
) -> tuple[str, str] | None:
    def state_key(reference: Any, expected_kind: str, relation: bool = False) -> tuple[str, str] | None:
        if not isinstance(reference, dict) or set(reference) not in ({"id"}, {"localRef"}):
            return None
        if "id" in reference:
            return (expected_kind, reference["id"])
        binding = local_bindings.get(reference["localRef"])
        if binding != (expected_kind, "@" + reference["localRef"], relation):
            return None
        return (expected_kind, binding[1])

    def reference_token(reference: Any, expected_kind: str, relation: bool = False) -> str | None:
        key = state_key(reference, expected_kind, relation)
        return None if key is None else f"{key[0]}:{key[1]}"

    def extension_duplicate(value: dict[str, Any]) -> bool:
        extensions = value.get("extensions", [])
        keys = [(item.get("ownerLockId"), item.get("schema"), item.get("version")) for item in extensions]
        return len(keys) != len(set(keys))

    if any(extension_duplicate(value) for value in list(entities.values()) + list(relations.values())):
        return "patch-invariant", "patch.invariant_violation"
    coordinates = [
        (value.get("coordinate", {}).get("row"), value.get("coordinate", {}).get("column"))
        for (kind, _), value in entities.items() if kind == "router"
    ]
    if len(coordinates) != len(set(coordinates)):
        return "patch-invariant", "patch.invariant_violation"
    link_pairs: set[tuple[str, str]] = set()
    for (kind, _), value in entities.items():
        if kind != "structural-link":
            continue
        endpoint_a = state_key(value.get("endpointA"), "router")
        endpoint_b = state_key(value.get("endpointB"), "router")
        if endpoint_a is None or endpoint_b is None:
            continue
        if endpoint_a == endpoint_b:
            return "patch-invariant", "patch.invariant_violation"
        pair = tuple(sorted((endpoint_a[1], endpoint_b[1])))
        if pair in link_pairs:
            return "patch-invariant", "patch.invariant_violation"
        link_pairs.add(pair)
        left, right = entities.get(endpoint_a, {}).get("coordinate"), entities.get(endpoint_b, {}).get("coordinate")
        if isinstance(left, dict) and isinstance(right, dict):
            row_delta = abs(left.get("row", 0) - right.get("row", 0))
            column_delta = abs(left.get("column", 0) - right.get("column", 0))
            if row_delta + column_delta != 1:
                return "patch-invariant", "patch.invariant_violation"
            if value.get("axis") != ("vertical" if row_delta else "horizontal"):
                return "patch-invariant", "patch.invariant_violation"
    slot_keys: list[tuple[str, str]] = []
    for (kind, _), value in entities.items():
        if kind != "access-slot":
            continue
        router_ref = value.get("routerRef", {})
        router_token = router_ref.get("id", "local:" + router_ref.get("localRef", ""))
        slot_keys.append((router_token, value.get("templateKey")))
        allowed = value.get("allowedContracts", [])
        allowed_keys = [
            (item.get("contractLockId"), json.dumps(item.get("capabilityConstraints", {}), sort_keys=True, separators=(",", ":")))
            for item in allowed
        ]
        if len(allowed_keys) != len(set(allowed_keys)) or any(
            len(item.get("roles", [])) != len(set(item.get("roles", []))) for item in allowed
        ):
            return "patch-invariant", "patch.invariant_violation"
    if len(slot_keys) != len(set(slot_keys)):
        return "patch-invariant", "patch.invariant_violation"
    attached_interfaces: list[str] = []
    for (kind, _), value in relations.items():
        if kind == "attachment":
            reference = value.get("interfaceRef", {})
            attached_interfaces.append(reference.get("id", "local:" + reference.get("localRef", "")))
        if kind == "package-relation":
            for member in ("sources", "targets"):
                keys = [json.dumps(item, ensure_ascii=False, sort_keys=True, separators=(",", ":")) for item in value.get(member, [])]
                if len(keys) != len(set(keys)):
                    return "patch-invariant", "patch.invariant_violation"
    if len(attached_interfaces) != len(set(attached_interfaces)):
        return "patch-invariant", "patch.invariant_violation"

    def resolves(reference: Any, expected_kind: str, relation: bool = False) -> bool:
        key = state_key(reference, expected_kind, relation)
        return key in (relations if relation else entities) if key is not None else False

    for (kind, _), value in entities.items():
        if kind == "interface" and not resolves(value.get("ownerComponentRef"), "component"):
            return "reference", "patch.unknown_reference"
        if kind == "structural-link":
            if not resolves(value.get("endpointA"), "router") or not resolves(value.get("endpointB"), "router"):
                return "reference", "patch.unknown_reference"
        if kind == "access-slot" and not resolves(value.get("routerRef"), "router"):
            return "reference", "patch.unknown_reference"
    occupied: set[str] = set()
    for (kind, _), value in relations.items():
        if kind == "attachment":
            if not resolves(value.get("interfaceRef"), "interface"):
                return "reference", "patch.unknown_reference"
            if value.get("state") == "resolved":
                if not resolves(value.get("routerRef"), "router") or not resolves(value.get("slotRef"), "access-slot"):
                    return "reference", "patch.unknown_reference"
                slot_key = state_key(value["slotRef"], "access-slot")
                slot_token = reference_token(value["slotRef"], "access-slot")
                if slot_key is not None and slot_token is not None:
                    if slot_token in occupied:
                        return "patch-invariant", "patch.invariant_violation"
                    occupied.add(slot_token)
                    slot = entities.get(slot_key, {})
                    if reference_token(slot.get("routerRef"), "router") != reference_token(value.get("routerRef"), "router"):
                        return "patch-invariant", "patch.invariant_violation"
            elif value.get("state") == "unresolved":
                target = value.get("intendedTarget", {})
                if not all(isinstance(target.get(member), dict) and set(target[member]) in ({"id"}, {"localRef"}) for member in ("routerRef", "slotRef")):
                    return "reference", "patch.unknown_reference"
        elif kind == "domain-membership":
            if not resolves(value.get("domainRef"), "domain") or not resolves(value.get("routerRef"), "router"):
                return "reference", "patch.unknown_reference"
        elif kind == "package-relation":
            for endpoint in value.get("sources", []) + value.get("targets", []):
                subject = endpoint.get("subject") if endpoint.get("state") == "resolved" else endpoint.get("intendedSubject")
                if not isinstance(subject, dict):
                    return "reference", "patch.unknown_reference"
                expected_kind = subject.get("kind")
                key = next(iter(subject.get("ref", {})), None)
                if expected_kind is None or key is None:
                    return "reference", "patch.unknown_reference"
                is_relation = expected_kind in {"attachment", "domain-membership", "package-relation"}
                if endpoint.get("state") == "resolved" and not resolves(subject.get("ref"), expected_kind, is_relation):
                    return "reference", "patch.unknown_reference"
    return None


def _patch_dependency_reference_failure(
    entities: dict[tuple[str, str], dict[str, Any]],
    relations: dict[tuple[str, str], dict[str, Any]],
) -> tuple[str, str] | None:
    project = next((value for (kind, _), value in entities.items() if kind == "project"), None)
    if not isinstance(project, dict):
        return "reference", "patch.unknown_reference"
    dependencies = {item.get("lockId"):item for item in project.get("dependencies", []) if isinstance(item, dict)}
    if len(dependencies) != len(project.get("dependencies", [])):
        return "reference", "patch.unknown_reference"

    def is_kind(lock_id: Any, kinds: set[str]) -> bool:
        return isinstance(lock_id, str) and dependencies.get(lock_id, {}).get("kind") in kinds

    extension_owner_kinds = {"noc-package", "interface-contract", "extension-provider"}
    extension_values: list[dict[str, Any]] = []
    for (kind, _), value in entities.items():
        if kind == "component" and not is_kind(value.get("packageLockId"), {"noc-package"}):
            return "reference", "patch.unknown_reference"
        if kind == "interface" and not is_kind(value.get("contract", {}).get("lockId"), {"interface-contract"}):
            return "reference", "patch.unknown_reference"
        if kind == "access-slot" and any(not is_kind(item.get("contractLockId"), {"interface-contract"}) for item in value.get("allowedContracts", [])):
            return "reference", "patch.unknown_reference"
        if kind == "topology":
            derivation = value.get("derivation", {})
            engine = dependencies.get(derivation.get("defaultEngineLockId"), {})
            authority = derivation.get("structureAuthority", {})
            authority_lock = dependencies.get(authority.get("lockId"), {})
            if engine.get("kind") != "default-engine" or authority_lock.get("kind") != authority.get("kind"):
                return "reference", "patch.unknown_reference"
            if any((
                derivation.get("defaultEngineBundleDigest") != engine.get("bundleManifestDigest"),
                derivation.get("engineHostContractVersion") != engine.get("engineHostContractVersion"),
                derivation.get("hostSideEffectContractVersion") != engine.get("hostSideEffectContractVersion"),
                authority.get("identity") != authority_lock.get("id"),
                authority.get("version") != authority_lock.get("version"),
                authority.get("bundleDigest") != authority_lock.get("bundleManifestDigest"),
            )):
                return "reference", "patch.unknown_reference"
        extension_values.extend(value.get("extensions", []) if isinstance(value.get("extensions"), list) else [])
    for value in relations.values():
        extension_values.extend(value.get("extensions", []) if isinstance(value.get("extensions"), list) else [])
    if any(not is_kind(item.get("ownerLockId"), extension_owner_kinds) for item in extension_values):
        return "reference", "patch.unknown_reference"
    for dependency in dependencies.values():
        if dependency.get("kind") in {"extension-provider", "drc-tool", "generator-tool"} and not is_kind(dependency.get("runtimeLockId"), {"runtime"}):
            return "reference", "patch.unknown_reference"
    return None


def _patch_domain_invariant_failure(
    entities: dict[tuple[str, str], dict[str, Any]],
    relations: dict[tuple[str, str], dict[str, Any]],
) -> tuple[str, str] | None:
    def token(reference: Any) -> str | None:
        if not isinstance(reference, dict):
            return None
        if set(reference) == {"id"}:
            return reference["id"]
        if set(reference) == {"localRef"}:
            return "@" + reference["localRef"]
        return None

    routers = {item_id for kind, item_id in entities if kind == "router"}
    domains = {item_id:value for (kind, item_id), value in entities.items() if kind == "domain"}
    domain_types = {value.get("typeKey") for value in domains.values()}
    if routers and not domain_types:
        return "patch-invariant", "patch.invariant_violation"
    for domain_type in domain_types:
        defaults = [item_id for item_id, value in domains.items() if value.get("typeKey") == domain_type and value.get("isDefault")]
        if len(defaults) != 1:
            return "patch-invariant", "patch.invariant_violation"
    memberships: list[tuple[str, str]] = []
    for (kind, _), value in relations.items():
        if kind == "domain-membership":
            domain_id, router_id = token(value.get("domainRef")), token(value.get("routerRef"))
            if domain_id is None or router_id is None:
                return "patch-invariant", "patch.invariant_violation"
            memberships.append((domain_id, router_id))
    for router_id in routers:
        for domain_type in domain_types:
            count = sum(1 for domain_id, member_router in memberships if member_router == router_id and domains.get(domain_id, {}).get("typeKey") == domain_type)
            if count != 1:
                return "patch-invariant", "patch.invariant_violation"
    adjacency = {router_id:set() for router_id in routers}
    for (kind, _), value in entities.items():
        if kind != "structural-link":
            continue
        endpoint_a, endpoint_b = token(value.get("endpointA")), token(value.get("endpointB"))
        if endpoint_a in adjacency and endpoint_b in adjacency:
            adjacency[endpoint_a].add(endpoint_b)
            adjacency[endpoint_b].add(endpoint_a)
    for domain_id, domain in domains.items():
        members = {router_id for member_domain, router_id in memberships if member_domain == domain_id}
        if not members and not domain.get("isDefault"):
            return "patch-invariant", "patch.invariant_violation"
        if len(members) > 1:
            reached: set[str] = set()
            frontier = [next(iter(members))]
            while frontier:
                current = frontier.pop()
                if current in reached:
                    continue
                reached.add(current)
                frontier.extend((adjacency[current] & members) - reached)
            if reached != members:
                return "patch-invariant", "patch.invariant_violation"
    return None


def _patch_package_relation_declaration_failure(
    relations: dict[tuple[str, str], dict[str, Any]], declarations: dict[str, Any],
) -> tuple[str, str] | None:
    for (kind, _), value in relations.items():
        if kind != "package-relation":
            continue
        declaration = declarations.get(value.get("typeKey"))
        if not isinstance(declaration, dict):
            return "reference", "patch.unknown_reference"
        for member in ("sources", "targets"):
            constraint = declaration.get(member, {})
            endpoints = value.get(member, [])
            if not (constraint.get("minimum", 0) <= len(endpoints) <= constraint.get("maximum", 0)):
                return "patch-invariant", "patch.invariant_violation"
            for endpoint in endpoints:
                if endpoint.get("state") == "unresolved" and not declaration.get("unresolvedAllowed"):
                    return "patch-invariant", "patch.invariant_violation"
                subject = endpoint.get("subject") if endpoint.get("state") == "resolved" else endpoint.get("intendedSubject")
                if not isinstance(subject, dict) or subject.get("kind") not in constraint.get("kinds", []):
                    return "patch-invariant", "patch.invariant_violation"
    return None


def _validate_patch_context(
    contracts: Path, validator: Draft202012Subset,
) -> tuple[dict[str, Any], dict[tuple[str, str], dict[str, Any]], dict[tuple[str, str], dict[str, Any]]]:
    context = load_strict_json(contracts / "patch-validation-context-v1.json")
    required = {
        "schema", "version", "authorityContexts", "packageEntityTypes", "packageRelationTypes",
        "dependencyLocks", "trustedOrdinaryPatchTransactions", "formalHistoryRecords", "applicationReconcileTransactions",
        "applicationMigrationTransactions",
        "entities", "relations", "occupiedSlots", "freeSlots", "contextDigest",
    }
    if set(context) != required or context.get("schema") != "ipcraft.patch-validation-context.v1" or context.get("version") != "1":
        raise FixtureVerificationError("patch validation context envelope is not closed")
    computed = _patch_context_digest(context)
    if context.get("contextDigest") != computed or computed != FROZEN_PATCH_CONTEXT_DIGEST:
        raise FixtureVerificationError("patch validation context digest differs from frozen contract")
    authority_contexts = context.get("authorityContexts")
    if not isinstance(authority_contexts, list) or [item.get("contextId") for item in authority_contexts] != ["authority.default", "authority.provider"]:
        raise FixtureVerificationError("patch validation authority contexts are incomplete")
    if [item.get("selectedAuthority", {}).get("kind") for item in authority_contexts] != ["default-engine", "extension-provider"]:
        raise FixtureVerificationError("patch validation selected authorities are incomplete")
    if any(set(item) != {"contextId", "selectedAuthority", "expectedApplicability"} for item in authority_contexts):
        raise FixtureVerificationError("patch validation authority context fields are not closed")
    for item in authority_contexts:
        applicability = item["expectedApplicability"]
        core_schema, core_path = validator.schemas["ipcraft.core-canonical-models.v1"]
        try:
            validator._validate(core_schema["$defs"]["reconcileApplicability"], applicability, core_schema, core_path, "$")
        except SchemaFailure:
            raise FixtureVerificationError("patch validation expected applicability is invalid")
        if applicability.get("structureAuthority") != item["selectedAuthority"]:
            raise FixtureVerificationError("patch validation expected applicability authority differs")
    trusted_replay = context.get("trustedOrdinaryPatchTransactions")
    if not isinstance(trusted_replay, list) or [item.get("sourceKind") for item in trusted_replay] != ["recovery", "undo-redo"]:
        raise FixtureVerificationError("patch validation trusted replay registry is not closed")
    if any(
        set(item) != {"sourceKind", "patchId", "sourceIdentity", "sourceVersion", "patchDigest"}
        or not re.fullmatch(r"sha256:[0-9a-f]{64}", item.get("patchDigest", ""))
        for item in trusted_replay
    ):
        raise FixtureVerificationError("patch validation trusted replay entries are malformed")
    entity_items = context.get("entities")
    relation_items = context.get("relations")
    if not isinstance(entity_items, list) or not isinstance(relation_items, list):
        raise FixtureVerificationError("patch validation state collections are malformed")
    entity_keys = [(item.get("kind"), item.get("id")) for item in entity_items]
    relation_keys = [(item.get("kind"), item.get("id")) for item in relation_items]
    if any(set(item) != {"kind", "id", "value"} for item in entity_items + relation_items):
        raise FixtureVerificationError("patch validation subject wrapper fields are not closed")
    if entity_keys != sorted(entity_keys) or len(entity_keys) != len(set(entity_keys)):
        raise FixtureVerificationError("patch validation entity state is not canonical and unique")
    if relation_keys != sorted(relation_keys) or len(relation_keys) != len(set(relation_keys)):
        raise FixtureVerificationError("patch validation relation state is not canonical and unique")
    if set(kind for kind, _ in entity_keys) != {"project", "topology", "component", "interface", "router", "structural-link", "access-slot", "domain", "package-entity"}:
        raise FixtureVerificationError("patch validation context does not cover every entity kind")
    if set(kind for kind, _ in relation_keys) != {"attachment", "domain-membership", "package-relation"}:
        raise FixtureVerificationError("patch validation context does not cover every relation kind")
    entities = {(item["kind"], item["id"]): copy.deepcopy(item["value"]) for item in entity_items}
    relations = {(item["kind"], item["id"]): copy.deepcopy(item["value"]) for item in relation_items}
    project_dependencies = {
        item["lockId"]: item for item in entities.get(("project", "project.mesh"), {}).get("dependencies", [])
    }
    dependency_locks = context.get("dependencyLocks")
    if not isinstance(dependency_locks, list) or dependency_locks != sorted(dependency_locks, key=lambda item: item.get("lockId", "")):
        raise FixtureVerificationError("patch validation dependency locks are not canonical")
    if {item.get("kind") for item in dependency_locks} != {
        "noc-package", "interface-contract", "default-engine", "extension-provider", "runtime", "drc-tool", "generator-tool",
    } or {item.get("lockId") for item in dependency_locks} != set(project_dependencies):
        raise FixtureVerificationError("patch validation dependency lock kinds are incomplete")
    if any(project_dependencies[item["lockId"]] != item for item in dependency_locks):
        raise FixtureVerificationError("patch validation dependency locks differ from current Project")
    for authority_context in authority_contexts:
        selected = authority_context["selectedAuthority"]
        if set(selected) != {"kind", "lockId", "identity", "version", "bundleDigest"}:
            raise FixtureVerificationError("patch validation selected authority fields are not closed")
        dependency = project_dependencies.get(selected["lockId"])
        if dependency is None or any((
            dependency.get("kind") != selected["kind"], dependency.get("id") != selected["identity"],
            dependency.get("version") != selected["version"],
            dependency.get("bundleManifestDigest") != selected["bundleDigest"],
        )):
            raise FixtureVerificationError("patch validation selected authority does not match current dependencies")
    for (kind, _), value in entities.items():
        if not _validate_patch_subject_schema(validator, kind, value, False):
            raise FixtureVerificationError(f"patch validation entity subject is invalid: {kind}")
    for (kind, _), value in relations.items():
        if not _validate_patch_subject_schema(validator, kind, value, True):
            raise FixtureVerificationError(f"patch validation relation subject is invalid: {kind}")
    for (kind, _), value in entities.items():
        if kind == "package-entity" and context["packageEntityTypes"].get(value.get("typeKey")) not in {"user", "engine"}:
            raise FixtureVerificationError("patch validation Package Entity ownership is unresolved")
    for (kind, _), value in relations.items():
        if kind == "package-relation" and context["packageRelationTypes"].get(value.get("typeKey"), {}).get("ownership") not in {"user", "engine"}:
            raise FixtureVerificationError("patch validation Package Relation ownership is unresolved")
    for type_key, declaration in context["packageRelationTypes"].items():
        if set(declaration) != {"ownership", "unresolvedAllowed", "sources", "targets"} or declaration.get("ownership") not in {"user", "engine"} or not isinstance(declaration.get("unresolvedAllowed"), bool):
            raise FixtureVerificationError(f"patch validation Package Relation declaration is malformed: {type_key}")
        if any(set(declaration[member]) != {"kinds", "minimum", "maximum"} for member in ("sources", "targets")):
            raise FixtureVerificationError(f"patch validation Package Relation endpoint declaration is malformed: {type_key}")
    reference_failure = _patch_state_reference_failure(entities, relations, {})
    if reference_failure:
        raise FixtureVerificationError(f"patch validation context references are invalid: {reference_failure}")
    dependency_failure = _patch_dependency_reference_failure(entities, relations)
    if dependency_failure:
        raise FixtureVerificationError(f"patch validation context dependency references are invalid: {dependency_failure}")
    domain_failure = _patch_domain_invariant_failure(entities, relations)
    if domain_failure:
        raise FixtureVerificationError(f"patch validation context Domain state is invalid: {domain_failure}")
    relation_declaration_failure = _patch_package_relation_declaration_failure(relations, context["packageRelationTypes"])
    if relation_declaration_failure:
        raise FixtureVerificationError(f"patch validation Package Relation declarations are invalid: {relation_declaration_failure}")
    occupied = sorted(
        value["slotRef"]["id"] for (kind, _), value in relations.items()
        if kind == "attachment" and value.get("state") == "resolved" and set(value.get("slotRef", {})) == {"id"}
    )
    slots = sorted(item_id for kind, item_id in entities if kind == "access-slot")
    if context.get("occupiedSlots") != occupied or context.get("freeSlots") != sorted(set(slots) - set(occupied)):
        raise FixtureVerificationError("patch validation slot occupancy is inconsistent")
    history_records = context.get("formalHistoryRecords")
    if not isinstance(history_records, list) or [item.get("kind") for item in history_records] != ["topology", "default-engine-migration"]:
        raise FixtureVerificationError("patch validation formal history coverage is incomplete")
    history_required = {
        "historyTransactionId", "kind", "recordDigest", "beforeAuthoritativeDesignDigest", "afterAuthoritativeDesignDigest",
        "beforeTopologyInputDigest", "afterTopologyInputDigest", "beforeDerivedStateDigest", "afterDerivedStateDigest",
        "forwardTransactionBody", "inverseTransactionBody",
        "forwardTransactionDigest", "inverseTransactionDigest", "affectedBeforeSubjects", "affectedAfterSubjects",
        "localRefToHostId", "tombstones", "revisions",
    }
    for record in history_records:
        if set(record) != history_required:
            raise FixtureVerificationError("patch validation formal history record is not closed")
        normalized = {key:value for key,value in record.items() if key != "recordDigest"}
        if record["recordDigest"] != _digest_json(normalized):
            raise FixtureVerificationError("patch validation formal history record digest differs")
        if record["forwardTransactionDigest"] != _digest_json(record["forwardTransactionBody"]) or record["inverseTransactionDigest"] != _digest_json(record["inverseTransactionBody"]):
            raise FixtureVerificationError("patch validation formal history transaction digest differs")
        revisions = record.get("revisions", {})
        if set(revisions) != {"committed", "replayIncrement"} or any(set(item) != {"sessionRevision", "topologyInputRevision", "derivedStateRevision"} for item in revisions.values()):
            raise FixtureVerificationError("patch validation formal history revision effects are not closed")
        if any(revisions["replayIncrement"].get(member) != 1 for member in ("sessionRevision", "topologyInputRevision", "derivedStateRevision")):
            raise FixtureVerificationError("patch validation formal history replay increments are not V1")
        if record["kind"] == "topology":
            mapping = record.get("localRefToHostId")
            tombstones = record.get("tombstones")
            if not isinstance(mapping, dict) or not mapping or len(set(mapping.values())) != len(mapping) or not isinstance(tombstones, list) or not tombstones:
                raise FixtureVerificationError("patch validation topology history identity evidence is incomplete")
            if record.get("affectedBeforeSubjects") != tombstones or record.get("affectedAfterSubjects") != []:
                raise FixtureVerificationError("patch validation topology history affected snapshots differ from tombstones")
            forward_intent = record["forwardTransactionBody"].get("topologyIntent")
            inverse_intent = record["inverseTransactionBody"].get("topologyIntent")
            if any(
                not isinstance(intent, dict) or set(intent) != {"rows", "columns"}
                or not all(isinstance(intent[member], int) and intent[member] >= 1 for member in ("rows", "columns"))
                for intent in (forward_intent, inverse_intent)
            ) or forward_intent == inverse_intent:
                raise FixtureVerificationError("patch validation topology history intent transition is invalid")
            if record.get("beforeTopologyInputDigest") != _digest_json(inverse_intent) or record.get("afterTopologyInputDigest") != _digest_json(forward_intent):
                raise FixtureVerificationError("patch validation topology history intent digests differ")
            if any(set(item) != {"subjectKind", "kind", "id", "value"} for item in tombstones):
                raise FixtureVerificationError("patch validation topology history tombstone is not complete")
            tombstone_by_id = {item["id"]:item for item in tombstones}
            if len(tombstone_by_id) != len(tombstones) or set(mapping.values()) != set(tombstone_by_id):
                raise FixtureVerificationError("patch validation topology history Host-ID map differs from tombstones")
            forward_deletes = [
                operation for member in ("authorityOperations", "applicationOperations")
                for operation in record["forwardTransactionBody"].get(member, []) if operation.get("op") in {"deleteEntity", "deleteRelation"}
            ]
            inverse_creates = [
                operation for member in ("authorityOperations", "applicationOperations")
                for operation in record["inverseTransactionBody"].get(member, []) if operation.get("op") in {"createEntity", "createRelation"}
            ]
            if {item.get("id") for item in forward_deletes} != set(tombstone_by_id) or {item.get("hostId") for item in inverse_creates} != set(tombstone_by_id):
                raise FixtureVerificationError("patch validation topology history forward/inverse subjects are not symmetric")
            for operation in inverse_creates:
                if mapping.get(operation.get("localRef")) != operation.get("hostId"):
                    raise FixtureVerificationError("patch validation topology history localRef/Host-ID correspondence differs")
                tombstone = tombstone_by_id[operation["hostId"]]
                expected_subject_kind = "relation" if operation["op"] == "createRelation" else "entity"
                expected_kind = operation.get("relationKind" if expected_subject_kind == "relation" else "entityKind")
                if tombstone["subjectKind"] != expected_subject_kind or tombstone["kind"] != expected_kind or tombstone["value"] != operation.get("value"):
                    raise FixtureVerificationError("patch validation topology history restore body differs from tombstone")
            if record["forwardTransactionBody"].get("hostIdMap") != mapping or record["inverseTransactionBody"].get("hostIdMap") != mapping:
                raise FixtureVerificationError("patch validation topology history body mapping differs from record")

            def affected_state(items: list[dict[str, Any]]) -> dict[tuple[str, str, str], dict[str, Any]]:
                return {(item["subjectKind"], item["kind"], item["id"]):copy.deepcopy(item["value"]) for item in items}

            def apply_affected(body: dict[str, Any], state: dict[tuple[str, str, str], dict[str, Any]]) -> dict[tuple[str, str, str], dict[str, Any]] | None:
                result = copy.deepcopy(state)
                for member in ("authorityOperations", "applicationOperations"):
                    for operation in body.get(member, []):
                        relation = operation.get("op") in {"createRelation", "updateRelation", "deleteRelation"}
                        subject_kind = "relation" if relation else "entity"
                        kind = operation.get("relationKind" if relation else "entityKind")
                        operation_name = operation.get("op")
                        item_id = operation.get("hostId") if operation_name in {"createEntity", "createRelation"} else operation.get("id")
                        key = (subject_kind, kind, item_id)
                        if operation_name in {"createEntity", "createRelation"}:
                            if key in result:
                                return None
                            result[key] = copy.deepcopy(operation.get("value"))
                        elif operation_name in {"deleteEntity", "deleteRelation"}:
                            if key not in result:
                                return None
                            del result[key]
                        else:
                            if key not in result:
                                return None
                            value = copy.deepcopy(result[key])
                            for field in operation.get("unset", []):
                                value.pop(field, None)
                            value.update(copy.deepcopy(operation.get("set", {})))
                            result[key] = value
                return result

            before_state = affected_state(record["affectedBeforeSubjects"])
            after_state = affected_state(record["affectedAfterSubjects"])
            if apply_affected(record["forwardTransactionBody"], before_state) != after_state or apply_affected(record["inverseTransactionBody"], after_state) != before_state:
                raise FixtureVerificationError("patch validation topology history forward/inverse execution differs from affected snapshots")
        else:
            if record.get("localRefToHostId") != {} or record.get("tombstones") != []:
                raise FixtureVerificationError("patch validation migration history must not invent Host IDs")
            forward, inverse = record["forwardTransactionBody"], record["inverseTransactionBody"]
            if any((
                forward.get("currentDefaultEngineLockId") != inverse.get("targetDefaultEngineLockId"),
                forward.get("targetDefaultEngineLockId") != inverse.get("currentDefaultEngineLockId"),
                forward.get("dependencyLockDigest") == inverse.get("dependencyLockDigest"),
                record.get("affectedBeforeSubjects") == record.get("affectedAfterSubjects"),
            )):
                raise FixtureVerificationError("patch validation migration history is not symmetric")
            if forward.get("authorityOperations") != [] or inverse.get("authorityOperations") != []:
                raise FixtureVerificationError("patch validation migration history cannot contain Authority operations")
            before_subjects, after_subjects = record.get("affectedBeforeSubjects"), record.get("affectedAfterSubjects")
            if not isinstance(before_subjects, list) or not isinstance(after_subjects, list) or len(before_subjects) != 2 or len(after_subjects) != 2:
                raise FixtureVerificationError("patch validation migration affected snapshots are incomplete")
            if any(set(item) != {"subjectKind", "kind", "id", "value"} or item.get("subjectKind") != "entity" for item in before_subjects + after_subjects):
                raise FixtureVerificationError("patch validation migration affected snapshot is malformed")
            before_state = {(item["kind"],item["id"]):copy.deepcopy(item["value"]) for item in before_subjects}
            after_state = {(item["kind"],item["id"]):copy.deepcopy(item["value"]) for item in after_subjects}

            def apply_migration(body: dict[str, Any], state: dict[tuple[str, str], dict[str, Any]]) -> dict[tuple[str, str], dict[str, Any]] | None:
                result = copy.deepcopy(state)
                operations = body.get("applicationOperations")
                if not isinstance(operations, list) or len(operations) != 2:
                    return None
                for operation in operations:
                    if operation.get("op") != "updateEntity" or operation.get("entityKind") not in {"project", "topology"} or operation.get("unset") != []:
                        return None
                    key = (operation["entityKind"], operation.get("id"))
                    if key not in result:
                        return None
                    expected_member = "dependencies" if operation["entityKind"] == "project" else "derivation"
                    if set(operation.get("set", {})) != {expected_member}:
                        return None
                    result[key][expected_member] = copy.deepcopy(operation["set"][expected_member])
                return result

            if apply_migration(forward, before_state) != after_state or apply_migration(inverse, after_state) != before_state:
                raise FixtureVerificationError("patch validation migration forward/inverse execution differs from affected snapshots")
            before_project = before_state.get(("project", "project.mesh"), {})
            after_project = after_state.get(("project", "project.mesh"), {})
            before_topology = before_state.get(("topology", "topology.mesh"), {})
            after_topology = after_state.get(("topology", "topology.mesh"), {})
            if any((
                forward.get("dependencyLockDigest") != _digest_json(after_project.get("dependencies")),
                inverse.get("dependencyLockDigest") != _digest_json(before_project.get("dependencies")),
                record.get("beforeAuthoritativeDesignDigest") != _digest_json(before_subjects),
                record.get("afterAuthoritativeDesignDigest") != _digest_json(after_subjects),
                record.get("beforeDerivedStateDigest") != _digest_json(before_topology.get("derivation")),
                record.get("afterDerivedStateDigest") != _digest_json(after_topology.get("derivation")),
                record.get("beforeTopologyInputDigest") != record.get("afterTopologyInputDigest"),
            )):
                raise FixtureVerificationError("patch validation migration evidence digests differ")
    reconcile_transactions = context.get("applicationReconcileTransactions")
    if not isinstance(reconcile_transactions, list) or [item.get("transactionId") for item in reconcile_transactions] != sorted(item.get("transactionId") for item in reconcile_transactions):
        raise FixtureVerificationError("patch validation reconcile transactions are not canonical")
    if any(set(item) != {"transactionId", "authorityOperations", "applicationOperations"} for item in reconcile_transactions):
        raise FixtureVerificationError("patch validation reconcile transaction fields are not closed")
    migration_transactions = context.get("applicationMigrationTransactions")
    if not isinstance(migration_transactions, list) or len(migration_transactions) != 1:
        raise FixtureVerificationError("patch validation migration transaction coverage is incomplete")
    migration = migration_transactions[0]
    if set(migration) != {"transactionId", "applicationOperations", "currentDefaultEngineLock", "targetDefaultEngineLock"}:
        raise FixtureVerificationError("patch validation migration transaction fields are not closed")
    current_engine, target_engine = migration["currentDefaultEngineLock"], migration["targetDefaultEngineLock"]
    if current_engine.get("bundleManifestDigest") == target_engine.get("bundleManifestDigest"):
        raise FixtureVerificationError("patch validation migration must change the exact Engine bundle")
    if project_dependencies.get(current_engine.get("lockId")) != current_engine:
        raise FixtureVerificationError("patch validation migration current Engine differs from Project")
    return context, entities, relations


def validate_history_replay_request(contracts: Path, request: dict[str, Any]) -> tuple[str, str] | None:
    validator = Draft202012Subset(contracts)
    context, _, _ = _validate_patch_context(contracts, validator)
    required = {
        "historyTransactionId", "recordDigest", "direction", "transactionBodyDigest",
        "currentAuthoritativeDesignDigest", "currentTopologyInputDigest", "currentDerivedStateDigest",
        "currentRevisions", "nextRevisions",
    }
    if set(request) != required or request.get("direction") not in {"undo", "redo"}:
        return "history-replay", "patch.source_not_allowed"
    record = next((item for item in context["formalHistoryRecords"] if item["historyTransactionId"] == request.get("historyTransactionId")), None)
    if record is None or request.get("recordDigest") != record.get("recordDigest"):
        return "history-replay", "patch.source_not_allowed"
    expected = record["inverseTransactionDigest" if request["direction"] == "undo" else "forwardTransactionDigest"]
    if request.get("transactionBodyDigest") != expected:
        return "history-replay", "patch.source_not_allowed"
    current_prefix = "after" if request["direction"] == "undo" else "before"
    if any((
        request.get("currentAuthoritativeDesignDigest") != record[f"{current_prefix}AuthoritativeDesignDigest"],
        request.get("currentTopologyInputDigest") != record[f"{current_prefix}TopologyInputDigest"],
        request.get("currentDerivedStateDigest") != record[f"{current_prefix}DerivedStateDigest"],
    )):
        return "history-replay", "patch.source_not_allowed"
    current_revisions, next_revisions = request.get("currentRevisions"), request.get("nextRevisions")
    revision_members = {"sessionRevision", "topologyInputRevision", "derivedStateRevision"}
    if not isinstance(current_revisions, dict) or not isinstance(next_revisions, dict) or set(current_revisions) != revision_members or set(next_revisions) != revision_members:
        return "history-replay", "patch.source_not_allowed"
    committed = record["revisions"]["committed"]
    increment = record["revisions"]["replayIncrement"]
    if any(current_revisions[member] < committed[member] or next_revisions[member] != current_revisions[member] + increment[member] for member in revision_members):
        return "history-replay", "patch.source_not_allowed"
    return None


def history_replay_plan(contracts: Path, request: dict[str, Any]) -> dict[str, Any] | None:
    if validate_history_replay_request(contracts, request) is not None:
        return None
    context = load_strict_json(contracts / "patch-validation-context-v1.json")
    record = next(item for item in context["formalHistoryRecords"] if item["historyTransactionId"] == request["historyTransactionId"])
    direction = request["direction"]
    result_prefix = "before" if direction == "undo" else "after"
    return {
        "transactionBody":copy.deepcopy(record["inverseTransactionBody" if direction == "undo" else "forwardTransactionBody"]),
        "nextRevisions":copy.deepcopy(request["nextRevisions"]),
        "resultAuthoritativeDesignDigest":record[f"{result_prefix}AuthoritativeDesignDigest"],
        "resultTopologyInputDigest":record[f"{result_prefix}TopologyInputDigest"],
        "resultDerivedStateDigest":record[f"{result_prefix}DerivedStateDigest"],
    }


def _patch_transaction_failure(
    contracts: Path, document: dict[str, Any], validator: Draft202012Subset,
) -> tuple[str, str] | None:
    context, entities, relations = _validate_patch_context(contracts, validator)
    source = document.get("source", {}).get("kind")
    source_envelope = document.get("source", {})
    applicability = document.get("applicability", {})
    trusted_replay = True
    if source in {"recovery", "undo-redo"}:
        signature = _digest_json(document)
        trusted_replay = any(
            item.get("sourceKind") == source and item.get("patchId") == document.get("patchId")
            and item.get("sourceIdentity") == document.get("source", {}).get("identity")
            and item.get("sourceVersion") == document.get("source", {}).get("version")
            and item.get("patchDigest") == signature
            for item in context["trustedOrdinaryPatchTransactions"]
        )
    operation_ids = [item["id"] for item in document.get("operations", []) if "id" in item]
    operation_local_refs = [item["localRef"] for item in document.get("operations", []) if "localRef" in item]
    if len(operation_ids) != len(set(operation_ids)):
        return "duplicate-id", "patch.duplicate_id"
    if len(operation_local_refs) != len(set(operation_local_refs)):
        return "local-reference", "patch.local_ref_invalid"
    required_entity_members = {
        "project":{"name","dependencies"}, "topology":{"derivation"},
        "component":{"kind","name","packageLockId","typeKey","config","extensions"},
        "interface":{"ownerComponentRef","templateKey","name","contract","capabilities","contractConfig","nocConfig","extensions"},
        "router":{"templateKey","identityCompatibilityVersion","coordinate","properties"},
        "structural-link":{"templateKey","identityCompatibilityVersion","endpointA","endpointB","axis","properties"},
        "access-slot":{"routerRef","templateKey","identityCompatibilityVersion","displayOrder","label","allowedContracts","properties"},
        "domain":{"typeKey","name","isDefault","config"}, "package-entity":{"typeKey","data","extensions"},
    }
    required_relation_members = {
        "attachment":{"interfaceRef","state"}, "domain-membership":{"domainRef","routerRef"},
        "package-relation":{"typeKey","sources","targets","data","extensions"},
    }
    for operation in document.get("operations", []):
        set_members, unset_members = set(operation.get("set", {})), set(operation.get("unset", []))
        if set_members & unset_members:
            return "patch-invariant", "patch.invariant_violation"
        if operation.get("op") == "updateEntity" and unset_members & required_entity_members.get(operation.get("entityKind"), set()):
            return "patched-subject-schema", "patch.schema_violation"
        if operation.get("op") == "updateRelation" and unset_members & required_relation_members.get(operation.get("relationKind"), set()):
            return "patched-subject-schema", "patch.schema_violation"
    if source in {"default-engine", "extension-provider", "application-reconcile", "application-migration"}:
        expected_authority_kind = (
            source if source in {"default-engine", "extension-provider"}
            else applicability.get("structureAuthority", {}).get("kind")
        )
        if expected_authority_kind not in {"default-engine", "extension-provider"}:
            return "structure-authority", "patch.authority_conflict"
        selected_contexts = [
            item for item in context["authorityContexts"]
            if item["selectedAuthority"].get("kind") == expected_authority_kind
        ]
        if len(selected_contexts) != 1:
            return "structure-authority", "patch.authority_conflict"
        selected_context = selected_contexts[0]
        selected = selected_context["selectedAuthority"]
        if source in {"default-engine", "extension-provider"} and any(
            selected.get(member) != source_envelope.get(member)
            for member in ("kind", "identity", "version", "bundleDigest")
        ):
            return "source-authority", "patch.source_not_allowed"
        if applicability != selected_context["expectedApplicability"]:
            return "structure-authority", "patch.authority_conflict"

    preconditions = document.get("preconditions", [])
    precondition_keys = [json.dumps(item, ensure_ascii=False, sort_keys=True, separators=(",", ":")) for item in preconditions]
    if len(precondition_keys) != len(set(precondition_keys)) or precondition_keys != sorted(precondition_keys):
        return "patch-invariant", "patch.invariant_violation"
    occupied_slots = {
        value["slotRef"]["id"] for (kind, _), value in relations.items()
        if kind == "attachment" and value.get("state") == "resolved" and set(value.get("slotRef", {})) == {"id"}
    }
    for precondition in preconditions:
        kind = precondition.get("kind")
        if kind in {"entity-exists", "entity-absent", "property-equals"}:
            key = (precondition.get("entityKind"), precondition.get("id"))
            exists = key in entities
            if kind == "entity-exists" and not exists:
                return "precondition", "patch.precondition_failed"
            if kind == "entity-absent" and exists:
                return "precondition", "patch.precondition_failed"
            if kind == "property-equals" and (
                not exists
                or precondition.get("property") not in entities[key]
                or not _json_equal(entities[key][precondition["property"]], precondition.get("value"))
            ):
                return "precondition", "patch.precondition_failed"
        elif kind == "relation-exists":
            if (precondition.get("relationKind"), precondition.get("id")) not in relations:
                return "precondition", "patch.precondition_failed"
        elif kind == "slot-unoccupied":
            slot_id = precondition.get("slotId")
            if ("access-slot", slot_id) not in entities or slot_id in occupied_slots:
                return "precondition", "patch.precondition_failed"

    local_bindings: dict[str, tuple[str, str, bool]] = {}
    entity_ownership = context["packageEntityTypes"]
    relation_ownership = context["packageRelationTypes"]

    reconcile_transaction = None
    if source == "application-reconcile":
        reconcile_transaction = next(
            (item for item in context["applicationReconcileTransactions"] if item["transactionId"] == document.get("transactionId")), None
        )
        if reconcile_transaction is None or document.get("operations") != reconcile_transaction.get("applicationOperations"):
            return "ownership", "patch.ownership_violation"
        for authority_operation in reconcile_transaction["authorityOperations"]:
            op = authority_operation.get("op")
            relation = op in {"createRelation", "updateRelation", "deleteRelation"}
            kind = authority_operation.get("relationKind" if relation else "entityKind")
            collection = relations if relation else entities
            if op in {"createEntity", "createRelation"}:
                local_ref = authority_operation["localRef"]
                synthetic_id = "@" + local_ref
                local_bindings[local_ref] = (kind, synthetic_id, relation)
                collection[(kind, synthetic_id)] = copy.deepcopy(authority_operation["value"])
            elif op in {"updateEntity", "updateRelation"}:
                key = (kind, authority_operation["id"])
                if key not in collection:
                    return "reference", "patch.unknown_reference"
                value = copy.deepcopy(collection[key])
                for member in authority_operation.get("unset", []):
                    value.pop(member, None)
                value.update(copy.deepcopy(authority_operation.get("set", {})))
                collection[key] = value
            else:
                key = (kind, authority_operation["id"])
                if key not in collection:
                    return "reference", "patch.unknown_reference"
                del collection[key]
    migration_transaction = None
    if source == "application-migration":
        migration_transaction = next(
            (item for item in context["applicationMigrationTransactions"] if item["transactionId"] == document.get("transactionId")), None
        )
        if migration_transaction is None or document.get("operations") != migration_transaction.get("applicationOperations"):
            return "engine-migration-binding", "engine.migration_invalid"
        derivation_updates = [
            operation for operation in document.get("operations", [])
            if operation.get("op") == "updateEntity" and operation.get("entityKind") == "topology"
            and set(operation.get("set", {})) == {"derivation"} and operation.get("unset") == []
        ]
        dependency_updates = [
            operation for operation in document.get("operations", [])
            if operation.get("op") == "updateEntity" and operation.get("entityKind") == "project"
            and set(operation.get("set", {})) == {"dependencies"} and operation.get("unset") == []
        ]
        if len(derivation_updates) != 1 or len(dependency_updates) != 1:
            return "patch-invariant", "patch.invariant_violation"
    if source in {"application-reconcile", "application-migration"}:
        derivation_operation = next(
            operation for operation in document["operations"]
            if operation.get("op") == "updateEntity" and operation.get("entityKind") == "topology"
        )
        result_derivation = derivation_operation["set"]["derivation"]
        causal_invalid = any((
            result_derivation.get("topologyInputRevision") != applicability.get("topologyInputRevision"),
            result_derivation.get("topologyInputDigest") != applicability.get("topologyInputDigest"),
            result_derivation.get("derivedStateRevision") != applicability.get("baseDerivedStateRevision", -1) + 1,
            result_derivation.get("derivedStateDigest") == applicability.get("baseDerivedStateDigest"),
            result_derivation.get("hostSideEffectContractVersion") != applicability.get("hostSideEffectContractVersion"),
        ))
        if source == "application-reconcile":
            causal_invalid = causal_invalid or any((
                result_derivation.get("defaultEngineLockId") != applicability.get("defaultEngineLockId"),
                result_derivation.get("defaultEngineBundleDigest") != applicability.get("defaultEngineBundleDigest"),
                result_derivation.get("engineHostContractVersion") != applicability.get("engineHostContractVersion"),
                result_derivation.get("structureAuthority") != applicability.get("structureAuthority"),
            ))
        else:
            target = migration_transaction["targetDefaultEngineLock"]
            causal_invalid = causal_invalid or any((
                result_derivation.get("defaultEngineLockId") != target.get("lockId"),
                result_derivation.get("defaultEngineBundleDigest") != target.get("bundleManifestDigest"),
                result_derivation.get("engineHostContractVersion") != target.get("engineHostContractVersion"),
                result_derivation.get("hostSideEffectContractVersion") != target.get("hostSideEffectContractVersion"),
                result_derivation.get("engineCompatibilityVersion") != target.get("engineCompatibilityVersion"),
                result_derivation.get("structureAuthority") != {
                    "kind":"default-engine", "lockId":target.get("lockId"), "identity":target.get("id"),
                    "version":target.get("version"), "bundleDigest":target.get("bundleManifestDigest"),
                },
            ))
        if causal_invalid:
            return ("engine-migration-binding", "engine.migration_invalid") if source == "application-migration" else ("patch-invariant", "patch.invariant_violation")

    def package_owner(operation: dict[str, Any], relation: bool) -> str | None:
        kind_name = operation.get("relationKind" if relation else "entityKind")
        if kind_name != ("package-relation" if relation else "package-entity"):
            return None
        collection = relations if relation else entities
        ownership = relation_ownership if relation else entity_ownership
        if operation.get("op", "").startswith("create"):
            type_key = operation.get("value", {}).get("typeKey")
        else:
            type_key = collection.get((kind_name, operation.get("id")), {}).get("typeKey")
        declaration = ownership.get(type_key)
        return declaration.get("ownership") if relation and isinstance(declaration, dict) else declaration

    def source_allows(operation: dict[str, Any]) -> bool:
        entity_kind = operation.get("entityKind")
        relation_kind = operation.get("relationKind")
        changed = set(operation.get("set", {})) | set(operation.get("unset", []))
        owner = package_owner(operation, relation_kind is not None)
        if source == "user-command":
            if entity_kind in {"router", "structural-link", "access-slot", "topology"} or owner == "engine":
                return False
            if entity_kind == "project" and changed - {"name"}:
                return False
            if entity_kind == "component" and changed & {"kind", "packageLockId", "typeKey"}:
                return False
            return not (operation.get("op") == "deleteEntity" and entity_kind == "component")
        if source in {"default-engine", "extension-provider"}:
            return (
                entity_kind in {None, "router", "structural-link", "access-slot", "package-entity"}
                and relation_kind in {None, "package-relation"}
                and owner != "user"
            )
        if source in {"application-reconcile", "application-migration"}:
            if entity_kind == "topology":
                return operation.get("op") == "updateEntity" and set(operation.get("set", {})) == {"derivation"} and operation.get("unset") == []
            if entity_kind == "project":
                return (
                    source == "application-migration" and operation.get("op") == "updateEntity"
                    and set(operation.get("set", {})) == {"dependencies"} and operation.get("unset") == []
                )
            if source == "application-migration":
                return False
            if entity_kind is not None:
                return entity_kind == "domain"
            if relation_kind == "attachment":
                existing = relations.get(("attachment", operation.get("id")))
                changed = operation.get("set", {})
                return (
                    operation.get("op") == "updateRelation"
                    and isinstance(existing, dict) and existing.get("state") == "resolved"
                    and set(changed) == {"state", "intendedTarget", "reasonCode"}
                    and changed.get("state") == "unresolved"
                    and changed.get("reasonCode") == "attachment.target_removed"
                    and operation.get("unset") == ["routerRef", "slotRef"]
                    and changed.get("intendedTarget") == {
                        "routerRef": existing.get("routerRef"), "slotRef": existing.get("slotRef")
                    }
                )
            if relation_kind == "package-relation":
                existing = relations.get(("package-relation", operation.get("id")), {})
                declaration = relation_ownership.get(existing.get("typeKey"), {})
                if owner != "user" or not declaration.get("unresolvedAllowed") or operation.get("op") != "updateRelation" or operation.get("unset") != []:
                    return False
                changed = operation.get("set", {})
                if not changed or set(changed) - {"sources", "targets"}:
                    return False
                converted = False
                for member_name, replacement in changed.items():
                    original = existing.get(member_name)
                    if not isinstance(original, list) or not isinstance(replacement, list) or len(original) != len(replacement):
                        return False
                    for before, after in zip(original, replacement):
                        if before == after:
                            continue
                        if not (
                            before.get("state") == "resolved" and after.get("state") == "unresolved"
                            and after.get("intendedSubject") == before.get("subject")
                            and after.get("reasonCode") == "relation.target_removed"
                        ):
                            return False
                        converted = True
                return converted
            return relation_kind == "domain-membership"
        return source in {"recovery", "undo-redo"}

    for operation in document.get("operations", []):
        if source == "user-command" and operation.get("op") == "deleteEntity" and operation.get("entityKind") == "component":
            return "patch-invariant", "patch.invariant_violation"
        operation_relation = operation.get("relationKind") is not None
        operation_subject_kind = operation.get("relationKind" if operation_relation else "entityKind")
        if operation_subject_kind in {"package-entity", "package-relation"} and package_owner(operation, operation_relation) is None:
            return "reference", "patch.unknown_reference"
        if not source_allows(operation):
            return "ownership", "patch.ownership_violation"
        op = operation.get("op")
        if op in {"createEntity", "createRelation"}:
            expected_prefix = "authority:" if source in {"default-engine", "extension-provider"} else "application:"
            if not operation.get("localRef", "").startswith(expected_prefix):
                return "local-reference", "patch.local_ref_invalid"
        relation = op in {"createRelation", "updateRelation", "deleteRelation"}
        kind = operation.get("relationKind" if relation else "entityKind")
        if op in {"updateEntity", "updateRelation"} and kind in {"package-entity", "package-relation"}:
            if "typeKey" in operation.get("set", {}) or "typeKey" in operation.get("unset", []):
                return "ownership", "patch.ownership_violation"
        collection = relations if relation else entities
        referenced_local_refs: list[str] = []
        stack = [operation.get("value"), operation.get("set")]
        while stack:
            node = stack.pop()
            if isinstance(node, dict):
                if set(node) == {"localRef"} and isinstance(node.get("localRef"), str):
                    referenced_local_refs.append(node["localRef"])
                else:
                    stack.extend(node.values())
            elif isinstance(node, list):
                stack.extend(node)
        if any(reference not in local_bindings for reference in referenced_local_refs):
            return "local-reference", "patch.local_ref_invalid"
        if op in {"createEntity", "createRelation"}:
            local_ref = operation.get("localRef")
            if local_ref in local_bindings:
                return "local-reference", "patch.local_ref_invalid"
            synthetic_id = "@" + local_ref
            key = (kind, synthetic_id)
            value = copy.deepcopy(operation.get("value", {}))
            if key in collection:
                return "duplicate-id", "patch.duplicate_id"
            local_bindings[local_ref] = (kind, synthetic_id, relation)
            collection[key] = value
        elif op in {"updateEntity", "updateRelation"}:
            key = (kind, operation.get("id"))
            if key not in collection:
                return "reference", "patch.unknown_reference"
            value = copy.deepcopy(collection[key])
            for member in operation.get("unset", []):
                value.pop(member, None)
            value.update(copy.deepcopy(operation.get("set", {})))
            collection[key] = value
        else:
            key = (kind, operation.get("id"))
            if key not in collection:
                return "reference", "patch.unknown_reference"
            del collection[key]

        current_value = collection.get(key)
        if current_value is not None:
            if kind == "access-slot":
                allowed = current_value.get("allowedContracts", [])
                contract_keys = [
                    (item.get("contractLockId"), json.dumps(item.get("capabilityConstraints", {}), sort_keys=True, separators=(",", ":")))
                    for item in allowed
                ]
                if len(contract_keys) != len(set(contract_keys)) or any(
                    len(item.get("roles", [])) != len(set(item.get("roles", []))) for item in allowed
                ):
                    return "patch-invariant", "patch.invariant_violation"
            if not _validate_patch_subject_schema(validator, kind, current_value, relation):
                return "patched-subject-schema", "patch.schema_violation"
        if source not in {"application-reconcile", "application-migration"}:
            reference_failure = _patch_state_reference_failure(entities, relations, local_bindings)
            if reference_failure:
                return reference_failure
            dependency_failure = _patch_dependency_reference_failure(entities, relations)
            if dependency_failure:
                return dependency_failure

    for (kind, _), value in entities.items():
        if not _validate_patch_subject_schema(validator, kind, value, False):
            return "patched-subject-schema", "patch.schema_violation"
    for (kind, _), value in relations.items():
        if not _validate_patch_subject_schema(validator, kind, value, True):
            return "patched-subject-schema", "patch.schema_violation"
    final_failure = _patch_state_reference_failure(entities, relations, local_bindings)
    if final_failure:
        return final_failure
    dependency_failure = _patch_dependency_reference_failure(entities, relations)
    if dependency_failure:
        return dependency_failure
    domain_failure = _patch_domain_invariant_failure(entities, relations)
    if domain_failure:
        return domain_failure
    relation_declaration_failure = _patch_package_relation_declaration_failure(relations, context["packageRelationTypes"])
    if relation_declaration_failure:
        return relation_declaration_failure
    if not trusted_replay:
        return "source-authority", "patch.source_not_allowed"
    return None


def semantic_failure(
    schema_id: str, document: Any, validator: Draft202012Subset | None = None
) -> tuple[str, str] | None:
    contracts = validator.contracts if validator is not None else Path(__file__).resolve().parents[1]
    if schema_id == "ipcraft.project-design.v1":
        groups = [document.get(name, []) for name in ("components", "interfaces", "topologies")]
        topology = document.get("topologies", [{}])[0] if document.get("topologies") else {}
        groups += [topology.get(name, []) for name in ("routers", "structuralLinks", "accessSlots", "attachments", "domains", "domainMemberships", "packageEntities", "packageRelations")]
        ids = [document.get("id")]
        ids += [item.get("id") for group in groups for item in group if isinstance(item, dict)]
        ids += [item.get("lockId") for item in document.get("dependencies", []) if isinstance(item, dict)]
        if len(ids) != len(set(ids)):
            return "project-duplicate-id", "project.duplicate_id"
        if ProjectCanonicalSetVerifier(
            (validator.contracts if validator is not None else Path(__file__).resolve().parents[1]), validator
        ).has_duplicate(document):
            return "project-invariant", "project.invariant_violation"
        known = set(ids)
        components = {item["id"]: item for item in document.get("components", [])}
        dependencies = {item["lockId"]: item for item in document.get("dependencies", [])}
        interfaces = {item["id"]: item for item in document.get("interfaces", [])}
        routers_by_id = {item["id"]: item for item in topology.get("routers", [])}
        slots_by_id = {item["id"]: item for item in topology.get("accessSlots", [])}
        domains_by_id = {item["id"]: item for item in topology.get("domains", [])}
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
        authority = derivation.get("structureAuthority", {})
        authority_lock = dependencies.get(authority.get("lockId"))
        authority_kind = authority.get("kind")
        if (
            engine_lock is None
            or engine_lock.get("kind") != "default-engine"
            or authority_kind not in {"default-engine", "extension-provider"}
            or authority_lock is None
            or authority_lock.get("kind") != authority_kind
            or (authority_kind == "default-engine" and authority.get("lockId") != derivation.get("defaultEngineLockId"))
        ):
            return "project-reference", "project.unknown_reference"
        package_lock = dependencies.get(next(iter(components.values())).get("packageLockId")) if components else None
        if (
            package_lock is None
            or derivation.get("packageBundleDigest") != package_lock.get("bundleManifestDigest")
            or derivation.get("defaultEngineBundleDigest") != engine_lock.get("bundleManifestDigest")
            or authority.get("bundleDigest") != authority_lock.get("bundleManifestDigest")
            or authority.get("identity") != authority_lock.get("id")
            or authority.get("version") != authority_lock.get("version")
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
                if _portable_paths_invalid(contracts, [closure.get("entrypoint")]):
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
            if membership.get("routerId") not in routers_by_id or membership.get("domainId") not in domains_by_id:
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
        domains = {item_id: item.get("typeKey") for item_id, item in domains_by_id.items()}
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
        if _portable_paths_invalid(contracts, [entry.get("path") for entry in document.get(member, [])]):
            return ("bundle-manifest", "dependency.manifest_invalid") if member == "files" else ("tool-artifact", "tool.artifact_invalid")
        if member == "files":
            if document.get("manifestDigest") != bundle_manifest_digest(contracts, document):
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
            if isinstance(default, (int, float, Decimal)) and not isinstance(default, bool):
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
        if _package_declaration_invalid(contracts, document):
            return "package-declaration", "package.invariant_violation"
    elif schema_id == "ipcraft.engine-bundle.v1":
        if _portable_paths_invalid(contracts, [document.get("entrypoint")]):
            return "engine-bundle-binding", "engine.bundle_mismatch"
        if document.get("engineHostContractVersion") != "ipcraft.engine-host.v1":
            return "engine-host-contract", "engine.host_contract_unsupported"
        if not any(value == "linux-x86_64-gnu-v1" for value in document.get("supportedPlatformAbis", [])):
            return "engine-platform", "engine.platform_unsupported"
    elif schema_id == "ipcraft.recovery.v1":
        if document.get("projectId") != document.get("authoritativeDesign", {}).get("id"):
            return "recovery-binding", "recovery.binding_mismatch"
    elif schema_id == "ipcraft.tool-input.v1":
        tool_paths = [document.get(name) for name in ("projectDesignFile", "resultFile", "reportDirectory", "outputDirectory") if document.get(name) is not None]
        if _portable_paths_invalid(contracts, tool_paths):
            return "tool-input", "tool.input_invalid"
        if document.get("kind") == "generator" and document.get("snapshotDigest") != document.get("formallySavedProjectDigest"):
            return "tool-input", "tool.input_invalid"
        report_directory = document.get("reportDirectory")
        project_file = document.get("projectDesignFile")
        result_file = document.get("resultFile")
        if project_file == result_file or report_directory == "inputs":
            return "tool-input", "tool.input_invalid"
        if not isinstance(report_directory, str) or not isinstance(result_file, str) or not result_file.startswith(report_directory + "/"):
            return "tool-input", "tool.input_invalid"
        if document.get("kind") == "semantic-drc" and document.get("outputDirectory") is not None:
            return "tool-input", "tool.input_invalid"
        if document.get("kind") == "generator" and document.get("outputDirectory") in {None, report_directory, "reports"}:
            return "tool-input", "tool.input_invalid"
        if _duplicates(document.get("dependencies", []), lambda item: item.get("lockId")):
            return "tool-input", "tool.input_invalid"
    elif schema_id == "ipcraft.tool-result.v1":
        result_paths = [document.get(name) for name in ("diagnosticReport", "artifactManifest") if document.get(name) is not None]
        if _portable_paths_invalid(contracts, result_paths):
            return "runtime-result-binding", "tool.result_mismatch"
        if document.get("expectedInvocationId") and document.get("expectedInvocationId") != document.get("invocationId"):
            return "runtime-result-binding", "tool.result_mismatch"
    elif schema_id == "ipcraft.pipeline-result.v1":
        if _portable_paths_invalid(contracts, [item.get("result") for item in document.get("steps", [])]):
            return "pipeline-result", "pipeline.result_invalid"
        step_ids = {item.get("stepId") for item in document.get("steps", [])}
        if document.get("failedStepId") is not None and document.get("failedStepId") not in step_ids:
            return "pipeline-result", "pipeline.result_invalid"
    elif schema_id == "ipcraft.diagnostic-report.v1":
        if _duplicates(document.get("diagnostics", []), lambda item: json.dumps(item, sort_keys=True)):
            return "diagnostic-report", "diagnostic.report_invalid"
    elif schema_id == "ipcraft.step-result.v1":
        if document.get("toolResult") is not None and _portable_paths_invalid(contracts, [document["toolResult"]]):
            return "generic-structure", "contract.schema_invalid"
    elif schema_id == "ipcraft.patch.v1":
        transaction_failure = _patch_transaction_failure(contracts, document, validator)
        if transaction_failure:
            return transaction_failure
        return None
        patch_context = load_strict_json(contracts / "patch-validation-context-v1.json")
        entity_type_ownership = patch_context.get("packageEntityTypes", {})
        entity_subject_types = {
            item["id"]: item.get("value", {}).get("typeKey")
            for item in patch_context.get("entities", []) if item.get("kind") == "package-entity"
        }
        relation_type_ownership = patch_context.get("packageRelationTypes", {})
        relation_subject_types = {
            item["id"]: item.get("value", {}).get("typeKey")
            for item in patch_context.get("relations", []) if item.get("kind") == "package-relation"
        }
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
        visible_local_refs: set[str] = set()
        for operation in operations:
            referenced: list[str] = []
            stack = [operation.get("value"), operation.get("set")]
            while stack:
                node = stack.pop()
                if isinstance(node, dict):
                    if set(node) == {"localRef"} and isinstance(node.get("localRef"), str):
                        referenced.append(node["localRef"])
                    else:
                        stack.extend(node.values())
                elif isinstance(node, list):
                    stack.extend(node)
            if any(reference not in visible_local_refs for reference in referenced):
                return "local-reference", "patch.local_ref_invalid"
            if "localRef" in operation:
                visible_local_refs.add(operation["localRef"])
        required_properties = {
            "project": {"name", "dependencies"},
            "topology": {"derivation"},
            "component": {"kind", "name", "packageLockId", "typeKey", "config", "extensions"},
            "interface": {"ownerComponentId", "templateKey", "name", "contract", "capabilities", "contractConfig", "nocConfig", "extensions"},
            "router": {"templateKey", "identityCompatibilityVersion", "coordinate", "properties"},
            "structural-link": {"templateKey", "identityCompatibilityVersion", "endpointA", "endpointB", "axis", "properties"},
            "access-slot": {"routerId", "templateKey", "identityCompatibilityVersion", "displayOrder", "label", "allowedContracts", "properties"},
            "domain": {"typeKey", "name", "isDefault", "config"},
            "package-entity": {"typeKey", "data", "extensions"},
        }
        required_relation_properties = {
            "attachment": {"interfaceRef", "state"},
            "domain-membership": {"domainRef", "routerRef"},
            "package-relation": {"typeKey", "sources", "targets", "data", "extensions"},
        }
        for operation in operations:
            if operation.get("op") in {"updateEntity", "updateRelation"} and set(operation.get("set", {})) & set(operation.get("unset", [])):
                return "patch-invariant", "patch.invariant_violation"
            if operation.get("op") == "updateEntity" and set(operation.get("unset", [])) & required_properties.get(operation.get("entityKind"), set()):
                return "patched-subject-schema", "patch.schema_violation"
            if operation.get("op") == "updateRelation" and set(operation.get("unset", [])) & required_relation_properties.get(operation.get("relationKind"), set()):
                return "patched-subject-schema", "patch.schema_violation"
        for operation in operations:
            entity_kind = operation.get("entityKind")
            relation_kind = operation.get("relationKind")
            changed = set(operation.get("set", {})) | set(operation.get("unset", []))
            package_ownership: str | None = None
            if entity_kind == "package-entity":
                type_key = operation.get("value", {}).get("typeKey") if operation.get("op") == "createEntity" else entity_subject_types.get(operation.get("id"))
                if type_key not in entity_type_ownership:
                    return "reference", "patch.unknown_reference"
                package_ownership = entity_type_ownership[type_key]
            if relation_kind == "package-relation":
                type_key = operation.get("value", {}).get("typeKey") if operation.get("op") == "createRelation" else relation_subject_types.get(operation.get("id"))
                if type_key not in relation_type_ownership:
                    return "reference", "patch.unknown_reference"
                package_ownership = relation_type_ownership[type_key]
            if source == "user-command":
                if operation.get("op") == "deleteEntity" and entity_kind == "component":
                    return "patch-invariant", "patch.invariant_violation"
                if entity_kind in {"router", "structural-link", "access-slot", "topology"}:
                    return "ownership", "patch.ownership_violation"
                if entity_kind == "project" and changed - {"name"}:
                    return "ownership", "patch.ownership_violation"
                if entity_kind == "component" and changed & {"kind", "packageLockId", "typeKey"}:
                    return "ownership", "patch.ownership_violation"
                if package_ownership == "engine":
                    return "ownership", "patch.ownership_violation"
            elif source in {"default-engine", "extension-provider"}:
                if entity_kind not in {None, "router", "structural-link", "access-slot", "package-entity"}:
                    return "ownership", "patch.ownership_violation"
                if relation_kind not in {None, "package-relation"}:
                    return "ownership", "patch.ownership_violation"
                if package_ownership == "user":
                    return "ownership", "patch.ownership_violation"
            elif source in {"application-reconcile", "application-migration"}:
                if entity_kind in {"component", "interface", "router", "structural-link", "access-slot", "package-entity"}:
                    return "ownership", "patch.ownership_violation"
            if operation.get("op", "").startswith("create") and "localRef" in operation:
                expected_prefix = "authority:" if source in {"default-engine", "extension-provider"} else "application:"
                if not operation["localRef"].startswith(expected_prefix):
                    return "local-reference", "patch.local_ref_invalid"
        if source in {"application-reconcile", "application-migration"}:
            derivation_updates = [
                operation for operation in operations
                if operation.get("op") == "updateEntity" and operation.get("entityKind") == "topology"
                and set(operation.get("set", {})) == {"derivation"} and operation.get("unset") == []
            ]
            dependency_updates = [
                operation for operation in operations
                if operation.get("op") == "updateEntity" and operation.get("entityKind") == "project"
                and set(operation.get("set", {})) == {"dependencies"} and operation.get("unset") == []
            ]
            if len(derivation_updates) != 1:
                return "patch-invariant", "patch.invariant_violation"
            if source == "application-reconcile" and dependency_updates:
                return "patch-invariant", "patch.invariant_violation"
            if source == "application-migration" and len(dependency_updates) != 1:
                return "patch-invariant", "patch.invariant_violation"
        if source in {"default-engine", "extension-provider"}:
            authority = document.get("applicability", {}).get("structureAuthority", {})
            source_envelope = document.get("source", {})
            if any(
                authority.get(member) != source_envelope.get(member)
                for member in ("kind", "identity", "version", "bundleDigest")
            ):
                return "structure-authority", "patch.authority_conflict"
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
    semantic = semantic_failure(schema_id, document, validator)
    if semantic:
        return Classification("core-semantic", semantic[0], semantic[1])
    return Classification(None, None, None)


def load_catalog(contracts: Path) -> list[dict[str, Any]]:
    return load_strict_json(contracts / "fixture-catalog.json")["items"]


def verify_authoring_coverage(contracts: Path, items: list[dict[str, Any]]) -> None:
    coverage = load_strict_json(contracts / "fixture-coverage-v1.json")
    if set(coverage) != {"schema", "requirementsDigest", "coverageDigest", "roots", "requirements"}:
        raise FixtureVerificationError("fixture coverage envelope fields are not closed")
    if coverage.get("schema") != "ipcraft.fixture-coverage.v1" or not isinstance(coverage.get("roots"), dict):
        raise FixtureVerificationError("fixture coverage envelope is invalid")
    catalog_roots = {
        item["id"] for item in load_strict_json(contracts / "schema-catalog.json")["items"]
        if item["id"] != "ipcraft.fixture-catalog.v1"
    }
    if set(coverage["roots"]) != catalog_roots:
        raise FixtureVerificationError("fixture coverage roots do not exactly match standalone schema roots")
    by_path = {item["path"]: item for item in items}
    loaded_by_path: dict[str, Any] = {}
    for schema_id, tiers in coverage["roots"].items():
        if set(tiers) != {"minimal", "representative", "maximumShape"}:
            raise FixtureVerificationError(f"fixture coverage tiers are incomplete for {schema_id}")
        for tier, paths in tiers.items():
            if not isinstance(paths, list) or not paths:
                raise FixtureVerificationError(f"fixture coverage {schema_id}/{tier} is empty")
            for path in paths:
                entry = by_path.get(path)
                if entry is None or entry["schemaId"] != schema_id or entry["expected"] != "accept":
                    raise FixtureVerificationError(f"fixture coverage path is not an accepted {schema_id}: {path}")
                loaded_by_path[path] = load_strict_json(contracts / path)

    requirements = coverage.get("requirements")
    if not isinstance(requirements, dict) or set(requirements) != catalog_roots:
        raise FixtureVerificationError("fixture coverage requirements do not exactly match standalone roots")
    requirements_digest = "sha256:" + hashlib.sha256(
        json.dumps(requirements, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    if coverage.get("requirementsDigest") != requirements_digest:
        raise FixtureVerificationError("fixture coverage requirements digest does not match content")
    if requirements_digest != FROZEN_COVERAGE_REQUIREMENTS_DIGEST:
        raise FixtureVerificationError("fixture coverage requirements differ from the frozen contract")
    coverage_digest = "sha256:" + hashlib.sha256(
        json.dumps({"roots": coverage["roots"], "requirements": requirements}, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    if coverage.get("coverageDigest") != coverage_digest or coverage_digest != FROZEN_COVERAGE_CONTRACT_DIGEST:
        raise FixtureVerificationError("fixture coverage root/tier mapping differs from the frozen contract")

    def values_at(node: Any, pointer: str) -> list[Any]:
        nodes = [node]
        for raw in pointer.removeprefix("/").split("/") if pointer else []:
            token = raw.replace("~1", "/").replace("~0", "~")
            next_nodes: list[Any] = []
            for current in nodes:
                if token == "*":
                    if isinstance(current, list):
                        next_nodes.extend(current)
                    elif isinstance(current, dict):
                        next_nodes.extend(current.values())
                elif isinstance(current, list) and re.fullmatch(r"0|[1-9][0-9]*", token):
                    index = int(token)
                    if index < len(current):
                        next_nodes.append(current[index])
                elif isinstance(current, dict) and token in current:
                    next_nodes.append(current[token])
            nodes = next_nodes
        return nodes

    for schema_id, tier_requirements in requirements.items():
        if not isinstance(tier_requirements, dict) or set(tier_requirements) != {"minimal", "representative", "maximumShape"}:
            raise FixtureVerificationError(f"fixture coverage requirements malformed for {schema_id}")
        for tier, predicates in tier_requirements.items():
            if tier not in {"minimal", "representative", "maximumShape"} or not isinstance(predicates, dict):
                raise FixtureVerificationError(f"fixture coverage predicate tier malformed for {schema_id}")
            unknown_predicates = set(predicates) - {"minimumArrayLengths", "requiredPointers", "discriminatorCoverage"}
            if unknown_predicates:
                raise FixtureVerificationError(
                    f"fixture coverage predicate keys unsupported for {schema_id}: {sorted(unknown_predicates)}"
                )
            if not isinstance(predicates.get("minimumArrayLengths", {}), dict):
                raise FixtureVerificationError(f"fixture coverage minimumArrayLengths malformed for {schema_id}")
            if not isinstance(predicates.get("requiredPointers", []), list):
                raise FixtureVerificationError(f"fixture coverage requiredPointers malformed for {schema_id}")
            if not isinstance(predicates.get("discriminatorCoverage", {}), dict):
                raise FixtureVerificationError(f"fixture coverage discriminatorCoverage malformed for {schema_id}")
            documents = [loaded_by_path[path] for path in coverage["roots"][schema_id][tier]]
            for pointer, minimum in predicates.get("minimumArrayLengths", {}).items():
                arrays = [value for document in documents for value in values_at(document, pointer) if isinstance(value, list)]
                if not arrays or max(map(len, arrays)) < minimum:
                    raise FixtureVerificationError(f"fixture coverage array predicate failed: {schema_id} {tier} {pointer}")
            for pointer in predicates.get("requiredPointers", []):
                if not any(values_at(document, pointer) for document in documents):
                    raise FixtureVerificationError(f"fixture coverage required pointer missing: {schema_id} {tier} {pointer}")
            for pointer, expected_values in predicates.get("discriminatorCoverage", {}).items():
                observed = {
                    value for document in documents for value in values_at(document, pointer)
                    if isinstance(value, (str, int, bool))
                }
                if not set(expected_values) <= observed:
                    raise FixtureVerificationError(
                        f"fixture coverage discriminator predicate failed: {schema_id} {tier} {pointer}; "
                        f"missing {sorted(set(expected_values) - observed)}"
                    )

    validator = Draft202012Subset(contracts)
    _validate_patch_context(contracts, validator)
    for source_kind in (
        "user-command", "application-reconcile", "application-migration", "default-engine",
        "extension-provider", "recovery", "undo-redo",
    ):
        path = f"fixtures/valid/patch-source-{source_kind}.json"
        document = load_strict_json(contracts / path)
        if document.get("source", {}).get("kind") != source_kind or not document.get("operations"):
            raise FixtureVerificationError(f"patch source witness is not executable: {source_kind}")


def copy_contract_tree(source: Path, destination: Path) -> None:
    shutil.copytree(source, destination)


def verify_all(contracts: Path) -> Summary:
    unsupported = audit_schema_keywords(contracts)
    if unsupported:
        raise FixtureVerificationError(f"unsupported JSON Schema keywords: {sorted(unsupported)}")
    audit_project_canonical_set_coverage(contracts)
    try:
        fixture_count, schema_count, _ = verify_fixture_catalog.verify(contracts / "fixture-catalog.json", contracts)
    except verify_fixture_catalog.VerificationError as error:
        raise FixtureVerificationError(str(error)) from error
    items = load_catalog(contracts)
    verify_authoring_coverage(contracts, items)
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
