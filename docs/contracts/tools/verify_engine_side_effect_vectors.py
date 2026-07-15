#!/usr/bin/env python3
"""Independently recompute Engine/Host behavioral golden vectors.

This verifier intentionally imports neither the authoring generator nor the
older schema smoke witness.  The behavioral evaluators below are separate
implementations of the frozen Appendix F rules.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
from typing import Any

from verify_canonical_vectors import SchemaViolation, SchemaWorld, VerificationError, normalize, normalize_candidate, sha256_digest

ROOT=Path(__file__).resolve().parents[3]
CONTRACTS=ROOT/"docs/contracts"
VECTORS=CONTRACTS/"vectors"
REQUIRED_RES={"engine-lock-exact-available","engine-lock-missing","engine-lock-revoked","engine-lock-corrupt","engine-lock-digest-mismatch","engine-lock-same-compatibility-different-digest","engine-lock-manifest-metadata-mismatch","engine-lock-manifest-id-mismatch","engine-lock-manifest-host-mismatch","engine-lock-manifest-side-effect-mismatch","engine-lock-manifest-compatibility-mismatch","engine-lock-manifest-platform-metadata-mismatch","engine-lock-platform-incompatible","engine-lock-host-abi-incompatible","engine-lock-side-effect-contract-incompatible","engine-lock-no-builtin-fallback","engine-lock-newer-target-discovered","unsupported-but-valid-bundle-retained"}
REQUIRED_MIG={"engine-migration-compatible-target-offered","engine-migration-incompatible-target-not-offered","engine-migration-atomic-commit","engine-migration-blocked-by-package-relation","engine-migration-undo-exact-inverse","engine-migration-undo-source-missing-degraded"}
REQUIRED_FRESH={"output-freshness-saved-current-equal","output-freshness-accepted-unsaved-edit","output-freshness-pending-topology","output-freshness-draft-overlay","output-freshness-engine-digest-mismatch","output-freshness-engine-host-contract-mismatch","output-freshness-side-effect-contract-mismatch","output-freshness-no-promotion"}
REQUIRED_SIDE={"side-effects-created-router-default-memberships","side-effects-created-routers-membership-order","side-effects-deleted-router-memberships","side-effects-deleted-router-attachment-unresolved","side-effects-deleted-slot-attachment-unresolved","side-effects-package-relation-endpoint-unresolved","side-effects-package-relation-endpoint-blocked","side-effects-empty-non-default-domain-tombstone","side-effects-empty-default-domain-preserved","side-effects-domain-disconnected-router-delete","side-effects-domain-disconnected-link-delete","side-effects-domain-disconnected-link-update","side-effects-domain-disconnected-membership-placement","side-effects-combined-deterministic-order"}


def fail(msg:str)->None: raise VerificationError(msg)
def load(path:Path)->Any:
    try:return json.loads(path.read_text(encoding="utf-8"))
    except Exception as e: fail(f"cannot load {path}: {e}")
def cj(v:Any)->str:return json.dumps(v,ensure_ascii=False,sort_keys=True,separators=(",",":"))
def dg(v:Any)->str:return "sha256:"+hashlib.sha256(cj(v).encode()).hexdigest()
def exact_keys(v:dict,keys:set[str],where:str)->None:
    if set(v)!=keys: fail(f"{where}: fields {sorted(set(v))}, expected {sorted(keys)}")
def ids(cases:list[dict],required:set[str],field:str,where:str)->dict[str,dict]:
    out={}
    for i,c in enumerate(cases):
        if not isinstance(c,dict) or not isinstance(c.get(field),str): fail(f"{where}[{i}] lacks {field}")
        if c[field] in out: fail(f"duplicate {field} {c[field]}")
        out[c[field]]=c
    if required!=set(out): fail(f"{where}: ID set mismatch; missing {sorted(required-set(out))}, extra {sorted(set(out)-required)}")
    return out


def resolve(inp:dict)->dict:
    lock=inp["projectLock"]; exact=next((b for b in inp["installedBundles"] if b["bundleManifestDigest"]==lock["bundleManifestDigest"] and b["installed"]),None)
    alternatives=inp["alternativeBundles"]
    upgrade=False
    if exact is None: return {"outcome":"degraded-inspect","selectedBundleManifestDigest":None,"diagnosticCode":"engine.bundle_missing","upgradeAvailable":False,"retainedInContentAddressedStore":False}
    if exact["revoked"]: code="engine.bundle_revoked"
    elif not exact["contentVerified"] or exact["verifiedBundleManifestDigest"]!=lock["bundleManifestDigest"]: code="engine.bundle_mismatch"
    else:
        m=exact["manifest"]
        fields=("id","version","engineCompatibilityVersion","engineHostContractVersion","hostSideEffectContractVersion","supportedPlatformAbis")
        if any(m[k]!=lock[k] for k in fields): code="engine.bundle_mismatch"
        elif inp["currentPlatformAbi"] not in m["supportedPlatformAbis"]: code="engine.platform_unsupported"
        elif m["engineHostContractVersion"] not in inp["supportedEngineHostContracts"]: code="engine.host_contract_unsupported"
        elif m["hostSideEffectContractVersion"] not in inp["supportedHostSideEffectContracts"]: code="host.side_effect_contract_unsupported"
        else:
            upgrade=any(b["bundleManifestDigest"]!=lock["bundleManifestDigest"] and lock["engineCompatibilityVersion"] in b["manifest"]["migrationFromCompatibilityVersions"] for b in alternatives)
            return {"outcome":"exact","selectedBundleManifestDigest":lock["bundleManifestDigest"],"diagnosticCode":None,"upgradeAvailable":upgrade,"retainedInContentAddressedStore":False}
    out={"outcome":"degraded-inspect","selectedBundleManifestDigest":None,"diagnosticCode":code,"upgradeAvailable":False,"retainedInContentAddressedStore":False}
    return out


def eval_resolution(case:dict)->dict:
    result=resolve(case["input"])
    if case["id"]=="unsupported-but-valid-bundle-retained": result["retainedInContentAddressedStore"]=bool(case["input"]["installedBundles"] and case["input"]["installedBundles"][0]["installed"])
    return result


def eval_fresh(inp:dict)->dict:
    p=inp["promotedManifest"]
    if p is None:return {"state":"none","staleReasons":[]}
    reasons=[]
    if p["snapshotDigest"]!=inp["currentAuthoritativeDesignDigest"]: reasons.append("authoritative-design-changed")
    if inp["currentAuthoritativeDesignDigest"]!=inp["formallySavedProjectDigest"]: reasons.append("not-formally-saved")
    if (p["dependencySetDigest"]!=inp["currentDependencySetDigest"] or p["defaultEngineBundleDigest"]!=inp["currentDefaultEngineBundleDigest"] or p["engineHostContractVersion"]!=inp["currentEngineHostContractVersion"] or p["hostSideEffectContractVersion"]!=inp["currentHostSideEffectContractVersion"]): reasons.append("dependency-changed")
    if inp["pendingTopologyGroup"]: reasons.append("pending-topology")
    if inp["draftOverlayCount"]: reasons.append("draft-overlay")
    order=["authoritative-design-changed","not-formally-saved","dependency-changed","pending-topology","draft-overlay"]
    reasons=[x for x in order if x in reasons]
    return {"state":"last-successful-stale" if reasons else "current-canonical","staleReasons":reasons}


def eval_migration(inp:dict)->dict:
    if inp["caseKind"]=="discovery":
        eligible=inp["currentDefaultEngineLock"]["engineCompatibilityVersion"] in inp["targetManifest"]["migrationFromCompatibilityVersions"]
        return {"offered":eligible,"diagnosticCode":None if eligible else "engine.migration_incompatible","engineExecutions":[]}
    cur=inp["migration"]["currentDefaultEngineLock"]; target=inp["migration"]["targetDefaultEngineLock"]; man=inp["targetManifest"]
    eligible=cur["engineCompatibilityVersion"] in man["migrationFromCompatibilityVersions"]
    if not eligible:return {"offered":False,"diagnosticCode":"engine.migration_incompatible","engineExecutions":[]}
    if inp.get("action")=="undo":
        restored=apply_inverse(inp["afterSnapshot"],inp["inverseTransaction"])
        return {"offered":True,"restoredSnapshot":"beforeSnapshot","stableHostIds":restored["hostIds"]==inp["beforeSnapshot"]["hostIds"],"engineExecutions":list(inp["inverseTransaction"]["engineInvocations"]),"resultMode":"normal" if inp["sourceBundleAvailable"] else "degraded-inspect",**({} if inp["sourceBundleAvailable"] else {"diagnosticCode":"engine.bundle_missing"})}
    blocked=any(i["code"]=="package_relation.endpoint_blocks_candidate" for i in recompute_migration_impacts(inp))
    return {"offered":True,"groupState":"blocked" if blocked else "ready-to-commit","requiresConfirmation":not blocked,"atomicCommit":not blocked,"engineExecutions":list(inp["migrationEngineInvocations"])}


def ref_id(ref:dict,mapping:dict)->str:return ref["id"] if "id" in ref else mapping[ref["localRef"]]


def apply_forward(before:dict,tx:dict)->dict:
    result=copy.deepcopy(before);mapping=tx["localRefToHostId"]
    collections={"router":"routers","structural-link":"structuralLinks","access-slot":"accessSlots"}
    for operation in tx["authorityPatch"]["operations"]:
        kind=operation["entityKind"]
        if kind not in collections:continue
        values=result["derivedState"][collections[kind]]
        if operation["op"]=="deleteEntity":values[:]=[v for v in values if v["id"]!=operation["id"]]
        elif operation["op"]=="createEntity":
            value=copy.deepcopy(operation["value"]);value["id"]=mapping[operation["localRef"]]
            if kind=="structural-link":value["endpointA"],value["endpointB"]=ref_id(value["endpointA"],mapping),ref_id(value["endpointB"],mapping)
            if kind=="access-slot":value["routerId"]=ref_id(value.pop("routerRef"),mapping)
            values.append(value)
    for values in (result["derivedState"]["routers"],result["derivedState"]["structuralLinks"],result["derivedState"]["accessSlots"]):values.sort(key=lambda x:x["id"])
    host=result["hostSideEffects"]
    for operation in tx["applicationPatch"]["operations"]:
        if operation["op"]=="createRelation" and operation["relationKind"]=="domain-membership":host["domainMemberships"].append({"id":mapping[operation["localRef"]],"domainId":ref_id(operation["value"]["domainRef"],mapping),"routerId":ref_id(operation["value"]["routerRef"],mapping)})
        elif operation["op"]=="deleteRelation" and operation["relationKind"]=="domain-membership":host["domainMemberships"]=[x for x in host["domainMemberships"] if x["id"]!=operation["id"]]
        elif operation["op"]=="updateRelation" and operation["relationKind"]=="attachment":
            prior=next(x for x in host["attachments"] if x["id"]==operation["id"]);interface=prior["interfaceId"];prior.clear();prior.update({"id":operation["id"],"interfaceId":interface,"state":"unresolved","intendedTarget":{"routerId":operation["set"]["intendedTarget"]["routerRef"]["id"],"slotId":operation["set"]["intendedTarget"]["slotRef"]["id"]},"reasonCode":operation["set"]["reasonCode"]})
        elif operation["op"]=="updateRelation" and operation["relationKind"]=="package-relation":
            relation=next(x for x in host["packageRelations"] if x["id"]==operation["id"])
            def persist(endpoint):
                if endpoint["state"]=="resolved":return {"state":"resolved","subject":{"kind":endpoint["subject"]["kind"],"id":ref_id(endpoint["subject"]["ref"],mapping)}}
                return {"state":"unresolved","intendedSubject":{"kind":endpoint["intendedSubject"]["kind"],"id":ref_id(endpoint["intendedSubject"]["ref"],mapping)},"reasonCode":endpoint["reasonCode"]}
            relation["sources"],relation["targets"]=[persist(x) for x in operation["set"]["sources"]],[persist(x) for x in operation["set"]["targets"]]
        elif operation["op"]=="deleteEntity" and operation["entityKind"]=="domain":host["domains"]=[x for x in host["domains"] if x["id"]!=operation["id"]]
    for values in host.values():values.sort(key=lambda x:x["id"])
    result["dependencies"]=copy.deepcopy(tx["targetDependencies"]);result["derivation"]=copy.deepcopy(tx["targetDerivation"]);result["hostIds"]={"localRefToHostId":copy.deepcopy(mapping)};result["engineInvocationCount"]=tx["resultEngineInvocationCount"]
    return result


def apply_inverse(after:dict,tx:dict)->dict:
    result=copy.deepcopy(after);result["dependencies"]=copy.deepcopy(tx["restoreDependencies"]);result["derivedState"]=copy.deepcopy(tx["restoreDerivedState"]);result["derivation"]=copy.deepcopy(tx["restoreDerivation"]);result["hostSideEffects"]=copy.deepcopy(tx["restoreHostSideEffects"]);result["hostIds"]=copy.deepcopy(tx["restoreHostIds"]);result["engineInvocationCount"]=tx["restoreEngineInvocationCount"]+len(tx["engineInvocations"]);return result


def recompute_migration_impacts(inp:dict)->list[dict]:
    current=inp["migration"]["currentDefaultEngineLock"];target=inp["migration"]["targetDefaultEngineLock"]
    mandatory={"code":"engine_migration.dependency_replaced","severity":"warning","dataLoss":False,"subjects":[{"kind":"project","id":"project.main"}],"details":{"lockId":current["lockId"],"currentBundleDigest":current["bundleManifestDigest"],"targetBundleDigest":target["bundleManifestDigest"]},"resolution":"confirm-or-discard"}
    impacts=evaluate_side(inp["sideEffectInput"])["impactReport"]["impacts"]+[mandatory]
    impacts.sort(key=lambda x:(x["code"],x["severity"],x["dataLoss"],cj(x["subjects"]),cj(x["details"]),x["resolution"]))
    return impacts


def snapshot_subject_table(case_id:str,label:str,snapshot:dict,world:SchemaWorld)->dict[tuple[str,str],dict]:
    core=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
    project=world.documents["ipcraft.project-design.v1"]["$defs"]
    derived=normalize(world,"ipcraft.core-canonical-models.v1",core["derivedState"],snapshot["derivedState"],f"{case_id} {label} Derived State")
    host={}
    for collection,definition in (("domains","domain"),("domainMemberships","domainMembership"),("attachments","attachment"),("packageRelations","packageRelation")):
        host[collection]=[normalize(world,"ipcraft.project-design.v1",project[definition],value,f"{case_id} {label} {collection}") for value in snapshot["hostSideEffects"][collection]]
        host[collection].sort(key=lambda value:value["id"])
    table={}
    sources=[("router",derived["routers"]),("structural-link",derived["structuralLinks"]),("access-slot",derived["accessSlots"]),("package-entity",derived["packageEntities"]),("package-relation",derived["packageRelations"]),("domain",host["domains"]),("domain-membership",host["domainMemberships"]),("attachment",host["attachments"]),("package-relation",host["packageRelations"])]
    for kind,values in sources:
        for value in values:
            key=(kind,value["id"])
            if key in table:fail(f"{case_id}: {label} subject collision expected unique (kind,id) actual={kind}:{value['id']}")
            table[key]=copy.deepcopy(value)
    return table


def create_subject_kind(operation:dict)->str|None:
    if operation["op"]=="createEntity":return operation["entityKind"]
    if operation["op"]=="createRelation":return operation["relationKind"]
    return None


def derive_state_deltas(case_id:str,inp:dict,world:SchemaWorld)->tuple[list[dict],set[tuple[str,str]]]:
    before=snapshot_subject_table(case_id,"beforeSnapshot",inp["beforeSnapshot"],world);after=snapshot_subject_table(case_id,"afterSnapshot",inp["afterSnapshot"],world)
    removed=sorted(set(before)-set(after));added=set(after)-set(before)
    expected_tombstones=[{"subjectKind":kind,"id":identity,"value":before[(kind,identity)]} for kind,identity in removed]
    return expected_tombstones,added


def validate_state_deltas(case_id:str,inp:dict,expected_tombstones:list[dict],added:set[tuple[str,str]])->None:
    candidate,forward=inp["candidate"],inp["forwardTransaction"]
    if candidate["tombstones"]!=expected_tombstones or forward["tombstones"]!=expected_tombstones:fail(f"{case_id}: tombstone delta mismatch expected={cj(expected_tombstones)} actualCandidate={cj(candidate['tombstones'])} actualForward={cj(forward['tombstones'])}")
    creates=[]
    for patch_name,prefix in (("authorityPatch","authority:"),("applicationPatch","application:")):
        for operation in candidate[patch_name]["operations"]:
            kind=create_subject_kind(operation)
            if kind is None:continue
            local_ref=operation.get("localRef")
            if not isinstance(local_ref,str) or not local_ref.startswith(prefix):fail(f"{case_id}: create localRef prefix mismatch expected={prefix!r} actual={local_ref!r}")
            creates.append((local_ref,kind))
    local_refs=[item[0] for item in creates]
    if len(local_refs)!=len(set(local_refs)):fail(f"{case_id}: candidate-wide create localRef uniqueness mismatch expected unique actual={local_refs}")
    expected_order=sorted(local_refs);mapping=forward["localRefToHostId"]
    if candidate["allocationOrder"]!=forward["allocationOrder"] or candidate["allocationOrder"]!=expected_order:fail(f"{case_id}: allocationOrder mismatch expected={expected_order} actualCandidate={candidate['allocationOrder']} actualForward={forward['allocationOrder']}")
    if list(mapping)!=expected_order or set(mapping)!=set(expected_order) or len(set(mapping.values()))!=len(mapping):fail(f"{case_id}: localRef mapping keys/order/values mismatch expected={expected_order} actual={list(mapping)}")
    after_mapping=inp["afterSnapshot"]["hostIds"]["localRefToHostId"]
    if mapping!=after_mapping:fail(f"{case_id}: materialized localRef mapping mismatch expected={cj(mapping)} actual={cj(after_mapping)}")
    materialized={(kind,mapping[local_ref]) for local_ref,kind in creates}
    if materialized!=added:fail(f"{case_id}: materialized create mapping mismatch expected added={sorted(added)} actual={sorted(materialized)}")


LOCK_FIELDS=("lockId","kind","id","version","bundleManifestDigest","engineCompatibilityVersion","engineHostContractVersion","hostSideEffectContractVersion","supportedPlatformAbis")


def require_lock_equal(case_id:str,label:str,actual:dict,expected:dict)->None:
    if tuple(actual.get(key) for key in LOCK_FIELDS)!=tuple(expected.get(key) for key in LOCK_FIELDS) or set(actual)!=set(LOCK_FIELDS):fail(f"{case_id}: {label} lock tuple mismatch expected={cj(expected)} actual={cj(actual)}")


def require_manifest_lock(case_id:str,label:str,manifest:dict,lock_value:dict)->None:
    fields=("id","version","engineCompatibilityVersion","engineHostContractVersion","hostSideEffectContractVersion","supportedPlatformAbis")
    expected=tuple(lock_value[key] for key in fields);actual=tuple(manifest.get(key) for key in fields)
    if actual!=expected:fail(f"{case_id}: {label} manifest metadata mismatch expected={expected} actual={actual}")


def require_authority_lock(case_id:str,label:str,source:dict,lock_value:dict)->None:
    expected=("default-engine",lock_value["id"],lock_value["version"],lock_value["bundleManifestDigest"])
    actual=(source.get("kind"),source.get("identity"),source.get("version"),source.get("bundleDigest"))
    if actual!=expected:fail(f"{case_id}: {label} Authority mismatch expected={expected} actual={actual}")


def require_applicability_lock(case_id:str,app:dict,lock_value:dict)->None:
    authority=app["structureAuthority"]
    expected=(lock_value["lockId"],lock_value["bundleManifestDigest"],lock_value["engineHostContractVersion"],lock_value["hostSideEffectContractVersion"],"default-engine",lock_value["lockId"],lock_value["id"],lock_value["version"],lock_value["bundleManifestDigest"])
    actual=(app["defaultEngineLockId"],app["defaultEngineBundleDigest"],app["engineHostContractVersion"],app["hostSideEffectContractVersion"],authority["kind"],authority["lockId"],authority["identity"],authority["version"],authority["bundleDigest"])
    if actual!=expected:fail(f"{case_id}: applicability Engine provenance mismatch expected={expected} actual={actual}")


def require_engine_dependency(case_id:str,label:str,dependencies:list[dict],lock_value:dict)->None:
    actual=[dependency for dependency in dependencies if dependency.get("kind")=="default-engine"]
    if len(actual)!=1:fail(f"{case_id}: {label} default Engine dependency count mismatch expected=1 actual={len(actual)}")
    require_lock_equal(case_id,label,actual[0],lock_value)


def require_derivation_lock(case_id:str,label:str,derivation_value:dict,lock_value:dict)->None:
    authority=derivation_value["structureAuthority"]
    expected=(lock_value["lockId"],lock_value["bundleManifestDigest"],lock_value["engineCompatibilityVersion"],lock_value["engineHostContractVersion"],lock_value["hostSideEffectContractVersion"],"default-engine",lock_value["lockId"],lock_value["id"],lock_value["version"],lock_value["bundleManifestDigest"])
    actual=(derivation_value["defaultEngineLockId"],derivation_value["defaultEngineBundleDigest"],derivation_value["engineCompatibilityVersion"],derivation_value["engineHostContractVersion"],derivation_value["hostSideEffectContractVersion"],authority["kind"],authority["lockId"],authority["identity"],authority["version"],authority["bundleDigest"])
    if actual!=expected:fail(f"{case_id}: {label} derivation Engine provenance mismatch expected={expected} actual={actual}")


def validate_migration_binding(case_id:str,inp:dict,world:SchemaWorld)->None:
    expected_tombstones,added=derive_state_deltas(case_id,inp,world)
    cur=inp["migration"]["currentDefaultEngineLock"]; target=inp["migration"]["targetDefaultEngineLock"]; app=inp["applicability"]
    validate_state_deltas(case_id,inp,expected_tombstones,added)
    if inp["caseKind"]!="offered":fail(f"{case_id}: caseKind mismatch expected='offered' actual={inp['caseKind']!r}")
    if inp["action"] not in {"migrate","undo"}:fail(f"{case_id}: action mismatch expected one of ['migrate','undo'] actual={inp['action']!r}")
    project=world.documents["ipcraft.project-design.v1"]["$defs"]["defaultEngineDependencyLock"]
    for label,value in (("current migration lock",cur),("target migration lock",target)):
        world.validate("ipcraft.project-design.v1",project,value,f"{case_id} {label}")
        require_lock_equal(case_id,label,value,value)
    if cur["lockId"]!=target["lockId"] or cur["bundleManifestDigest"]==target["bundleManifestDigest"]:fail(f"{case_id}: migration target identity mismatch expected=same lockId and distinct digest actualCurrent={cj(cur)} actualTarget={cj(target)}")
    for label,manifest_value,lock_value in (("current",inp["currentManifest"],cur),("target",inp["targetManifest"],target)):
        world.validate("ipcraft.engine-bundle.v1",world.documents["ipcraft.engine-bundle.v1"],manifest_value,f"{case_id} {label} manifest")
        require_manifest_lock(case_id,label,manifest_value,lock_value)
    candidate=inp["candidate"];forward=inp["forwardTransaction"];inverse=inp["inverseTransaction"];before=inp["beforeSnapshot"];after=inp["afterSnapshot"]
    require_lock_equal(case_id,"candidate current migration",candidate["migration"]["currentDefaultEngineLock"],cur)
    require_lock_equal(case_id,"candidate target migration",candidate["migration"]["targetDefaultEngineLock"],target)
    require_engine_dependency(case_id,"beforeSnapshot",before["dependencies"],cur)
    require_engine_dependency(case_id,"inverse restoreDependencies",inverse["restoreDependencies"],cur)
    require_engine_dependency(case_id,"forward targetDependencies",forward["targetDependencies"],target)
    require_engine_dependency(case_id,"afterSnapshot",after["dependencies"],target)
    before_non_engine=[x for x in before["dependencies"] if x["kind"]!="default-engine"]
    for label,dependencies in (("inverse",inverse["restoreDependencies"]),("forward",forward["targetDependencies"]),("after",after["dependencies"])):
        actual=[x for x in dependencies if x["kind"]!="default-engine"]
        if actual!=before_non_engine:fail(f"{case_id}: {label} non-Engine dependencies mismatch expected={cj(before_non_engine)} actual={cj(actual)}")
    require_derivation_lock(case_id,"beforeSnapshot",before["derivation"],cur)
    require_derivation_lock(case_id,"inverse restoreDerivation",inverse["restoreDerivation"],cur)
    require_derivation_lock(case_id,"forward targetDerivation",forward["targetDerivation"],target)
    require_derivation_lock(case_id,"afterSnapshot",after["derivation"],target)
    require_applicability_lock(case_id,app,cur)
    if candidate["applicability"]!=app:fail(f"{case_id}: candidate applicability mismatch expected={cj(app)} actual={cj(candidate['applicability'])}")
    for label,source in (("candidate",candidate["authorityPatch"]["source"]),("forward",forward["authorityPatch"]["source"]),("sideEffectInput",inp["sideEffectInput"]["authorityPatch"]["source"])):
        require_authority_lock(case_id,label,source,target)
    if candidate["authorityPatch"]!=forward["authorityPatch"] or candidate["authorityPatch"]!=inp["sideEffectInput"]["authorityPatch"]:fail(f"{case_id}: Authority patch mismatch expected identical candidate/forward/side-effect patches actualCandidate={cj(candidate['authorityPatch'])} actualForward={cj(forward['authorityPatch'])} actualSide={cj(inp['sideEffectInput']['authorityPatch'])}")
    if inp["sideEffectInput"]["currentDerivedState"]!=before["derivedState"]:fail(f"{case_id}: side-effect current Derived State mismatch expected={cj(before['derivedState'])} actual={cj(inp['sideEffectInput']['currentDerivedState'])}")
    if forward["targetDerivation"]!=after["derivation"]:fail(f"{case_id}: forward/after derivation mismatch expected={cj(forward['targetDerivation'])} actual={cj(after['derivation'])}")
    if forward["targetDependencies"]!=after["dependencies"]:fail(f"{case_id}: forward/after dependencies mismatch expected={cj(forward['targetDependencies'])} actual={cj(after['dependencies'])}")
    expected_invocations=[target["bundleManifestDigest"]]
    if inp["migrationEngineInvocations"]!=expected_invocations:fail(f"{case_id}: migration Engine invocation evidence mismatch expected={expected_invocations} actual={inp['migrationEngineInvocations']}")
    if forward["engineInvocations"]!=[]:fail(f"{case_id}: stored forward/redo Engine invocations mismatch expected=[] actual={forward['engineInvocations']}")
    if inverse["engineInvocations"]!=[]:fail(f"{case_id}: undo Engine invocations mismatch expected=[] actual={inverse['engineInvocations']}")
    expected_count=before["engineInvocationCount"]+len(expected_invocations)
    if forward["resultEngineInvocationCount"]!=expected_count or after["engineInvocationCount"]!=expected_count:fail(f"{case_id}: migration Engine invocation count mismatch expected={expected_count} actualForward={forward['resultEngineInvocationCount']} actualAfter={after['engineInvocationCount']}")
    side=evaluate_side(inp["sideEffectInput"]);expected_ops=side["applicationPatch"]["operations"]+[{"op":"updateEntity","entityKind":"project","id":"project.main","set":{"dependencies":inp["forwardTransaction"]["targetDependencies"]},"unset":[]},{"op":"updateEntity","entityKind":"topology","id":"topology.main","set":{"derivation":inp["forwardTransaction"]["targetDerivation"]},"unset":[]}]
    side_document={"schema":"ipcraft.noc-side-effects.v1","contractVersion":"ipcraft.noc-side-effects.v1","input":inp["sideEffectInput"],"expected":side}
    world.validate("ipcraft.noc-side-effects.v1",world.documents["ipcraft.noc-side-effects.v1"],side_document,"migration side effects")
    core_defs=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
    for name in ("beforeSnapshot","afterSnapshot"):world.validate("ipcraft.core-canonical-models.v1",core_defs["derivedState"],inp[name]["derivedState"],"migration "+name+" derivedState")
    if candidate["applicationPatch"]["operations"]!=expected_ops or candidate["applicationPatch"]!=forward["applicationPatch"]:fail(f"{case_id}: migration Application transaction mismatch expected={cj(expected_ops)} actual={cj(candidate['applicationPatch']['operations'])}")
    impacts=recompute_migration_impacts(inp)
    if candidate["impactReport"]["impacts"]!=impacts:fail(f"{case_id}: migration impact report mismatch expected={cj(impacts)} actual={cj(candidate['impactReport']['impacts'])}")
    normalized=normalize_candidate(world,candidate,[],"migration candidate")
    expected_digest=sha256_digest(normalized)
    if candidate["candidateDigest"]!=expected_digest:fail(f"{case_id}: migration candidateDigest mismatch expected={expected_digest} actual={candidate['candidateDigest']}")
    materialized=apply_forward(before,forward)
    if materialized!=after:fail(f"{case_id}: migration forward result mismatch expected={cj(after)} actual={cj(materialized)}")
    restored=apply_inverse(after,inverse)
    if restored!=before:fail(f"{case_id}: migration inverse result mismatch expected={cj(before)} actual={cj(restored)}")
    redone=apply_forward(restored,forward)
    if redone!=after:fail(f"{case_id}: migration redo result mismatch expected={cj(after)} actual={cj(redone)}")
    core_defs=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
    before_norm=normalize(world,"ipcraft.core-canonical-models.v1",core_defs["derivedState"],inp["beforeSnapshot"]["derivedState"],"before Derived State")
    after_norm=normalize(world,"ipcraft.core-canonical-models.v1",core_defs["derivedState"],inp["afterSnapshot"]["derivedState"],"after Derived State")
    before_digest,after_digest=sha256_digest(before_norm),sha256_digest(after_norm)
    before_der,after_der=inp["beforeSnapshot"]["derivation"],inp["afterSnapshot"]["derivation"]
    if before_digest==after_digest:fail(f"{case_id}: migration Derived State distinctness mismatch expected distinct actualBefore={before_digest} actualAfter={after_digest}")
    if before_der["derivedStateDigest"]!=before_digest:fail(f"{case_id}: before derivation Derived State digest mismatch expected={before_digest} actual={before_der['derivedStateDigest']}")
    if after_der["derivedStateDigest"]!=after_digest:fail(f"{case_id}: after derivation Derived State digest mismatch expected={after_digest} actual={after_der['derivedStateDigest']}")
    expected_app=(before_der["derivedStateRevision"],before_digest);actual_app=(app["baseDerivedStateRevision"],app["baseDerivedStateDigest"])
    if actual_app!=expected_app:fail(f"{case_id}: applicability base Derived State mismatch expected={expected_app} actual={actual_app}")
    if after_der["derivedStateRevision"]<=before_der["derivedStateRevision"]:fail(f"{case_id}: after Derived State revision monotonicity mismatch expected greater than {before_der['derivedStateRevision']} actual={after_der['derivedStateRevision']}")


def validate_migration_discovery(inp:dict,world:SchemaWorld)->None:
    project=world.documents["ipcraft.project-design.v1"]["$defs"]["defaultEngineDependencyLock"]
    case_id="engine-migration-incompatible-target-not-offered"
    if inp["caseKind"]!="discovery":fail(f"{case_id}: caseKind mismatch expected='discovery' actual={inp['caseKind']!r}")
    world.validate("ipcraft.project-design.v1",project,inp["currentDefaultEngineLock"],"discovery current lock");world.validate("ipcraft.project-design.v1",project,inp["targetDefaultEngineLock"],"discovery target lock")
    current,target=inp["currentDefaultEngineLock"],inp["targetDefaultEngineLock"]
    require_lock_equal(case_id,"discovery current",current,current);require_lock_equal(case_id,"discovery target",target,target)
    for label,manifest_value,lock_value in (("current",inp["currentManifest"],current),("target",inp["targetManifest"],target)):
        world.validate("ipcraft.engine-bundle.v1",world.documents["ipcraft.engine-bundle.v1"],manifest_value,f"discovery {label} manifest")
        require_manifest_lock(case_id,label,manifest_value,lock_value)
    if current["lockId"]!=target["lockId"] or current["bundleManifestDigest"]==target["bundleManifestDigest"]:fail(f"{case_id}: discovery lock identity mismatch expected=same lockId and distinct digest actualCurrent={cj(current)} actualTarget={cj(target)}")
    metadata=("id","version","engineCompatibilityVersion","engineHostContractVersion","hostSideEffectContractVersion","supportedPlatformAbis")
    if any(inp["targetManifest"][key]!=target[key] for key in metadata):fail(f"{case_id}: discovery target manifest mismatch expected={cj(target)} actual={cj(inp['targetManifest'])}")
    if target["engineHostContractVersion"] not in inp["supportedEngineHostContracts"] or target["hostSideEffectContractVersion"] not in inp["supportedHostSideEffectContracts"] or inp["currentPlatformAbi"] not in target["supportedPlatformAbis"]:fail(f"{case_id}: discovery execution context mismatch expected target contracts/platform supported actualHost={inp['supportedEngineHostContracts']} actualSideEffect={inp['supportedHostSideEffectContracts']} actualPlatform={inp['currentPlatformAbi']}")
    snapshot=inp["currentSnapshot"];core=world.documents["ipcraft.core-canonical-models.v1"]["$defs"];normalized=normalize(world,"ipcraft.core-canonical-models.v1",core["derivedState"],snapshot["derivedState"],"discovery current Derived State")
    expected_digest=sha256_digest(normalized)
    if snapshot["derivation"]["derivedStateDigest"]!=expected_digest:fail(f"{case_id}: discovery current Snapshot Derived State digest mismatch expected={expected_digest} actual={snapshot['derivation']['derivedStateDigest']}")
    require_engine_dependency(case_id,"discovery currentSnapshot",snapshot["dependencies"],current)
    require_derivation_lock(case_id,"discovery currentSnapshot",snapshot["derivation"],current)


def token(ref:dict)->str:return "id:"+ref["id"] if "id" in ref else "localRef:"+ref["localRef"]
def evaluate_side(inp:dict)->dict:
    authority=inp["authorityPatch"]["operations"]; out=[]; impacts=[]; tomb=[]; diags=[]
    dr={o["id"] for o in authority if o["op"]=="deleteEntity" and o["entityKind"]=="router"}; ds={o["id"] for o in authority if o["op"]=="deleteEntity" and o["entityKind"]=="access-slot"}
    created=[o for o in authority if o["op"]=="createEntity" and o["entityKind"]=="router"]
    defaults={d["typeKey"]:d["id"] for d in inp["domains"] if d["isDefault"]}
    for n,(typ,o) in enumerate(sorted(((t["key"],o) for t in inp["domainTypes"] for o in created),key=lambda p:(p[0],token({"localRef":p[1]["localRef"]}))),1):
        out.append({"op":"createRelation","relationKind":"domain-membership","localRef":f"application:{n:06d}","value":{"domainRef":{"id":defaults[typ]},"routerRef":{"localRef":o["localRef"]}}})
    for m in sorted(inp["domainMemberships"],key=lambda x:x["id"]):
        if m["routerId"] in dr:out.append({"op":"deleteRelation","relationKind":"domain-membership","id":m["id"]})
    for a in sorted(inp["attachments"],key=lambda x:x["id"]):
        if a["state"]=="resolved" and (a["routerId"] in dr or a["slotId"] in ds):
            out.append({"op":"updateRelation","relationKind":"attachment","id":a["id"],"set":{"state":"unresolved","intendedTarget":{"routerRef":{"id":a["routerId"]},"slotRef":{"id":a["slotId"]}},"reasonCode":"attachment.target_removed"},"unset":["routerRef","slotRef"]})
            impacts.append(impact("attachment.target_removed","warning",False,[{"kind":"attachment","id":a["id"]}],{"routerId":a["routerId"],"slotId":a["slotId"]},"reattach-or-detach"))
    declarations={d["typeKey"]:d for d in inp["relationDeclarations"]}
    for r in sorted(inp["packageRelations"],key=lambda x:x["id"]):
        hit=lambda e:e["state"]=="resolved" and e["subject"]["kind"]=="router" and e["subject"]["id"] in dr
        if not any(hit(e) for e in r["sources"]+r["targets"]):continue
        if declarations[r["typeKey"]]["unresolvedAllowed"]:
            def conv(e):
                if hit(e):return {"state":"unresolved","intendedSubject":{"kind":e["subject"]["kind"],"ref":{"id":e["subject"]["id"]}},"reasonCode":"relation.target_removed"}
                if e["state"]=="resolved":return {"state":"resolved","subject":{"kind":e["subject"]["kind"],"ref":{"id":e["subject"]["id"]}}}
                return {"state":"unresolved","intendedSubject":{"kind":e["intendedSubject"]["kind"],"ref":{"id":e["intendedSubject"]["id"]}},"reasonCode":e["reasonCode"]}
            out.append({"op":"updateRelation","relationKind":"package-relation","id":r["id"],"set":{"sources":[conv(e) for e in r["sources"]],"targets":[conv(e) for e in r["targets"]]},"unset":[]})
            impacts.append(impact("package_relation.endpoint_unresolved","warning",False,[{"kind":"package-relation","id":r["id"]}],{"typeKey":r["typeKey"]},"reattach-or-delete-relation"))
        else: impacts.append(impact("package_relation.endpoint_blocks_candidate","error",False,[{"kind":"package-relation","id":r["id"]}],{"typeKey":r["typeKey"]},"discard-and-repair"))
    members=[(m["domainId"],"id:"+m["routerId"]) for m in inp["domainMemberships"] if m["routerId"] not in dr]
    members += [(o["value"]["domainRef"]["id"],token(o["value"]["routerRef"])) for o in out if o["op"]=="createRelation" and o["relationKind"]=="domain-membership"]
    links={l["id"]:("id:"+l["endpointA"],"id:"+l["endpointB"]) for l in inp["currentDerivedState"]["structuralLinks"]}
    for op in authority:
        if op["op"]=="deleteEntity" and op["entityKind"]=="structural-link":links.pop(op["id"],None)
        elif op["op"]=="createEntity" and op["entityKind"]=="structural-link":links[op["localRef"]]=(token(op["value"]["endpointA"]),token(op["value"]["endpointB"]))
        elif op["op"]=="updateEntity" and op["entityKind"]=="structural-link":
            old=links[op["id"]]; links[op["id"]]=(token(op["set"].get("endpointA",{"id":old[0][3:]})),token(op["set"].get("endpointB",{"id":old[1][3:]})))
    links={key:value for key,value in links.items() if all(not (v.startswith("id:") and v[3:] in dr) for v in value)}
    for d in sorted(inp["domains"],key=lambda x:x["id"]):
        nodes={router for domain_id,router in members if domain_id==d["id"]}
        if not nodes and not d["isDefault"]:
            out.append({"op":"deleteEntity","entityKind":"domain","id":d["id"]});tomb.append({"subjectKind":"domain","id":d["id"],"value":d});impacts.append(impact("domain.non_default_deleted","warning",True,[{"kind":"domain","id":d["id"]}],{"discardedConfig":True},"confirm-or-discard"));continue
        if len(nodes)>1:
            edges=set(links.values()); seen={min(nodes)}
            changed=True
            while changed:
                before=len(seen);seen|={b for a,b in edges if a in seen and b in nodes}|{a for a,b in edges if b in seen and a in nodes};changed=len(seen)>before
            if seen!=nodes:
                subjects=[{"kind":"domain","id":d["id"]}]; impacts.append(impact("domain.disconnected","error",False,subjects,{"memberCount":len(nodes)},"repair-domain"));diags.append({"ruleId":"domain.disconnected","severity":"error","message":"Domain is disconnected.","blocking":True,"subjects":subjects,"properties":[]})
    impacts.sort(key=lambda x:(x["code"],x["severity"],x["dataLoss"],cj(x["subjects"]),cj(x["details"]),x["resolution"]));codes={i["code"] for i in impacts}
    state,confirm,disp=("blocked",False,"blocked") if "package_relation.endpoint_blocks_candidate" in codes else (("ready-to-commit",True,"confirmation-required") if "domain.non_default_deleted" in codes else ("auto-commit",False,"auto-commit"))
    creates=[o["localRef"] for o in authority+out if o["op"] in ("createEntity","createRelation")]
    return {"applicationPatch":{"patchId":"patch.application","source":{"kind":"application-reconcile","identity":"host","version":"1"},"operations":out},"tombstones":sorted(tomb,key=lambda x:(x["subjectKind"],x["id"])),"allocationOrder":sorted(creates),"impactReport":{"schema":"ipcraft.topology-impact-report.v1","impacts":impacts},"coreDiagnostics":diags,"groupState":state,"requiresConfirmation":confirm,"commitDisposition":disp}


def impact(code,severity,loss,subjects,details,resolution):return {"code":code,"severity":severity,"dataLoss":loss,"subjects":subjects,"details":details,"resolution":resolution}


RES_INPUT={"projectLock","installedBundles","currentPlatformAbi","supportedEngineHostContracts","supportedHostSideEffectContracts","alternativeBundles"}
BUNDLE_FIELDS={"bundleManifestDigest","verifiedBundleManifestDigest","installed","revoked","contentVerified","source","manifest"}
RES_EXPECTED={"outcome","selectedBundleManifestDigest","diagnosticCode","upgradeAvailable","retainedInContentAddressedStore"}
MIG_INPUT={"caseKind","action","sourceBundleAvailable","migration","applicability","currentManifest","targetManifest","sideEffectInput","beforeSnapshot","afterSnapshot","candidate","forwardTransaction","inverseTransaction","migrationEngineInvocations"}
MIG_DISCOVERY_INPUT={"caseKind","currentDefaultEngineLock","targetDefaultEngineLock","currentManifest","targetManifest","supportedEngineHostContracts","supportedHostSideEffectContracts","currentPlatformAbi","currentSnapshot"}
SNAPSHOT_FIELDS={"dependencies","derivedState","derivation","hostSideEffects","hostIds","engineInvocationCount"}
HOST_EFFECT_FIELDS={"domains","domainMemberships","attachments","packageRelations"}
FORWARD_FIELDS={"authorityPatch","applicationPatch","tombstones","allocationOrder","localRefToHostId","targetDependencies","targetDerivation","resultEngineInvocationCount","engineInvocations"}
INVERSE_FIELDS={"restoreDependencies","restoreDerivedState","restoreDerivation","restoreHostSideEffects","restoreHostIds","restoreEngineInvocationCount","engineInvocations"}
FRESH_INPUT={"currentAuthoritativeDesignDigest","formallySavedProjectDigest","currentDependencySetDigest","currentDefaultEngineBundleDigest","currentEngineHostContractVersion","currentHostSideEffectContractVersion","pendingTopologyGroup","draftOverlayCount","promotedManifest"}
PROMOTED_FIELDS={"snapshotDigest","dependencySetDigest","defaultEngineBundleDigest","engineHostContractVersion","hostSideEffectContractVersion"}


def validate_closed_engine_case(case:dict,kind:str)->None:
    exact_keys(case,{"id","description","input","expected"},case.get("id","case"));inp=case["input"];expected=case["expected"]
    if kind=="resolution":
        exact_keys(inp,RES_INPUT,case["id"]+" input");exact_keys(expected,RES_EXPECTED,case["id"]+" expected")
        for item in inp["installedBundles"]+inp["alternativeBundles"]:exact_keys(item,BUNDLE_FIELDS,case["id"]+" bundle")
    elif kind=="migration":
        if case["id"]=="engine-migration-incompatible-target-not-offered":
            exact_keys(inp,MIG_DISCOVERY_INPUT,case["id"]+" input");exact_keys(inp["currentSnapshot"],SNAPSHOT_FIELDS,case["id"]+" currentSnapshot");exact_keys(inp["currentSnapshot"]["hostSideEffects"],HOST_EFFECT_FIELDS,case["id"]+" currentSnapshot hostSideEffects");exact_keys(inp["currentSnapshot"]["hostIds"],{"localRefToHostId"},case["id"]+" currentSnapshot hostIds");exact_keys(expected,{"offered","diagnosticCode","engineExecutions"},case["id"]+" expected")
            if inp["caseKind"]!="discovery":fail(f"{case['id']}: caseKind mismatch expected='discovery' actual={inp['caseKind']!r}")
            return
        exact_keys(inp,MIG_INPUT,case["id"]+" input")
        if inp["caseKind"]!="offered":fail(f"{case['id']}: caseKind mismatch expected='offered' actual={inp['caseKind']!r}")
        if inp["action"] not in {"migrate","undo"}:fail(f"{case['id']}: action mismatch expected one of ['migrate','undo'] actual={inp['action']!r}")
        for name in ("beforeSnapshot","afterSnapshot"):exact_keys(inp[name],SNAPSHOT_FIELDS,case["id"]+" "+name);exact_keys(inp[name]["hostSideEffects"],HOST_EFFECT_FIELDS,case["id"]+" "+name+" hostSideEffects");exact_keys(inp[name]["hostIds"],{"localRefToHostId"},case["id"]+" "+name+" hostIds")
        exact_keys(inp["forwardTransaction"],FORWARD_FIELDS,case["id"]+" forwardTransaction");exact_keys(inp["inverseTransaction"],INVERSE_FIELDS,case["id"]+" inverseTransaction")
        if expected.get("offered") is False:exact_keys(expected,{"offered","diagnosticCode","engineExecutions"},case["id"]+" expected")
        elif inp["action"]=="undo":exact_keys(expected,{"offered","restoredSnapshot","stableHostIds","engineExecutions","resultMode"}|({"diagnosticCode"} if expected["resultMode"]=="degraded-inspect" else set()),case["id"]+" expected")
        else:exact_keys(expected,{"offered","groupState","requiresConfirmation","atomicCommit","engineExecutions"},case["id"]+" expected")
    else:
        exact_keys(inp,FRESH_INPUT,case["id"]+" input");exact_keys(expected,{"state","staleReasons"},case["id"]+" expected")
        if inp["promotedManifest"] is not None:exact_keys(inp["promotedManifest"],PROMOTED_FIELDS,case["id"]+" promotedManifest")


def validate_nested_migration_envelopes(case_id:str,inp:dict,world:SchemaWorld)->None:
    project=world.documents["ipcraft.project-design.v1"]["$defs"]
    if inp["caseKind"]=="discovery":
        world.validate("ipcraft.project-design.v1",project["derivation"],inp["currentSnapshot"]["derivation"],case_id+" input.currentSnapshot.derivation")
        return
    core=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
    world.validate("ipcraft.core-canonical-models.v1",core["migrationContext"],inp["migration"],case_id+" input.migration")
    for path,value in (
      ("input.beforeSnapshot.derivation",inp["beforeSnapshot"]["derivation"]),
      ("input.afterSnapshot.derivation",inp["afterSnapshot"]["derivation"]),
      ("input.forwardTransaction.targetDerivation",inp["forwardTransaction"]["targetDerivation"]),
      ("input.inverseTransaction.restoreDerivation",inp["inverseTransaction"]["restoreDerivation"]),
    ):
        world.validate("ipcraft.project-design.v1",project["derivation"],value,case_id+" "+path)


def verify_engine(doc:dict,world:SchemaWorld|None=None)->tuple[int,int,int]:
    world=world or SchemaWorld(CONTRACTS)
    exact_keys(doc,{"schema","canonicalization","resolutionCases","migrationCases","freshnessCases"},"Engine catalog")
    if doc["schema"]!="ipcraft.default-engine-behavior-vectors.v1" or doc["canonicalization"]!="RFC8785-after-Appendix-F-set-projection":fail("wrong Engine catalog identity/canonicalization")
    rs=ids(doc["resolutionCases"],REQUIRED_RES,"id","resolutionCases");ms=ids(doc["migrationCases"],REQUIRED_MIG,"id","migrationCases");fs=ids(doc["freshnessCases"],REQUIRED_FRESH,"id","freshnessCases")
    for cid,c in rs.items():
        validate_closed_engine_case(c,"resolution")
        world.validate("ipcraft.project-design.v1",world.documents["ipcraft.project-design.v1"]["$defs"]["defaultEngineDependencyLock"],c["input"]["projectLock"],cid+" lock")
        for b in c["input"]["installedBundles"]+c["input"]["alternativeBundles"]:
            valid=world.is_valid("ipcraft.engine-bundle.v1",world.documents["ipcraft.engine-bundle.v1"],b["manifest"],cid+" manifest")
            if not valid and c["expected"]["diagnosticCode"]!="engine.bundle_mismatch": fail(f"{cid}: invalid manifest diagnostic mismatch expected='engine.bundle_mismatch' actual={c['expected']['diagnosticCode']!r}")
        got=eval_resolution(c)
        if got!=c["expected"]:fail(f"{cid}: resolution mismatch\n{got}\n{c['expected']}")
        if got["selectedBundleManifestDigest"] not in (None,c["input"]["projectLock"]["bundleManifestDigest"]):fail(f"{cid}: selected a different digest")
    for cid,c in ms.items():
        validate_closed_engine_case(c,"migration")
        validate_nested_migration_envelopes(cid,c["input"],world)
        if c["input"]["caseKind"]=="discovery":
            validate_migration_discovery(c["input"],world)
            got=eval_migration(c["input"])
            if got!=c["expected"]:fail(f"{cid}: migration discovery mismatch expected={cj(c['expected'])} actual={cj(got)}")
            continue
        validate_migration_binding(cid,c["input"],world)
        defs=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
        world.validate("ipcraft.core-canonical-models.v1",defs["candidateTransaction"],c["input"]["candidate"],cid+" candidate")
        got=eval_migration(c["input"])
        if got!=c["expected"]:fail(f"{cid}: migration mismatch expected={cj(c['expected'])} actual={cj(got)}")
        if c["input"].get("action") in ("undo","redo") and got["engineExecutions"]:fail(f"{cid}: Undo/Redo invoked Engine")
    for cid,c in fs.items():
        validate_closed_engine_case(c,"freshness");got=eval_fresh(c["input"])
        if got!=c["expected"]:fail(f"{cid}: freshness mismatch {got} != {c['expected']}")
    return len(rs),len(ms),len(fs)


def verify_side(doc:dict,world:SchemaWorld)->int:
    exact_keys(doc,{"schema","contractVersion","canonicalization","cases"},"side-effect catalog")
    if doc["schema"]!="ipcraft.host-side-effect-behavior-vectors.v1" or doc["contractVersion"]!="ipcraft.noc-side-effects.v1" or doc["canonicalization"]!="RFC8785-after-Appendix-F-set-projection":fail("wrong side-effect catalog identity/canonicalization")
    cases=ids(doc["cases"],REQUIRED_SIDE,"caseId","side-effect cases")
    schema=world.documents["ipcraft.noc-side-effects.v1"]
    core=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
    for cid,c in cases.items():
        exact_keys(c,{"caseId","description","document","expectedCanonicalDigest"},cid);world.validate("ipcraft.noc-side-effects.v1",schema,c["document"],cid)
        got=evaluate_side(c["document"]["input"])
        if got!=c["document"]["expected"]:fail(f"{cid}: causal side-effect mismatch expected={cj(c['document']['expected'])} actual={cj(got)}")
        actual_digest=dg(got)
        if actual_digest!=c["expectedCanonicalDigest"]:fail(f"{cid}: expected digest mismatch expected={c['expectedCanonicalDigest']} actual={actual_digest}")
        world.validate("ipcraft.core-canonical-models.v1",core["sourcePatch"],got["applicationPatch"],cid+" applicationPatch")
        if cid in {"side-effects-created-router-default-memberships","side-effects-created-routers-membership-order"} and any(i["code"]=="domain.disconnected" for i in got["impactReport"]["impacts"]):fail(f"{cid}: connected create case became disconnected")
        if cid in {"side-effects-domain-disconnected-router-delete","side-effects-domain-disconnected-link-delete","side-effects-domain-disconnected-link-update","side-effects-domain-disconnected-membership-placement"}:
            if {i["code"] for i in got["impactReport"]["impacts"]}!={"domain.disconnected"} or (got["groupState"],got["requiresConfirmation"],got["commitDisposition"])!=("auto-commit",False,"auto-commit") or not got["coreDiagnostics"]:fail(f"{cid}: connectivity case is not isolated auto-commit blocking DRC")
    return len(cases)


def mutation_tests(engine:dict,side:dict)->int:
    tests=[]
    def reject(name,fn):
        try:fn()
        except (VerificationError,SchemaViolation):return
        fail(f"mutation accepted: {name}")
    r={c["id"]:c for c in engine["resolutionCases"]};f={c["id"]:c for c in engine["freshnessCases"]};m={c["id"]:c for c in engine["migrationCases"]};s={c["caseId"]:c for c in side["cases"]}
    def engine_case(section:str,x:dict):return {**engine,section:[x if c["id"]==x["id"] else c for c in engine[section]]}
    def side_case(x:dict):return {**side,"cases":[x if c["caseId"]==x["caseId"] else c for c in side["cases"]]}
    x=copy.deepcopy(r["engine-lock-exact-available"]);x["expected"]["selectedBundleManifestDigest"]="sha256:"+"f"*64;tests.append(("wrong selected digest",lambda x=x: verify_engine(engine_case("resolutionCases",x))))
    x=copy.deepcopy(r["engine-lock-exact-available"]);x["input"]["installedBundles"][0]["installed"]=False;tests.append(("exact resolution contradicts installed=false",lambda x=x:verify_engine(engine_case("resolutionCases",x))))
    x=copy.deepcopy(r["engine-lock-no-builtin-fallback"]);x["expected"].update(outcome="exact",selectedBundleManifestDigest=x["input"]["alternativeBundles"][0]["bundleManifestDigest"],diagnosticCode=None);tests.append(("fallback alternative selected",lambda x=x:verify_engine(engine_case("resolutionCases",x))))
    x=copy.deepcopy(f["output-freshness-engine-digest-mismatch"]);x["expected"]["staleReasons"]=["authoritative-design-changed"];tests.append(("wrong freshness reason",lambda x=x:verify_engine(engine_case("freshnessCases",x))))
    for name,cid,mut in [("missing membership","side-effects-created-router-default-memberships",lambda e:e["applicationPatch"]["operations"].pop()),("nondeterministic localRef","side-effects-created-router-default-memberships",lambda e:e["applicationPatch"]["operations"][0].update(localRef="application:999999")),("deleted Attachment instead of unresolved","side-effects-deleted-slot-attachment-unresolved",lambda e:e["applicationPatch"]["operations"].__setitem__(0,{"op":"deleteRelation","relationKind":"attachment","id":"attachment.a"})),("blocked-vs-confirm priority","side-effects-package-relation-endpoint-blocked",lambda e:e.update(groupState="ready-to-commit",requiresConfirmation=True,commitDisposition="confirmation-required")),("wrong connectivity outcome","side-effects-domain-disconnected-link-delete",lambda e:e.update(coreDiagnostics=[]))]:
        x=copy.deepcopy(s[cid]);mut(x["document"]["expected"]);tests.append((name,lambda x=x: verify_side(side_case(x),SchemaWorld(CONTRACTS))))
    x=copy.deepcopy(s["side-effects-domain-disconnected-link-update"]);x["document"]["expected"]["impactReport"]["impacts"]=[];tests.append(("wrong link-update connectivity",lambda x=x:verify_side(side_case(x),SchemaWorld(CONTRACTS))))
    base=m["engine-migration-atomic-commit"]
    migrations=[
      ("corrupt inverse derived state",lambda x:x["input"]["inverseTransaction"]["restoreDerivedState"]["routers"].pop()),
      ("corrupt inverse Host side effects",lambda x:x["input"]["inverseTransaction"]["restoreHostSideEffects"]["domainMemberships"].pop()),
      ("corrupt inverse provenance",lambda x:x["input"]["inverseTransaction"]["restoreDerivation"].update(defaultEngineBundleDigest="sha256:"+"f"*64)),
      ("wrong Authority digest",lambda x:(x["input"]["candidate"]["authorityPatch"]["source"].update(bundleDigest="sha256:"+"f"*64),x["input"]["forwardTransaction"]["authorityPatch"]["source"].update(bundleDigest="sha256:"+"f"*64))),
      ("wrong migration impact details",lambda x:x["input"]["candidate"]["impactReport"]["impacts"][-1]["details"].update(targetBundleDigest="sha256:"+"f"*64)),
      ("wrong dependency replacement",lambda x:x["input"]["forwardTransaction"]["targetDependencies"][1].update(version="9")),
      ("wrong candidateDigest",lambda x:x["input"]["candidate"].update(candidateDigest="sha256:"+"f"*64)),
      ("Undo invoking Engine",lambda x:x["input"]["inverseTransaction"]["engineInvocations"].append("sha256:"+"a"*64)),
      ("wrong before Derived State digest",lambda x:x["input"]["beforeSnapshot"]["derivation"].update(derivedStateDigest="sha256:"+"f"*64)),
      ("wrong before Derived State revision",lambda x:x["input"]["beforeSnapshot"]["derivation"].update(derivedStateRevision=99)),
      ("wrong after Derived State digest",lambda x:x["input"]["afterSnapshot"]["derivation"].update(derivedStateDigest="sha256:"+"f"*64)),
      ("wrong after Derived State revision",lambda x:x["input"]["afterSnapshot"]["derivation"].update(derivedStateRevision=7)),
      ("wrong applicability base Derived State digest",lambda x:x["input"]["applicability"].update(baseDerivedStateDigest="sha256:"+"f"*64)),
      ("wrong applicability base Derived State revision",lambda x:x["input"]["applicability"].update(baseDerivedStateRevision=99)),
    ]
    for name,mut in migrations:
        x=copy.deepcopy(base);mut(x);tests.append((name,lambda x=x:verify_engine(engine_case("migrationCases",x))))
    for name,mut in [
      ("missing tombstone",lambda x:(x["input"]["candidate"]["tombstones"].pop(),x["input"]["forwardTransaction"]["tombstones"].pop())),
      ("extra tombstone",lambda x:(x["input"]["candidate"]["tombstones"].append(copy.deepcopy(x["input"]["candidate"]["tombstones"][0])|{"id":"extra.id"}),x["input"]["forwardTransaction"]["tombstones"].append(copy.deepcopy(x["input"]["forwardTransaction"]["tombstones"][0])|{"id":"extra.id"}))),
      ("wrong tombstone value",lambda x:(x["input"]["candidate"]["tombstones"][0]["value"].update(id="wrong.id"),x["input"]["forwardTransaction"]["tombstones"][0]["value"].update(id="wrong.id"))),
    ]:
        x=copy.deepcopy(base);mut(x);x["input"]["candidate"]["candidateDigest"]=sha256_digest(normalize_candidate(SchemaWorld(CONTRACTS),x["input"]["candidate"],[],name));tests.append((name,lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["candidate"]["tombstones"].pop();x["input"]["forwardTransaction"]["tombstones"].pop();world=SchemaWorld(CONTRACTS);x["input"]["candidate"]["candidateDigest"]=sha256_digest(normalize_candidate(world,x["input"]["candidate"],[],"mutually corrupt candidate"));tests.append(("mutually corrupt candidate and forward tombstones",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    for name,mut in [
      ("missing allocation",lambda x:(x["input"]["candidate"]["allocationOrder"].pop(),x["input"]["forwardTransaction"]["allocationOrder"].pop())),
      ("extra allocation",lambda x:(x["input"]["candidate"]["allocationOrder"].append("authority:extra"),x["input"]["forwardTransaction"]["allocationOrder"].append("authority:extra"))),
      ("reverse allocation",lambda x:(x["input"]["candidate"]["allocationOrder"].reverse(),x["input"]["forwardTransaction"]["allocationOrder"].reverse())),
    ]:
        x=copy.deepcopy(base);mut(x);x["input"]["candidate"]["candidateDigest"]=sha256_digest(normalize_candidate(SchemaWorld(CONTRACTS),x["input"]["candidate"],[],name));tests.append((name,lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["afterSnapshot"]["hostIds"]["localRefToHostId"]={};tests.append(("materialized Host mapping missing",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["forwardTransaction"]["localRefToHostId"]["authority:router-d"]="wrong-host-router";tests.append(("wrong localRef mapping",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["forwardTransaction"]["localRefToHostId"].pop("authority:router-d");tests.append(("missing localRef mapping",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["forwardTransaction"]["localRefToHostId"]["authority:extra"]="host-extra";tests.append(("extra localRef mapping",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["currentManifest"]["version"]="9.0.0";tests.append(("current manifest version mismatch",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["beforeSnapshot"]["dependencies"][0]["version"]="9.0.0";x["input"]["inverseTransaction"]["restoreDependencies"][0]["version"]="9.0.0";tests.append(("current dependency lock mismatch",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["beforeSnapshot"]["derivation"]["engineCompatibilityVersion"]="9";x["input"]["inverseTransaction"]["restoreDerivation"]["engineCompatibilityVersion"]="9";tests.append(("current derivation compatibility mismatch",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["candidate"]["authorityPatch"]["source"]["version"]="9.0.0";x["input"]["forwardTransaction"]["authorityPatch"]["source"]["version"]="9.0.0";x["input"]["sideEffectInput"]["authorityPatch"]["source"]["version"]="9.0.0";x["input"]["candidate"]["candidateDigest"]=sha256_digest(normalize_candidate(SchemaWorld(CONTRACTS),x["input"]["candidate"],[],"Authority version"));tests.append(("Authority version mismatch",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    for field,value in (("version","9.0.0"),("engineCompatibilityVersion","9"),("engineHostContractVersion","ipcraft.engine-host.v9"),("hostSideEffectContractVersion","ipcraft.noc-side-effects.v9")):
        x=copy.deepcopy(base)
        x["input"]["candidate"]["migration"]["targetDefaultEngineLock"][field]=value
        target_dependency=next(dependency for dependency in x["input"]["forwardTransaction"]["targetDependencies"] if dependency["kind"]=="default-engine");target_dependency[field]=value
        x["input"]["candidate"]["candidateDigest"]=sha256_digest(normalize_candidate(SchemaWorld(CONTRACTS),x["input"]["candidate"],[],"mutually corrupt target "+field))
        tests.append(("mutually corrupt target "+field,lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["forwardTransaction"]["engineInvocations"].append("sha256:"+"b"*64);tests.append(("Redo stored forward invokes Engine",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["forwardTransaction"]["resultEngineInvocationCount"]=9;x["input"]["afterSnapshot"]["engineInvocationCount"]=9;tests.append(("migration invocation count mismatch",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["migrationEngineInvocations"]=["sha256:"+"a"*64];x["expected"]["engineExecutions"]=["sha256:"+"a"*64];tests.append(("mutually corrupt migration Engine evidence",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["action"]="redo";tests.append(("unknown migration action",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(base);x["input"]["caseKind"]="other";tests.append(("unknown migration caseKind",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(m["engine-migration-blocked-by-package-relation"]);x["input"]["sideEffectInput"]["packageRelations"]=[];tests.append(("causal blocked relation removed",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    discovery=m["engine-migration-incompatible-target-not-offered"]
    x=copy.deepcopy(discovery);x["input"]["caseKind"]="other";tests.append(("unknown discovery caseKind",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(discovery);x["input"]["currentSnapshot"]["derivation"]["derivedStateDigest"]="sha256:"+"f"*64;tests.append(("discovery current digest corrupt",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    for evidence in ("candidate","afterSnapshot","forwardTransaction","engineInvocations"):
        x=copy.deepcopy(discovery);x["input"][evidence]={} if evidence!="engineInvocations" else ["sha256:"+"b"*64];tests.append((f"discovery carries {evidence} evidence",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(discovery);x["expected"]["engineExecutions"]=["sha256:"+"b"*64];tests.append(("discovery expected target execution",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    # Exact ID-set rejection for every behavior section/catalog.
    for section in ("resolutionCases","migrationCases","freshnessCases"):
        missing=copy.deepcopy(engine);missing[section]=missing[section][1:];tests.append((section+" missing ID",lambda x=missing:verify_engine(x)))
        extra=copy.deepcopy(engine);addition=copy.deepcopy(extra[section][0]);addition["id"]="unexpected-extra-id";extra[section].append(addition);tests.append((section+" extra ID",lambda x=extra:verify_engine(x)))
        duplicate=copy.deepcopy(engine);duplicate[section].append(copy.deepcopy(duplicate[section][0]));tests.append((section+" duplicate ID",lambda x=duplicate:verify_engine(x)))
    missing=copy.deepcopy(side);missing["cases"]=missing["cases"][1:];tests.append(("side cases missing ID",lambda x=missing:verify_side(x,SchemaWorld(CONTRACTS))))
    extra=copy.deepcopy(side);addition=copy.deepcopy(extra["cases"][0]);addition["caseId"]="unexpected-extra-id";extra["cases"].append(addition);tests.append(("side cases extra ID",lambda x=extra:verify_side(x,SchemaWorld(CONTRACTS))))
    duplicate=copy.deepcopy(side);duplicate["cases"].append(copy.deepcopy(duplicate["cases"][0]));tests.append(("side cases duplicate ID",lambda x=duplicate:verify_side(x,SchemaWorld(CONTRACTS))))
    # Arbitrary fields are rejected at every major closed envelope family.
    mutations=[]
    x=copy.deepcopy(engine);x["extra"]=True;mutations.append(("Engine catalog extra field",x,True))
    for section,cid,locations in [("resolutionCases","engine-lock-exact-available",("case","input","expected")),("migrationCases","engine-migration-atomic-commit",("case","input","expected")),("freshnessCases","output-freshness-saved-current-equal",("case","input","expected"))]:
        for location in locations:
            x=copy.deepcopy({"resolutionCases":r,"migrationCases":m,"freshnessCases":f}[section][cid]);x if location=="case" else x[location]
            (x if location=="case" else x[location])["extra"]=True;mutations.append((f"{section} {location} extra field",engine_case(section,x),True))
    x=copy.deepcopy(side);x["extra"]=True;tests.append(("side catalog extra field",lambda x=x:verify_side(x,SchemaWorld(CONTRACTS))))
    for location in ("case","document"):
        x=copy.deepcopy(s["side-effects-created-router-default-memberships"]);(x if location=="case" else x["document"])["extra"]=True;tests.append((f"side {location} extra field",lambda x=x:verify_side(side_case(x),SchemaWorld(CONTRACTS))))
    x=copy.deepcopy(r["engine-lock-exact-available"]);x["input"]["installedBundles"][0]["extra"]=True;tests.append(("resolution bundle extra field",lambda x=x:verify_engine(engine_case("resolutionCases",x))))
    x=copy.deepcopy(m["engine-migration-atomic-commit"]);x["input"]["migration"]["extra"]=True;tests.append(("migration context extra field",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    for location in ("beforeSnapshot","afterSnapshot"):
        x=copy.deepcopy(m["engine-migration-atomic-commit"]);x["input"][location]["derivation"]["extra"]=True;tests.append((f"migration {location} derivation extra field",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(m["engine-migration-atomic-commit"]);x["input"]["forwardTransaction"]["targetDerivation"]["extra"]=True;tests.append(("migration forward derivation extra field",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(m["engine-migration-atomic-commit"]);x["input"]["inverseTransaction"]["restoreDerivation"]["extra"]=True;tests.append(("migration inverse restoreDerivation extra field",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(m["engine-migration-incompatible-target-not-offered"]);x["input"]["currentSnapshot"]["derivation"]["extra"]=True;tests.append(("migration current derivation extra field",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    for location in ("beforeSnapshot","forwardTransaction","inverseTransaction"):
        x=copy.deepcopy(m["engine-migration-atomic-commit"]);x["input"][location]["extra"]=True;tests.append((f"migration {location} extra field",lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(f["output-freshness-saved-current-equal"]);x["input"]["promotedManifest"]["extra"]=True;tests.append(("freshness promoted manifest extra field",lambda x=x:verify_engine(engine_case("freshnessCases",x))))
    for location in ("input","expected"):
        x=copy.deepcopy(s["side-effects-created-router-default-memberships"]);x["document"][location]["extra"]=True;tests.append((f"side document {location} extra field",lambda x=x:verify_side(side_case(x),SchemaWorld(CONTRACTS))))
    for name,value,_ in mutations:tests.append((name,lambda x=value:verify_engine(x)))
    for name,fn in tests:reject(name,fn)
    return len(tests)


def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument("--skip-mutations",action="store_true");a=ap.parse_args()
    engine=load(VECTORS/"default-engine-lock-v1.json");side=load(VECTORS/"host-side-effects-v1.json");world=SchemaWorld(CONTRACTS)
    rc,mc,fc=verify_engine(engine,world);sc=verify_side(side,world);mut=0 if a.skip_mutations else mutation_tests(engine,side)
    print(f"engine/side-effect behavioral vectors passed: {rc} resolution, {mc} migration, {fc} freshness, {sc} causal side-effect cases; {mut} mutations rejected")
    return 0


if __name__=="__main__":
    try:raise SystemExit(main())
    except (VerificationError,SchemaViolation) as e:raise SystemExit(f"verification failed: {e}")
