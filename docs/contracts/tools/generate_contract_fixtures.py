#!/usr/bin/env python3
"""Generate the deterministic Gate 0 standalone fixture corpus."""

from __future__ import annotations

import copy
import argparse
import hashlib
import json
import shutil
from pathlib import Path
from typing import Any

import verify_contract_fixtures as verifier


CONTRACTS = Path(__file__).resolve().parents[1]
DIGEST_A = "sha256:" + "a" * 64
DIGEST_B = "sha256:" + "b" * 64


class SampleBuilder:
    def __init__(self, contracts: Path):
        self.validator = verifier.Draft202012Subset(contracts)

    def root(self, schema_id: str) -> Any:
        schema, path = self.validator.schemas[schema_id]
        value = self.build(schema, schema, path)
        return self.satisfy(schema, value, schema, path)

    def build(self, schema: dict[str, Any], root: dict[str, Any], root_path: Path) -> Any:
        if "$ref" in schema:
            target, target_root, target_path = self.validator._resolve(schema["$ref"], root, root_path)
            return self.build(target, target_root, target_path)
        if "const" in schema: return copy.deepcopy(schema["const"])
        if "enum" in schema: return copy.deepcopy(schema["enum"][0])
        if "oneOf" in schema:
            nullable = next((branch for branch in schema["oneOf"] if branch.get("type") == "null"), None)
            return self.build(nullable if nullable is not None else schema["oneOf"][0], root, root_path)
        if "anyOf" in schema: return self.build(schema["anyOf"][0], root, root_path)
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
                result[member] = self.build(schema.get("properties", {}).get(member, {}), root, root_path)
            return self.satisfy(schema, result, root, root_path)
        if declared == "array":
            result = [self.build(schema.get("items", {}), root, root_path) for _ in range(schema.get("minItems", 0))]
            if "contains" in schema:
                required = schema.get("minContains", 1)
                while sum(self.validator._is_valid(schema["contains"], item, root, root_path) for item in result) < required:
                    result.append(self.build(schema["contains"], root, root_path))
            return result
        if declared == "string":
            pattern = schema.get("pattern", "")
            if "sha256:" in pattern: return DIGEST_A
            if "authority:" in pattern: return "authority:000001"
            if schema.get("format") == "date-time": return "2026-07-14T00:00:00Z"
            if "\\.json" in pattern: return "fixture.json"
            return "value"
        if declared == "integer": return max(0, schema.get("minimum", 0))
        if declared == "number": return max(0, schema.get("minimum", 0))
        if declared == "boolean": return False
        if declared == "null": return None
        raise RuntimeError(f"unsupported sample type {declared!r}")

    def merge(self, target: Any, addition: Any) -> Any:
        if isinstance(target, dict) and isinstance(addition, dict):
            for key, value in addition.items():
                if key not in target: target[key] = value
                elif isinstance(target[key], dict) and isinstance(value, dict): self.merge(target[key], value)
            return target
        return target

    def satisfy(self, schema: dict[str, Any], value: Any, root: dict[str, Any], root_path: Path) -> Any:
        if "$ref" in schema:
            target, target_root, target_path = self.validator._resolve(schema["$ref"], root, root_path)
            value = self.satisfy(target, value, target_root, target_path)
        for branch in schema.get("allOf", []):
            selected = branch
            if "if" in branch:
                selected = branch.get("then", {}) if self.validator._is_valid(branch["if"], value, root, root_path) else branch.get("else", {})
            addition = self.build(selected, root, root_path)
            value = self.merge(value, addition)
            value = self.satisfy(selected, value, root, root_path)
        if isinstance(value, dict):
            for member in schema.get("required", []):
                if member not in value:
                    value[member] = self.build(schema.get("properties", {}).get(member, {}), root, root_path)
            for member, member_schema in schema.get("properties", {}).items():
                if member in value:
                    value[member] = self.satisfy(member_schema, value[member], root, root_path)
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
        project["interfaces"] = [{"id":"interface.axi","ownerComponentId":"component.noc","templateKey":"axi-boundary","name":"AXI Boundary","contract":{"lockId":"dep.contract.axi5","role":"initiator"},"capabilities":{},"contractConfig":{},"nocConfig":{},"extensions":[]}]
        topology["attachments"] = [{"id":"attachment.axi","interfaceId":"interface.axi","state":"resolved","routerId":"router.0.0","slotId":"slot.0.0"}]
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
        relative = f"fixtures/{'valid' if expected == 'accept' else 'invalid'}/{name}.json"
        write_json(contracts / relative, document)
        entries.append({
            "path": relative, "schemaId": schema_id, "validationPhase": phase,
            "failureBoundary": boundary, "expected": expected, "errorCode": code,
            "behaviorEvidence": evidence,
        })

    for schema_id in schema_ids:
        document = builder.root(schema_id)
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
            document["topology"].update({"rowField":"rows","columnField":"columns"})
        if schema_id == "ipcraft.project-design.v1":
            document["dependencies"] = [
                {"lockId":"dep.engine.default","kind":"default-engine","id":"ipcraft.default-noc-engine","version":"1.0.0","bundleManifestDigest":DIGEST_A,"engineHostContractVersion":"ipcraft.engine-host.v1","engineCompatibilityVersion":"1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1","supportedPlatformAbis":["linux-x86_64-gnu-v1"]},
                {"lockId":"dep.noc.simple","kind":"noc-package","id":"simple-noc","version":"1.0.0","bundleManifestDigest":DIGEST_B},
            ]
            document["components"][0].update({"id":"component.noc","packageLockId":"dep.noc.simple"})
            document["topologies"][0].update({"id":"topology.mesh","ownerComponentId":"component.noc"})
            derivation = document["topologies"][0]["derivation"]
            derivation.update({"defaultEngineLockId":"dep.engine.default","defaultEngineBundleDigest":DIGEST_A,"engineHostContractVersion":"ipcraft.engine-host.v1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1"})
            derivation["structureAuthority"].update({"lockId":"dep.engine.default","identity":"ipcraft.default-noc-engine","bundleDigest":DIGEST_A})
        if schema_id == "ipcraft.recovery.v1":
            project = builder.root("ipcraft.project-design.v1")
            project["dependencies"] = [
                {"lockId":"dep.engine.default","kind":"default-engine","id":"ipcraft.default-noc-engine","version":"1.0.0","bundleManifestDigest":DIGEST_A,"engineHostContractVersion":"ipcraft.engine-host.v1","engineCompatibilityVersion":"1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1","supportedPlatformAbis":["linux-x86_64-gnu-v1"]},
                {"lockId":"dep.noc.simple","kind":"noc-package","id":"simple-noc","version":"1.0.0","bundleManifestDigest":DIGEST_B},
            ]
            project["components"][0].update({"id":"component.noc","packageLockId":"dep.noc.simple"})
            project["topologies"][0].update({"id":"topology.mesh","ownerComponentId":"component.noc"})
            project["topologies"][0]["derivation"]["structureAuthority"].update({"lockId":"dep.engine.default","identity":"ipcraft.default-noc-engine","bundleDigest":DIGEST_A})
            document["authoritativeDesign"] = project
            document["projectId"] = project["id"]
        if schema_id == "ipcraft.noc-side-effects.v1":
            document["input"]["authorityPatch"]["source"].update({"identity":"ipcraft.default-noc-engine","bundleDigest":DIGEST_A})
            document["expected"]["applicationPatch"]["source"] = {"kind":"application-reconcile","identity":"ipcraft.host","version":"1"}
        valid[schema_id] = document
        add(schema_id.removeprefix("ipcraft.").removesuffix(".v1").replace(".", "-"), schema_id,
            document, "accept", "schema", None, None)

    for protocol in ("axi5", "ace", "chi"):
        contract = copy.deepcopy(valid["ipcraft.interface-contract.v1"])
        contract.update({"id":f"amba.{protocol}","name":protocol.upper(),"version":"1.0"})
        contract["roles"] = [{"key":"initiator","label":"Initiator"},{"key":"target","label":"Target"}]
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
    add("project-mesh-1x1", "ipcraft.project-design.v1", project, "accept", "core-semantic", None, None)
    project_2x2 = mesh_project(valid["ipcraft.project-design.v1"], 2, 2, True)
    add("project-mesh-2x2-interface-attachment-domain", "ipcraft.project-design.v1", project_2x2, "accept", "core-semantic", None, None)
    degraded = copy.deepcopy(project); degraded["id"] = "project.degraded"; degraded["name"] = "Exact Engine Missing"
    add("project-exact-engine-unavailable", "ipcraft.project-design.v1", degraded, "accept", "core-semantic", None, None,
        "vectors/default-engine-lock-v1.json#engine-lock-missing")
    duplicate = copy.deepcopy(project); duplicate["topologies"][0]["id"] = duplicate["components"][0]["id"]
    add("project-duplicate-id", "ipcraft.project-design.v1", duplicate, "reject", "core-semantic", "project-duplicate-id", "project.duplicate_id")
    unknown = copy.deepcopy(project_2x2); unknown["topologies"][0]["attachments"][0]["slotId"] = "slot.missing"
    add("project-unknown-attachment-slot", "ipcraft.project-design.v1", unknown, "reject", "core-semantic", "project-reference", "project.unknown_reference")
    uncovered = copy.deepcopy(project_2x2); uncovered["topologies"][0]["domainMemberships"].pop()
    add("project-incomplete-domain-coverage", "ipcraft.project-design.v1", uncovered, "reject", "core-semantic", "project-invariant", "project.invariant_violation")
    disconnected = copy.deepcopy(project_2x2)
    disconnected["topologies"][0]["structuralLinks"] = [link for link in disconnected["topologies"][0]["structuralLinks"] if link["axis"] == "horizontal"]
    add("project-disconnected-domain", "ipcraft.project-design.v1", disconnected, "reject", "core-semantic", "project-invariant", "project.invariant_violation")
    legacy = {"project": {"id": "legacy"}, "instances": []}
    add("project-legacy-root", "ipcraft.project-design.v1", legacy, "reject", "schema", "legacy-project-root", "project.legacy_format_unsupported")

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
