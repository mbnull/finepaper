#!/usr/bin/env python3
"""Authoring witnesses for exact Engine resolution, migration Patch, and side effects."""

from __future__ import annotations

import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_canonical_vectors import SchemaWorld, SchemaViolation  # noqa: E402


ROOT = Path(__file__).resolve().parents[3]
CONTRACTS = ROOT / "docs" / "contracts"
DIGEST_A = "sha256:" + "a" * 64
DIGEST_B = "sha256:" + "b" * 64
DIGEST_C = "sha256:" + "c" * 64


def engine_lock(digest: str = DIGEST_A) -> dict:
    return {
        "lockId": "dep.default-engine", "kind": "default-engine",
        "id": "ipcraft.default-noc-engine", "version": "1.0.0",
        "bundleManifestDigest": digest, "engineCompatibilityVersion": "1",
        "engineHostContractVersion": "ipcraft.engine-host.v1",
        "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
        "supportedPlatformAbis": ["linux-x86_64-gnu-v1"],
    }


def engine_manifest() -> dict:
    return {
        "schema": "ipcraft.engine-bundle.v1", "id": "ipcraft.default-noc-engine",
        "version": "1.0.0", "engineHostContractVersion": "ipcraft.engine-host.v1",
        "engineCompatibilityVersion": "1",
        "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
        "migrationFromCompatibilityVersions": ["1"],
        "supportedPlatformAbis": ["linux-x86_64-gnu-v1"],
        "entrypoint": "lib/libipcraft_noc_engine.so",
    }


def resolve(lock: dict, *, installed=True, revoked=False, content_digest=DIGEST_A,
            platform="linux-x86_64-gnu-v1", host="ipcraft.engine-host.v1",
            side_effect="ipcraft.noc-side-effects.v1") -> tuple[str, str | None]:
    if not installed: return "degraded-inspect", "engine.bundle_missing"
    if revoked: return "degraded-inspect", "engine.bundle_revoked"
    if content_digest != lock["bundleManifestDigest"]: return "degraded-inspect", "engine.bundle_mismatch"
    if platform not in lock["supportedPlatformAbis"]: return "degraded-inspect", "engine.platform_unsupported"
    if lock["engineHostContractVersion"] != host: return "degraded-inspect", "engine.host_contract_unsupported"
    if lock["hostSideEffectContractVersion"] != side_effect: return "degraded-inspect", "host.side_effect_contract_unsupported"
    return "exact", None


IMPACTS = [
    ("attachment.target_removed", "warning", False, "reattach-or-detach", "auto-commit", False, "auto-commit"),
    ("domain.non_default_deleted", "warning", True, "confirm-or-discard", "ready-to-commit", True, "confirmation-required"),
    ("domain.disconnected", "error", False, "repair-domain", "auto-commit", False, "auto-commit"),
    ("package_relation.endpoint_unresolved", "warning", False, "reattach-or-delete-relation", "auto-commit", False, "auto-commit"),
    ("package_relation.endpoint_blocks_candidate", "error", False, "discard-and-repair", "blocked", False, "blocked"),
    ("engine_migration.dependency_replaced", "warning", False, "confirm-or-discard", "ready-to-commit", True, "confirmation-required"),
]


def source_patch(kind: str) -> dict:
    source = {"kind": kind, "identity": "host" if kind.startswith("application-") else "ipcraft.default-noc-engine", "version": "1"}
    if kind == "default-engine": source["bundleDigest"] = DIGEST_A
    return {"patchId": "patch." + kind, "source": source, "operations": []}


def side_effect_example(disposition: tuple) -> dict:
    code, severity, data_loss, resolution, group_state, confirmation, commit = disposition
    return {
        "schema": "ipcraft.noc-side-effects.v1",
        "contractVersion": "ipcraft.noc-side-effects.v1",
        "input": {
            "domainTypes": [], "relationDeclarations": [],
            "currentDerivedState": {"schema": "ipcraft.derived-state.v1", "topologyId": "topology.main", "routers": [], "structuralLinks": [], "accessSlots": [], "packageEntities": [], "packageRelations": []},
            "domains": [], "domainMemberships": [], "attachments": [], "packageRelations": [],
            "authorityPatch": source_patch("default-engine"),
        },
        "expected": {
            "applicationPatch": source_patch("application-reconcile"), "tombstones": [], "allocationOrder": [],
            "impactReport": {"schema": "ipcraft.topology-impact-report.v1", "impacts": [{
                "code": code, "severity": severity, "dataLoss": data_loss,
                "subjects": [{"kind": "project", "id": "project.main"}],
                "details": {}, "resolution": resolution,
            }]},
            "coreDiagnostics": [], "groupState": group_state,
            "requiresConfirmation": confirmation, "commitDisposition": commit,
        },
    }


def derivation() -> dict:
    return {
        "topologyInputRevision": 1, "topologyInputDigest": DIGEST_A,
        "derivedStateRevision": 1, "derivedStateDigest": DIGEST_B,
        "packageBundleDigest": DIGEST_C, "reconcileDependencySetDigest": DIGEST_A,
        "defaultEngineLockId": "dep.default-engine", "defaultEngineBundleDigest": DIGEST_A,
        "engineHostContractVersion": "ipcraft.engine-host.v1",
        "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
        "structureAuthority": {"kind": "default-engine", "lockId": "dep.default-engine", "identity": "ipcraft.default-noc-engine", "version": "1.0.0", "bundleDigest": DIGEST_A},
        "engineCompatibilityVersion": "1",
    }


def source_allows_application_owned_updates(source: str, operations: list[dict]) -> bool:
    kinds = {operation.get("entityKind") for operation in operations}
    if "project" in kinds:
        return source == "application-migration"
    if "topology" in kinds:
        return source in {"application-reconcile", "application-migration"}
    return True


def main() -> int:
    world = SchemaWorld(CONTRACTS)
    project = world.documents["ipcraft.project-design.v1"]["$defs"]
    core = world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
    world.validate("ipcraft.project-design.v1", project["defaultEngineDependencyLock"], engine_lock(), "engine lock")
    world.validate("ipcraft.engine-bundle.v1", world.documents["ipcraft.engine-bundle.v1"], engine_manifest(), "engine manifest")

    expected = [
        ("exact", None), ("degraded-inspect", "engine.bundle_missing"),
        ("degraded-inspect", "engine.bundle_revoked"), ("degraded-inspect", "engine.bundle_mismatch"),
        ("degraded-inspect", "engine.platform_unsupported"),
        ("degraded-inspect", "engine.host_contract_unsupported"),
        ("degraded-inspect", "host.side_effect_contract_unsupported"),
    ]
    actual = [
        resolve(engine_lock()), resolve(engine_lock(), installed=False), resolve(engine_lock(), revoked=True),
        resolve(engine_lock(), content_digest=DIGEST_B), resolve(engine_lock(), platform="other-abi"),
        resolve(engine_lock(), host="ipcraft.engine-host.v2"), resolve(engine_lock(), side_effect="ipcraft.noc-side-effects.v2"),
    ]
    assert actual == expected
    assert resolve(engine_lock(DIGEST_B), content_digest=DIGEST_A)[0] == "degraded-inspect"
    unsupported = engine_lock(); unsupported["engineHostContractVersion"] = "ipcraft.engine-host.v999"
    world.validate("ipcraft.project-design.v1", project["defaultEngineDependencyLock"], unsupported, "unsupported lock parses")
    assert resolve(unsupported)[0] == "degraded-inspect"

    dependencies = [
        {"lockId": "dep.noc", "kind": "noc-package", "id": "vendor.noc", "version": "1", "bundleManifestDigest": DIGEST_C},
        engine_lock(DIGEST_B),
    ]
    operations = [
        {"op": "updateEntity", "entityKind": "project", "id": "project.main", "set": {"dependencies": dependencies}, "unset": []},
        {"op": "updateEntity", "entityKind": "topology", "id": "topology.main", "set": {"derivation": derivation()}, "unset": []},
    ]
    for index, dependency in enumerate(dependencies):
        world.validate("ipcraft.project-design.v1", project["dependencyLock"], dependency, f"migration dependency {index}")
    world.validate("ipcraft.core-canonical-models.v1", core["operation"], operations[1], "migration topology operation")
    assert core["patchProjectUpdateSet"]["properties"]["dependencies"]["$ref"].endswith("#/properties/dependencies")
    assert operations[0]["set"] == {"dependencies": dependencies} and operations[0]["unset"] == []
    assert source_allows_application_owned_updates("application-migration", operations)
    assert not source_allows_application_owned_updates("user-command", operations)
    assert not source_allows_application_owned_updates("default-engine", operations)
    assert source_allows_application_owned_updates("application-reconcile", operations[1:])
    assert "topology" not in core["createEntityOperation"]["properties"]["entityKind"]["enum"]
    assert "topology" not in core["deleteEntityOperation"]["properties"]["entityKind"]["enum"]

    side_schema = world.documents["ipcraft.noc-side-effects.v1"]
    for disposition in IMPACTS:
        world.validate("ipcraft.noc-side-effects.v1", side_schema, side_effect_example(disposition), disposition[0])
    print("engine/side-effect witnesses passed: 7 resolution states, migration Patch, 6 dispositions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
