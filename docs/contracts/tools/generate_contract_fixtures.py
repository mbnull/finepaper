#!/usr/bin/env python3
"""Generate the deterministic Gate 0 standalone fixture corpus."""

from __future__ import annotations

import copy
import argparse
import hashlib
import json
import shutil
from decimal import Decimal, ROUND_CEILING
from pathlib import Path
from typing import Any

import verify_contract_fixtures as verifier


CONTRACTS = Path(__file__).resolve().parents[1]
DIGEST_A = "sha256:" + "a" * 64
DIGEST_B = "sha256:" + "b" * 64


class UnsatisfiableSampleError(RuntimeError):
    """A deterministic authoring failure for a schema with no valid sample."""


class SampleBuilder:
    def __init__(self, contracts: Path):
        self.validator = verifier.Draft202012Subset(contracts)

    def root(self, schema_id: str, validate: bool = True) -> Any:
        schema, path = self.validator.schemas[schema_id]
        value = self.build(schema, schema, path)
        value = self.satisfy(schema, value, schema, path)
        if validate:
            self.validate_sample(schema_id, value)
        return value

    def validate_sample(self, schema_id: str, value: Any) -> None:
        schema, path = self.validator.schemas[schema_id]
        try:
            self.validator._validate(schema, value, schema, path, "$")
        except verifier.SchemaFailure as error:
            raise UnsatisfiableSampleError(
                f"generated sample for {schema_id} does not satisfy its schema: {error}"
            ) from error

    def build(self, schema: Any, root: Any, root_path: Path, context: str = "false schema") -> Any:
        if schema is True:
            return None
        if schema is False:
            raise UnsatisfiableSampleError(f"cannot generate sample for {context}: false schema is unsatisfiable")
        if "$ref" in schema:
            target, target_root, target_path = self.validator._resolve(schema["$ref"], root, root_path)
            return self.build(target, target_root, target_path, context)
        if "const" in schema: return copy.deepcopy(schema["const"])
        if "enum" in schema: return copy.deepcopy(schema["enum"][0])
        if "oneOf" in schema:
            nullable = next((branch for branch in schema["oneOf"] if isinstance(branch, dict) and branch.get("type") == "null"), None)
            branches = ([nullable] if nullable is not None else []) + [branch for branch in schema["oneOf"] if branch is not nullable]
            for branch in branches:
                try:
                    candidate = self.build(branch, root, root_path, context)
                except UnsatisfiableSampleError:
                    continue
                if self.validator._is_valid(schema, candidate, root, root_path):
                    return candidate
            raise UnsatisfiableSampleError(f"cannot generate sample for {context}: oneOf has no satisfiable branch")
        if "anyOf" in schema:
            for branch in schema["anyOf"]:
                try:
                    candidate = self.build(branch, root, root_path, context)
                except UnsatisfiableSampleError:
                    continue
                if self.validator._is_valid(schema, candidate, root, root_path):
                    return candidate
            raise UnsatisfiableSampleError(f"cannot generate sample for {context}: anyOf has no satisfiable branch")
        declared = schema.get("type")
        if isinstance(declared, list): declared = next(value for value in declared if value != "null")
        if declared is None:
            if "properties" in schema or "required" in schema: declared = "object"
            elif "items" in schema: declared = "array"
            elif "allOf" in schema:
                value: Any = {}
                return self.satisfy(schema, value, root, root_path)
            else: return {}
        if declared == "object":
            result = {}
            for member in schema.get("required", []):
                result[member] = self.build(
                    schema.get("properties", {}).get(member, {}), root, root_path,
                    f"required property {member}",
                )
            return self.satisfy(schema, result, root, root_path)
        if declared == "array":
            result = [self.build(schema.get("items", {}), root, root_path, "array item") for _ in range(schema.get("minItems", 0))]
            if "contains" in schema:
                required = schema.get("minContains", 1)
                while sum(self.validator._is_valid(schema["contains"], item, root, root_path) for item in result) < required:
                    result.append(self.build(schema["contains"], root, root_path, "contains item"))
            return result
        if declared == "string":
            pattern = schema.get("pattern", "")
            if "sha256:" in pattern: return DIGEST_A
            if "authority:" in pattern: return "authority:000001"
            if schema.get("format") == "date-time": return "2026-07-14T00:00:00Z"
            if "\\.json" in pattern: return "fixture.json"
            return "value"
        if declared == "integer": return self.numeric_sample(schema, True, context)
        if declared == "number": return self.numeric_sample(schema, False, context)
        if declared == "boolean": return False
        if declared == "null": return None
        raise RuntimeError(f"unsupported sample type {declared!r}")

    def numeric_sample(self, schema: dict[str, Any], integer: bool, context: str) -> int | float:
        lower = Decimal(str(schema.get("minimum", schema.get("exclusiveMinimum", 0))))
        exclusive = "exclusiveMinimum" in schema and "minimum" not in schema
        step = Decimal(str(schema["multipleOf"])) if "multipleOf" in schema else None
        if step is not None:
            multiplier = (lower / step).to_integral_value(rounding=ROUND_CEILING)
            candidate = multiplier * step
            if exclusive and candidate <= lower:
                candidate += step
        elif integer:
            candidate = lower.to_integral_value(rounding=ROUND_CEILING)
            if exclusive and candidate <= lower:
                candidate += 1
        else:
            candidate = lower + (Decimal(1) if exclusive else Decimal(0))

        if integer and candidate != candidate.to_integral_value():
            if step is None:
                candidate = candidate.to_integral_value(rounding=ROUND_CEILING)
            else:
                for _ in range(10000):
                    candidate += step
                    if candidate == candidate.to_integral_value():
                        break
                else:
                    raise UnsatisfiableSampleError(
                        f"cannot generate sample for {context}: integer multipleOf search exceeded V1 authoring bound"
                    )
        if "maximum" in schema and candidate > Decimal(str(schema["maximum"])):
            raise UnsatisfiableSampleError(f"cannot generate sample for {context}: numeric bounds are unsatisfiable")
        if integer:
            return int(candidate)
        return int(candidate) if candidate == candidate.to_integral_value() else float(candidate)

    def satisfy(
        self, schema: Any, value: Any, root: Any, root_path: Path,
        fallback_schema: Any = None,
    ) -> Any:
        if schema is True:
            return value
        if schema is False:
            raise UnsatisfiableSampleError("cannot satisfy sample: false schema is unsatisfiable")
        if "const" in schema:
            return copy.deepcopy(schema["const"])
        if "enum" in schema and not any(verifier._json_equal(value, candidate) for candidate in schema["enum"]):
            return copy.deepcopy(schema["enum"][0])
        if "$ref" in schema:
            target, target_root, target_path = self.validator._resolve(schema["$ref"], root, root_path)
            value = self.satisfy(target, value, target_root, target_path, fallback_schema)
        for branch in schema.get("allOf", []):
            if branch is False:
                raise UnsatisfiableSampleError("cannot generate sample for false schema: allOf contains an unsatisfiable branch")
            if branch is True:
                continue
            selected = branch
            if "if" in branch:
                selected = branch.get("then", {}) if self.validator._is_valid(branch["if"], value, root, root_path) else branch.get("else", {})
            value = self.satisfy(selected, value, root, root_path, schema)
        if isinstance(value, dict):
            fallback_properties = fallback_schema.get("properties", {}) if isinstance(fallback_schema, dict) else {}
            for member in schema.get("required", []):
                if member not in value:
                    member_schema = schema.get("properties", {}).get(member, fallback_properties.get(member, {}))
                    value[member] = self.build(
                        member_schema, root, root_path,
                        f"required property {member}",
                    )
            for member, member_schema in schema.get("properties", {}).items():
                if member in value:
                    value[member] = self.satisfy(
                        member_schema, value[member], root, root_path,
                        fallback_properties.get(member),
                    )
        return value


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def mesh_project(base: dict[str, Any], rows: int, columns: int, with_interface: bool) -> dict[str, Any]:
    project = copy.deepcopy(base)
    project["id"] = f"project.mesh-{rows}x{columns}"
    project["name"] = f"Mesh {rows}x{columns}"
    topology = project["topologies"][0]
    routers = []
    slots = []
    for row in range(rows):
        for column in range(columns):
            router_id = f"router.{row}.{column}"
            routers.append({"id":router_id,"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":row,"column":column},"properties":{}})
            slots.append({"id":f"slot.{row}.{column}","routerId":router_id,"templateKey":"local-0","identityCompatibilityVersion":1,"displayOrder":0,"label":"Local 0","allowedContracts":[],"properties":{}})
    links = []
    for row in range(rows):
        for column in range(columns):
            if column + 1 < columns:
                links.append({"id":f"link.h.{row}.{column}","templateKey":"mesh-link","identityCompatibilityVersion":1,"endpointA":f"router.{row}.{column}","endpointB":f"router.{row}.{column+1}","axis":"horizontal","properties":{}})
            if row + 1 < rows:
                links.append({"id":f"link.v.{row}.{column}","templateKey":"mesh-link","identityCompatibilityVersion":1,"endpointA":f"router.{row}.{column}","endpointB":f"router.{row+1}.{column}","axis":"vertical","properties":{}})
    domain = {"id":"domain.power.default","typeKey":"power","name":"Default Power","isDefault":True,"config":{}}
    memberships = [{"id":f"membership.{item['id']}","domainId":domain["id"],"routerId":item["id"]} for item in routers]
    topology.update({"routers":routers,"structuralLinks":links,"accessSlots":slots,"domains":[domain],"domainMemberships":memberships})
    if with_interface:
        project["dependencies"].append({"lockId":"dep.contract.axi5","kind":"interface-contract","id":"amba.axi5","version":"1.0.0","bundleManifestDigest":DIGEST_B})
        project["interfaces"] = [{"id":"interface.axi","ownerComponentId":"component.noc","templateKey":"axi-boundary","name":"AXI Boundary","contract":{"lockId":"dep.contract.axi5","role":"initiator"},"capabilities":{"dataWidth":128,"coherent":False},"contractConfig":{"addressWidth":48,"idWidth":8},"nocConfig":{},"extensions":[]}]
        slots[0]["allowedContracts"] = [{"contractLockId":"dep.contract.axi5","roles":["initiator"],"capabilityConstraints":{"dataWidth":128,"coherent":False}}]
        topology["attachments"] = [{"id":"attachment.axi","interfaceId":"interface.axi","state":"resolved","routerId":"router.0.0","slotId":"slot.0.0"}]
    return project


def package_extension_project(base: dict[str, Any]) -> dict[str, Any]:
    project = mesh_project(base, 2, 2, False)
    project["id"] = "project.mesh-2x2-package-extension"
    project["name"] = "Mesh 2x2 Package Extension"
    topology = project["topologies"][0]
    topology["packageEntities"] = [{"id":"package-entity.endpoint","typeKey":"vendor.endpoint","data":{"label":"Endpoint"},"extensions":[]}]
    topology["packageRelations"] = [{
        "id":"package-relation.route","typeKey":"vendor.route",
        "sources":[{"state":"resolved","subject":{"kind":"router","id":"router.0.0"}}],
        "targets":[{"state":"resolved","subject":{"kind":"package-entity","id":"package-entity.endpoint"}}],
        "data":{"enabled":True},"extensions":[],
    }]
    project["extensions"] = [{"ownerLockId":"dep.noc.simple","schema":"vendor.extension.v1","version":"1","data":{"opaque":{"mode":"preview"}}}]
    return project


def frozen_reconcile_applicability(authority_kind: str) -> dict[str, Any]:
    authority = {
        "default-engine": {
            "kind":"default-engine", "lockId":"dep.engine.default",
            "identity":"ipcraft.default-noc-engine", "version":"1.0.0", "bundleDigest":DIGEST_A,
        },
        "extension-provider": {
            "kind":"extension-provider", "lockId":"dep.provider",
            "identity":"vendor.provider", "version":"1", "bundleDigest":DIGEST_B,
        },
    }[authority_kind]
    return {
        "schema":"ipcraft.reconcile-applicability.v1",
        "groupId":"group.topology.1", "requestGeneration":1,
        "topologyInputRevision":1, "topologyInputDigest":DIGEST_A,
        "baseDerivedStateRevision":0, "baseDerivedStateDigest":DIGEST_A,
        "baseAuthoritativeDesignDigest":DIGEST_A,
        "structureAuthority":copy.deepcopy(authority),
        "packageBundleDigest":DIGEST_B, "reconcileDependencySetDigest":DIGEST_A,
        "defaultEngineLockId":"dep.engine.default", "defaultEngineBundleDigest":DIGEST_A,
        "engineHostContractVersion":"ipcraft.engine-host.v1",
        "hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1",
    }


def digest_json(value: Any) -> str:
    canonical = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def patch_context_document(project: dict[str, Any]) -> dict[str, Any]:
    derivation = copy.deepcopy(project["topologies"][0]["derivation"])
    dependencies = copy.deepcopy(project["dependencies"])
    dependencies.extend([
        {"lockId":"dep.noc.simple","kind":"noc-package","id":"simple-noc","version":"1.0.0","bundleManifestDigest":DIGEST_B},
        {"lockId":"dep.engine.default","kind":"default-engine","id":"ipcraft.default-noc-engine","version":"1.0.0","bundleManifestDigest":DIGEST_A,"engineHostContractVersion":"ipcraft.engine-host.v1","engineCompatibilityVersion":"1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1","supportedPlatformAbis":["linux-x86_64-gnu-v1"]},
        {"lockId":"dep.contract.axi5","kind":"interface-contract","id":"amba.axi5","version":"1.0.0","bundleManifestDigest":DIGEST_B},
        {"lockId":"dep.runtime.provider","kind":"runtime","id":"provider-runtime","version":"1","bundleManifestDigest":DIGEST_A,"runtimeClosure":{"closureKind":"package-contained","runtimeId":"provider-runtime","runtimeVersion":"1","runtimeDistributionBundleDigest":DIGEST_A,"entrypoint":"bin/provider-runtime","platformAbi":"linux-x86_64-gnu-v1","invocationProfile":"ipcraft.native-isolated.v1","moduleSearchPolicy":"runtime-and-tool-bundles-only","environmentProfile":"ipcraft.empty-utf8-utc.v1","networkPolicy":"prohibited"}},
        {"lockId":"dep.provider","kind":"extension-provider","id":"vendor.provider","version":"1","bundleManifestDigest":DIGEST_B,"protocolVersion":"ipcraft.provider-protocol.v1","runtimeLockId":"dep.runtime.provider"},
        {"lockId":"dep.tool.drc","kind":"drc-tool","id":"vendor.drc","version":"1","bundleManifestDigest":DIGEST_B,"toolProtocolVersion":"ipcraft.tool-input.v1","runtimeLockId":"dep.runtime.provider"},
        {"lockId":"dep.tool.generator","kind":"generator-tool","id":"vendor.generator","version":"1","bundleManifestDigest":DIGEST_B,"toolProtocolVersion":"ipcraft.tool-input.v1","runtimeLockId":"dep.runtime.provider"},
    ])
    dependencies = list({item["lockId"]:item for item in dependencies}.values())
    dependencies.sort(key=lambda item: item["lockId"])
    entities = [
        {"kind":"access-slot","id":"slot.free","value":{"routerRef":{"id":"router.0.1"},"templateKey":"local-0","identityCompatibilityVersion":1,"displayOrder":0,"label":"Free","allowedContracts":[{"contractLockId":"dep.contract.axi5","roles":["initiator"],"capabilityConstraints":{}}],"properties":{}}},
        {"kind":"access-slot","id":"slot.occupied","value":{"routerRef":{"id":"router.0.0"},"templateKey":"local-0","identityCompatibilityVersion":1,"displayOrder":0,"label":"Occupied","allowedContracts":[{"contractLockId":"dep.contract.axi5","roles":["initiator"],"capabilityConstraints":{}}],"properties":{}}},
        {"kind":"component","id":"component.noc","value":{"kind":"noc","name":"NoC","packageLockId":"dep.noc.simple","typeKey":"mesh-noc","config":{"rows":1,"columns":2},"extensions":[]}},
        {"kind":"domain","id":"domain.default","value":{"typeKey":"power","name":"Default Power","isDefault":True,"config":{}}},
        {"kind":"domain","id":"domain.secondary","value":{"typeKey":"power","name":"Secondary Power","isDefault":False,"config":{}}},
        {"kind":"interface","id":"interface.boundary","value":{"ownerComponentRef":{"id":"component.noc"},"templateKey":"axi-boundary","name":"AXI Boundary","contract":{"lockId":"dep.contract.axi5","role":"initiator"},"capabilities":{},"contractConfig":{},"nocConfig":{},"extensions":[]}},
        {"kind":"interface","id":"interface.unattached","value":{"ownerComponentRef":{"id":"component.noc"},"templateKey":"axi-boundary","name":"Unattached","contract":{"lockId":"dep.contract.axi5","role":"initiator"},"capabilities":{},"contractConfig":{},"nocConfig":{},"extensions":[]}},
        {"kind":"package-entity","id":"package-entity.engine","value":{"typeKey":"vendor.engine-entity","data":{"state":"derived"},"extensions":[]}},
        {"kind":"package-entity","id":"package-entity.user","value":{"typeKey":"vendor.user-entity","data":{"label":"user"},"extensions":[]}},
        {"kind":"project","id":"project.mesh","value":{"name":"Mesh","dependencies":dependencies}},
        {"kind":"router","id":"router.0.0","value":{"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":0,"column":0},"properties":{}}},
        {"kind":"router","id":"router.0.1","value":{"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":0,"column":1},"properties":{}}},
        {"kind":"structural-link","id":"link.0.0-0.1","value":{"templateKey":"mesh-link","identityCompatibilityVersion":1,"endpointA":{"id":"router.0.0"},"endpointB":{"id":"router.0.1"},"axis":"horizontal","properties":{}}},
        {"kind":"topology","id":"topology.mesh","value":{"derivation":derivation}},
    ]
    relations = [
        {"kind":"attachment","id":"attachment.boundary","value":{"interfaceRef":{"id":"interface.boundary"},"state":"resolved","routerRef":{"id":"router.0.0"},"slotRef":{"id":"slot.occupied"}}},
        {"kind":"domain-membership","id":"membership.0","value":{"domainRef":{"id":"domain.default"},"routerRef":{"id":"router.0.0"}}},
        {"kind":"domain-membership","id":"membership.1","value":{"domainRef":{"id":"domain.secondary"},"routerRef":{"id":"router.0.1"}}},
        {"kind":"package-relation","id":"package-relation.engine","value":{"typeKey":"vendor.engine-relation","sources":[],"targets":[],"data":{"state":"derived"},"extensions":[]}},
        {"kind":"package-relation","id":"package-relation.resolved-only","value":{"typeKey":"vendor.user-resolved-only","sources":[{"state":"resolved","subject":{"kind":"router","ref":{"id":"router.0.0"}}}],"targets":[],"data":{"label":"resolved-only"},"extensions":[]}},
        {"kind":"package-relation","id":"package-relation.user","value":{"typeKey":"vendor.user-relation","sources":[{"state":"resolved","subject":{"kind":"structural-link","ref":{"id":"link.0.0-0.1"}}}],"targets":[],"data":{"label":"user"},"extensions":[]}},
    ]
    replay_operation = {"op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"name":"Restored"},"unset":[]}
    trusted_replays = []
    for source_kind in ("recovery", "undo-redo"):
        source = {"kind":source_kind,"identity":"ipcraft.host","version":"1"}
        patch_id = f"trusted.{source_kind}.1"
        signature_input = {
            "schema":"ipcraft.patch.v1", "transactionId":"value", "patchId":patch_id,
            "source":source, "causality":{"sessionRevision":0}, "preconditions":[], "operations":[replay_operation],
        }
        signature = digest_json(signature_input)
        trusted_replays.append({"sourceKind":source_kind,"patchId":patch_id,"sourceIdentity":"ipcraft.host","sourceVersion":"1","patchDigest":signature})
    topology_host_id_map = {
        "application:000101":"domain.secondary", "application:000102":"membership.1",
        "authority:link.0.0-0.1":"link.0.0-0.1", "authority:router.0.1":"router.0.1",
        "authority:slot.free":"slot.free",
    }
    topology_tombstones = [
        {"subjectKind":"entity","kind":kind,"id":item_id,"value":copy.deepcopy(value)}
        for (kind, item_id), value in {(item["kind"], item["id"]):item["value"] for item in entities}.items()
        if item_id in {"domain.secondary","router.0.1","slot.free","link.0.0-0.1"}
    ] + [
        {"subjectKind":"relation","kind":kind,"id":item_id,"value":copy.deepcopy(value)}
        for (kind, item_id), value in {(item["kind"], item["id"]):item["value"] for item in relations}.items()
        if item_id == "membership.1"
    ]
    topology_tombstones.sort(key=lambda item:(item["subjectKind"],item["kind"],item["id"]))
    topology_forward = {
        "topologyIntent":{"rows":1,"columns":1},
        "authorityOperations":[
            {"op":"deleteEntity","entityKind":"access-slot","id":"slot.free"},
            {"op":"deleteEntity","entityKind":"structural-link","id":"link.0.0-0.1"},
            {"op":"deleteEntity","entityKind":"router","id":"router.0.1"},
        ],
        "applicationOperations":[
            {"op":"deleteRelation","relationKind":"domain-membership","id":"membership.1"},
            {"op":"deleteEntity","entityKind":"domain","id":"domain.secondary"},
        ],
        "hostIdMap":copy.deepcopy(topology_host_id_map), "tombstones":copy.deepcopy(topology_tombstones),
    }
    topology_inverse = {
        "topologyIntent":{"rows":1,"columns":2},
        "authorityOperations":[
            {"op":"createEntity","entityKind":"router","localRef":"authority:router.0.1","hostId":"router.0.1","value":copy.deepcopy(next(item["value"] for item in entities if item["id"] == "router.0.1"))},
            {"op":"createEntity","entityKind":"structural-link","localRef":"authority:link.0.0-0.1","hostId":"link.0.0-0.1","value":copy.deepcopy(next(item["value"] for item in entities if item["id"] == "link.0.0-0.1"))},
            {"op":"createEntity","entityKind":"access-slot","localRef":"authority:slot.free","hostId":"slot.free","value":copy.deepcopy(next(item["value"] for item in entities if item["id"] == "slot.free"))},
        ],
        "applicationOperations":[
            {"op":"createEntity","entityKind":"domain","localRef":"application:000101","hostId":"domain.secondary","value":copy.deepcopy(next(item["value"] for item in entities if item["id"] == "domain.secondary"))},
            {"op":"createRelation","relationKind":"domain-membership","localRef":"application:000102","hostId":"membership.1","value":copy.deepcopy(next(item["value"] for item in relations if item["id"] == "membership.1"))},
        ],
        "hostIdMap":copy.deepcopy(topology_host_id_map), "tombstones":[],
    }
    history_target_engine_lock = {
        "lockId":"dep.engine.target", "kind":"default-engine", "id":"ipcraft.default-noc-engine", "version":"2.0.0",
        "bundleManifestDigest":DIGEST_B, "engineHostContractVersion":"ipcraft.engine-host.v1", "engineCompatibilityVersion":"2",
        "hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1", "supportedPlatformAbis":["linux-x86_64-gnu-v1"],
    }
    history_target_dependencies = [copy.deepcopy(history_target_engine_lock) if item["kind"] == "default-engine" else copy.deepcopy(item) for item in dependencies]
    history_target_dependencies.sort(key=lambda item:item["lockId"])
    history_target_derivation = copy.deepcopy(derivation)
    history_target_derivation.update({
        "derivedStateRevision":derivation.get("derivedStateRevision", 0) + 1, "derivedStateDigest":DIGEST_B,
        "defaultEngineLockId":"dep.engine.target", "defaultEngineBundleDigest":DIGEST_B, "engineCompatibilityVersion":"2",
        "structureAuthority":{"kind":"default-engine","lockId":"dep.engine.target","identity":"ipcraft.default-noc-engine","version":"2.0.0","bundleDigest":DIGEST_B},
    })
    migration_before_subjects = [
        {"subjectKind":"entity","kind":"project","id":"project.mesh","value":{"name":"Mesh","dependencies":copy.deepcopy(dependencies)}},
        {"subjectKind":"entity","kind":"topology","id":"topology.mesh","value":{"derivation":copy.deepcopy(derivation)}},
    ]
    migration_after_subjects = [
        {"subjectKind":"entity","kind":"project","id":"project.mesh","value":{"name":"Mesh","dependencies":copy.deepcopy(history_target_dependencies)}},
        {"subjectKind":"entity","kind":"topology","id":"topology.mesh","value":{"derivation":copy.deepcopy(history_target_derivation)}},
    ]
    migration_forward = {
        "currentDefaultEngineLockId":"dep.engine.default", "targetDefaultEngineLockId":"dep.engine.target",
        "dependencyLockDigest":digest_json(history_target_dependencies), "authorityOperations":[], "applicationOperations":[
            {"op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"dependencies":copy.deepcopy(history_target_dependencies)},"unset":[]},
            {"op":"updateEntity","entityKind":"topology","id":"topology.mesh","set":{"derivation":copy.deepcopy(history_target_derivation)},"unset":[]},
        ],
        "hostIdMap":{}, "tombstones":[],
    }
    migration_inverse = {
        "currentDefaultEngineLockId":"dep.engine.target", "targetDefaultEngineLockId":"dep.engine.default",
        "dependencyLockDigest":digest_json(dependencies), "authorityOperations":[], "applicationOperations":[
            {"op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"dependencies":copy.deepcopy(dependencies)},"unset":[]},
            {"op":"updateEntity","entityKind":"topology","id":"topology.mesh","set":{"derivation":copy.deepcopy(derivation)},"unset":[]},
        ],
        "hostIdMap":{}, "tombstones":[],
    }
    formal_history = []
    for history_id, kind, forward, inverse, revisions, before_subjects, after_subjects, host_id_map, tombstones in (
        ("history.topology.1", "topology", topology_forward, topology_inverse,
         {"committed":{"sessionRevision":10,"topologyInputRevision":4,"derivedStateRevision":7},"replayIncrement":{"sessionRevision":1,"topologyInputRevision":1,"derivedStateRevision":1}},
         topology_tombstones, [], topology_host_id_map, topology_tombstones),
        ("history.migration.1", "default-engine-migration", migration_forward, migration_inverse,
         {"committed":{"sessionRevision":20,"topologyInputRevision":8,"derivedStateRevision":12},"replayIncrement":{"sessionRevision":1,"topologyInputRevision":1,"derivedStateRevision":1}},
         migration_before_subjects, migration_after_subjects, {}, []),
    ):
        record = {
            "historyTransactionId":history_id, "kind":kind,
            "beforeAuthoritativeDesignDigest":DIGEST_A, "afterAuthoritativeDesignDigest":DIGEST_B,
            "beforeTopologyInputDigest":DIGEST_A, "afterTopologyInputDigest":DIGEST_B,
            "beforeDerivedStateDigest":DIGEST_A, "afterDerivedStateDigest":DIGEST_B,
            "forwardTransactionBody":forward, "inverseTransactionBody":inverse,
            "forwardTransactionDigest":digest_json(forward), "inverseTransactionDigest":digest_json(inverse),
            "affectedBeforeSubjects":before_subjects, "affectedAfterSubjects":after_subjects,
            "localRefToHostId":host_id_map, "tombstones":tombstones, "revisions":revisions,
        }
        if kind == "topology":
            record["beforeTopologyInputDigest"] = digest_json(inverse["topologyIntent"])
            record["afterTopologyInputDigest"] = digest_json(forward["topologyIntent"])
        else:
            record["afterTopologyInputDigest"] = record["beforeTopologyInputDigest"]
            record["beforeAuthoritativeDesignDigest"] = digest_json(before_subjects)
            record["afterAuthoritativeDesignDigest"] = digest_json(after_subjects)
            record["beforeDerivedStateDigest"] = digest_json(before_subjects[1]["value"]["derivation"])
            record["afterDerivedStateDigest"] = digest_json(after_subjects[1]["value"]["derivation"])
        record["recordDigest"] = digest_json(record)
        formal_history.append(record)
    reconciled_derivation = copy.deepcopy(derivation)
    reconciled_derivation["derivedStateRevision"] = frozen_reconcile_applicability("default-engine")["baseDerivedStateRevision"] + 1
    reconciled_derivation["derivedStateDigest"] = DIGEST_B
    derivation_operation = {
        "op":"updateEntity", "entityKind":"topology", "id":"topology.mesh",
        "set":{"derivation":reconciled_derivation}, "unset":[],
    }
    provider_reconciled_derivation = copy.deepcopy(reconciled_derivation)
    provider_reconciled_derivation["structureAuthority"] = copy.deepcopy(frozen_reconcile_applicability("extension-provider")["structureAuthority"])
    provider_derivation_operation = {
        "op":"updateEntity", "entityKind":"topology", "id":"topology.mesh",
        "set":{"derivation":provider_reconciled_derivation}, "unset":[],
    }
    router_create_authority = [
        {"op":"createEntity","entityKind":"router","localRef":"authority:router.new","value":{"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":1,"column":0},"properties":{}}},
        {"op":"createEntity","entityKind":"structural-link","localRef":"authority:link.new","value":{"templateKey":"mesh-link","identityCompatibilityVersion":1,"endpointA":{"id":"router.0.0"},"endpointB":{"localRef":"authority:router.new"},"axis":"vertical","properties":{}}},
        {"op":"createEntity","entityKind":"access-slot","localRef":"authority:slot.new","value":{"routerRef":{"localRef":"authority:router.new"},"templateKey":"local-0","identityCompatibilityVersion":1,"displayOrder":0,"label":"New","allowedContracts":[],"properties":{}}},
    ]
    router_create_application = [copy.deepcopy(derivation_operation), {
        "op":"createRelation","relationKind":"domain-membership","localRef":"application:000020",
        "value":{"domainRef":{"id":"domain.default"},"routerRef":{"localRef":"authority:router.new"}},
    }]
    router_delete_authority = [
        {"op":"deleteEntity","entityKind":"access-slot","id":"slot.free"},
        {"op":"deleteEntity","entityKind":"structural-link","id":"link.0.0-0.1"},
        {"op":"deleteEntity","entityKind":"router","id":"router.0.1"},
    ]
    router_delete_application = [
        copy.deepcopy(derivation_operation),
        {"op":"deleteRelation","relationKind":"domain-membership","id":"membership.1"},
        {"op":"deleteEntity","entityKind":"domain","id":"domain.secondary"},
        {"op":"updateRelation","relationKind":"package-relation","id":"package-relation.user","set":{"sources":[{"state":"unresolved","intendedSubject":{"kind":"structural-link","ref":{"id":"link.0.0-0.1"}},"reasonCode":"relation.target_removed"}]},"unset":[]},
    ]
    slot_delete_application = [copy.deepcopy(derivation_operation), {
        "op":"updateRelation","relationKind":"attachment","id":"attachment.boundary",
        "set":{"state":"unresolved","intendedTarget":{"routerRef":{"id":"router.0.0"},"slotRef":{"id":"slot.occupied"}},"reasonCode":"attachment.target_removed"},
        "unset":["routerRef","slotRef"],
    }]
    relation_delete_application = [copy.deepcopy(derivation_operation), {
        "op":"updateRelation","relationKind":"package-relation","id":"package-relation.user",
        "set":{"sources":[{"state":"unresolved","intendedSubject":{"kind":"structural-link","ref":{"id":"link.0.0-0.1"}},"reasonCode":"relation.target_removed"}]}, "unset":[],
    }]
    target_engine_lock = copy.deepcopy(history_target_engine_lock)
    migration_dependencies = [copy.deepcopy(target_engine_lock) if item["kind"] == "default-engine" else copy.deepcopy(item) for item in dependencies]
    migration_dependencies.sort(key=lambda item:item["lockId"])
    migration_derivation = copy.deepcopy(reconciled_derivation)
    migration_derivation.update({
        "defaultEngineLockId":"dep.engine.target", "defaultEngineBundleDigest":DIGEST_B,
        "engineCompatibilityVersion":"2",
        "structureAuthority":{"kind":"default-engine","lockId":"dep.engine.target","identity":"ipcraft.default-noc-engine","version":"2.0.0","bundleDigest":DIGEST_B},
    })
    migration_application_operations = [
        {"op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"dependencies":migration_dependencies},"unset":[]},
        {"op":"updateEntity","entityKind":"topology","id":"topology.mesh","set":{"derivation":migration_derivation},"unset":[]},
    ]
    context = {
        "schema":"ipcraft.patch-validation-context.v1", "version":"1",
        "authorityContexts":[
            {"contextId":"authority.default","selectedAuthority":{"kind":"default-engine","lockId":"dep.engine.default","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":DIGEST_A},"expectedApplicability":frozen_reconcile_applicability("default-engine")},
            {"contextId":"authority.provider","selectedAuthority":{"kind":"extension-provider","lockId":"dep.provider","identity":"vendor.provider","version":"1","bundleDigest":DIGEST_B},"expectedApplicability":frozen_reconcile_applicability("extension-provider")},
        ],
        "dependencyLocks":copy.deepcopy(dependencies),
        "packageEntityTypes":{"vendor.engine-entity":"engine","vendor.user-entity":"user"},
        "packageRelationTypes":{
            "vendor.engine-relation":{"ownership":"engine","unresolvedAllowed":False,"sources":{"kinds":["router","structural-link"],"minimum":0,"maximum":1},"targets":{"kinds":["package-entity"],"minimum":0,"maximum":1}},
            "vendor.user-relation":{"ownership":"user","unresolvedAllowed":True,"sources":{"kinds":["router","structural-link"],"minimum":0,"maximum":1},"targets":{"kinds":["package-entity"],"minimum":0,"maximum":1}},
            "vendor.user-resolved-only":{"ownership":"user","unresolvedAllowed":False,"sources":{"kinds":["router"],"minimum":1,"maximum":1},"targets":{"kinds":["package-entity"],"minimum":0,"maximum":1}},
        },
        "trustedOrdinaryPatchTransactions":trusted_replays,
        "formalHistoryRecords":formal_history,
        "applicationReconcileTransactions":[
            {"transactionId":"txn.reconcile.base","authorityOperations":[],"applicationOperations":[copy.deepcopy(derivation_operation)]},
            {"transactionId":"txn.reconcile.base.provider","authorityOperations":[],"applicationOperations":[copy.deepcopy(provider_derivation_operation)]},
            {"transactionId":"txn.reconcile.relation-target-delete","authorityOperations":[{"op":"deleteEntity","entityKind":"structural-link","id":"link.0.0-0.1"}],"applicationOperations":relation_delete_application},
            {"transactionId":"txn.reconcile.router-create","authorityOperations":router_create_authority,"applicationOperations":router_create_application},
            {"transactionId":"txn.reconcile.router-delete","authorityOperations":router_delete_authority,"applicationOperations":router_delete_application},
            {"transactionId":"txn.reconcile.slot-delete","authorityOperations":[{"op":"deleteEntity","entityKind":"access-slot","id":"slot.occupied"}],"applicationOperations":slot_delete_application},
        ],
        "applicationMigrationTransactions":[
            {"transactionId":"txn.migration.default-engine","applicationOperations":migration_application_operations,
             "currentDefaultEngineLock":copy.deepcopy(next(item for item in dependencies if item["kind"] == "default-engine")),
             "targetDefaultEngineLock":copy.deepcopy(target_engine_lock)},
        ],
        "entities":entities, "relations":relations,
        "occupiedSlots":["slot.occupied"], "freeSlots":["slot.free"],
    }
    canonical = json.dumps(context, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    context["contextDigest"] = "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()
    return context


def generate(contracts: Path) -> None:
    builder = SampleBuilder(contracts)
    fixtures_root = contracts / "fixtures"
    if fixtures_root.exists(): shutil.rmtree(fixtures_root)
    entries: list[dict[str, Any]] = []
    valid: dict[str, Any] = {}
    catalog = json.loads((contracts / "schema-catalog.json").read_text())["items"]
    schema_ids = [item["id"] for item in catalog if item["id"] != "ipcraft.fixture-catalog.v1"]

    def add(name: str, schema_id: str, document: Any, expected: str, phase: str,
            boundary: str | None, code: str | None, evidence: str | None = None) -> None:
        if expected == "accept":
            builder.validate_sample(schema_id, document)
        relative = f"fixtures/{'valid' if expected == 'accept' else 'invalid'}/{name}.json"
        write_json(contracts / relative, document)
        entries.append({
            "path": relative, "schemaId": schema_id, "validationPhase": phase,
            "failureBoundary": boundary, "expected": expected, "errorCode": code,
            "behaviorEvidence": evidence,
        })

    for schema_id in schema_ids:
        document = builder.root(schema_id, validate=False)
        if schema_id == "ipcraft.bundle-manifest.v1":
            document["manifestDigest"] = verifier.bundle_manifest_digest(contracts, document)
        if schema_id == "ipcraft.engine-bundle.v1":
            document["engineHostContractVersion"] = "ipcraft.engine-host.v1"
            document["hostSideEffectContractVersion"] = "ipcraft.noc-side-effects.v1"
            document["supportedPlatformAbis"] = ["linux-x86_64-gnu-v1"]
            document["entrypoint"] = "bin/default-noc-engine"
        if schema_id == "ipcraft.noc-package.v1":
            def dimension_field(key: str) -> dict[str, Any]:
                return {"key":key,"type":"int","label":key.title(),"description":"Mesh dimension","default":1,"required":True,"readOnly":False,"minimum":1,"maximum":64,"unit":None,"values":None,"visibleWhen":None,"enabledWhen":None,"topologyDriving":True}
            document["configuration"]["global"]["fields"] = [dimension_field("columns"), dimension_field("rows")]
            document.update({"id":"simple-noc","name":"Simple Mesh NoC","version":"1.0.0"})
            document["component"].update({"typeKey":"mesh-noc","displayName":"Mesh NoC"})
            document["topology"].update({"rowField":"rows","columnField":"columns"})
            document["packageEntityTypes"] = [{"typeKey":"vendor.endpoint","ownership":"user","genericEditable":True,"topologyDriving":False,"schema":{"type":"object"}}]
            document["packageRelationTypes"] = [{"typeKey":"vendor.route","ownership":"user","topologyDriving":False,"sources":{"kinds":["router"],"minimum":1,"maximum":1},"targets":{"kinds":["package-entity"],"minimum":1,"maximum":1},"unresolvedAllowed":False,"schema":{"type":"object"}}]
            document["domainTypes"] = [{"key":"power","label":"Power","defaultName":"Default Power","visual":{"fill":"#e8f1ff","border":"#3b6ea8","pattern":"solid"},"configuration":{"fields":[]}}]
        if schema_id == "ipcraft.project-design.v1":
            document["dependencies"] = [
                {"lockId":"dep.engine.default","kind":"default-engine","id":"ipcraft.default-noc-engine","version":"1.0.0","bundleManifestDigest":DIGEST_A,"engineHostContractVersion":"ipcraft.engine-host.v1","engineCompatibilityVersion":"1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1","supportedPlatformAbis":["linux-x86_64-gnu-v1"]},
                {"lockId":"dep.noc.simple","kind":"noc-package","id":"simple-noc","version":"1.0.0","bundleManifestDigest":DIGEST_B},
            ]
            document["components"][0].update({"id":"component.noc","packageLockId":"dep.noc.simple","typeKey":"mesh-noc"})
            document["topologies"][0].update({"id":"topology.mesh","ownerComponentId":"component.noc"})
            derivation = document["topologies"][0]["derivation"]
            derivation.update({"packageBundleDigest":DIGEST_B,"defaultEngineLockId":"dep.engine.default","defaultEngineBundleDigest":DIGEST_A,"engineHostContractVersion":"ipcraft.engine-host.v1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1","engineCompatibilityVersion":"1"})
            derivation["structureAuthority"].update({"lockId":"dep.engine.default","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":DIGEST_A})
            document = mesh_project(document, 1, 1, False)
        if schema_id == "ipcraft.recovery.v1":
            project = copy.deepcopy(valid["ipcraft.project-design.v1"])
            document["authoritativeDesign"] = project
            document["projectId"] = project["id"]
        if schema_id == "ipcraft.tool-input.v1":
            document.update({
                "projectDesignFile": "inputs/project-design.json",
                "resultFile": "reports/tool-result.json",
                "reportDirectory": "reports",
            })
        if schema_id == "ipcraft.pipeline-result.v1":
            for step in document.get("steps", []):
                step["result"] = f"steps/{step['stepId']}/step-result.json"
        if schema_id == "ipcraft.noc-side-effects.v1":
            document["input"]["authorityPatch"]["source"].update({"identity":"ipcraft.default-noc-engine","bundleDigest":DIGEST_A})
            document["expected"]["applicationPatch"]["source"] = {"kind":"application-reconcile","identity":"ipcraft.host","version":"1"}
        builder.validate_sample(schema_id, document)
        valid[schema_id] = document
        add(schema_id.removeprefix("ipcraft.").removesuffix(".v1").replace(".", "-"), schema_id,
            document, "accept", "schema", None, None)

    write_json(contracts / "patch-validation-context-v1.json", patch_context_document(valid["ipcraft.project-design.v1"]))

    for protocol in ("axi5", "ace", "chi"):
        contract = copy.deepcopy(valid["ipcraft.interface-contract.v1"])
        contract.update({"id":f"amba.{protocol}","name":protocol.upper(),"version":"1.0"})
        roles = {
            "axi5": [{"key":"initiator","label":"Initiator"},{"key":"target","label":"Target"}],
            "ace": [{"key":"coherent-manager","label":"Coherent Manager"},{"key":"coherent-subordinate","label":"Coherent Subordinate"}],
            "chi": [{"key":"requester","label":"Requester"},{"key":"home","label":"Home"},{"key":"subordinate","label":"Subordinate"}],
        }
        contract["roles"] = roles[protocol]
        contract["capabilities"] = [
            {"key":"dataWidth","type":"int","default":128,"required":True,"editable":True,"values":[64,128,256]},
            {"key":"coherent","type":"bool","default":protocol != "axi5","required":True,"editable":False,"values":None},
        ]
        contract["fields"] = [
            {"key":"addressWidth","type":"int","label":"Address Width","description":"Address channel width","default":48,"required":True,"readOnly":False,"minimum":32,"maximum":64,"unit":"bits","values":None,"visibleWhen":None,"enabledWhen":None},
            {"key":"idWidth","type":"int","label":"ID Width","description":"Transaction ID width","default":8,"required":True,"readOnly":False,"minimum":1,"maximum":32,"unit":"bits","values":None,"visibleWhen":None,"enabledWhen":None},
        ]
        add(f"interface-contract-{protocol}", "ipcraft.interface-contract.v1", contract, "accept", "core-semantic", None, None)

    # A rich declaration witness exercises the complete standalone NoC Package V1 surface.
    rich_package = copy.deepcopy(valid["ipcraft.noc-package.v1"])
    rich_package.update({"id": "rich-noc", "name": "Rich Declarative Mesh NoC"})
    rich_package["topology"]["routerTemplate"].update({"stableKey": "mesh-router", "properties": {"pipelineStages": 2}})
    rich_package["topology"]["linkTemplate"].update({"stableKey": "mesh-link", "properties": {"width": 256}})
    rich_package["topology"]["slotTemplates"] = [
        {
            "stableKey": "local-0", "identityCompatibilityVersion": 1, "displayOrder": 0,
            "label": "Local 0", "allowedContracts": [{
                "contractId": "amba.axi5", "version": "1.0", "bundleManifestDigest": DIGEST_A,
                "roles": ["initiator"], "capabilityConstraints": {"dataWidth": 128},
            }], "properties": {"position": 0},
        },
        {
            "stableKey": "local-1", "identityCompatibilityVersion": 1, "displayOrder": 1,
            "label": "Local 1", "allowedContracts": [], "properties": {"position": 1},
        },
    ]
    rich_package["interfaceTemplates"] = [{
        "key": "axi-boundary", "label": "AXI Boundary", "contractId": "amba.axi5",
        "contractVersion": "1.0", "contractBundleManifestDigest": DIGEST_A, "role": "initiator",
        "capabilityDefaults": {"dataWidth": 128}, "contractConfigDefaults": {"addressWidth": 48},
        "nocConfig": {
            "fields": [{
                "key": "interleave", "type": "enum", "label": "Interleave", "description": "Placement policy",
                "default": "none", "required": True, "readOnly": False, "minimum": None, "maximum": None,
                "unit": None, "values": ["none", "stripe"], "visibleWhen": None, "enabledWhen": None,
                "topologyDriving": False,
            }],
            "defaults": {"interleave": "none"},
        },
    }]
    rich_package["domainTypes"][0]["configuration"]["fields"] = [{
        "key": "voltage", "type": "double", "label": "Voltage", "description": "Nominal voltage",
        "default": 0.8, "required": True, "readOnly": False, "minimum": 0.5, "maximum": 1.2,
        "unit": "V", "values": None, "visibleWhen": None, "enabledWhen": None,
        "topologyDriving": False,
    }]
    rich_package["packageEntityTypes"].append({
        "typeKey": "vendor.route-state", "ownership": "engine", "genericEditable": False,
        "topologyDriving": False, "schema": {"type": "object"},
    })
    rich_package["packageRelationTypes"].append({
        "typeKey": "vendor.route-state-link", "ownership": "engine", "topologyDriving": False,
        "sources": {"kinds": ["router"], "minimum": 1, "maximum": 1},
        "targets": {"kinds": ["package-entity"], "minimum": 1, "maximum": 2},
        "unresolvedAllowed": True, "schema": {"type": "object"},
    })
    rich_package["tools"] = {
        "drc": {"id":"tool.drc","version":"1","protocol":"ipcraft.tool-input.v1","runtimeLockId":"dep.runtime","bundleManifestDigest":DIGEST_A,"requestedTimeoutSeconds":30,"environmentProfile":"isolated","networkPolicy":"prohibited","command":["tools/drc.py"]},
        "generate": {"id":"tool.generate","version":"1","protocol":"ipcraft.tool-input.v1","runtimeLockId":"dep.runtime","bundleManifestDigest":DIGEST_B,"requestedTimeoutSeconds":60,"environmentProfile":"isolated","networkPolicy":"prohibited","command":["tools/generate.py"]},
    }
    rich_package["extensions"] = [{"ownerLockId":"dep.noc.rich","schema":"vendor.opaque.v1","version":"1","data":{"mode":"preview"}}]
    add("noc-package-maximum", "ipcraft.noc-package.v1", rich_package, "accept", "core-semantic", None, None)
    provider_package = copy.deepcopy(rich_package)
    provider_package.update({"id":"provider-noc","name":"Provider-authority NoC","structureAuthority":"extension-provider"})
    provider_package["extensionProvider"] = {"protocol":"ipcraft.provider-protocol.v1","providerLockId":"dep.provider","manifestPath":"provider/provider.json"}
    add("noc-package-extension-authority", "ipcraft.noc-package.v1", provider_package, "accept", "core-semantic", None, None)

    # Patch operation/source coverage is executable authoring evidence, not an implied runtime base state.
    patch_schema, patch_schema_path = builder.validator.schemas["ipcraft.patch.v1"]
    core_schema, core_schema_path = builder.validator.schemas["ipcraft.core-canonical-models.v1"]
    def patch_with(source: dict[str, Any], operations: list[dict[str, Any]]) -> dict[str, Any]:
        patch = copy.deepcopy(valid["ipcraft.patch.v1"])
        patch["source"] = source
        patch["operations"] = operations
        if source["kind"] in {"recovery", "undo-redo"}:
            patch["patchId"] = f"trusted.{source['kind']}.1"
        if source["kind"] in {"default-engine", "extension-provider", "application-reconcile", "application-migration"}:
            authority_kind = "extension-provider" if source["kind"] == "extension-provider" else "default-engine"
            patch["applicability"] = frozen_reconcile_applicability(authority_kind)
            if source["kind"] == "application-reconcile":
                patch["transactionId"] = "txn.reconcile.base"
            if source["kind"] == "application-migration":
                patch["transactionId"] = "txn.migration.default-engine"
        else:
            patch.pop("applicability", None)
        return patch

    operation_defs = {
        "createEntity": "createEntityOperation", "updateEntity": "updateEntityOperation",
        "deleteEntity": "deleteEntityOperation", "createRelation": "createRelationOperation",
        "updateRelation": "updateRelationOperation", "deleteRelation": "deleteRelationOperation",
    }
    for operation_name, definition in operation_defs.items():
        operation = builder.build(core_schema["$defs"][definition], core_schema, core_schema_path)
        if "localRef" in operation:
            operation["localRef"] = "application:000001"
        if operation_name == "createEntity":
            operation.update({
                "entityKind": "interface", "localRef": "application:000001",
                "value": {
                    "ownerComponentRef": {"id": "component.noc"}, "templateKey": "axi-boundary",
                    "name": "AXI Boundary", "contract": {"lockId": "dep.contract.axi5", "role": "initiator"},
                    "capabilities": {}, "contractConfig": {}, "nocConfig": {}, "extensions": [],
                },
            })
        if operation_name == "updateEntity":
            operation.update({"entityKind": "project", "id": "project.mesh", "set": {"name": "Renamed"}, "unset": []})
        elif operation_name == "deleteEntity":
            operation.update({"entityKind": "interface", "id": "interface.unattached"})
        elif operation_name == "createRelation":
            operation.update({"relationKind": "package-relation", "localRef": "application:000001", "value": {"typeKey":"vendor.user-relation","sources":[],"targets":[],"data":{"created":True},"extensions":[]}})
        elif operation_name == "updateRelation":
            operation.update({"relationKind": "package-relation", "id": "package-relation.user", "set": {"data":{"label":"updated"}}, "unset": []})
        elif operation_name == "deleteRelation":
            operation.update({"relationKind": "package-relation", "id": "package-relation.user"})
        add(f"patch-operation-{operation_name}", "ipcraft.patch.v1", patch_with({"kind":"user-command","identity":"ipcraft.host","version":"1"}, [operation]), "accept", "core-semantic", None, None)

    sources = {
        "user-command": {"kind":"user-command","identity":"ipcraft.host","version":"1"},
        "application-reconcile": {"kind":"application-reconcile","identity":"ipcraft.host","version":"1"},
        "application-migration": {"kind":"application-migration","identity":"ipcraft.host","version":"1"},
        "default-engine": {"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":DIGEST_A},
        "extension-provider": {"kind":"extension-provider","identity":"vendor.provider","version":"1","bundleDigest":DIGEST_B},
        "recovery": {"kind":"recovery","identity":"ipcraft.host","version":"1"},
        "undo-redo": {"kind":"undo-redo","identity":"ipcraft.host","version":"1"},
    }
    for source_name, source in sources.items():
        source_operations: list[dict[str, Any]] = []
        if source_name == "user-command":
            source_operations = [copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-operation-createEntity.json").read_text())["operations"][0])]
        if source_name == "application-reconcile":
            source_operations.append({
                "op":"updateEntity", "entityKind":"topology", "id":valid["ipcraft.project-design.v1"]["topologies"][0]["id"],
                "set":{"derivation":copy.deepcopy(patch_context_document(valid["ipcraft.project-design.v1"])["applicationReconcileTransactions"][0]["applicationOperations"][0]["set"]["derivation"])}, "unset":[],
            })
        if source_name == "application-migration":
            source_operations = copy.deepcopy(patch_context_document(valid["ipcraft.project-design.v1"])["applicationMigrationTransactions"][0]["applicationOperations"])
        if source_name in {"default-engine", "extension-provider"}:
            source_operations = [{
                "op":"updateEntity", "entityKind":"router", "id":"router.0.0",
                "set":{"properties":{"verified":True}}, "unset":[],
            }]
        if source_name in {"recovery", "undo-redo"}:
            source_operations = [{"op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"name":"Restored"},"unset":[]}]
        add(f"patch-source-{source_name}", "ipcraft.patch.v1", patch_with(source, source_operations), "accept", "core-semantic", None, None)

    context_model = patch_context_document(valid["ipcraft.project-design.v1"])
    transaction_models = {item["transactionId"]:item for item in context_model["applicationReconcileTransactions"]}
    provider_transaction = transaction_models["txn.reconcile.base.provider"]
    provider_application = patch_with(sources["application-reconcile"], copy.deepcopy(provider_transaction["applicationOperations"]))
    provider_application["transactionId"] = provider_transaction["transactionId"]
    provider_application["applicability"] = frozen_reconcile_applicability("extension-provider")
    add("patch-source-application-reconcile-provider-authority", "ipcraft.patch.v1", provider_application, "accept", "core-semantic", None, None)
    for fixture_name, transaction_id in (
        ("patch-application-router-create-default-membership", "txn.reconcile.router-create"),
        ("patch-application-router-delete-membership", "txn.reconcile.router-delete"),
        ("patch-application-delete-emptied-nondefault-domain", "txn.reconcile.router-delete"),
    ):
        transaction = transaction_models[transaction_id]
        side_effect = patch_with(sources["application-reconcile"], copy.deepcopy(transaction["applicationOperations"]))
        side_effect["transactionId"] = transaction_id
        add(fixture_name, "ipcraft.patch.v1", side_effect, "accept", "core-semantic", None, None)

    precondition_patch = patch_with(sources["user-command"], [])
    precondition_patch["preconditions"] = [
        {"kind":"entity-absent","entityKind":"interface","id":"interface.new"},
        {"kind":"entity-exists","entityKind":"component","id":"component.noc"},
        {"kind":"property-equals","entityKind":"project","id":"project.mesh","property":"name","value":"Mesh"},
        {"kind":"relation-exists","relationKind":"domain-membership","id":"membership.0"},
        {"kind":"slot-unoccupied","slotId":"slot.free"},
    ]
    precondition_patch["preconditions"].sort(key=lambda item: json.dumps(item, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
    add("patch-preconditions-all", "ipcraft.patch.v1", precondition_patch, "accept", "core-semantic", None, None)
    for precondition in precondition_patch["preconditions"]:
        witness = patch_with(sources["user-command"], [copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-operation-updateEntity.json").read_text())["operations"][0])])
        witness["preconditions"] = [copy.deepcopy(precondition)]
        add(f"patch-precondition-{precondition['kind']}", "ipcraft.patch.v1", witness, "accept", "core-semantic", None, None)
    package_ownership_witnesses = {
        "user-entity": patch_with(sources["user-command"], [{"op":"createEntity","entityKind":"package-entity","localRef":"application:000010","value":{"typeKey":"vendor.user-entity","data":{},"extensions":[]}}]),
        "engine-entity": patch_with(sources["default-engine"], [{"op":"createEntity","entityKind":"package-entity","localRef":"authority:engine-entity","value":{"typeKey":"vendor.engine-entity","data":{},"extensions":[]}}]),
        "user-relation": patch_with(sources["user-command"], [{"op":"createRelation","relationKind":"package-relation","localRef":"application:000011","value":{"typeKey":"vendor.user-relation","sources":[],"targets":[],"data":{},"extensions":[]}}]),
        "engine-relation": patch_with(sources["default-engine"], [{"op":"createRelation","relationKind":"package-relation","localRef":"authority:engine-relation","value":{"typeKey":"vendor.engine-relation","sources":[],"targets":[],"data":{},"extensions":[]}}]),
    }
    for name, witness in package_ownership_witnesses.items():
        add(f"patch-package-ownership-{name}", "ipcraft.patch.v1", witness, "accept", "core-semantic", None, None)
    relation_transaction = transaction_models["txn.reconcile.relation-target-delete"]
    application_relation_side_effect = patch_with(sources["application-reconcile"], copy.deepcopy(relation_transaction["applicationOperations"]))
    application_relation_side_effect["transactionId"] = relation_transaction["transactionId"]
    add("patch-application-package-relation-unresolved", "ipcraft.patch.v1", application_relation_side_effect, "accept", "core-semantic", None, None)
    attachment_transaction = transaction_models["txn.reconcile.slot-delete"]
    application_attachment_side_effect = patch_with(sources["application-reconcile"], copy.deepcopy(attachment_transaction["applicationOperations"]))
    application_attachment_side_effect["transactionId"] = attachment_transaction["transactionId"]
    add("patch-application-attachment-unresolved", "ipcraft.patch.v1", application_attachment_side_effect, "accept", "core-semantic", None, None)

    for definition in (
        "normalizedTopologyInput", "derivedState", "reconcileApplicability", "patchBody",
        "impactReport", "candidateTransaction", "pipelinePlan", "outputManifest",
    ):
        model = builder.build(core_schema["$defs"][definition], core_schema, core_schema_path)
        add(f"core-{definition}", "ipcraft.core-canonical-models.v1", model, "accept", "core-semantic", None, None)

    for disposition, pending_id, draft_id in (
        ("pending-topology", "group.1", None),
        ("drafted", None, "draft.1"),
        ("rejected", None, None),
    ):
        command = copy.deepcopy(valid["ipcraft.command-result.v1"])
        command.update({"disposition": disposition, "pendingGroupId": pending_id, "draftId": draft_id})
        if disposition == "rejected":
            command["diagnostics"] = [{"code":"command.rejected","path":"/command","data":{"reason":"invalid"}}]
        add(f"command-result-{disposition}", "ipcraft.command-result.v1", command, "accept", "core-semantic", None, None)

    pipeline_variants: dict[str, dict[str, Any]] = {}
    for status in ("failed", "cancelled", "timed-out"):
        pipeline = copy.deepcopy(valid["ipcraft.pipeline-result.v1"])
        pipeline["status"] = status
        pipeline["failedStepId"] = pipeline["steps"][0]["stepId"] if status == "failed" else None
        pipeline_variants[status] = pipeline
        add(f"pipeline-result-{status}", "ipcraft.pipeline-result.v1", pipeline, "accept", "core-semantic", None, None)

    external_step = copy.deepcopy(valid["ipcraft.step-result.v1"])
    external_step.update({"stepKind":"external-tool","invocationId":"invocation.1","toolLockId":"dep.tool","toolResult":"invocations/invocation.1/tool-result.json"})
    add("step-result-external-success", "ipcraft.step-result.v1", external_step, "accept", "core-semantic", None, None)
    for status in ("failed", "cancelled", "timed-out", "skipped"):
        step = copy.deepcopy(valid["ipcraft.step-result.v1"])
        step.update({"status":status,"failure":{"code":f"step.{status}","source":"host"}})
        add(f"step-result-{status}", "ipcraft.step-result.v1", step, "accept", "core-semantic", None, None)

    generator_input = copy.deepcopy(valid["ipcraft.tool-input.v1"])
    generator_input.update({
        "kind":"generator", "formallySavedProjectDigest":generator_input["snapshotDigest"],
        "outputDirectory":"artifacts",
    })
    add("tool-input-generator", "ipcraft.tool-input.v1", generator_input, "accept", "core-semantic", None, None)

    failed_tool_result = copy.deepcopy(valid["ipcraft.tool-result.v1"])
    failed_tool_result.update({"status":"failed","failure":{"code":"tool.failed","data":{"reason":"tool failed"}}})
    add("tool-result-failed", "ipcraft.tool-result.v1", failed_tool_result, "accept", "core-semantic", None, None)

    artifact_max = copy.deepcopy(valid["ipcraft.artifact-manifest.v1"])
    artifact_max["artifacts"] = [
        {"path":"reports/design.json","kind":"report","mediaType":"application/json","size":42,"digest":DIGEST_A},
        {"path":"rtl/noc.sv","kind":"rtl","mediaType":"text/x-systemverilog","size":128,"digest":DIGEST_B},
    ]
    add("artifact-manifest-maximum", "ipcraft.artifact-manifest.v1", artifact_max, "accept", "core-semantic", None, None)
    bundle_max = copy.deepcopy(valid["ipcraft.bundle-manifest.v1"])
    bundle_max["files"] = [
        {"path":"bin/engine","size":128,"digest":DIGEST_A,"executable":True},
        {"path":"share/schema.json","size":64,"digest":DIGEST_B,"executable":False},
    ]
    bundle_max["manifestDigest"] = verifier.bundle_manifest_digest(contracts, bundle_max)
    add("bundle-manifest-maximum", "ipcraft.bundle-manifest.v1", bundle_max, "accept", "core-semantic", None, None)
    diagnostic_max = copy.deepcopy(valid["ipcraft.diagnostic-report.v1"])
    diagnostic_max["diagnostics"] = [
        {"ruleId":"rule.info","severity":"info","message":"info","blocking":False,"subjects":[],"properties":[]},
        {"ruleId":"rule.warning","severity":"warning","message":"warning","blocking":False,"subjects":[{"kind":"router","id":"router.0.0"}],"properties":["coordinate"]},
        {"ruleId":"rule.error","severity":"error","message":"error","blocking":True,"subjects":[{"kind":"domain","id":"domain.power"}],"properties":["membership"]},
    ]
    add("diagnostic-report-maximum", "ipcraft.diagnostic-report.v1", diagnostic_max, "accept", "core-semantic", None, None)
    engine_max = copy.deepcopy(valid["ipcraft.engine-bundle.v1"])
    engine_max.update({"migrationFromCompatibilityVersions":["0.8","0.9"],"supportedPlatformAbis":["linux-aarch64-gnu-v1","linux-x86_64-gnu-v1"]})
    add("engine-bundle-maximum", "ipcraft.engine-bundle.v1", engine_max, "accept", "core-semantic", None, None)

    interface_max = copy.deepcopy(valid["ipcraft.interface-contract.v1"])
    interface_max.update({"id":"contract.maximum","name":"Maximum Contract","version":"1"})
    interface_max["roles"] = [{"key":"initiator","label":"Initiator"},{"key":"target","label":"Target"}]
    interface_max["capabilities"] = [
        {"key":"aBool","type":"bool","default":True,"required":True,"editable":False,"values":None},
        {"key":"bDouble","type":"double","default":1.5,"required":True,"editable":True,"values":[1.0,1.5]},
        {"key":"cEnum","type":"enum","default":"a","required":True,"editable":True,"values":["a","b"]},
        {"key":"dInt","type":"int","default":8,"required":True,"editable":True,"values":[4,8]},
        {"key":"eString","type":"string","default":"value","required":False,"editable":True,"values":None},
    ]
    def contract_field(key: str, kind: str, default: Any, values: Any = None, condition: Any = None) -> dict[str, Any]:
        return {"key":key,"type":kind,"label":key,"description":"maximum field","default":default,"required":True,"readOnly":False,"minimum":0 if kind in {"int","double"} else None,"maximum":64 if kind in {"int","double"} else None,"unit":"bits" if kind in {"int","double"} else None,"values":values,"visibleWhen":condition,"enabledWhen":None}
    interface_max["fields"] = [
        contract_field("aBool","bool",True), contract_field("bDouble","double",1.5),
        contract_field("cEnum","enum","a",["a","b"],{"field":"aBool","equals":True}),
        contract_field("dInt","int",8), contract_field("eString","string","value"),
    ]
    add("interface-contract-maximum", "ipcraft.interface-contract.v1", interface_max, "accept", "core-semantic", None, None)

    side_effect_vectors = json.loads((contracts / "vectors/host-side-effects-v1.json").read_text())["cases"]
    by_group_state: dict[str, dict[str, Any]] = {}
    for case in side_effect_vectors:
        by_group_state.setdefault(case["document"]["expected"]["groupState"], case["document"])
    for state in ("auto-commit", "ready-to-commit", "blocked"):
        add(f"noc-side-effects-{state}", "ipcraft.noc-side-effects.v1", copy.deepcopy(by_group_state[state]), "accept", "core-semantic", None, None)

    pipeline_plan_max = copy.deepcopy(valid["ipcraft.pipeline-plan.v1"])
    pipeline_plan_max.update({"kind":"generate","formallySavedProjectDigest":pipeline_plan_max["snapshotDigest"]})
    pipeline_plan_max["steps"] = [
        {"stepId":"structural-drc","kind":"host"},
        {"stepId":"semantic-drc","kind":"external-tool","toolLockId":"dep.tool.drc"},
        {"stepId":"generator","kind":"external-tool","toolLockId":"dep.tool.generate"},
        {"stepId":"artifact-verify","kind":"host"},
        {"stepId":"promotion","kind":"host"},
    ]
    add("pipeline-plan-maximum", "ipcraft.pipeline-plan.v1", pipeline_plan_max, "accept", "core-semantic", None, None)

    recovery_schema, recovery_schema_path = builder.validator.schemas["ipcraft.recovery.v1"]
    recovery_max = copy.deepcopy(valid["ipcraft.recovery.v1"])
    recovery_max["pendingTopologyGroup"] = builder.build(recovery_schema["$defs"]["pendingTopologyGroup"], recovery_schema, recovery_schema_path)
    draft_entry = builder.build(recovery_schema["$defs"]["draftEntry"], recovery_schema, recovery_schema_path)
    recovery_max["draftOverlay"] = [draft_entry]
    recovery_max["draftUndo"] = [{"before":[],"after":[copy.deepcopy(draft_entry)]}]
    recovery_max["draftRedo"] = [{"before":[copy.deepcopy(draft_entry)],"after":[]}]
    add("recovery-maximum", "ipcraft.recovery.v1", recovery_max, "accept", "core-semantic", None, None)

    tool_input_max = copy.deepcopy(generator_input)
    tool_input_max["dependencies"] = copy.deepcopy(valid["ipcraft.project-design.v1"]["dependencies"])
    add("tool-input-maximum", "ipcraft.tool-input.v1", tool_input_max, "accept", "core-semantic", None, None)
    tool_result_max = copy.deepcopy(valid["ipcraft.tool-result.v1"])
    tool_result_max.update({"diagnosticReport":"reports/diagnostics.json","artifactManifest":"reports/artifact-manifest.json","metrics":{"elapsedMs":10,"artifacts":2}})
    add("tool-result-maximum", "ipcraft.tool-result.v1", tool_result_max, "accept", "core-semantic", None, None)

    # A schema-phase witness for every standalone root.
    for schema_id in schema_ids:
        document = copy.deepcopy(valid[schema_id])
        if isinstance(document, dict): document.pop("schema", None)
        boundary, code = verifier.SCHEMA_BOUNDARY.get(schema_id, ("generic-structure", "contract.schema_invalid"))
        add("schema-missing-" + schema_id.removeprefix("ipcraft.").removesuffix(".v1").replace(".", "-"),
            schema_id, document, "reject", "schema", boundary, code)

    command_pending = copy.deepcopy(valid["ipcraft.command-result.v1"]); command_pending["disposition"] = "pending-topology"
    add("command-pending-without-group", "ipcraft.command-result.v1", command_pending, "reject", "schema", "command-result", "command.result_invalid")
    diagnostic_blocking = copy.deepcopy(valid["ipcraft.diagnostic-report.v1"]); diagnostic_blocking["diagnostics"] = [{"ruleId":"rule.blocking","severity":"warning","message":"bad severity","blocking":True,"subjects":[],"properties":[]}]
    add("diagnostic-blocking-warning", "ipcraft.diagnostic-report.v1", diagnostic_blocking, "reject", "schema", "diagnostic-report", "diagnostic.report_invalid")
    pipeline_failed = copy.deepcopy(valid["ipcraft.pipeline-result.v1"]); pipeline_failed.update({"status":"failed","failedStepId":None})
    add("pipeline-failed-without-step", "ipcraft.pipeline-result.v1", pipeline_failed, "reject", "schema", "pipeline-result", "pipeline.result_invalid")
    step_external = copy.deepcopy(valid["ipcraft.step-result.v1"]); step_external["stepKind"] = "external-tool"; step_external["toolResult"] = "tool-result.json"
    add("step-external-without-invocation", "ipcraft.step-result.v1", step_external, "reject", "schema", "generic-structure", "contract.schema_invalid")
    tool_failed = copy.deepcopy(valid["ipcraft.tool-result.v1"]); tool_failed["status"] = "failed"
    add("tool-result-failed-without-failure", "ipcraft.tool-result.v1", tool_failed, "reject", "schema", "tool-result", "tool.result_invalid")
    generator_no_output = copy.deepcopy(valid["ipcraft.tool-input.v1"]); generator_no_output.update({"kind":"generator","formallySavedProjectDigest":generator_no_output["snapshotDigest"]})
    add("tool-input-generator-without-output", "ipcraft.tool-input.v1", generator_no_output, "reject", "schema", "tool-input", "tool.input_invalid")
    authority_no_applicability = copy.deepcopy(valid["ipcraft.patch.v1"]); authority_no_applicability["source"] = {"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1","bundleDigest":DIGEST_A}
    add("patch-authority-without-applicability", "ipcraft.patch.v1", authority_no_applicability, "reject", "schema", "patch-envelope", "patch.schema_invalid")

    package_schema, package_path = builder.validator.schemas["ipcraft.noc-package.v1"]
    relation_type = builder.build(package_schema["$defs"]["relationType"], package_schema, package_path)
    relation_type.update({"ownership":"engine","topologyDriving":True})
    package_relation = copy.deepcopy(valid["ipcraft.noc-package.v1"]); package_relation["packageRelationTypes"] = [relation_type]
    add("package-engine-relation-topology-driving", "ipcraft.noc-package.v1", package_relation, "reject", "schema", "generic-structure", "contract.schema_invalid")

    # ProjectDesign representative and semantic boundaries.
    project = mesh_project(valid["ipcraft.project-design.v1"], 1, 1, False)
    add("minimal-1x1", "ipcraft.project-design.v1", project, "accept", "core-semantic", None, None)
    project_2x2 = mesh_project(valid["ipcraft.project-design.v1"], 2, 2, True)
    add("mesh-2x2-attached", "ipcraft.project-design.v1", project_2x2, "accept", "core-semantic", None, None)
    add("mesh-2x2-package-extension", "ipcraft.project-design.v1", package_extension_project(valid["ipcraft.project-design.v1"]), "accept", "core-semantic", None, None)
    project_maximum = copy.deepcopy(project_2x2)
    extension_shape = package_extension_project(valid["ipcraft.project-design.v1"])
    project_maximum["topologies"][0]["packageEntities"] = copy.deepcopy(extension_shape["topologies"][0]["packageEntities"])
    project_maximum["topologies"][0]["packageRelations"] = copy.deepcopy(extension_shape["topologies"][0]["packageRelations"])
    project_maximum["extensions"] = copy.deepcopy(extension_shape["extensions"])
    project_maximum["id"] = "project.maximum"
    project_maximum["name"] = "Maximum Project Shape"
    runtime_lock = {
        "lockId":"dep.runtime.python","kind":"runtime","id":"python-runtime","version":"3.12.4","bundleManifestDigest":DIGEST_A,
        "runtimeClosure":{"closureKind":"host-managed","runtimeId":"python","runtimeVersion":"3.12.4","runtimeDistributionBundleDigest":DIGEST_A,"entrypoint":"bin/python3","platformAbi":"linux-x86_64-gnu-v1","invocationProfile":"ipcraft.python-isolated.v1","moduleSearchPolicy":"runtime-and-tool-bundles-only","environmentProfile":"ipcraft.empty-utf8-utc.v1","networkPolicy":"prohibited"},
    }
    project_maximum["dependencies"].extend([
        runtime_lock,
        {"lockId":"dep.provider","kind":"extension-provider","id":"vendor.provider","version":"1","bundleManifestDigest":DIGEST_A,"protocolVersion":"ipcraft.provider-protocol.v1","runtimeLockId":"dep.runtime.python"},
        {"lockId":"dep.tool.drc","kind":"drc-tool","id":"vendor.drc","version":"1","bundleManifestDigest":DIGEST_A,"toolProtocolVersion":"ipcraft.tool-input.v1","runtimeLockId":"dep.runtime.python"},
        {"lockId":"dep.tool.generator","kind":"generator-tool","id":"vendor.generator","version":"1","bundleManifestDigest":DIGEST_B,"toolProtocolVersion":"ipcraft.tool-input.v1","runtimeLockId":"dep.runtime.python"},
    ])
    add("project-design-maximum", "ipcraft.project-design.v1", project_maximum, "accept", "core-semantic", None, None)
    provider_authority_project = copy.deepcopy(project_maximum)
    provider_authority_project.update({"id":"project.provider-authority","name":"Provider Authority Project"})
    provider_dependency = next(item for item in provider_authority_project["dependencies"] if item["kind"] == "extension-provider")
    provider_dependency["bundleManifestDigest"] = DIGEST_B
    provider_authority_project["topologies"][0]["derivation"]["structureAuthority"] = {
        "kind":"extension-provider", "lockId":provider_dependency["lockId"], "identity":provider_dependency["id"],
        "version":provider_dependency["version"], "bundleDigest":provider_dependency["bundleManifestDigest"],
    }
    add("project-provider-authority", "ipcraft.project-design.v1", provider_authority_project, "accept", "core-semantic", None, None)
    degraded = copy.deepcopy(project); degraded["id"] = "project.degraded"; degraded["name"] = "Exact Engine Missing"
    add("project-exact-engine-unavailable", "ipcraft.project-design.v1", degraded, "accept", "core-semantic", None, None,
        "vectors/default-engine-lock-v1.json#engine-lock-missing")
    duplicate = copy.deepcopy(project); duplicate["topologies"][0]["id"] = duplicate["components"][0]["id"]
    add("project-duplicate-id", "ipcraft.project-design.v1", duplicate, "reject", "core-semantic", "project-duplicate-id", "project.duplicate_id")
    wrong_schema = copy.deepcopy(project); wrong_schema["schema"] = "ipcraft.project-design.v0"
    add("project-wrong-schema-id", "ipcraft.project-design.v1", wrong_schema, "reject", "schema", "generic-structure", "contract.schema_invalid")
    uncovered = copy.deepcopy(project_2x2); uncovered["topologies"][0]["domainMemberships"].pop()
    add("project-missing-domain-membership", "ipcraft.project-design.v1", uncovered, "reject", "core-semantic", "project-invariant", "project.invariant_violation")
    disconnected = copy.deepcopy(project_2x2)
    disconnected["topologies"][0]["structuralLinks"] = [link for link in disconnected["topologies"][0]["structuralLinks"] if link["axis"] == "horizontal"]
    add("project-disconnected-domain", "ipcraft.project-design.v1", disconnected, "reject", "core-semantic", "project-invariant", "project.invariant_violation")
    legacy = {"project": {"id": "legacy"}, "instances": []}
    add("project-legacy-root", "ipcraft.project-design.v1", legacy, "reject", "schema", "legacy-project-root", "project.legacy_format_unsupported")

    project_schema_cases: list[tuple[str, dict[str, Any]]] = []
    missing_profile = copy.deepcopy(project); missing_profile.pop("profile")
    project_schema_cases.append(("project-missing-noc-profile", missing_profile))
    zero_components = copy.deepcopy(project); zero_components["components"] = []
    project_schema_cases.append(("project-zero-components", zero_components))
    two_components = copy.deepcopy(project); two_components["components"].append({**copy.deepcopy(two_components["components"][0]), "id":"component.second"})
    project_schema_cases.append(("project-two-components", two_components))
    connections = copy.deepcopy(project); connections["connections"] = [{"id":"connection.forbidden"}]
    project_schema_cases.append(("project-nonempty-connections", connections))
    wrong_type = copy.deepcopy(project); wrong_type["topologies"][0]["routers"][0]["coordinate"]["row"] = "zero"
    project_schema_cases.append(("project-core-field-wrong-type", wrong_type))
    opaque = copy.deepcopy(project); opaque["vendorOpaque"] = {"mode":"forbidden"}
    project_schema_cases.append(("project-opaque-outside-extension", opaque))
    runtime = copy.deepcopy(project); runtime["dependencies"].append({"lockId":"dep.runtime.python","kind":"runtime","id":"python-runtime","version":"3.12.4","bundleManifestDigest":DIGEST_A,"runtimeClosure":{"closureKind":"host-managed"}})
    project_schema_cases.append(("project-runtime-lock-incomplete", runtime))
    for name, document in project_schema_cases:
        add(name, "ipcraft.project-design.v1", document, "reject", "schema", "generic-structure", "contract.schema_invalid")

    project_semantic_cases: list[tuple[str, dict[str, Any], str]] = []
    slot_missing_router = copy.deepcopy(project_2x2); slot_missing_router["topologies"][0]["accessSlots"][1]["routerId"] = "router.missing"
    project_semantic_cases.append(("project-slot-missing-router", slot_missing_router, "project-reference"))
    attachment_missing_slot = copy.deepcopy(project_2x2); attachment_missing_slot["topologies"][0]["attachments"][0]["slotId"] = "slot.missing"
    project_semantic_cases.append(("project-attachment-missing-slot", attachment_missing_slot, "project-reference"))
    occupied = copy.deepcopy(project_2x2)
    occupied["interfaces"].append({**copy.deepcopy(occupied["interfaces"][0]), "id":"interface.second"})
    occupied["topologies"][0]["attachments"].append({**copy.deepcopy(occupied["topologies"][0]["attachments"][0]), "id":"attachment.second", "interfaceId":"interface.second"})
    project_semantic_cases.append(("project-attachment-occupied-slot", occupied, "project-invariant"))
    contract_mismatch = copy.deepcopy(project_2x2)
    contract_mismatch["dependencies"].append({"lockId":"dep.contract.chi","kind":"interface-contract","id":"amba.chi","version":"1.0.0","bundleManifestDigest":DIGEST_A})
    contract_mismatch["interfaces"][0]["contract"]["lockId"] = "dep.contract.chi"
    project_semantic_cases.append(("project-attachment-contract-mismatch", contract_mismatch, "project-invariant"))
    role_mismatch = copy.deepcopy(project_2x2); role_mismatch["interfaces"][0]["contract"]["role"] = "target"
    project_semantic_cases.append(("project-attachment-role-mismatch", role_mismatch, "project-invariant"))
    capability_mismatch = copy.deepcopy(project_2x2); capability_mismatch["interfaces"][0]["capabilities"]["dataWidth"] = 64
    project_semantic_cases.append(("project-attachment-capability-mismatch", capability_mismatch, "project-invariant"))
    duplicate_coordinate = copy.deepcopy(project_2x2); duplicate_coordinate["topologies"][0]["routers"][1]["coordinate"] = copy.deepcopy(duplicate_coordinate["topologies"][0]["routers"][0]["coordinate"])
    project_semantic_cases.append(("project-duplicate-router-coordinate", duplicate_coordinate, "project-invariant"))
    duplicate_slot_key = copy.deepcopy(project_2x2); duplicate_slot_key["topologies"][0]["accessSlots"][1].update({"routerId":"router.0.0","templateKey":"local-0"})
    project_semantic_cases.append(("project-duplicate-slot-key", duplicate_slot_key, "project-invariant"))
    router_slot_mismatch = copy.deepcopy(project_2x2); router_slot_mismatch["topologies"][0]["attachments"][0]["routerId"] = "router.0.1"
    project_semantic_cases.append(("project-attachment-router-slot-mismatch", router_slot_mismatch, "project-invariant"))
    multiple_attachments = copy.deepcopy(project_2x2); multiple_attachments["topologies"][0]["accessSlots"][1]["allowedContracts"] = copy.deepcopy(multiple_attachments["topologies"][0]["accessSlots"][0]["allowedContracts"]); multiple_attachments["topologies"][0]["attachments"].append({"id":"attachment.second","interfaceId":"interface.axi","state":"resolved","routerId":"router.0.1","slotId":"slot.0.1"})
    project_semantic_cases.append(("project-interface-multiple-attachments", multiple_attachments, "project-invariant"))
    two_defaults = copy.deepcopy(project_2x2); two_defaults["topologies"][0]["domains"].append({**copy.deepcopy(two_defaults["topologies"][0]["domains"][0]), "id":"domain.power.second"})
    project_semantic_cases.append(("project-two-default-domains", two_defaults, "project-invariant"))
    no_default = copy.deepcopy(project_2x2); no_default["topologies"][0]["domains"][0]["isDefault"] = False
    project_semantic_cases.append(("project-no-default-domain", no_default, "project-invariant"))
    missing_owner = copy.deepcopy(project_2x2); missing_owner["topologies"][0]["ownerComponentId"] = "component.missing"
    project_semantic_cases.append(("project-topology-owner-missing", missing_owner, "project-reference"))
    missing_package = copy.deepcopy(project_2x2); missing_package["components"][0]["packageLockId"] = "dep.noc.missing"
    project_semantic_cases.append(("project-component-package-missing", missing_package, "project-reference"))
    missing_slot_contract = copy.deepcopy(project_2x2); missing_slot_contract["topologies"][0]["accessSlots"][0]["allowedContracts"][0]["contractLockId"] = "dep.contract.missing"
    project_semantic_cases.append(("project-slot-contract-lock-missing", missing_slot_contract, "project-reference"))
    missing_extension_owner = package_extension_project(valid["ipcraft.project-design.v1"]); missing_extension_owner["extensions"][0]["ownerLockId"] = "dep.missing"
    project_semantic_cases.append(("project-extension-owner-missing", missing_extension_owner, "project-reference"))
    duplicate_root_extension = package_extension_project(valid["ipcraft.project-design.v1"])
    duplicate_root_extension["extensions"].append(copy.deepcopy(duplicate_root_extension["extensions"][0]))
    project_semantic_cases.append(("project-duplicate-root-extension-key", duplicate_root_extension, "project-invariant"))
    duplicate_relation_source = package_extension_project(valid["ipcraft.project-design.v1"])
    duplicate_relation_source["topologies"][0]["packageRelations"][0]["sources"].append(
        copy.deepcopy(duplicate_relation_source["topologies"][0]["packageRelations"][0]["sources"][0])
    )
    project_semantic_cases.append(("project-duplicate-relation-source-key", duplicate_relation_source, "project-invariant"))
    duplicate_allowed_contract = copy.deepcopy(project_2x2)
    duplicate_allowed_contract["topologies"][0]["accessSlots"][0]["allowedContracts"].append(
        copy.deepcopy(duplicate_allowed_contract["topologies"][0]["accessSlots"][0]["allowedContracts"][0])
    )
    project_semantic_cases.append(("project-duplicate-slot-allowed-contract-key", duplicate_allowed_contract, "project-invariant"))
    duplicate_slot_role = copy.deepcopy(project_2x2)
    duplicate_slot_role["topologies"][0]["accessSlots"][0]["allowedContracts"][0]["roles"].append("initiator")
    project_semantic_cases.append(("project-duplicate-slot-role-key", duplicate_slot_role, "project-invariant"))
    membership_wrong_kind = copy.deepcopy(project_2x2)
    membership_wrong_kind["topologies"][0]["domainMemberships"][0]["domainId"] = "interface.axi"
    project_semantic_cases.append(("project-domain-membership-wrong-kind", membership_wrong_kind, "project-reference"))
    derivation_mismatch = copy.deepcopy(project_2x2); derivation_mismatch["topologies"][0]["derivation"]["defaultEngineBundleDigest"] = DIGEST_B
    project_semantic_cases.append(("project-derivation-engine-lock-mismatch", derivation_mismatch, "project-invariant"))
    link_missing = copy.deepcopy(project_2x2); link_missing["topologies"][0]["structuralLinks"][0]["endpointB"] = "router.missing"
    project_semantic_cases.append(("project-link-missing-endpoint", link_missing, "project-reference"))
    link_self = copy.deepcopy(project_2x2); link_self["topologies"][0]["structuralLinks"][0]["endpointB"] = link_self["topologies"][0]["structuralLinks"][0]["endpointA"]
    project_semantic_cases.append(("project-link-self", link_self, "project-invariant"))
    link_nonadjacent = copy.deepcopy(project_2x2); link_nonadjacent["topologies"][0]["structuralLinks"][0].update({"endpointA":"router.0.0","endpointB":"router.1.1"})
    project_semantic_cases.append(("project-link-nonadjacent", link_nonadjacent, "project-invariant"))
    link_axis = copy.deepcopy(project_2x2); link_axis["topologies"][0]["structuralLinks"][0]["axis"] = "vertical"
    project_semantic_cases.append(("project-link-axis-mismatch", link_axis, "project-invariant"))
    link_duplicate = copy.deepcopy(project_2x2); link_duplicate["topologies"][0]["structuralLinks"].append({**copy.deepcopy(link_duplicate["topologies"][0]["structuralLinks"][0]), "id":"link.duplicate"})
    project_semantic_cases.append(("project-link-duplicate-pair", link_duplicate, "project-invariant"))
    provider_missing = copy.deepcopy(provider_authority_project); provider_missing["topologies"][0]["derivation"]["structureAuthority"]["lockId"] = "dep.missing"
    project_semantic_cases.append(("project-provider-authority-missing-lock", provider_missing, "project-reference"))
    provider_wrong_kind = copy.deepcopy(provider_authority_project); provider_wrong_kind["topologies"][0]["derivation"]["structureAuthority"]["lockId"] = "dep.engine.default"
    project_semantic_cases.append(("project-provider-authority-wrong-lock-kind", provider_wrong_kind, "project-reference"))
    provider_mismatch = copy.deepcopy(provider_authority_project); provider_mismatch["topologies"][0]["derivation"]["structureAuthority"]["identity"] = "wrong.provider"
    project_semantic_cases.append(("project-provider-authority-metadata-mismatch", provider_mismatch, "project-invariant"))
    for name, document, boundary in project_semantic_cases:
        code = "project.unknown_reference" if boundary == "project-reference" else "project.invariant_violation"
        add(name, "ipcraft.project-design.v1", document, "reject", "core-semantic", boundary, code)

    # Generic semantic mutations with document-owned evidence.
    semantic_cases: list[tuple[str, str, Any, str, str]] = []
    bundle = copy.deepcopy(valid["ipcraft.bundle-manifest.v1"]); bundle["files"] = [{"path":"a","size":0,"digest":DIGEST_A,"executable":False}]*2
    semantic_cases.append(("bundle-duplicate-path", "ipcraft.bundle-manifest.v1", bundle, "bundle-manifest", "dependency.manifest_invalid"))
    bundle_digest = copy.deepcopy(valid["ipcraft.bundle-manifest.v1"]); bundle_digest["manifestDigest"] = DIGEST_B
    semantic_cases.append(("bundle-manifest-digest", "ipcraft.bundle-manifest.v1", bundle_digest, "bundle-manifest", "dependency.manifest_invalid"))
    bundle_reserved = copy.deepcopy(valid["ipcraft.bundle-manifest.v1"]); bundle_reserved["files"] = [{"path":"CON.txt","size":0,"digest":DIGEST_A,"executable":False}]
    semantic_cases.append(("bundle-reserved-path", "ipcraft.bundle-manifest.v1", bundle_reserved, "bundle-manifest", "dependency.manifest_invalid"))
    artifact = copy.deepcopy(valid["ipcraft.artifact-manifest.v1"]); artifact["artifacts"] = [{"path":"a","kind":"rtl","mediaType":"text/plain","size":0,"digest":DIGEST_A}]*2
    semantic_cases.append(("artifact-duplicate-path", "ipcraft.artifact-manifest.v1", artifact, "tool-artifact", "tool.artifact_invalid"))
    artifact_fold = copy.deepcopy(valid["ipcraft.artifact-manifest.v1"]); artifact_fold["artifacts"] = [
        {"path":"A.txt","kind":"rtl","mediaType":"text/plain","size":0,"digest":DIGEST_A},
        {"path":"a.txt","kind":"rtl","mediaType":"text/plain","size":0,"digest":DIGEST_B},
    ]
    semantic_cases.append(("artifact-fold-collision", "ipcraft.artifact-manifest.v1", artifact_fold, "tool-artifact", "tool.artifact_invalid"))
    contract = copy.deepcopy(valid["ipcraft.interface-contract.v1"]); contract["roles"] = [{"key":"initiator","label":"Initiator"}]*2
    semantic_cases.append(("contract-duplicate-role", "ipcraft.interface-contract.v1", contract, "contract-declaration", "contract.invariant_violation"))
    rich_contract = json.loads((contracts / "fixtures/valid/interface-contract-axi5.json").read_text())
    capability_type = copy.deepcopy(rich_contract); capability_type["capabilities"][0]["default"] = "128"
    semantic_cases.append(("contract-capability-default-type", "ipcraft.interface-contract.v1", capability_type, "contract-declaration", "contract.invariant_violation"))
    field_range = copy.deepcopy(rich_contract); field_range["fields"][0].update({"minimum":64,"maximum":32})
    semantic_cases.append(("contract-field-range", "ipcraft.interface-contract.v1", field_range, "contract-declaration", "contract.invariant_violation"))
    condition_reference = copy.deepcopy(rich_contract); condition_reference["fields"][0]["visibleWhen"] = {"field":"missingField","equals":True}
    semantic_cases.append(("contract-condition-reference", "ipcraft.interface-contract.v1", condition_reference, "contract-declaration", "contract.invariant_violation"))
    package = copy.deepcopy(valid["ipcraft.noc-package.v1"]); package["domainTypes"] = [copy.deepcopy(package["domainTypes"][0])]*2 if package["domainTypes"] else []
    domain_type = builder.build(package_schema["$defs"]["domainType"], package_schema, package_path)
    package["domainTypes"] = [domain_type, copy.deepcopy(domain_type)]
    semantic_cases.append(("package-duplicate-domain-type", "ipcraft.noc-package.v1", package, "package-declaration", "package.invariant_violation"))
    package_duplicate_global = copy.deepcopy(valid["ipcraft.noc-package.v1"])
    package_duplicate_global["configuration"]["global"]["fields"].append(copy.deepcopy(package_duplicate_global["configuration"]["global"]["fields"][0]))
    semantic_cases.append(("package-duplicate-global-field", "ipcraft.noc-package.v1", package_duplicate_global, "package-declaration", "package.invariant_violation"))
    package_field_type = copy.deepcopy(valid["ipcraft.noc-package.v1"])
    package_field_type["configuration"]["global"]["fields"][0]["default"] = "wrong"
    semantic_cases.append(("package-field-default-type", "ipcraft.noc-package.v1", package_field_type, "package-declaration", "package.invariant_violation"))
    package_condition = copy.deepcopy(valid["ipcraft.noc-package.v1"])
    package_condition["configuration"]["global"]["fields"][0]["visibleWhen"] = {"field":"missing","equals":1}
    semantic_cases.append(("package-field-condition-reference", "ipcraft.noc-package.v1", package_condition, "package-declaration", "package.invariant_violation"))
    package_cardinality = copy.deepcopy(valid["ipcraft.noc-package.v1"])
    package_cardinality["packageRelationTypes"][0]["sources"].update({"minimum":2,"maximum":1})
    semantic_cases.append(("package-relation-cardinality", "ipcraft.noc-package.v1", package_cardinality, "package-declaration", "package.invariant_violation"))
    package_slot_duplicate = copy.deepcopy(rich_package)
    package_slot_duplicate["topology"]["slotTemplates"][1]["stableKey"] = package_slot_duplicate["topology"]["slotTemplates"][0]["stableKey"]
    semantic_cases.append(("package-duplicate-slot-template", "ipcraft.noc-package.v1", package_slot_duplicate, "package-declaration", "package.invariant_violation"))
    package_allowed_duplicate = copy.deepcopy(rich_package)
    package_allowed_duplicate["topology"]["slotTemplates"][0]["allowedContracts"].append(copy.deepcopy(package_allowed_duplicate["topology"]["slotTemplates"][0]["allowedContracts"][0]))
    semantic_cases.append(("package-duplicate-allowed-contract", "ipcraft.noc-package.v1", package_allowed_duplicate, "package-declaration", "package.invariant_violation"))
    package_extension_duplicate = copy.deepcopy(rich_package)
    package_extension_duplicate["extensions"].append(copy.deepcopy(package_extension_duplicate["extensions"][0]))
    semantic_cases.append(("package-duplicate-extension", "ipcraft.noc-package.v1", package_extension_duplicate, "package-declaration", "package.invariant_violation"))
    package_noc_unknown = copy.deepcopy(rich_package)
    package_noc_unknown["interfaceTemplates"][0]["nocConfig"]["defaults"]["unknown"] = True
    semantic_cases.append(("package-interface-noc-default-unknown", "ipcraft.noc-package.v1", package_noc_unknown, "package-declaration", "package.invariant_violation"))
    engine = copy.deepcopy(valid["ipcraft.engine-bundle.v1"]); engine["engineHostContractVersion"] = "ipcraft.engine-host.v999"
    semantic_cases.append(("engine-host-contract", "ipcraft.engine-bundle.v1", engine, "engine-host-contract", "engine.host_contract_unsupported"))
    platform = copy.deepcopy(valid["ipcraft.engine-bundle.v1"]); platform["supportedPlatformAbis"] = ["unsupported-abi"]
    semantic_cases.append(("engine-platform", "ipcraft.engine-bundle.v1", platform, "engine-platform", "engine.platform_unsupported"))
    engine_path = copy.deepcopy(valid["ipcraft.engine-bundle.v1"]); engine_path["entrypoint"] = "CON.exe"
    semantic_cases.append(("engine-entrypoint-path", "ipcraft.engine-bundle.v1", engine_path, "engine-bundle-binding", "engine.bundle_mismatch"))
    recovery = copy.deepcopy(valid["ipcraft.recovery.v1"]); recovery["projectId"] = "different-project"
    semantic_cases.append(("recovery-binding", "ipcraft.recovery.v1", recovery, "recovery-binding", "recovery.binding_mismatch"))
    pipeline = copy.deepcopy(valid["ipcraft.pipeline-result.v1"]); pipeline["status"]="failed"; pipeline["failedStepId"]="missing-step"
    semantic_cases.append(("pipeline-failed-step-binding", "ipcraft.pipeline-result.v1", pipeline, "pipeline-result", "pipeline.result_invalid"))
    tool_input = copy.deepcopy(valid["ipcraft.tool-input.v1"]); tool_input.update({"kind":"generator","formallySavedProjectDigest":DIGEST_B,"outputDirectory":"output"})
    semantic_cases.append(("tool-input-generator-snapshot-binding", "ipcraft.tool-input.v1", tool_input, "tool-input", "tool.input_invalid"))
    tool_input_alias = copy.deepcopy(valid["ipcraft.tool-input.v1"]); tool_input_alias["resultFile"] = tool_input_alias["projectDesignFile"]
    semantic_cases.append(("tool-input-path-alias", "ipcraft.tool-input.v1", tool_input_alias, "tool-input", "tool.input_invalid"))
    pipeline_path = copy.deepcopy(valid["ipcraft.pipeline-result.v1"]); pipeline_path["steps"][0]["result"] = "CON.json"
    semantic_cases.append(("pipeline-result-reserved-path", "ipcraft.pipeline-result.v1", pipeline_path, "pipeline-result", "pipeline.result_invalid"))
    step_path = copy.deepcopy(external_step); step_path["toolResult"] = "CON.json"
    semantic_cases.append(("step-result-reserved-path", "ipcraft.step-result.v1", step_path, "generic-structure", "contract.schema_invalid"))
    tool_result_path = copy.deepcopy(valid["ipcraft.tool-result.v1"]); tool_result_path["diagnosticReport"] = "CON.json"
    semantic_cases.append(("tool-result-reserved-path", "ipcraft.tool-result.v1", tool_result_path, "runtime-result-binding", "tool.result_mismatch"))
    diagnostics = copy.deepcopy(valid["ipcraft.diagnostic-report.v1"])
    diagnostic = {"ruleId":"rule.duplicate","severity":"warning","message":"duplicate","blocking":False,"subjects":[],"properties":[]}
    diagnostics["diagnostics"] = [diagnostic, copy.deepcopy(diagnostic)]
    semantic_cases.append(("diagnostic-report-duplicate-entry", "ipcraft.diagnostic-report.v1", diagnostics, "diagnostic-report", "diagnostic.report_invalid"))

    patch = copy.deepcopy(valid["ipcraft.patch.v1"])
    ownership = copy.deepcopy(patch); ownership["operations"] = [{"op":"updateEntity","entityKind":"router","id":"router.0.0","set":{"properties":{}},"unset":[]}]
    semantic_cases.append(("patch-user-owned-router", "ipcraft.patch.v1", ownership, "ownership", "patch.ownership_violation"))
    duplicate_patch = copy.deepcopy(patch); duplicate_patch["operations"] = [{"op":"deleteEntity","entityKind":"component","id":"component.same"},{"op":"deleteEntity","entityKind":"component","id":"component.same"}]
    semantic_cases.append(("patch-duplicate-id", "ipcraft.patch.v1", duplicate_patch, "duplicate-id", "patch.duplicate_id"))
    precondition = copy.deepcopy(patch); precondition["preconditions"] = [
        {"kind":"entity-absent","entityKind":"component","id":"component.noc"},
        {"kind":"entity-exists","entityKind":"component","id":"component.noc"},
    ]
    semantic_cases.append(("patch-precondition-failed", "ipcraft.patch.v1", precondition, "precondition", "patch.precondition_failed"))
    false_preconditions = {
        "entity-exists":{"kind":"entity-exists","entityKind":"interface","id":"interface.missing"},
        "entity-absent":{"kind":"entity-absent","entityKind":"component","id":"component.noc"},
        "property-equals":{"kind":"property-equals","entityKind":"project","id":"project.mesh","property":"name","value":"Wrong"},
        "relation-exists":{"kind":"relation-exists","relationKind":"domain-membership","id":"membership.missing"},
        "slot-unoccupied":{"kind":"slot-unoccupied","slotId":"slot.occupied"},
    }
    for precondition_kind, false_precondition in false_preconditions.items():
        failed = copy.deepcopy(patch); failed["preconditions"] = [false_precondition]
        semantic_cases.append((f"patch-precondition-false-{precondition_kind}", "ipcraft.patch.v1", failed, "precondition", "patch.precondition_failed"))
    duplicate_precondition = copy.deepcopy(precondition_patch); duplicate_precondition["preconditions"].append(copy.deepcopy(duplicate_precondition["preconditions"][0]))
    semantic_cases.append(("patch-precondition-duplicate", "ipcraft.patch.v1", duplicate_precondition, "patch-invariant", "patch.invariant_violation"))
    noncanonical_precondition = copy.deepcopy(precondition_patch); noncanonical_precondition["preconditions"].reverse()
    semantic_cases.append(("patch-precondition-noncanonical", "ipcraft.patch.v1", noncanonical_precondition, "patch-invariant", "patch.invariant_violation"))
    local_ref = copy.deepcopy(patch); local_ref["operations"] = [
        {"op":"createEntity","entityKind":"package-entity","localRef":"application:000001","value":{"typeKey":"endpoint","data":{},"extensions":[]}},
        {"op":"createEntity","entityKind":"package-entity","localRef":"application:000001","value":{"typeKey":"endpoint","data":{},"extensions":[]}},
    ]
    semantic_cases.append(("patch-duplicate-local-ref", "ipcraft.patch.v1", local_ref, "local-reference", "patch.local_ref_invalid"))
    invariant = copy.deepcopy(patch); invariant["operations"] = [{"op":"updateEntity","entityKind":"interface","id":"interface.axi","set":{"name":"Renamed"},"unset":["name"]}]
    semantic_cases.append(("patch-set-unset-overlap", "ipcraft.patch.v1", invariant, "patch-invariant", "patch.invariant_violation"))
    subject = copy.deepcopy(patch); subject["operations"] = [{"op":"updateEntity","entityKind":"interface","id":"interface.axi","set":{},"unset":["name"]}]
    semantic_cases.append(("patch-subject-schema", "ipcraft.patch.v1", subject, "patched-subject-schema", "patch.schema_violation"))
    component_identity = copy.deepcopy(patch); component_identity["operations"] = [{"op":"updateEntity","entityKind":"component","id":"component.noc","set":{"packageLockId":"dep.other"},"unset":[]}]
    semantic_cases.append(("patch-user-component-identity", "ipcraft.patch.v1", component_identity, "ownership", "patch.ownership_violation"))
    authority_domain = patch_with({"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":DIGEST_A}, [{"op":"updateEntity","entityKind":"domain","id":"domain.default","set":{"name":"Changed"},"unset":[]}])
    semantic_cases.append(("patch-authority-mutates-domain", "ipcraft.patch.v1", authority_domain, "ownership", "patch.ownership_violation"))
    reconcile_component = patch_with({"kind":"application-reconcile","identity":"ipcraft.host","version":"1"}, [{"op":"updateEntity","entityKind":"component","id":"component.noc","set":{"config":{}},"unset":[]}])
    semantic_cases.append(("patch-reconcile-mutates-component", "ipcraft.patch.v1", reconcile_component, "ownership", "patch.ownership_violation"))
    attachment_transition = copy.deepcopy(patch); attachment_transition["operations"] = [{"op":"updateRelation","relationKind":"attachment","id":"attachment.boundary","set":{"state":"unresolved"},"unset":["routerRef","slotRef"]}]
    semantic_cases.append(("patch-attachment-unresolved-incomplete", "ipcraft.patch.v1", attachment_transition, "patched-subject-schema", "patch.schema_violation"))
    interface_owner_unset = copy.deepcopy(patch); interface_owner_unset["operations"] = [{"op":"updateEntity","entityKind":"interface","id":"interface.boundary","set":{},"unset":["ownerComponentRef"]}]
    semantic_cases.append(("patch-interface-owner-unset", "ipcraft.patch.v1", interface_owner_unset, "patched-subject-schema", "patch.schema_violation"))
    slot_missing_router = patch_with(sources["default-engine"], [{"op":"createEntity","entityKind":"access-slot","localRef":"authority:missing-slot","value":{"routerRef":{"id":"router.missing"},"templateKey":"local-x","identityCompatibilityVersion":1,"displayOrder":2,"label":"Missing","allowedContracts":[],"properties":{}}}])
    semantic_cases.append(("patch-access-slot-missing-router", "ipcraft.patch.v1", slot_missing_router, "reference", "patch.unknown_reference"))
    slot_duplicate_role = patch_with(sources["default-engine"], [{"op":"createEntity","entityKind":"access-slot","localRef":"authority:duplicate-role-slot","value":{"routerRef":{"id":"router.0.0"},"templateKey":"local-x","identityCompatibilityVersion":1,"displayOrder":2,"label":"Duplicate","allowedContracts":[{"contractLockId":"dep.contract.axi5","roles":["initiator","initiator"],"capabilityConstraints":{}}],"properties":{}}}])
    semantic_cases.append(("patch-access-slot-duplicate-role", "ipcraft.patch.v1", slot_duplicate_role, "patch-invariant", "patch.invariant_violation"))
    reconcile_rename = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-source-application-reconcile.json").read_text()))
    reconcile_rename["operations"].append({"op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"name":"Forbidden"},"unset":[]})
    semantic_cases.append(("patch-reconcile-project-rename", "ipcraft.patch.v1", reconcile_rename, "ownership", "patch.ownership_violation"))
    local_slot_occupied = patch_with(sources["recovery"], [
        {"op":"deleteRelation","relationKind":"attachment","id":"attachment.boundary"},
        {"op":"createEntity","entityKind":"access-slot","localRef":"application:000020","value":{"routerRef":{"id":"router.0.0"},"templateKey":"local-new","identityCompatibilityVersion":1,"displayOrder":2,"label":"New","allowedContracts":[],"properties":{}}},
        {"op":"createRelation","relationKind":"attachment","localRef":"application:000021","value":{"interfaceRef":{"id":"interface.boundary"},"state":"resolved","routerRef":{"id":"router.0.0"},"slotRef":{"localRef":"application:000020"}}},
        {"op":"createRelation","relationKind":"attachment","localRef":"application:000022","value":{"interfaceRef":{"id":"interface.unattached"},"state":"resolved","routerRef":{"id":"router.0.0"},"slotRef":{"localRef":"application:000020"}}},
    ])
    semantic_cases.append(("patch-local-slot-double-occupancy", "ipcraft.patch.v1", local_slot_occupied, "patch-invariant", "patch.invariant_violation"))
    local_slot_router_mismatch = patch_with(sources["recovery"], [
        {"op":"createEntity","entityKind":"access-slot","localRef":"application:000023","value":{"routerRef":{"id":"router.0.0"},"templateKey":"local-new","identityCompatibilityVersion":1,"displayOrder":2,"label":"New","allowedContracts":[],"properties":{}}},
        {"op":"createRelation","relationKind":"attachment","localRef":"application:000024","value":{"interfaceRef":{"id":"interface.unattached"},"state":"resolved","routerRef":{"id":"router.0.1"},"slotRef":{"localRef":"application:000023"}}},
    ])
    semantic_cases.append(("patch-local-slot-router-mismatch", "ipcraft.patch.v1", local_slot_router_mismatch, "patch-invariant", "patch.invariant_violation"))
    self_link = patch_with(sources["default-engine"], [{"op":"createEntity","entityKind":"structural-link","localRef":"authority:self-link","value":{"templateKey":"mesh-link","identityCompatibilityVersion":1,"endpointA":{"id":"router.0.0"},"endpointB":{"id":"router.0.0"},"axis":"horizontal","properties":{}}}])
    semantic_cases.append(("patch-structural-link-self", "ipcraft.patch.v1", self_link, "patch-invariant", "patch.invariant_violation"))
    for suffix, operation in {
        "engine-update": {"op":"updateRelation","relationKind":"package-relation","id":"package-relation.engine","set":{"targets":[]},"unset":[]},
        "engine-delete": {"op":"deleteRelation","relationKind":"package-relation","id":"package-relation.engine"},
        "user-data": {"op":"updateRelation","relationKind":"package-relation","id":"package-relation.user","set":{"data":{"forbidden":True}},"unset":[]},
    }.items():
        illegal = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-source-application-reconcile.json").read_text()))
        illegal["operations"].append(operation)
        semantic_cases.append((f"patch-reconcile-package-relation-{suffix}", "ipcraft.patch.v1", illegal, "ownership", "patch.ownership_violation"))
    for suffix, operation in {
        "attachment-delete": {"op":"deleteRelation","relationKind":"attachment","id":"attachment.boundary"},
        "attachment-create": {"op":"createRelation","relationKind":"attachment","localRef":"application:000030","value":{"interfaceRef":{"id":"interface.unattached"},"state":"resolved","routerRef":{"id":"router.0.1"},"slotRef":{"id":"slot.free"}}},
        "attachment-reattach": {"op":"updateRelation","relationKind":"attachment","id":"attachment.boundary","set":{"routerRef":{"id":"router.0.1"},"slotRef":{"id":"slot.free"}},"unset":[]},
    }.items():
        illegal = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-source-application-reconcile.json").read_text()))
        illegal["operations"].append(operation)
        semantic_cases.append((f"patch-reconcile-{suffix}", "ipcraft.patch.v1", illegal, "ownership", "patch.ownership_violation"))
    tampered_replay = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-source-recovery.json").read_text()))
    tampered_replay["operations"][0]["set"]["name"] = "Tampered replay"
    semantic_cases.append(("patch-recovery-tampered-replay", "ipcraft.patch.v1", tampered_replay, "source-authority", "patch.source_not_allowed"))
    arbitrary_relation_reason = copy.deepcopy(application_relation_side_effect)
    arbitrary_relation_reason["operations"][-1]["set"]["sources"][0]["reasonCode"] = "arbitrary"
    semantic_cases.append(("patch-reconcile-package-relation-reason-code", "ipcraft.patch.v1", arbitrary_relation_reason, "ownership", "patch.ownership_violation"))
    arbitrary_attachment_reason = copy.deepcopy(application_attachment_side_effect)
    arbitrary_attachment_reason["operations"][-1]["set"]["reasonCode"] = "arbitrary"
    semantic_cases.append(("patch-reconcile-attachment-reason-code", "ipcraft.patch.v1", arbitrary_attachment_reason, "ownership", "patch.ownership_violation"))
    reconcile_missing = patch_with({"kind":"application-reconcile","identity":"ipcraft.host","version":"1"}, [])
    semantic_cases.append(("patch-reconcile-missing-derivation", "ipcraft.patch.v1", reconcile_missing, "ownership", "patch.ownership_violation"))
    migration_missing = patch_with({"kind":"application-migration","identity":"ipcraft.host","version":"1"}, [{
        "op":"updateEntity","entityKind":"topology","id":"topology.mesh",
        "set":{"derivation":copy.deepcopy(valid["ipcraft.project-design.v1"]["topologies"][0]["derivation"])},"unset":[],
    }])
    semantic_cases.append(("patch-migration-missing-dependencies", "ipcraft.patch.v1", migration_missing, "engine-migration-binding", "engine.migration_invalid"))
    component_delete = copy.deepcopy(patch); component_delete["operations"] = [{"op":"deleteEntity","entityKind":"component","id":"component.noc"}]
    semantic_cases.append(("patch-component-delete-forbidden", "ipcraft.patch.v1", component_delete, "patch-invariant", "patch.invariant_violation"))
    forward_ref = copy.deepcopy(patch); forward_ref["operations"] = [
        {"op":"createEntity","entityKind":"interface","localRef":"application:000001","value":{
            "ownerComponentRef":{"localRef":"application:000002"},"templateKey":"axi-boundary","name":"AXI",
            "contract":{"lockId":"dep.contract.axi5","role":"initiator"},"capabilities":{},"contractConfig":{},"nocConfig":{},"extensions":[]}},
        {"op":"createEntity","entityKind":"component","localRef":"application:000002","value":{
            "kind":"noc","name":"NoC","packageLockId":"dep.noc","typeKey":"mesh","config":{},"extensions":[]}},
    ]
    semantic_cases.append(("patch-local-ref-forward", "ipcraft.patch.v1", forward_ref, "local-reference", "patch.local_ref_invalid"))
    user_engine_entity = patch_with(sources["user-command"], [{"op":"updateEntity","entityKind":"package-entity","id":"package-entity.engine","set":{"data":{}},"unset":[]}])
    semantic_cases.append(("patch-user-mutates-engine-package-entity", "ipcraft.patch.v1", user_engine_entity, "ownership", "patch.ownership_violation"))
    authority_user_entity = patch_with(sources["default-engine"], [{"op":"updateEntity","entityKind":"package-entity","id":"package-entity.user","set":{"data":{}},"unset":[]}])
    semantic_cases.append(("patch-authority-mutates-user-package-entity", "ipcraft.patch.v1", authority_user_entity, "ownership", "patch.ownership_violation"))
    user_engine_relation = patch_with(sources["user-command"], [{"op":"updateRelation","relationKind":"package-relation","id":"package-relation.engine","set":{"data":{}},"unset":[]}])
    semantic_cases.append(("patch-user-mutates-engine-package-relation", "ipcraft.patch.v1", user_engine_relation, "ownership", "patch.ownership_violation"))
    authority_user_relation = patch_with(sources["default-engine"], [{"op":"updateRelation","relationKind":"package-relation","id":"package-relation.user","set":{"data":{}},"unset":[]}])
    semantic_cases.append(("patch-authority-mutates-user-package-relation", "ipcraft.patch.v1", authority_user_relation, "ownership", "patch.ownership_violation"))
    authority_mismatch = patch_with(sources["extension-provider"], [{
        "op":"createEntity","entityKind":"router","localRef":"authority:router.0.0",
        "value":{"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":0,"column":0},"properties":{}},
    }])
    authority_mismatch["applicability"]["structureAuthority"].update({
        "kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":DIGEST_A,
    })
    semantic_cases.append(("patch-authority-applicability-mismatch", "ipcraft.patch.v1", authority_mismatch, "structure-authority", "patch.authority_conflict"))
    applicability_members = (
        "groupId", "requestGeneration", "topologyInputRevision", "topologyInputDigest",
        "baseDerivedStateRevision", "baseDerivedStateDigest", "baseAuthoritativeDesignDigest",
        "packageBundleDigest", "reconcileDependencySetDigest", "defaultEngineLockId",
        "defaultEngineBundleDigest", "engineHostContractVersion", "hostSideEffectContractVersion",
    )
    for source_name in ("default-engine", "extension-provider", "application-reconcile", "application-migration"):
        base = copy.deepcopy(json.loads((contracts / f"fixtures/valid/patch-source-{source_name}.json").read_text()))
        for member in applicability_members:
            mutated = copy.deepcopy(base)
            value = mutated["applicability"][member]
            mutated["applicability"][member] = value + 1 if isinstance(value, int) else ("sha256:" + "f" * 64 if str(value).startswith("sha256:") else "wrong")
            semantic_cases.append((f"patch-{source_name}-applicability-{member}", "ipcraft.patch.v1", mutated, "structure-authority", "patch.authority_conflict"))
        for member in ("lockId", "version", "bundleDigest"):
            mutated = copy.deepcopy(base)
            value = mutated["applicability"]["structureAuthority"][member]
            mutated["applicability"]["structureAuthority"][member] = "sha256:" + "f" * 64 if str(value).startswith("sha256:") else "wrong"
            semantic_cases.append((f"patch-{source_name}-applicability-authority-{member}", "ipcraft.patch.v1", mutated, "structure-authority", "patch.authority_conflict"))
    provider_application_base = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-source-application-reconcile-provider-authority.json").read_text()))
    for member in ("topologyInputDigest", "defaultEngineBundleDigest", "hostSideEffectContractVersion"):
        mutated = copy.deepcopy(provider_application_base)
        value = mutated["applicability"][member]
        mutated["applicability"][member] = "sha256:" + "e" * 64 if str(value).startswith("sha256:") else "wrong"
        semantic_cases.append((f"patch-application-reconcile-provider-applicability-{member}", "ipcraft.patch.v1", mutated, "structure-authority", "patch.authority_conflict"))

    typed_interface = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-source-user-command.json").read_text()))
    missing_contract = copy.deepcopy(typed_interface); missing_contract["operations"][0]["value"]["contract"]["lockId"] = "dep.missing"
    semantic_cases.append(("patch-interface-contract-lock-missing", "ipcraft.patch.v1", missing_contract, "reference", "patch.unknown_reference"))
    wrong_contract = copy.deepcopy(typed_interface); wrong_contract["operations"][0]["value"]["contract"]["lockId"] = "dep.noc.simple"
    semantic_cases.append(("patch-interface-contract-lock-wrong-kind", "ipcraft.patch.v1", wrong_contract, "reference", "patch.unknown_reference"))
    for suffix, owner in (("missing", "dep.missing"), ("wrong-kind", "dep.runtime.provider")):
        invalid_extension = copy.deepcopy(typed_interface)
        invalid_extension["operations"][0]["value"]["extensions"] = [{"ownerLockId":owner,"schema":"vendor.x.v1","version":"1","data":{}}]
        semantic_cases.append((f"patch-extension-owner-{suffix}", "ipcraft.patch.v1", invalid_extension, "reference", "patch.unknown_reference"))
    for suffix, contract_lock in (("missing", "dep.missing"), ("wrong-kind", "dep.noc.simple")):
        invalid_slot = patch_with(sources["default-engine"], [{
            "op":"createEntity","entityKind":"access-slot","localRef":"authority:typed-slot",
            "value":{"routerRef":{"id":"router.0.0"},"templateKey":f"typed-{suffix}","identityCompatibilityVersion":1,"displayOrder":4,"label":"Typed","allowedContracts":[{"contractLockId":contract_lock,"roles":["initiator"],"capabilityConstraints":{}}],"properties":{}},
        }])
        semantic_cases.append((f"patch-slot-contract-lock-{suffix}", "ipcraft.patch.v1", invalid_slot, "reference", "patch.unknown_reference"))
    for suffix, package_lock in (("missing", "dep.missing"), ("wrong-kind", "dep.contract.axi5")):
        invalid_component = patch_with(sources["user-command"], [{
            "op":"createEntity","entityKind":"component","localRef":"application:000030",
            "value":{"kind":"noc","name":"Other","packageLockId":package_lock,"typeKey":"mesh-noc","config":{},"extensions":[]},
        }])
        semantic_cases.append((f"patch-component-package-lock-{suffix}", "ipcraft.patch.v1", invalid_component, "reference", "patch.unknown_reference"))
    migration_dependency_base = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-source-application-migration.json").read_text()))
    remove_live_contract = copy.deepcopy(migration_dependency_base)
    remove_live_contract["operations"][0]["set"]["dependencies"] = [item for item in remove_live_contract["operations"][0]["set"]["dependencies"] if item["kind"] != "interface-contract"]
    semantic_cases.append(("patch-migration-removes-live-contract", "ipcraft.patch.v1", remove_live_contract, "engine-migration-binding", "engine.migration_invalid"))
    provider_runtime_wrong = copy.deepcopy(migration_dependency_base)
    next(item for item in provider_runtime_wrong["operations"][0]["set"]["dependencies"] if item["kind"] == "extension-provider")["runtimeLockId"] = "dep.noc.simple"
    semantic_cases.append(("patch-migration-provider-runtime-wrong-kind", "ipcraft.patch.v1", provider_runtime_wrong, "engine-migration-binding", "engine.migration_invalid"))
    tool_runtime_missing = copy.deepcopy(migration_dependency_base)
    next(item for item in tool_runtime_missing["operations"][0]["set"]["dependencies"] if item["kind"] == "drc-tool")["runtimeLockId"] = "dep.missing"
    semantic_cases.append(("patch-migration-tool-runtime-missing", "ipcraft.patch.v1", tool_runtime_missing, "engine-migration-binding", "engine.migration_invalid"))

    type_key_attacks = (
        ("user-entity-to-engine", sources["user-command"], {"op":"updateEntity","entityKind":"package-entity","id":"package-entity.user","set":{"typeKey":"vendor.engine-entity"},"unset":[]}),
        ("engine-entity-to-user", sources["default-engine"], {"op":"updateEntity","entityKind":"package-entity","id":"package-entity.engine","set":{"typeKey":"vendor.user-entity"},"unset":[]}),
        ("user-relation-to-engine", sources["user-command"], {"op":"updateRelation","relationKind":"package-relation","id":"package-relation.user","set":{"typeKey":"vendor.engine-relation"},"unset":[]}),
        ("engine-relation-to-user", sources["default-engine"], {"op":"updateRelation","relationKind":"package-relation","id":"package-relation.engine","set":{"typeKey":"vendor.user-relation"},"unset":[]}),
    )
    for suffix, source_envelope, operation in type_key_attacks:
        semantic_cases.append((f"patch-type-key-{suffix}", "ipcraft.patch.v1", patch_with(source_envelope, [operation]), "ownership", "patch.ownership_violation"))
    for suffix, fixture_name in (
        ("user-entity", "patch-package-ownership-user-entity.json"),
        ("engine-entity", "patch-package-ownership-engine-entity.json"),
        ("user-relation", "patch-package-ownership-user-relation.json"),
        ("engine-relation", "patch-package-ownership-engine-relation.json"),
    ):
        undeclared = copy.deepcopy(json.loads((contracts / "fixtures/valid" / fixture_name).read_text()))
        undeclared["operations"][0]["value"]["typeKey"] = "vendor.undeclared"
        semantic_cases.append((f"patch-package-undeclared-type-{suffix}", "ipcraft.patch.v1", undeclared, "reference", "patch.unknown_reference"))
    resolved_only_unresolved = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-operation-updateRelation.json").read_text()))
    resolved_only_unresolved["operations"] = [{
        "op":"updateRelation","relationKind":"package-relation","id":"package-relation.resolved-only",
        "set":{"sources":[{"state":"unresolved","intendedSubject":{"kind":"router","ref":{"id":"router.0.0"}},"reasonCode":"relation.target_removed"}]},"unset":[],
    }]
    semantic_cases.append(("patch-package-relation-unresolved-forbidden", "ipcraft.patch.v1", resolved_only_unresolved, "patch-invariant", "patch.invariant_violation"))

    domain_base = copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-source-application-reconcile.json").read_text()))
    domain_attacks = {
        "rename":{"op":"updateEntity","entityKind":"domain","id":"domain.default","set":{"name":"Changed"},"unset":[]},
        "delete-default":{"op":"deleteEntity","entityKind":"domain","id":"domain.default"},
        "second-default":{"op":"createEntity","entityKind":"domain","localRef":"application:000099","value":{"typeKey":"power","name":"Second","isDefault":True,"config":{}}},
        "membership-move":{"op":"updateRelation","relationKind":"domain-membership","id":"membership.0","set":{"routerRef":{"id":"router.0.1"}},"unset":[]},
    }
    for suffix, operation in domain_attacks.items():
        mutated = copy.deepcopy(domain_base); mutated["operations"].append(operation)
        semantic_cases.append((f"patch-reconcile-domain-{suffix}", "ipcraft.patch.v1", mutated, "ownership", "patch.ownership_violation"))
    for relation_kind, required_name in (("domain-membership", "domainRef"), ("package-relation", "sources"), ("attachment", "interfaceRef")):
        relation_unset = copy.deepcopy(patch)
        relation_id = {"domain-membership":"membership.0","package-relation":"package-relation.user","attachment":"attachment.boundary"}[relation_kind]
        relation_unset["operations"] = [{"op":"updateRelation","relationKind":relation_kind,"id":relation_id,"set":{},"unset":[required_name]}]
        semantic_cases.append((f"patch-{relation_kind}-required-unset", "ipcraft.patch.v1", relation_unset, "patched-subject-schema", "patch.schema_violation"))

    malformed_operation = copy.deepcopy(patch); malformed_operation["operations"] = [{"op":"createEntity","entityKind":"router","localRef":"application:000003"}]
    add("patch-operation-shape", "ipcraft.patch.v1", malformed_operation, "reject", "schema", "operation-shape", "patch.operation_invalid")

    vectors = json.loads((contracts / "vectors/default-engine-lock-v1.json").read_text())
    migration = copy.deepcopy(vectors["migrationCases"][0]["input"]["candidate"])
    migration["migration"]["targetDefaultEngineLock"]["bundleManifestDigest"] = migration["migration"]["currentDefaultEngineLock"]["bundleManifestDigest"]
    semantic_cases.append(("engine-migration-same-bundle", "ipcraft.core-canonical-models.v1", migration, "engine-migration-binding", "engine.migration_invalid"))

    for name, schema_id, document, boundary, code in semantic_cases:
        add(name, schema_id, document, "reject", "core-semantic", boundary, code)

    entries.sort(key=lambda item: item["path"])
    write_json(contracts / "fixture-catalog.json", {"schema": "ipcraft.fixture-catalog.v1", "items": entries})

    base_paths = {
        schema_id: "fixtures/valid/" + schema_id.removeprefix("ipcraft.").removesuffix(".v1").replace(".", "-") + ".json"
        for schema_id in schema_ids
    }
    coverage = {
        schema_id: {
            "minimal": [path],
            "representative": [path],
            "maximumShape": [path],
        }
        for schema_id, path in base_paths.items()
    }
    coverage["ipcraft.interface-contract.v1"]["representative"] = ["fixtures/valid/interface-contract-axi5.json"]
    coverage["ipcraft.interface-contract.v1"]["maximumShape"] = ["fixtures/valid/interface-contract-maximum.json"]
    coverage["ipcraft.noc-package.v1"]["maximumShape"] = ["fixtures/valid/noc-package-maximum.json", "fixtures/valid/noc-package-extension-authority.json"]
    coverage["ipcraft.project-design.v1"].update({
        "minimal": ["fixtures/valid/minimal-1x1.json"],
        "representative": ["fixtures/valid/mesh-2x2-attached.json"],
        "maximumShape": ["fixtures/valid/project-design-maximum.json", "fixtures/valid/project-provider-authority.json"],
    })
    coverage["ipcraft.core-canonical-models.v1"].update({
        "representative": ["fixtures/valid/core-normalizedTopologyInput.json"],
        "maximumShape": [base_paths["ipcraft.core-canonical-models.v1"]] + [
            f"fixtures/valid/core-{name}.json" for name in (
                "normalizedTopologyInput", "derivedState", "reconcileApplicability", "patchBody",
                "impactReport", "candidateTransaction", "pipelinePlan", "outputManifest",
            )
        ],
    })
    coverage["ipcraft.patch.v1"].update({
        "representative": ["fixtures/valid/patch-operation-updateEntity.json"],
        "maximumShape": [
            *[f"fixtures/valid/patch-operation-{name}.json" for name in operation_defs],
            *[f"fixtures/valid/patch-source-{name}.json" for name in sources],
            "fixtures/valid/patch-preconditions-all.json",
            *[f"fixtures/valid/patch-package-ownership-{name}.json" for name in package_ownership_witnesses],
            "fixtures/valid/patch-application-package-relation-unresolved.json",
            "fixtures/valid/patch-application-attachment-unresolved.json",
        ],
    })
    coverage["ipcraft.command-result.v1"].update({
        "representative": ["fixtures/valid/command-result-pending-topology.json"],
        "maximumShape": [base_paths["ipcraft.command-result.v1"], *[
            f"fixtures/valid/command-result-{name}.json" for name in ("pending-topology", "drafted", "rejected")
        ]],
    })
    coverage["ipcraft.pipeline-result.v1"].update({
        "representative": ["fixtures/valid/pipeline-result-failed.json"],
        "maximumShape": [base_paths["ipcraft.pipeline-result.v1"], *[
            f"fixtures/valid/pipeline-result-{name}.json" for name in ("failed", "cancelled", "timed-out")
        ]],
    })
    coverage["ipcraft.step-result.v1"].update({
        "representative": ["fixtures/valid/step-result-external-success.json"],
        "maximumShape": [base_paths["ipcraft.step-result.v1"], "fixtures/valid/step-result-external-success.json", *[
            f"fixtures/valid/step-result-{name}.json" for name in ("failed", "cancelled", "timed-out", "skipped")
        ]],
    })
    coverage["ipcraft.tool-input.v1"].update({
        "representative": ["fixtures/valid/tool-input-generator.json"],
        "maximumShape": [base_paths["ipcraft.tool-input.v1"], "fixtures/valid/tool-input-generator.json"],
    })
    coverage["ipcraft.tool-result.v1"].update({
        "representative": ["fixtures/valid/tool-result-failed.json"],
        "maximumShape": ["fixtures/valid/tool-result-maximum.json", "fixtures/valid/tool-result-failed.json"],
    })
    for schema_id, fixture_name in {
        "ipcraft.artifact-manifest.v1":"artifact-manifest-maximum",
        "ipcraft.bundle-manifest.v1":"bundle-manifest-maximum",
        "ipcraft.diagnostic-report.v1":"diagnostic-report-maximum",
        "ipcraft.engine-bundle.v1":"engine-bundle-maximum",
        "ipcraft.pipeline-plan.v1":"pipeline-plan-maximum",
        "ipcraft.recovery.v1":"recovery-maximum",
        "ipcraft.tool-input.v1":"tool-input-maximum",
    }.items():
        path = f"fixtures/valid/{fixture_name}.json"
        coverage[schema_id]["representative"] = [path]
        coverage[schema_id]["maximumShape"] = [path]
    coverage["ipcraft.noc-side-effects.v1"]["representative"] = ["fixtures/valid/noc-side-effects-ready-to-commit.json"]
    coverage["ipcraft.noc-side-effects.v1"]["maximumShape"] = [
        f"fixtures/valid/noc-side-effects-{state}.json" for state in ("auto-commit", "ready-to-commit", "blocked")
    ]
    coverage["ipcraft.tool-input.v1"]["maximumShape"] = [base_paths["ipcraft.tool-input.v1"], "fixtures/valid/tool-input-maximum.json"]

    requirements = {
        "ipcraft.artifact-manifest.v1": {"representative":{"minimumArrayLengths":{"/artifacts":1}},"maximumShape":{"minimumArrayLengths":{"/artifacts":2}}},
        "ipcraft.bundle-manifest.v1": {"representative":{"minimumArrayLengths":{"/files":1}},"maximumShape":{"minimumArrayLengths":{"/files":2}}},
        "ipcraft.command-result.v1": {"maximumShape":{"discriminatorCoverage":{"/disposition":["accepted","pending-topology","drafted","rejected"]}}},
        "ipcraft.core-canonical-models.v1": {"maximumShape":{"discriminatorCoverage":{"/schema":["ipcraft.topology-intent.v1","ipcraft.normalized-topology-input.v1","ipcraft.derived-state.v1","ipcraft.reconcile-applicability.v1","ipcraft.patch-body.v1","ipcraft.topology-impact-report.v1","ipcraft.candidate-transaction.v1","ipcraft.pipeline-plan.v1","ipcraft.output-manifest.v1"]}}},
        "ipcraft.diagnostic-report.v1": {"representative":{"minimumArrayLengths":{"/diagnostics":1}},"maximumShape":{"minimumArrayLengths":{"/diagnostics":3},"discriminatorCoverage":{"/diagnostics/*/severity":["info","warning","error"]}}},
        "ipcraft.engine-bundle.v1": {"maximumShape":{"minimumArrayLengths":{"/migrationFromCompatibilityVersions":2,"/supportedPlatformAbis":2}}},
        "ipcraft.interface-contract.v1": {"maximumShape":{"minimumArrayLengths":{"/roles":2,"/capabilities":5,"/fields":5},"discriminatorCoverage":{"/capabilities/*/type":["bool","int","double","string","enum"],"/fields/*/type":["bool","int","double","string","enum"]}}},
        "ipcraft.noc-package.v1": {"maximumShape":{"minimumArrayLengths":{"/topology/slotTemplates":2,"/packageEntityTypes":2,"/packageRelationTypes":2,"/extensions":1},"requiredPointers":["/tools/drc","/tools/generate","/interfaceTemplates/0","/extensionProvider/protocol"],"discriminatorCoverage":{"/structureAuthority":["default-engine","extension-provider"]}}},
        "ipcraft.noc-side-effects.v1": {"maximumShape":{"discriminatorCoverage":{"/expected/groupState":["auto-commit","ready-to-commit","blocked"]}}},
        "ipcraft.output-manifest.v1": {"maximumShape":{"requiredPointers":["/artifactManifestDigest","/defaultEngineBundleDigest","/hostSideEffectContractVersion"]}},
        "ipcraft.patch.v1": {"maximumShape":{"discriminatorCoverage":{"/operations/*/op":["createEntity","updateEntity","deleteEntity","createRelation","updateRelation","deleteRelation"],"/source/kind":["user-command","application-reconcile","application-migration","default-engine","extension-provider","recovery","undo-redo"],"/preconditions/*/kind":["entity-exists","entity-absent","property-equals","relation-exists","slot-unoccupied"]}}},
        "ipcraft.pipeline-plan.v1": {"maximumShape":{"minimumArrayLengths":{"/steps":5},"discriminatorCoverage":{"/kind":["generate"],"/steps/*/kind":["host","external-tool"]}}},
        "ipcraft.pipeline-result.v1": {"maximumShape":{"discriminatorCoverage":{"/status":["succeeded","failed","cancelled","timed-out"]}}},
        "ipcraft.project-design.v1": {"maximumShape":{"minimumArrayLengths":{"/dependencies":7,"/topologies/0/routers":4},"requiredPointers":["/interfaces/0","/topologies/0/packageEntities/0","/topologies/0/packageRelations/0"],"discriminatorCoverage":{"/dependencies/*/kind":["default-engine","noc-package","interface-contract","extension-provider","drc-tool","generator-tool","runtime"],"/topologies/*/derivation/structureAuthority/kind":["default-engine","extension-provider"]}}},
        "ipcraft.recovery.v1": {"maximumShape":{"minimumArrayLengths":{"/draftOverlay":1,"/draftUndo":1,"/draftRedo":1},"requiredPointers":["/pendingTopologyGroup/groupId"]}},
        "ipcraft.step-result.v1": {"maximumShape":{"discriminatorCoverage":{"/status":["succeeded","failed","cancelled","timed-out","skipped"],"/stepKind":["host","external-tool"]}}},
        "ipcraft.tool-input.v1": {"maximumShape":{"minimumArrayLengths":{"/dependencies":2},"discriminatorCoverage":{"/kind":["semantic-drc","generator"]},"requiredPointers":["/outputDirectory"]}},
        "ipcraft.tool-result.v1": {"maximumShape":{"discriminatorCoverage":{"/status":["succeeded","failed"]},"requiredPointers":["/diagnosticReport","/artifactManifest","/failure/code"]}}
    }
    requirements = {
        schema_id: {tier: requirements[schema_id].get(tier, {}) for tier in ("minimal", "representative", "maximumShape")}
        for schema_id in schema_ids
    }
    requirements_digest = "sha256:" + hashlib.sha256(
        json.dumps(requirements, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    coverage_digest = "sha256:" + hashlib.sha256(
        json.dumps({"roots": coverage, "requirements": requirements}, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    write_json(contracts / "fixture-coverage-v1.json", {
        "schema": "ipcraft.fixture-coverage.v1", "requirementsDigest": requirements_digest,
        "coverageDigest": coverage_digest,
        "roots": coverage, "requirements": requirements,
    })


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contracts-root", type=Path, default=CONTRACTS)
    args = parser.parse_args()
    generate(args.contracts_root.resolve())


if __name__ == "__main__":
    main()
