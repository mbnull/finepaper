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
    artifact = copy.deepcopy(valid["ipcraft.artifact-manifest.v1"]); artifact["artifacts"] = [{"path":"a","kind":"rtl","mediaType":"text/plain","size":0,"digest":DIGEST_A}]*2
    semantic_cases.append(("artifact-duplicate-path", "ipcraft.artifact-manifest.v1", artifact, "tool-artifact", "tool.artifact_invalid"))
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
    engine = copy.deepcopy(valid["ipcraft.engine-bundle.v1"]); engine["engineHostContractVersion"] = "ipcraft.engine-host.v999"
    semantic_cases.append(("engine-host-contract", "ipcraft.engine-bundle.v1", engine, "engine-host-contract", "engine.host_contract_unsupported"))
    platform = copy.deepcopy(valid["ipcraft.engine-bundle.v1"]); platform["supportedPlatformAbis"] = ["unsupported-abi"]
    semantic_cases.append(("engine-platform", "ipcraft.engine-bundle.v1", platform, "engine-platform", "engine.platform_unsupported"))
    recovery = copy.deepcopy(valid["ipcraft.recovery.v1"]); recovery["projectId"] = "different-project"
    semantic_cases.append(("recovery-binding", "ipcraft.recovery.v1", recovery, "recovery-binding", "recovery.binding_mismatch"))
    pipeline = copy.deepcopy(valid["ipcraft.pipeline-result.v1"]); pipeline["status"]="failed"; pipeline["failedStepId"]="missing-step"
    semantic_cases.append(("pipeline-failed-step-binding", "ipcraft.pipeline-result.v1", pipeline, "pipeline-result", "pipeline.result_invalid"))
    tool_input = copy.deepcopy(valid["ipcraft.tool-input.v1"]); tool_input.update({"kind":"generator","formallySavedProjectDigest":DIGEST_B,"outputDirectory":"output"})
    semantic_cases.append(("tool-input-generator-snapshot-binding", "ipcraft.tool-input.v1", tool_input, "tool-input", "tool.input_invalid"))
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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contracts-root", type=Path, default=CONTRACTS)
    args = parser.parse_args()
    generate(args.contracts_root.resolve())


if __name__ == "__main__":
    main()
