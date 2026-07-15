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
            digest_input = {key: value for key, value in document.items() if key != "manifestDigest"}
            canonical = json.dumps(digest_input, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            document["manifestDigest"] = "sha256:" + hashlib.sha256(canonical.encode()).hexdigest()
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
    applicability = builder.build(core_schema["$defs"]["reconcileApplicability"], core_schema, core_schema_path)

    def patch_with(source: dict[str, Any], operations: list[dict[str, Any]]) -> dict[str, Any]:
        patch = copy.deepcopy(valid["ipcraft.patch.v1"])
        patch["source"] = source
        patch["operations"] = operations
        if source["kind"] in {"default-engine", "extension-provider", "application-reconcile", "application-migration"}:
            patch["applicability"] = copy.deepcopy(applicability)
            if source["kind"] in {"default-engine", "extension-provider"}:
                patch["applicability"]["structureAuthority"].update({
                    "kind": source["kind"], "identity": source["identity"],
                    "version": source["version"], "bundleDigest": source["bundleDigest"],
                })
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
            operation.update({"entityKind": "interface", "id": "interface.boundary"})
        elif operation_name == "createRelation":
            operation.update({"relationKind": "domain-membership", "localRef": "application:000001", "value": {"domainRef": {"id":"domain.default"}, "routerRef": {"id":"router.0.0"}}})
        elif operation_name == "updateRelation":
            operation.update({"relationKind": "domain-membership", "id": "membership.0", "set": {"routerRef": {"id":"router.0.1"}}, "unset": []})
        elif operation_name == "deleteRelation":
            operation.update({"relationKind": "domain-membership", "id": "membership.0"})
        add(f"patch-operation-{operation_name}", "ipcraft.patch.v1", patch_with({"kind":"user-command","identity":"ipcraft.host","version":"1"}, [operation]), "accept", "core-semantic", None, None)

    sources = {
        "user-command": {"kind":"user-command","identity":"ipcraft.host","version":"1"},
        "application-reconcile": {"kind":"application-reconcile","identity":"ipcraft.host","version":"1"},
        "application-migration": {"kind":"application-migration","identity":"ipcraft.host","version":"1"},
        "default-engine": {"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1","bundleDigest":DIGEST_A},
        "extension-provider": {"kind":"extension-provider","identity":"vendor.provider","version":"1","bundleDigest":DIGEST_B},
        "recovery": {"kind":"recovery","identity":"ipcraft.host","version":"1"},
        "undo-redo": {"kind":"undo-redo","identity":"ipcraft.host","version":"1"},
    }
    for source_name, source in sources.items():
        source_operations: list[dict[str, Any]] = []
        if source_name == "user-command":
            source_operations = [copy.deepcopy(json.loads((contracts / "fixtures/valid/patch-operation-createEntity.json").read_text())["operations"][0])]
        if source_name in {"application-reconcile", "application-migration"}:
            source_operations.append({
                "op":"updateEntity", "entityKind":"topology", "id":valid["ipcraft.project-design.v1"]["topologies"][0]["id"],
                "set":{"derivation":copy.deepcopy(valid["ipcraft.project-design.v1"]["topologies"][0]["derivation"])}, "unset":[],
            })
        if source_name == "application-migration":
            source_operations.insert(0, {
                "op":"updateEntity", "entityKind":"project", "id":valid["ipcraft.project-design.v1"]["id"],
                "set":{"dependencies":copy.deepcopy(valid["ipcraft.project-design.v1"]["dependencies"])}, "unset":[],
            })
        if source_name in {"default-engine", "extension-provider"}:
            source_operations = [{
                "op":"createEntity", "entityKind":"router", "localRef":"authority:router.0.0",
                "value":{"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":0,"column":0},"properties":{}},
            }]
        if source_name in {"recovery", "undo-redo"}:
            source_operations = [{"op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"name":"Restored"},"unset":[]}]
        add(f"patch-source-{source_name}", "ipcraft.patch.v1", patch_with(source, source_operations), "accept", "core-semantic", None, None)

    precondition_patch = patch_with(sources["user-command"], [])
    precondition_patch["preconditions"] = [
        {"kind":"entity-absent","entityKind":"interface","id":"interface.new"},
        {"kind":"entity-exists","entityKind":"component","id":"component.noc"},
        {"kind":"property-equals","entityKind":"project","id":"project.mesh","property":"name","value":"Mesh"},
        {"kind":"relation-exists","relationKind":"domain-membership","id":"membership.0"},
        {"kind":"slot-unoccupied","slotId":"slot.0.0"},
    ]
    add("patch-preconditions-all", "ipcraft.patch.v1", precondition_patch, "accept", "core-semantic", None, None)
    package_ownership_witnesses = {
        "user-entity": patch_with(sources["user-command"], [{"op":"createEntity","entityKind":"package-entity","localRef":"application:000010","value":{"typeKey":"vendor.user-entity","data":{},"extensions":[]}}]),
        "engine-entity": patch_with(sources["default-engine"], [{"op":"createEntity","entityKind":"package-entity","localRef":"authority:engine-entity","value":{"typeKey":"vendor.engine-entity","data":{},"extensions":[]}}]),
        "user-relation": patch_with(sources["user-command"], [{"op":"createRelation","relationKind":"package-relation","localRef":"application:000011","value":{"typeKey":"vendor.user-relation","sources":[],"targets":[],"data":{},"extensions":[]}}]),
        "engine-relation": patch_with(sources["default-engine"], [{"op":"createRelation","relationKind":"package-relation","localRef":"authority:engine-relation","value":{"typeKey":"vendor.engine-relation","sources":[],"targets":[],"data":{},"extensions":[]}}]),
    }
    for name, witness in package_ownership_witnesses.items():
        add(f"patch-package-ownership-{name}", "ipcraft.patch.v1", witness, "accept", "core-semantic", None, None)

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
    digest_input = {key: value for key, value in bundle_max.items() if key != "manifestDigest"}
    bundle_max["manifestDigest"] = "sha256:" + hashlib.sha256(json.dumps(digest_input, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
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
    authority_domain = patch_with({"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1","bundleDigest":DIGEST_A}, [{"op":"updateEntity","entityKind":"domain","id":"domain.default","set":{"name":"Changed"},"unset":[]}])
    semantic_cases.append(("patch-authority-mutates-domain", "ipcraft.patch.v1", authority_domain, "ownership", "patch.ownership_violation"))
    reconcile_component = patch_with({"kind":"application-reconcile","identity":"ipcraft.host","version":"1"}, [{"op":"updateEntity","entityKind":"component","id":"component.noc","set":{"config":{}},"unset":[]}])
    semantic_cases.append(("patch-reconcile-mutates-component", "ipcraft.patch.v1", reconcile_component, "ownership", "patch.ownership_violation"))
    reconcile_missing = patch_with({"kind":"application-reconcile","identity":"ipcraft.host","version":"1"}, [])
    semantic_cases.append(("patch-reconcile-missing-derivation", "ipcraft.patch.v1", reconcile_missing, "patch-invariant", "patch.invariant_violation"))
    migration_missing = patch_with({"kind":"application-migration","identity":"ipcraft.host","version":"1"}, [{
        "op":"updateEntity","entityKind":"topology","id":"topology.mesh",
        "set":{"derivation":copy.deepcopy(valid["ipcraft.project-design.v1"]["topologies"][0]["derivation"])},"unset":[],
    }])
    semantic_cases.append(("patch-migration-missing-dependencies", "ipcraft.patch.v1", migration_missing, "patch-invariant", "patch.invariant_violation"))
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
        "kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1","bundleDigest":DIGEST_A,
    })
    semantic_cases.append(("patch-authority-applicability-mismatch", "ipcraft.patch.v1", authority_mismatch, "structure-authority", "patch.authority_conflict"))
    for relation_kind, required_name in (("domain-membership", "domainRef"), ("package-relation", "sources"), ("attachment", "interfaceRef")):
        relation_unset = copy.deepcopy(patch)
        relation_unset["operations"] = [{"op":"updateRelation","relationKind":relation_kind,"id":f"relation.{relation_kind}","set":{},"unset":[required_name]}]
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
        "maximumShape": ["fixtures/valid/project-design-maximum.json"],
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
        "ipcraft.project-design.v1": {"maximumShape":{"minimumArrayLengths":{"/dependencies":7,"/topologies/0/routers":4},"requiredPointers":["/interfaces/0","/topologies/0/packageEntities/0","/topologies/0/packageRelations/0"],"discriminatorCoverage":{"/dependencies/*/kind":["default-engine","noc-package","interface-contract","extension-provider","drc-tool","generator-tool","runtime"]}}},
        "ipcraft.recovery.v1": {"maximumShape":{"minimumArrayLengths":{"/draftOverlay":1,"/draftUndo":1,"/draftRedo":1},"requiredPointers":["/pendingTopologyGroup/groupId"]}},
        "ipcraft.step-result.v1": {"maximumShape":{"discriminatorCoverage":{"/status":["succeeded","failed","cancelled","timed-out","skipped"],"/stepKind":["host","external-tool"]}}},
        "ipcraft.tool-input.v1": {"maximumShape":{"minimumArrayLengths":{"/dependencies":2},"discriminatorCoverage":{"/kind":["semantic-drc","generator"]},"requiredPointers":["/outputDirectory"]}},
        "ipcraft.tool-result.v1": {"maximumShape":{"discriminatorCoverage":{"/status":["succeeded","failed"]},"requiredPointers":["/diagnosticReport","/artifactManifest","/failure/code"]}}
    }
    write_json(contracts / "fixture-coverage-v1.json", {"schema": "ipcraft.fixture-coverage.v1", "roots": coverage, "requirements": requirements})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contracts-root", type=Path, default=CONTRACTS)
    args = parser.parse_args()
    generate(args.contracts_root.resolve())


if __name__ == "__main__":
    main()
