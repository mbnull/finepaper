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


def _json_equal(left: Any, right: Any) -> bool:
    return type(left) is type(right) and left == right


class Draft202012Subset:
    def __init__(self, contracts: Path):
        self.contracts = contracts
        self.schemas: dict[str, tuple[dict[str, Any], Path]] = {}
        for item in json.loads((contracts / "schema-catalog.json").read_text())["items"]:
            path = contracts / item["path"]
            self.schemas[item["id"]] = (json.loads(path.read_text()), path)

    def validate(self, schema_id: str, instance: Any) -> None:
        root, path = self.schemas[schema_id]
        self._validate(root, instance, root, path, "$")

    def _resolve(self, reference: str, root: dict[str, Any], root_path: Path) -> tuple[dict[str, Any], dict[str, Any], Path]:
        file_part, _, fragment = reference.partition("#")
        if file_part:
            target_path = (root_path.parent / file_part).resolve()
            target = json.loads(target_path.read_text())
        else:
            target_path, target = root_path, root
        node: Any = target
        if fragment:
            if not fragment.startswith("/"):
                raise FixtureVerificationError(f"unsupported schema anchor {reference!r}")
            for raw in fragment[1:].split("/"):
                token = raw.replace("~1", "/").replace("~0", "~")
                node = node[int(token)] if isinstance(node, list) else node[token]
        if not isinstance(node, dict):
            raise FixtureVerificationError(f"schema reference {reference!r} does not resolve to an object")
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

    def _is_valid(self, schema: dict[str, Any], instance: Any, root: dict[str, Any], root_path: Path) -> bool:
        try:
            self._validate(schema, instance, root, root_path, "$")
            return True
        except SchemaFailure:
            return False

    def _validate(self, schema: dict[str, Any], instance: Any, root: dict[str, Any], root_path: Path, path: str) -> None:
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
                encodings = [json.dumps(value, sort_keys=True, separators=(",", ":")) for value in instance]
                if len(encodings) != len(set(encodings)):
                    raise SchemaFailure(path, "duplicate array item")
            if isinstance(schema.get("items"), dict):
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
                elif schema.get("additionalProperties") is False:
                    raise SchemaFailure(path, f"additional property {member}")
                elif isinstance(schema.get("additionalProperties"), dict):
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
            branch = schema.get("then") if self._is_valid(schema["if"], instance, root, root_path) else schema.get("else")
            if isinstance(branch, dict):
                self._validate(branch, instance, root, root_path, path)


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


def semantic_failure(schema_id: str, document: Any) -> tuple[str, str] | None:
    if schema_id == "ipcraft.project-design.v1":
        groups = [document.get(name, []) for name in ("components", "interfaces", "topologies")]
        topology = document.get("topologies", [{}])[0] if document.get("topologies") else {}
        groups += [topology.get(name, []) for name in ("routers", "structuralLinks", "accessSlots", "attachments", "domains", "domainMemberships", "packageEntities", "packageRelations")]
        ids = [item.get("id") for group in groups for item in group if isinstance(item, dict)]
        ids += [item.get("lockId") for item in document.get("dependencies", []) if isinstance(item, dict)]
        if len(ids) != len(set(ids)):
            return "project-duplicate-id", "project.duplicate_id"
        known = set(ids)
        for attachment in topology.get("attachments", []):
            for key in ("interfaceId", "slotId"):
                if attachment.get(key) not in known:
                    return "project-reference", "project.unknown_reference"
        for membership in topology.get("domainMemberships", []):
            if membership.get("routerId") not in known or membership.get("domainId") not in known:
                return "project-reference", "project.unknown_reference"
        for link in topology.get("structuralLinks", []):
            if link.get("endpointA") not in known or link.get("endpointB") not in known:
                return "project-reference", "project.unknown_reference"
        for slot in topology.get("accessSlots", []):
            if slot.get("routerId") not in known:
                return "project-reference", "project.unknown_reference"
        routers = {item.get("id") for item in topology.get("routers", [])}
        domains = {item.get("id"): item.get("typeKey") for item in topology.get("domains", [])}
        domain_types = set(domains.values())
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
        for field in document.get("fields", []):
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
    return json.loads((contracts / "fixture-catalog.json").read_text())["items"]


def copy_contract_tree(source: Path, destination: Path) -> None:
    shutil.copytree(source, destination)


def verify_all(contracts: Path) -> Summary:
    try:
        fixture_count, schema_count, _ = verify_fixture_catalog.verify(contracts / "fixture-catalog.json", contracts)
    except verify_fixture_catalog.VerificationError as error:
        raise FixtureVerificationError(str(error)) from error
    items = load_catalog(contracts)
    valid = invalid = schema_phase = semantic_phase = 0
    for entry in items:
        document = json.loads((contracts / entry["path"]).read_text())
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
