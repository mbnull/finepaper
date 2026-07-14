#!/usr/bin/env python3
"""Schema-backed witnesses for exact Engine resolution, migration, and side effects."""

from __future__ import annotations

import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_canonical_vectors import SchemaWorld  # noqa: E402


ROOT = Path(__file__).resolve().parents[3]
CONTRACTS = ROOT / "docs" / "contracts"
DIGEST_A = "sha256:" + "a" * 64
DIGEST_B = "sha256:" + "b" * 64
DIGEST_C = "sha256:" + "c" * 64


def engine_lock(digest: str = DIGEST_A, lock_id: str = "dep.default-engine") -> dict:
    return {
        "lockId": lock_id, "kind": "default-engine", "id": "ipcraft.default-noc-engine",
        "version": "1.0.0", "bundleManifestDigest": digest,
        "engineCompatibilityVersion": "1", "engineHostContractVersion": "ipcraft.engine-host.v1",
        "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
        "supportedPlatformAbis": ["linux-x86_64-gnu-v1"],
    }


def engine_manifest() -> dict:
    return {
        "schema": "ipcraft.engine-bundle.v1", "id": "ipcraft.default-noc-engine",
        "version": "1.0.0", "engineHostContractVersion": "ipcraft.engine-host.v1",
        "engineCompatibilityVersion": "1", "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
        "migrationFromCompatibilityVersions": ["1"],
        "supportedPlatformAbis": ["linux-x86_64-gnu-v1"],
        "entrypoint": "lib/libipcraft_noc_engine.so",
    }


def resolve(lock: dict, manifest: dict, *, installed: bool = True, installed_digest: str = DIGEST_A,
            revoked: bool = False, platform: str = "linux-x86_64-gnu-v1",
            supported_host_contracts=frozenset({"ipcraft.engine-host.v1"}),
            supported_side_effect_contracts=frozenset({"ipcraft.noc-side-effects.v1"})) -> tuple[str, str | None, str | None]:
    if not installed:
        return "degraded-inspect", "engine.bundle_missing", None
    if revoked:
        return "degraded-inspect", "engine.bundle_revoked", None
    if installed_digest != lock["bundleManifestDigest"]:
        return "degraded-inspect", "engine.bundle_mismatch", None
    metadata = ("id", "version", "engineHostContractVersion", "hostSideEffectContractVersion", "engineCompatibilityVersion", "supportedPlatformAbis")
    if any(manifest[field] != lock[field] for field in metadata):
        return "degraded-inspect", "engine.bundle_mismatch", None
    if platform not in manifest["supportedPlatformAbis"]:
        return "degraded-inspect", "engine.platform_unsupported", None
    if manifest["engineHostContractVersion"] not in supported_host_contracts:
        return "degraded-inspect", "engine.host_contract_unsupported", None
    if manifest["hostSideEffectContractVersion"] not in supported_side_effect_contracts:
        return "degraded-inspect", "host.side_effect_contract_unsupported", None
    return "exact", None, lock["bundleManifestDigest"]


def upgrade_overlay(resolution: tuple[str, str | None, str | None], newer_digest: str | None) -> tuple[bool, str | None]:
    return resolution[0] == "exact" and newer_digest is not None, resolution[2]


def freshness_reason(lock: dict, supported_host: set[str], supported_side_effect: set[str]) -> str | None:
    if lock["engineHostContractVersion"] not in supported_host or lock["hostSideEffectContractVersion"] not in supported_side_effect:
        return "dependency-changed"
    return None


def applicability(digest: str = DIGEST_A) -> dict:
    return {
        "schema": "ipcraft.reconcile-applicability.v1", "groupId": "group.migration",
        "requestGeneration": 1, "topologyInputRevision": 1, "topologyInputDigest": DIGEST_A,
        "baseDerivedStateRevision": 1, "baseDerivedStateDigest": DIGEST_B,
        "baseAuthoritativeDesignDigest": DIGEST_C,
        "structureAuthority": {"kind": "default-engine", "lockId": "dep.default-engine", "identity": "ipcraft.default-noc-engine", "version": "1.0.0", "bundleDigest": digest},
        "packageBundleDigest": DIGEST_C, "reconcileDependencySetDigest": DIGEST_A,
        "defaultEngineLockId": "dep.default-engine", "defaultEngineBundleDigest": digest,
        "engineHostContractVersion": "ipcraft.engine-host.v1",
        "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
    }


def topology_intent() -> dict:
    return {"schema": "ipcraft.topology-intent.v1", "componentId": "component.noc", "topologyId": "topology.main", "topologyKind": "mesh", "globalConfig": {}, "packageEntities": [], "packageRelations": []}


def normalized_input(host_contract: str = "ipcraft.engine-host.v1") -> dict:
    template = {"stableKey": "mesh-router", "identityCompatibilityVersion": 1, "properties": {}}
    return {
        "schema": "ipcraft.normalized-topology-input.v1", "intent": topology_intent(),
        "mesh": {"rows": 1, "columns": 1, "routerTemplate": template, "linkTemplate": {**template, "stableKey": "mesh-link"}, "slotTemplates": []},
        "dependencyContext": {"nocPackageLockId": "dep.noc", "nocPackageBundleDigest": DIGEST_C, "defaultEngineLockId": "dep.default-engine", "defaultEngineBundleDigest": DIGEST_A, "engineHostContractVersion": host_contract, "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1", "structureAuthority": {"kind": "default-engine", "lockId": "dep.default-engine", "identity": "ipcraft.default-noc-engine", "version": "1.0.0", "bundleDigest": DIGEST_A}, "reconcileDependencySetDigest": DIGEST_A},
    }


def recovery_migration_group() -> dict:
    return {
        "groupId": "group.migration", "kind": "default-engine-migration",
        "topologyInputRevision": 2, "requestGeneration": 1,
        "baseAuthoritativeDesignDigest": DIGEST_C, "baseDerivedStateRevision": 1,
        "baseDerivedStateDigest": DIGEST_B, "topologyIntent": topology_intent(),
        "intentUndo": [], "intentRedo": [], "normalizedTopologyInput": normalized_input(),
        "topologyInputDigest": DIGEST_A, "status": "drafting", "candidate": None,
        "defaultEngineLockId": "dep.default-engine", "currentDefaultEngineBundleDigest": DIGEST_A,
        "targetDefaultEngineBundleDigest": DIGEST_B,
        "targetEngineHostContractVersion": "ipcraft.engine-host.v999",
        "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v999",
    }


def derivation(target: dict) -> dict:
    return {
        "topologyInputRevision": 2, "topologyInputDigest": DIGEST_B,
        "derivedStateRevision": 2, "derivedStateDigest": DIGEST_C,
        "packageBundleDigest": DIGEST_C, "reconcileDependencySetDigest": DIGEST_B,
        "defaultEngineLockId": target["lockId"], "defaultEngineBundleDigest": target["bundleManifestDigest"],
        "engineHostContractVersion": target["engineHostContractVersion"],
        "hostSideEffectContractVersion": target["hostSideEffectContractVersion"],
        "structureAuthority": {"kind": "default-engine", "lockId": target["lockId"], "identity": target["id"], "version": target["version"], "bundleDigest": target["bundleManifestDigest"]},
        "engineCompatibilityVersion": target["engineCompatibilityVersion"],
    }


def migration_candidate() -> dict:
    current, target = engine_lock(DIGEST_A), engine_lock(DIGEST_B)
    dependencies = [
        {"lockId": "dep.noc", "kind": "noc-package", "id": "vendor.noc", "version": "1", "bundleManifestDigest": DIGEST_C},
        target,
    ]
    return {
        "schema": "ipcraft.candidate-transaction.v1", "transactionId": "tx.migration",
        "kind": "default-engine-migration", "applicability": applicability(DIGEST_A),
        "topologyIntent": topology_intent(),
        "authorityPatch": {"patchId": "patch.target-engine", "source": {"kind": "default-engine", "identity": "ipcraft.default-noc-engine", "version": target["version"], "bundleDigest": target["bundleManifestDigest"]}, "operations": []},
        "applicationPatch": {"patchId": "patch.migration", "source": {"kind": "application-migration", "identity": "host", "version": "1"}, "operations": [
            {"op": "updateEntity", "entityKind": "project", "id": "project.main", "set": {"dependencies": dependencies}, "unset": []},
            {"op": "updateEntity", "entityKind": "topology", "id": "topology.main", "set": {"derivation": derivation(target)}, "unset": []},
        ]},
        "migration": {"currentDefaultEngineLock": current, "targetDefaultEngineLock": target},
        "tombstones": [], "allocationOrder": [],
        "impactReport": {"schema": "ipcraft.topology-impact-report.v1", "impacts": [{"code": "engine_migration.dependency_replaced", "severity": "warning", "dataLoss": False, "subjects": [{"kind": "project", "id": "project.main"}], "details": {}, "resolution": "confirm-or-discard"}]},
        "candidateDigest": DIGEST_C,
    }


def migration_semantic_errors(candidate: dict) -> list[str]:
    errors = []
    current = candidate["migration"]["currentDefaultEngineLock"]
    target = candidate["migration"]["targetDefaultEngineLock"]
    if current["lockId"] != target["lockId"]: errors.append("lockId")
    if current["bundleManifestDigest"] == target["bundleManifestDigest"]: errors.append("digest")
    operations = candidate["applicationPatch"]["operations"]
    project_update = next((op for op in operations if op.get("entityKind") == "project" and "dependencies" in op.get("set", {})), None)
    topology_update = next((op for op in operations if op.get("entityKind") == "topology" and "derivation" in op.get("set", {})), None)
    if project_update is None or target not in project_update["set"]["dependencies"]: errors.append("target-dependency")
    expected_derivation = derivation(target)
    if topology_update is None or topology_update["set"]["derivation"] != expected_derivation: errors.append("target-derivation")
    impacts = candidate["impactReport"]["impacts"]
    if not any(impact["code"] == "engine_migration.dependency_replaced" for impact in impacts): errors.append("confirmation-impact")
    if disposition_for_impacts(impacts) != ("ready-to-commit", True, "confirmation-required"):
        errors.append("confirmation-disposition")
    return errors


def disposition_for_impacts(impacts: list[dict]) -> tuple[str, bool, str]:
    codes = {impact["code"] for impact in impacts}
    if "package_relation.endpoint_blocks_candidate" in codes:
        return "blocked", False, "blocked"
    if codes & {"domain.non_default_deleted", "engine_migration.dependency_replaced"}:
        return "ready-to-commit", True, "confirmation-required"
    return "auto-commit", False, "auto-commit"


IMPACTS = [
    ("attachment.target_removed", "warning", False, "reattach-or-detach", "auto-commit", False, "auto-commit"),
    ("domain.non_default_deleted", "warning", True, "confirm-or-discard", "ready-to-commit", True, "confirmation-required"),
    ("domain.disconnected", "error", False, "repair-domain", "auto-commit", False, "auto-commit"),
    ("package_relation.endpoint_unresolved", "warning", False, "reattach-or-delete-relation", "auto-commit", False, "auto-commit"),
    ("package_relation.endpoint_blocks_candidate", "error", False, "discard-and-repair", "blocked", False, "blocked"),
    ("engine_migration.dependency_replaced", "warning", False, "confirm-or-discard", "ready-to-commit", True, "confirmation-required"),
]


def source_patch(kind: str, operations: list[dict] | None = None) -> dict:
    source = {"kind": kind, "identity": "host" if kind.startswith("application-") else "ipcraft.default-noc-engine", "version": "1"}
    if kind == "default-engine": source["bundleDigest"] = DIGEST_A
    return {"patchId": "patch." + kind, "source": source, "operations": operations or []}


def side_effect_example(disposition: tuple) -> dict:
    code, severity, data_loss, resolution, group_state, confirmation, commit = disposition
    diagnostics = []
    if code == "domain.disconnected":
        diagnostics.append({"ruleId": "domain.disconnected", "severity": "error", "message": "Domain is disconnected", "blocking": True, "subjects": [{"kind": "domain", "id": "domain.main"}], "properties": []})
    return {
        "schema": "ipcraft.noc-side-effects.v1", "contractVersion": "ipcraft.noc-side-effects.v1",
        "input": {"domainTypes": [], "relationDeclarations": [], "currentDerivedState": {"schema": "ipcraft.derived-state.v1", "topologyId": "topology.main", "routers": [], "structuralLinks": [], "accessSlots": [], "packageEntities": [], "packageRelations": []}, "domains": [], "domainMemberships": [], "attachments": [], "packageRelations": [], "authorityPatch": source_patch("default-engine")},
        "expected": {"applicationPatch": source_patch("application-reconcile"), "tombstones": [], "allocationOrder": [], "impactReport": {"schema": "ipcraft.topology-impact-report.v1", "impacts": [{"code": code, "severity": severity, "dataLoss": data_loss, "subjects": [{"kind": "project", "id": "project.main"}], "details": {}, "resolution": resolution}]}, "coreDiagnostics": diagnostics, "groupState": group_state, "requiresConfirmation": confirmation, "commitDisposition": commit},
    }


def assert_valid(world: SchemaWorld, schema_id: str, schema: dict, value: dict, name: str) -> None:
    world.validate(schema_id, schema, value, name)


def assert_invalid(world: SchemaWorld, schema_id: str, schema: dict, value: dict, name: str) -> None:
    if world.is_valid(schema_id, schema, value, name):
        raise AssertionError(f"{name} unexpectedly validated")


def main() -> int:
    world = SchemaWorld(CONTRACTS)
    project = world.documents["ipcraft.project-design.v1"]["$defs"]
    core_doc = world.documents["ipcraft.core-canonical-models.v1"]
    core = core_doc["$defs"]
    side_schema = world.documents["ipcraft.noc-side-effects.v1"]
    patch_schema = world.documents["ipcraft.patch.v1"]
    recovery = world.documents["ipcraft.recovery.v1"]["$defs"]

    assert_valid(world, "ipcraft.project-design.v1", project["defaultEngineDependencyLock"], engine_lock(), "engine lock")
    assert_valid(world, "ipcraft.engine-bundle.v1", world.documents["ipcraft.engine-bundle.v1"], engine_manifest(), "engine manifest")
    wrong_id_lock = engine_lock(); wrong_id_lock["id"] = "vendor.other-engine"
    assert_invalid(world, "ipcraft.project-design.v1", project["defaultEngineDependencyLock"], wrong_id_lock, "Default Engine lock metadata ID")
    wrong_id_manifest = engine_manifest(); wrong_id_manifest["id"] = "vendor.other-engine"
    assert_invalid(world, "ipcraft.engine-bundle.v1", world.documents["ipcraft.engine-bundle.v1"], wrong_id_manifest, "Default Engine manifest metadata ID")
    unsupported_lock = engine_lock(); unsupported_lock["engineHostContractVersion"] = "ipcraft.engine-host.v999"
    assert_valid(world, "ipcraft.project-design.v1", project["defaultEngineDependencyLock"], unsupported_lock, "unsupported lock parses")
    assert_valid(world, "ipcraft.recovery.v1", recovery["pendingTopologyGroup"], recovery_migration_group(), "unsupported recovery versions parse")

    manifest = engine_manifest()
    outcomes = [
        resolve(engine_lock(), manifest),
        resolve(engine_lock(), manifest, installed=False),
        resolve(engine_lock(), manifest, revoked=True),
        resolve(engine_lock(), manifest, installed_digest=DIGEST_B),
    ]
    metadata_mismatch = copy.deepcopy(manifest); metadata_mismatch["version"] = "1.0.1"
    outcomes.extend([
        resolve(engine_lock(), metadata_mismatch),
        resolve(engine_lock(), manifest, platform="other-abi"),
        resolve(unsupported_lock, {**manifest, "engineHostContractVersion": "ipcraft.engine-host.v999"}),
        resolve({**engine_lock(), "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v999"}, {**manifest, "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v999"}),
        resolve(engine_lock(DIGEST_B), manifest, installed_digest=DIGEST_A),
    ])
    assert [outcome[:2] for outcome in outcomes] == [
        ("exact", None), ("degraded-inspect", "engine.bundle_missing"),
        ("degraded-inspect", "engine.bundle_revoked"), ("degraded-inspect", "engine.bundle_mismatch"),
        ("degraded-inspect", "engine.bundle_mismatch"), ("degraded-inspect", "engine.platform_unsupported"),
        ("degraded-inspect", "engine.host_contract_unsupported"),
        ("degraded-inspect", "host.side_effect_contract_unsupported"),
        ("degraded-inspect", "engine.bundle_mismatch"),
    ]
    for field, value in {
        "id": "vendor.other-engine", "version": "2.0.0",
        "engineHostContractVersion": "ipcraft.engine-host.v2",
        "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v2",
        "engineCompatibilityVersion": "2", "supportedPlatformAbis": ["other-abi"],
    }.items():
        mismatched = copy.deepcopy(manifest); mismatched[field] = value
        assert resolve(engine_lock(), mismatched)[:2] == ("degraded-inspect", "engine.bundle_mismatch")
    store_retained = True
    retained = resolve(engine_lock(), manifest, installed=store_retained, platform="other-abi")
    assert store_retained and retained[0] == "degraded-inspect" and retained[1] == "engine.platform_unsupported"
    overlay, selected = upgrade_overlay(resolve(engine_lock(), manifest), DIGEST_B)
    assert overlay and selected == DIGEST_A
    assert freshness_reason(unsupported_lock, {"ipcraft.engine-host.v1"}, {"ipcraft.noc-side-effects.v1"}) == "dependency-changed"
    unsupported_side = engine_lock(); unsupported_side["hostSideEffectContractVersion"] = "ipcraft.noc-side-effects.v999"
    assert freshness_reason(unsupported_side, {"ipcraft.engine-host.v1"}, {"ipcraft.noc-side-effects.v1"}) == "dependency-changed"

    candidate = migration_candidate()
    assert_valid(world, "ipcraft.core-canonical-models.v1", core["candidateTransaction"], candidate, "complete migration candidate")
    assert migration_semantic_errors(candidate) == []
    schema_negatives = []
    empty = copy.deepcopy(candidate); empty["applicationPatch"]["operations"] = []; schema_negatives.append(("empty migration application Patch", empty))
    no_project = copy.deepcopy(candidate); no_project["applicationPatch"]["operations"].pop(0); schema_negatives.append(("missing project dependency update", no_project))
    no_topology = copy.deepcopy(candidate); no_topology["applicationPatch"]["operations"].pop(1); schema_negatives.append(("missing topology derivation update", no_topology))
    no_impact = copy.deepcopy(candidate); no_impact["impactReport"]["impacts"] = []; schema_negatives.append(("missing migration impact", no_impact))
    for name, negative in schema_negatives:
        assert_invalid(world, "ipcraft.core-canonical-models.v1", core["candidateTransaction"], negative, name)
    semantic_negatives = []
    wrong_lock = copy.deepcopy(candidate); wrong_lock["migration"]["targetDefaultEngineLock"]["lockId"] = "dep.other"; semantic_negatives.append(("differing lockId", wrong_lock, "lockId"))
    wrong_dependency = copy.deepcopy(candidate); wrong_dependency["applicationPatch"]["operations"][0]["set"]["dependencies"][1] = engine_lock(DIGEST_C); semantic_negatives.append(("wrong dependency target", wrong_dependency, "target-dependency"))
    wrong_derivation = copy.deepcopy(candidate); wrong_derivation["applicationPatch"]["operations"][1]["set"]["derivation"]["defaultEngineBundleDigest"] = DIGEST_C; semantic_negatives.append(("wrong derivation target", wrong_derivation, "target-derivation"))
    for name, negative, expected_error in semantic_negatives:
        assert_valid(world, "ipcraft.core-canonical-models.v1", core["candidateTransaction"], negative, name + " structurally parses")
        assert expected_error in migration_semantic_errors(negative)

    project_update = candidate["applicationPatch"]["operations"][0]
    topology_update = candidate["applicationPatch"]["operations"][1]
    assert_valid(world, "ipcraft.core-canonical-models.v1", core["sourcePatch"], source_patch("application-migration", [project_update, topology_update]), "migration source owns updates")
    assert_valid(world, "ipcraft.core-canonical-models.v1", core["sourcePatch"], source_patch("application-reconcile", [topology_update]), "reconcile owns derivation")
    assert_invalid(world, "ipcraft.core-canonical-models.v1", core["sourcePatch"], source_patch("application-reconcile", [project_update]), "reconcile cannot replace dependencies")
    for source in ("default-engine", "extension-provider"):
        patch = source_patch(source, [project_update, topology_update])
        if source == "extension-provider": patch["source"] = {"kind": source, "identity": "vendor.provider", "version": "1", "bundleDigest": DIGEST_A}
        assert_invalid(world, "ipcraft.core-canonical-models.v1", core["sourcePatch"], patch, source + " cannot author application updates")
    wrong_engine_source = source_patch("default-engine"); wrong_engine_source["source"]["identity"] = "vendor.other-engine"
    assert_invalid(world, "ipcraft.core-canonical-models.v1", core["sourcePatch"], wrong_engine_source, "Default Engine source metadata identity")
    public_base = {"schema": "ipcraft.patch.v1", "transactionId": "tx", "patchId": "patch", "causality": {"sessionRevision": 1}, "preconditions": []}
    for source in ("user-command", "recovery", "undo-redo"):
        envelope = {**public_base, "source": {"kind": source, "identity": "host", "version": "1"}, "operations": [project_update, topology_update]}
        assert_invalid(world, "ipcraft.patch.v1", patch_schema, envelope, source + " public envelope ownership")
    public_authority = {**public_base, "source": {"kind": "default-engine", "identity": "ipcraft.default-noc-engine", "version": "1", "bundleDigest": DIGEST_A}, "applicability": applicability(), "operations": [project_update, topology_update]}
    assert_invalid(world, "ipcraft.patch.v1", patch_schema, public_authority, "Authority public envelope ownership")
    public_reconcile = {**public_base, "source": {"kind": "application-reconcile", "identity": "host", "version": "1"}, "applicability": applicability(), "operations": [topology_update]}
    assert_valid(world, "ipcraft.patch.v1", patch_schema, public_reconcile, "Application reconcile derivation envelope")
    public_reconcile_bad = copy.deepcopy(public_reconcile); public_reconcile_bad["operations"] = [project_update]
    assert_invalid(world, "ipcraft.patch.v1", patch_schema, public_reconcile_bad, "Application reconcile dependency envelope")
    public_migration = {**public_base, "source": {"kind": "application-migration", "identity": "host", "version": "1"}, "applicability": applicability(), "operations": [project_update, topology_update]}
    assert_valid(world, "ipcraft.patch.v1", patch_schema, public_migration, "Application migration public envelope")
    user_name = {**public_base, "source": {"kind": "user-command", "identity": "user", "version": "1"}, "operations": [{"op": "updateEntity", "entityKind": "project", "id": "project.main", "set": {"name": "Renamed"}, "unset": []}]}
    assert_valid(world, "ipcraft.patch.v1", patch_schema, user_name, "ordinary user project name update")
    assert "topology" not in core["createEntityOperation"]["properties"]["entityKind"]["enum"]
    assert "topology" not in core["deleteEntityOperation"]["properties"]["entityKind"]["enum"]

    for disposition in IMPACTS:
        assert_valid(world, "ipcraft.noc-side-effects.v1", side_schema, side_effect_example(disposition), disposition[0])
    blocked_priority = side_effect_example(IMPACTS[4])
    blocked_priority["expected"]["impactReport"]["impacts"].append(side_effect_example(IMPACTS[1])["expected"]["impactReport"]["impacts"][0])
    assert_valid(world, "ipcraft.noc-side-effects.v1", side_schema, blocked_priority, "blocking impact priority")
    confirmation_priority = side_effect_example(IMPACTS[1])
    confirmation_priority["expected"]["impactReport"]["impacts"].append(side_effect_example(IMPACTS[0])["expected"]["impactReport"]["impacts"][0])
    assert_valid(world, "ipcraft.noc-side-effects.v1", side_schema, confirmation_priority, "confirmation impact priority")
    contradictory = []
    blocked = side_effect_example(IMPACTS[4]); blocked["expected"].update(groupState="auto-commit", requiresConfirmation=False, commitDisposition="auto-commit"); contradictory.append(("blocked contradiction", blocked))
    confirm = side_effect_example(IMPACTS[1]); confirm["expected"].update(groupState="auto-commit", requiresConfirmation=False, commitDisposition="auto-commit"); contradictory.append(("confirmation contradiction", confirm))
    automatic = side_effect_example(IMPACTS[0]); automatic["expected"].update(groupState="ready-to-commit", requiresConfirmation=True, commitDisposition="confirmation-required"); contradictory.append(("auto-commit contradiction", automatic))
    disconnected = side_effect_example(IMPACTS[2]); disconnected["expected"]["coreDiagnostics"] = []; contradictory.append(("disconnected missing blocking DRC", disconnected))
    unknown_contract = side_effect_example(IMPACTS[0]); unknown_contract["contractVersion"] = "ipcraft.noc-side-effects.v999"; contradictory.append(("unknown side-effect contract", unknown_contract))
    for name, negative in contradictory:
        assert_invalid(world, "ipcraft.noc-side-effects.v1", side_schema, negative, name)

    print("engine/side-effect witnesses passed: 9 resolution cases, Patch ownership, migration + 7 negatives, 6 dispositions + 5 negatives")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
