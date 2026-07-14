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

from verify_canonical_vectors import SchemaWorld, normalize_candidate, sha256_digest

ROOT=Path(__file__).resolve().parents[3]
CONTRACTS=ROOT/"docs/contracts"
VECTORS=CONTRACTS/"vectors"
REQUIRED_RES={"engine-lock-exact-available","engine-lock-missing","engine-lock-revoked","engine-lock-corrupt","engine-lock-digest-mismatch","engine-lock-same-compatibility-different-digest","engine-lock-manifest-metadata-mismatch","engine-lock-manifest-id-mismatch","engine-lock-manifest-host-mismatch","engine-lock-manifest-side-effect-mismatch","engine-lock-manifest-compatibility-mismatch","engine-lock-manifest-platform-metadata-mismatch","engine-lock-platform-incompatible","engine-lock-host-abi-incompatible","engine-lock-side-effect-contract-incompatible","engine-lock-no-builtin-fallback","engine-lock-newer-target-discovered","unsupported-but-valid-bundle-retained"}
REQUIRED_MIG={"engine-migration-compatible-target-offered","engine-migration-incompatible-target-not-offered","engine-migration-atomic-commit","engine-migration-blocked-by-package-relation","engine-migration-undo-exact-inverse","engine-migration-undo-source-missing-degraded"}
REQUIRED_FRESH={"output-freshness-saved-current-equal","output-freshness-accepted-unsaved-edit","output-freshness-pending-topology","output-freshness-draft-overlay","output-freshness-engine-digest-mismatch","output-freshness-engine-host-contract-mismatch","output-freshness-side-effect-contract-mismatch","output-freshness-no-promotion"}
REQUIRED_SIDE={"side-effects-created-router-default-memberships","side-effects-created-routers-membership-order","side-effects-deleted-router-memberships","side-effects-deleted-router-attachment-unresolved","side-effects-deleted-slot-attachment-unresolved","side-effects-package-relation-endpoint-unresolved","side-effects-package-relation-endpoint-blocked","side-effects-empty-non-default-domain-tombstone","side-effects-empty-default-domain-preserved","side-effects-domain-disconnected-router-delete","side-effects-domain-disconnected-link-delete","side-effects-domain-disconnected-link-update","side-effects-domain-disconnected-membership-placement","side-effects-combined-deterministic-order"}


class Error(RuntimeError): pass
def fail(msg:str)->None: raise Error(msg)
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
    lock=inp["projectLock"]; exact=next((b for b in inp["installedBundles"] if b["bundleManifestDigest"]==lock["bundleManifestDigest"]),None)
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
    cur=inp["migration"]["currentDefaultEngineLock"]; target=inp["migration"]["targetDefaultEngineLock"]; man=inp["targetManifest"]
    eligible=cur["engineCompatibilityVersion"] in man["migrationFromCompatibilityVersions"]
    if not eligible:return {"offered":False,"diagnosticCode":"engine.migration_incompatible","engineExecutions":[]}
    if inp.get("action")=="undo":
        restored=apply_inverse(inp["afterSnapshot"],inp["inverseTransaction"])
        return {"offered":True,"restoredSnapshot":"beforeSnapshot","stableHostIds":restored["hostIds"]==inp["beforeSnapshot"]["hostIds"],"engineExecutions":list(inp["inverseTransaction"]["engineInvocations"]),"resultMode":"normal" if inp["sourceBundleAvailable"] else "degraded-inspect",**({} if inp["sourceBundleAvailable"] else {"diagnosticCode":"engine.bundle_missing"})}
    blocked=any(i["code"]=="package_relation.endpoint_blocks_candidate" for i in recompute_migration_impacts(inp))
    return {"offered":True,"groupState":"blocked" if blocked else "ready-to-commit","requiresConfirmation":not blocked,"atomicCommit":not blocked,"engineExecutions":list(inp["forwardTransaction"]["engineInvocations"])}


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
    result["dependencies"]=copy.deepcopy(tx["targetDependencies"]);result["derivation"]=copy.deepcopy(tx["targetDerivation"]);result["hostIds"]={"localRefToHostId":copy.deepcopy(mapping)};result["engineInvocationCount"]+=len(tx["engineInvocations"])
    return result


def apply_inverse(after:dict,tx:dict)->dict:
    result=copy.deepcopy(after);result["dependencies"]=copy.deepcopy(tx["restoreDependencies"]);result["derivedState"]=copy.deepcopy(tx["restoreDerivedState"]);result["derivation"]=copy.deepcopy(tx["restoreDerivation"]);result["hostSideEffects"]=copy.deepcopy(tx["restoreHostSideEffects"]);result["hostIds"]=copy.deepcopy(tx["restoreHostIds"]);result["engineInvocationCount"]=tx["restoreEngineInvocationCount"]+len(tx["engineInvocations"]);return result


def recompute_migration_impacts(inp:dict)->list[dict]:
    current=inp["migration"]["currentDefaultEngineLock"];target=inp["migration"]["targetDefaultEngineLock"]
    mandatory={"code":"engine_migration.dependency_replaced","severity":"warning","dataLoss":False,"subjects":[{"kind":"project","id":"project.main"}],"details":{"lockId":current["lockId"],"currentBundleDigest":current["bundleManifestDigest"],"targetBundleDigest":target["bundleManifestDigest"]},"resolution":"confirm-or-discard"}
    impacts=evaluate_side(inp["sideEffectInput"])["impactReport"]["impacts"]+[mandatory]
    impacts.sort(key=lambda x:(x["code"],x["severity"],x["dataLoss"],cj(x["subjects"]),cj(x["details"]),x["resolution"]))
    return impacts


def validate_migration_binding(inp:dict,world:SchemaWorld)->None:
    cur=inp["migration"]["currentDefaultEngineLock"]; target=inp["migration"]["targetDefaultEngineLock"]; app=inp["applicability"]
    if cur["bundleManifestDigest"]!=app["defaultEngineBundleDigest"] or cur["lockId"]!=app["defaultEngineLockId"]: fail("migration current lock is not bound to applicability")
    if cur["lockId"]!=target["lockId"] or cur["bundleManifestDigest"]==target["bundleManifestDigest"]: fail("migration target identity invalid")
    meta=("id","version","engineCompatibilityVersion","engineHostContractVersion","hostSideEffectContractVersion","supportedPlatformAbis")
    if any(inp["targetManifest"][k]!=target[k] for k in meta): fail("migration target manifest mismatch")
    old={x["lockId"]:x for x in inp["beforeSnapshot"]["dependencies"] if x["kind"]!="default-engine"}; new={x["lockId"]:x for x in inp["forwardTransaction"]["targetDependencies"] if x["kind"]!="default-engine"}
    if old!=new: fail("migration changed non-Engine dependency")
    if inp["forwardTransaction"]["targetDerivation"]["defaultEngineBundleDigest"]!=target["bundleManifestDigest"]: fail("migration derivation target mismatch")
    candidate=inp["candidate"]
    if candidate["authorityPatch"]!=inp["forwardTransaction"]["authorityPatch"] or candidate["authorityPatch"]["source"]["bundleDigest"]!=target["bundleManifestDigest"]:fail("migration Authority source mismatch")
    side=evaluate_side(inp["sideEffectInput"]);expected_ops=side["applicationPatch"]["operations"]+[{"op":"updateEntity","entityKind":"project","id":"project.main","set":{"dependencies":inp["forwardTransaction"]["targetDependencies"]},"unset":[]},{"op":"updateEntity","entityKind":"topology","id":"topology.main","set":{"derivation":inp["forwardTransaction"]["targetDerivation"]},"unset":[]}]
    side_document={"schema":"ipcraft.noc-side-effects.v1","contractVersion":"ipcraft.noc-side-effects.v1","input":inp["sideEffectInput"],"expected":side}
    world.validate("ipcraft.noc-side-effects.v1",world.documents["ipcraft.noc-side-effects.v1"],side_document,"migration side effects")
    core_defs=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
    for name in ("beforeSnapshot","afterSnapshot"):world.validate("ipcraft.core-canonical-models.v1",core_defs["derivedState"],inp[name]["derivedState"],"migration "+name+" derivedState")
    if candidate["applicationPatch"]["operations"]!=expected_ops or candidate["applicationPatch"]!=inp["forwardTransaction"]["applicationPatch"]:fail("migration Application transaction mismatch")
    impacts=recompute_migration_impacts(inp)
    if candidate["impactReport"]["impacts"]!=impacts:fail("migration impact report is not causally derived")
    normalized=normalize_candidate(world,candidate,[],"migration candidate")
    if candidate["candidateDigest"]!=sha256_digest(normalized):fail("migration candidateDigest mismatch")
    if apply_forward(inp["beforeSnapshot"],inp["forwardTransaction"])!=inp["afterSnapshot"]:fail("migration forward transaction does not produce afterSnapshot")
    if apply_inverse(inp["afterSnapshot"],inp["inverseTransaction"])!=inp["beforeSnapshot"]:fail("migration inverse transaction does not restore beforeSnapshot")
    if inp["inverseTransaction"]["engineInvocations"]:fail("Undo/Redo invokes an Engine")


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
MIG_INPUT={"action","sourceBundleAvailable","migration","applicability","targetManifest","sideEffectInput","beforeSnapshot","afterSnapshot","candidate","forwardTransaction","inverseTransaction"}
SNAPSHOT_FIELDS={"dependencies","derivedState","derivation","hostSideEffects","hostIds","engineInvocationCount"}
HOST_EFFECT_FIELDS={"domains","domainMemberships","attachments","packageRelations"}
FORWARD_FIELDS={"authorityPatch","applicationPatch","tombstones","allocationOrder","localRefToHostId","targetDependencies","targetDerivation","engineInvocations"}
INVERSE_FIELDS={"restoreDependencies","restoreDerivedState","restoreDerivation","restoreHostSideEffects","restoreHostIds","restoreEngineInvocationCount","engineInvocations"}
FRESH_INPUT={"currentAuthoritativeDesignDigest","formallySavedProjectDigest","currentDependencySetDigest","currentDefaultEngineBundleDigest","currentEngineHostContractVersion","currentHostSideEffectContractVersion","pendingTopologyGroup","draftOverlayCount","promotedManifest"}
PROMOTED_FIELDS={"snapshotDigest","dependencySetDigest","defaultEngineBundleDigest","engineHostContractVersion","hostSideEffectContractVersion"}


def validate_closed_engine_case(case:dict,kind:str)->None:
    exact_keys(case,{"id","description","input","expected"},case.get("id","case"));inp=case["input"];expected=case["expected"]
    if kind=="resolution":
        exact_keys(inp,RES_INPUT,case["id"]+" input");exact_keys(expected,RES_EXPECTED,case["id"]+" expected")
        for item in inp["installedBundles"]+inp["alternativeBundles"]:exact_keys(item,BUNDLE_FIELDS,case["id"]+" bundle")
    elif kind=="migration":
        exact_keys(inp,MIG_INPUT,case["id"]+" input")
        for name in ("beforeSnapshot","afterSnapshot"):exact_keys(inp[name],SNAPSHOT_FIELDS,case["id"]+" "+name);exact_keys(inp[name]["hostSideEffects"],HOST_EFFECT_FIELDS,case["id"]+" "+name+" hostSideEffects");exact_keys(inp[name]["hostIds"],{"localRefToHostId"},case["id"]+" "+name+" hostIds")
        exact_keys(inp["forwardTransaction"],FORWARD_FIELDS,case["id"]+" forwardTransaction");exact_keys(inp["inverseTransaction"],INVERSE_FIELDS,case["id"]+" inverseTransaction")
        if expected.get("offered") is False:exact_keys(expected,{"offered","diagnosticCode","engineExecutions"},case["id"]+" expected")
        elif inp["action"]=="undo":exact_keys(expected,{"offered","restoredSnapshot","stableHostIds","engineExecutions","resultMode"}|({"diagnosticCode"} if expected["resultMode"]=="degraded-inspect" else set()),case["id"]+" expected")
        else:exact_keys(expected,{"offered","groupState","requiresConfirmation","atomicCommit","engineExecutions"},case["id"]+" expected")
    else:
        exact_keys(inp,FRESH_INPUT,case["id"]+" input");exact_keys(expected,{"state","staleReasons"},case["id"]+" expected")
        if inp["promotedManifest"] is not None:exact_keys(inp["promotedManifest"],PROMOTED_FIELDS,case["id"]+" promotedManifest")


def verify_engine(doc:dict,world:SchemaWorld|None=None)->tuple[int,int,int]:
    world=world or SchemaWorld(CONTRACTS)
    exact_keys(doc,{"schema","canonicalization","resolutionCases","migrationCases","freshnessCases"},"Engine catalog")
    if doc["schema"]!="ipcraft.default-engine-behavior-vectors.v1":fail("wrong Engine catalog schema")
    rs=ids(doc["resolutionCases"],REQUIRED_RES,"id","resolutionCases");ms=ids(doc["migrationCases"],REQUIRED_MIG,"id","migrationCases");fs=ids(doc["freshnessCases"],REQUIRED_FRESH,"id","freshnessCases")
    for cid,c in rs.items():
        validate_closed_engine_case(c,"resolution")
        world.validate("ipcraft.project-design.v1",world.documents["ipcraft.project-design.v1"]["$defs"]["defaultEngineDependencyLock"],c["input"]["projectLock"],cid+" lock")
        for b in c["input"]["installedBundles"]+c["input"]["alternativeBundles"]:
            valid=world.is_valid("ipcraft.engine-bundle.v1",world.documents["ipcraft.engine-bundle.v1"],b["manifest"],cid+" manifest")
            if not valid and c["expected"]["diagnosticCode"]!="engine.bundle_mismatch": fail(f"{cid}: invalid manifest is not classified as bundle mismatch")
        got=eval_resolution(c)
        if got!=c["expected"]:fail(f"{cid}: resolution mismatch\n{got}\n{c['expected']}")
        if got["selectedBundleManifestDigest"] not in (None,c["input"]["projectLock"]["bundleManifestDigest"]):fail(f"{cid}: selected a different digest")
    for cid,c in ms.items():
        validate_closed_engine_case(c,"migration");validate_migration_binding(c["input"],world)
        defs=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
        world.validate("ipcraft.core-canonical-models.v1",defs["candidateTransaction"],c["input"]["candidate"],cid+" candidate")
        got=eval_migration(c["input"])
        if got!=c["expected"]:fail(f"{cid}: migration mismatch")
        if c["input"].get("action") in ("undo","redo") and got["engineExecutions"]:fail(f"{cid}: Undo/Redo invoked Engine")
    for cid,c in fs.items():
        validate_closed_engine_case(c,"freshness");got=eval_fresh(c["input"])
        if got!=c["expected"]:fail(f"{cid}: freshness mismatch {got} != {c['expected']}")
    return len(rs),len(ms),len(fs)


def verify_side(doc:dict,world:SchemaWorld)->int:
    exact_keys(doc,{"schema","contractVersion","canonicalization","cases"},"side-effect catalog")
    if doc["schema"]!="ipcraft.host-side-effect-behavior-vectors.v1" or doc["contractVersion"]!="ipcraft.noc-side-effects.v1":fail("wrong side-effect catalog identity")
    cases=ids(doc["cases"],REQUIRED_SIDE,"caseId","side-effect cases")
    schema=world.documents["ipcraft.noc-side-effects.v1"]
    core=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
    for cid,c in cases.items():
        exact_keys(c,{"caseId","description","document","expectedCanonicalDigest"},cid);world.validate("ipcraft.noc-side-effects.v1",schema,c["document"],cid)
        got=evaluate_side(c["document"]["input"])
        if got!=c["document"]["expected"]:fail(f"{cid}: causal side-effect mismatch")
        if dg(got)!=c["expectedCanonicalDigest"]:fail(f"{cid}: expected digest mismatch")
        world.validate("ipcraft.core-canonical-models.v1",core["sourcePatch"],got["applicationPatch"],cid+" applicationPatch")
        if cid in {"side-effects-created-router-default-memberships","side-effects-created-routers-membership-order"} and any(i["code"]=="domain.disconnected" for i in got["impactReport"]["impacts"]):fail(f"{cid}: connected create case became disconnected")
        if cid in {"side-effects-domain-disconnected-router-delete","side-effects-domain-disconnected-link-delete","side-effects-domain-disconnected-link-update","side-effects-domain-disconnected-membership-placement"}:
            if {i["code"] for i in got["impactReport"]["impacts"]}!={"domain.disconnected"} or (got["groupState"],got["requiresConfirmation"],got["commitDisposition"])!=("auto-commit",False,"auto-commit") or not got["coreDiagnostics"]:fail(f"{cid}: connectivity case is not isolated auto-commit blocking DRC")
    return len(cases)


def mutation_tests(engine:dict,side:dict)->int:
    tests=[]
    def reject(name,fn):
        try:fn()
        except Exception:return
        fail(f"mutation accepted: {name}")
    r={c["id"]:c for c in engine["resolutionCases"]};f={c["id"]:c for c in engine["freshnessCases"]};m={c["id"]:c for c in engine["migrationCases"]};s={c["caseId"]:c for c in side["cases"]}
    def engine_case(section:str,x:dict):return {**engine,section:[x if c["id"]==x["id"] else c for c in engine[section]]}
    def side_case(x:dict):return {**side,"cases":[x if c["caseId"]==x["caseId"] else c for c in side["cases"]]}
    x=copy.deepcopy(r["engine-lock-exact-available"]);x["expected"]["selectedBundleManifestDigest"]="sha256:"+"f"*64;tests.append(("wrong selected digest",lambda x=x: verify_engine(engine_case("resolutionCases",x))))
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
    ]
    for name,mut in migrations:
        x=copy.deepcopy(base);mut(x);tests.append((name,lambda x=x:verify_engine(engine_case("migrationCases",x))))
    x=copy.deepcopy(m["engine-migration-blocked-by-package-relation"]);x["input"]["sideEffectInput"]["packageRelations"]=[];tests.append(("causal blocked relation removed",lambda x=x:verify_engine(engine_case("migrationCases",x))))
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
    except Error as e:raise SystemExit(f"verification failed: {e}")
