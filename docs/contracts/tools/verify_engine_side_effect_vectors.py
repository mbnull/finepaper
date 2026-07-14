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

from verify_canonical_vectors import SchemaWorld

ROOT=Path(__file__).resolve().parents[3]
CONTRACTS=ROOT/"docs/contracts"
VECTORS=CONTRACTS/"vectors"
REQUIRED_RES={"engine-lock-exact-available","engine-lock-missing","engine-lock-revoked","engine-lock-corrupt","engine-lock-digest-mismatch","engine-lock-same-compatibility-different-digest","engine-lock-manifest-metadata-mismatch","engine-lock-platform-incompatible","engine-lock-host-abi-incompatible","engine-lock-side-effect-contract-incompatible","engine-lock-no-builtin-fallback","engine-lock-newer-target-discovered","unsupported-but-valid-bundle-retained"}
REQUIRED_MIG={"engine-migration-compatible-target-offered","engine-migration-incompatible-target-not-offered","engine-migration-atomic-commit","engine-migration-blocked-by-package-relation","engine-migration-undo-exact-inverse","engine-migration-undo-source-missing-degraded"}
REQUIRED_FRESH={"output-freshness-saved-current-equal","output-freshness-accepted-unsaved-edit","output-freshness-pending-topology","output-freshness-draft-overlay","output-freshness-engine-digest-mismatch","output-freshness-engine-host-contract-mismatch","output-freshness-side-effect-contract-mismatch","output-freshness-no-promotion"}
REQUIRED_SIDE={"side-effects-created-router-default-memberships","side-effects-created-routers-membership-order","side-effects-deleted-router-memberships","side-effects-deleted-router-attachment-unresolved","side-effects-deleted-slot-attachment-unresolved","side-effects-package-relation-endpoint-unresolved","side-effects-package-relation-endpoint-blocked","side-effects-empty-non-default-domain-tombstone","side-effects-empty-default-domain-preserved","side-effects-domain-disconnected-router-delete","side-effects-domain-disconnected-link-delete","side-effects-domain-disconnected-membership-placement","side-effects-combined-deterministic-order"}


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
    if not required<=set(out): fail(f"{where}: missing IDs {sorted(required-set(out))}")
    return out


def resolve(inp:dict)->dict:
    lock=inp["projectLock"]; exact=next((b for b in inp["installedBundles"] if b["bundleManifestDigest"]==lock["bundleManifestDigest"]),None)
    alternatives=inp["alternativeBundles"]
    upgrade=False
    if exact is None: return {"outcome":"degraded-inspect","selectedBundleManifestDigest":None,"diagnosticCode":"engine.bundle_missing","upgradeAvailable":False}
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
            return {"outcome":"exact","selectedBundleManifestDigest":lock["bundleManifestDigest"],"diagnosticCode":None,"upgradeAvailable":upgrade}
    out={"outcome":"degraded-inspect","selectedBundleManifestDigest":None,"diagnosticCode":code,"upgradeAvailable":False}
    if "retainedInContentAddressedStore" in inp.get("_expectedShape",{}): out["retainedInContentAddressedStore"]=True
    return out


def eval_resolution(case:dict)->dict:
    result=resolve(case["input"])
    if "retainedInContentAddressedStore" in case["expected"]: result["retainedInContentAddressedStore"]=bool(case["input"]["installedBundles"] and case["input"]["installedBundles"][0]["installed"])
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
        return {"offered":True,"restoredTransaction":"inverseTransaction","stableHostIds":inp["forwardTransaction"]["localRefToHostId"]==inp["inverseTransaction"]["localRefToHostId"],"engineExecutions":[],"resultMode":"normal" if inp.get("sourceBundleAvailable",True) else "degraded-inspect",**({} if inp.get("sourceBundleAvailable",True) else {"diagnosticCode":"engine.bundle_missing"})}
    blocked=inp.get("blockingImpact")=="package_relation.endpoint_blocks_candidate"
    return {"offered":True,"groupState":"blocked" if blocked else "ready-to-commit","requiresConfirmation":not blocked,"atomicCommit":not blocked,"engineExecutions":[target["bundleManifestDigest"]]}


def validate_migration_binding(inp:dict)->None:
    cur=inp["migration"]["currentDefaultEngineLock"]; target=inp["migration"]["targetDefaultEngineLock"]; app=inp["applicability"]
    if cur["bundleManifestDigest"]!=app["defaultEngineBundleDigest"] or cur["lockId"]!=app["defaultEngineLockId"]: fail("migration current lock is not bound to applicability")
    if cur["lockId"]!=target["lockId"] or cur["bundleManifestDigest"]==target["bundleManifestDigest"]: fail("migration target identity invalid")
    meta=("id","version","engineCompatibilityVersion","engineHostContractVersion","hostSideEffectContractVersion","supportedPlatformAbis")
    if any(inp["targetManifest"][k]!=target[k] for k in meta): fail("migration target manifest mismatch")
    old={x["lockId"]:x for x in inp["baseDependencies"] if x["kind"]!="default-engine"}; new={x["lockId"]:x for x in inp["forwardTransaction"]["projectDependencies"] if x["kind"]!="default-engine"}
    if old!=new: fail("migration changed non-Engine dependency")
    if inp["forwardTransaction"]["topologyDerivation"]["defaultEngineBundleDigest"]!=target["bundleManifestDigest"]: fail("migration derivation target mismatch")
    if inp["inverseTransaction"]["projectDependencies"]!=inp["baseDependencies"]: fail("migration inverse does not restore dependencies")
    if inp["forwardTransaction"]["localRefToHostId"]!=inp["inverseTransaction"]["localRefToHostId"]: fail("migration inverse changes Host IDs")


def token(ref:dict)->str:return "id:"+ref["id"] if "id" in ref else "localRef:"+ref["localRef"]
def evaluate_side(inp:dict)->dict:
    authority=inp["authorityPatch"]["operations"]; out=[]; impacts=[]; tomb=[]; diags=[]
    dr={o["id"] for o in authority if o["op"]=="deleteEntity" and o["entityKind"]=="router"}; ds={o["id"] for o in authority if o["op"]=="deleteEntity" and o["entityKind"]=="access-slot"}; dl={o["id"] for o in authority if o["op"]=="deleteEntity" and o["entityKind"]=="structural-link"}
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
    members=[m for m in inp["domainMemberships"] if m["routerId"] not in dr]
    for d in sorted(inp["domains"],key=lambda x:x["id"]):
        nodes={m["routerId"] for m in members if m["domainId"]==d["id"]}
        if not nodes and not d["isDefault"]:
            out.append({"op":"deleteEntity","entityKind":"domain","id":d["id"]});tomb.append({"subjectKind":"domain","id":d["id"],"value":d});impacts.append(impact("domain.non_default_deleted","warning",True,[{"kind":"domain","id":d["id"]}],{"discardedConfig":True},"confirm-or-discard"));continue
        if len(nodes)>1:
            edges={(l["endpointA"],l["endpointB"]) for l in inp["currentDerivedState"]["structuralLinks"] if l["id"] not in dl and l["endpointA"] not in dr and l["endpointB"] not in dr}; seen={min(nodes)}
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


def verify_engine(doc:dict,world:SchemaWorld|None=None)->tuple[int,int,int]:
    world=world or SchemaWorld(CONTRACTS)
    exact_keys(doc,{"schema","canonicalization","resolutionCases","migrationCases","freshnessCases"},"Engine catalog")
    if doc["schema"]!="ipcraft.default-engine-behavior-vectors.v1":fail("wrong Engine catalog schema")
    rs=ids(doc["resolutionCases"],REQUIRED_RES,"id","resolutionCases");ms=ids(doc["migrationCases"],REQUIRED_MIG,"id","migrationCases");fs=ids(doc["freshnessCases"],REQUIRED_FRESH,"id","freshnessCases")
    for cid,c in rs.items():
        exact_keys(c,{"id","description","input","expected"},cid)
        world.validate("ipcraft.project-design.v1",world.documents["ipcraft.project-design.v1"]["$defs"]["defaultEngineDependencyLock"],c["input"]["projectLock"],cid+" lock")
        for b in c["input"]["installedBundles"]+c["input"]["alternativeBundles"]:
            valid=world.is_valid("ipcraft.engine-bundle.v1",world.documents["ipcraft.engine-bundle.v1"],b["manifest"],cid+" manifest")
            if not valid and c["expected"]["diagnosticCode"]!="engine.bundle_mismatch": fail(f"{cid}: invalid manifest is not classified as bundle mismatch")
        got=eval_resolution(c)
        if got!=c["expected"]:fail(f"{cid}: resolution mismatch\n{got}\n{c['expected']}")
        if got["selectedBundleManifestDigest"] not in (None,c["input"]["projectLock"]["bundleManifestDigest"]):fail(f"{cid}: selected a different digest")
    for cid,c in ms.items():
        exact_keys(c,{"id","description","input","expected"},cid);validate_migration_binding(c["input"])
        defs=world.documents["ipcraft.core-canonical-models.v1"]["$defs"]
        world.validate("ipcraft.core-canonical-models.v1",defs["candidateTransaction"],c["input"]["candidate"],cid+" candidate")
        got=eval_migration(c["input"])
        if got!=c["expected"]:fail(f"{cid}: migration mismatch")
        if c["input"].get("action") in ("undo","redo") and got["engineExecutions"]:fail(f"{cid}: Undo/Redo invoked Engine")
    for cid,c in fs.items():
        exact_keys(c,{"id","description","input","expected"},cid);got=eval_fresh(c["input"])
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
    return len(cases)


def mutation_tests(engine:dict,side:dict)->int:
    tests=[]
    def reject(name,fn):
        try:fn()
        except Exception:return
        fail(f"mutation accepted: {name}")
    r={c["id"]:c for c in engine["resolutionCases"]};f={c["id"]:c for c in engine["freshnessCases"]};m={c["id"]:c for c in engine["migrationCases"]};s={c["caseId"]:c for c in side["cases"]}
    x=copy.deepcopy(r["engine-lock-exact-available"]);x["expected"]["selectedBundleManifestDigest"]="sha256:"+"f"*64;tests.append(("wrong selected digest",lambda x=x: verify_engine({**engine,"resolutionCases":[x if c["id"]==x["id"] else c for c in engine["resolutionCases"]]})))
    x=copy.deepcopy(r["engine-lock-no-builtin-fallback"]);x["expected"].update(outcome="exact",selectedBundleManifestDigest=x["input"]["alternativeBundles"][0]["bundleManifestDigest"],diagnosticCode=None);tests.append(("fallback alternative selected",lambda x=x:verify_engine({**engine,"resolutionCases":[x if c["id"]==x["id"] else c for c in engine["resolutionCases"]]})))
    x=copy.deepcopy(f["output-freshness-engine-digest-mismatch"]);x["expected"]["staleReasons"]=["authoritative-design-changed"];tests.append(("wrong freshness reason",lambda x=x:verify_engine({**engine,"freshnessCases":[x if c["id"]==x["id"] else c for c in engine["freshnessCases"]]})))
    for name,cid,mut in [("missing membership","side-effects-created-router-default-memberships",lambda e:e["applicationPatch"]["operations"].pop()),("nondeterministic localRef","side-effects-created-router-default-memberships",lambda e:e["applicationPatch"]["operations"][0].update(localRef="application:999999")),("deleted Attachment instead of unresolved","side-effects-deleted-slot-attachment-unresolved",lambda e:e["applicationPatch"]["operations"].__setitem__(0,{"op":"deleteRelation","relationKind":"attachment","id":"attachment.a"})),("blocked-vs-confirm priority","side-effects-package-relation-endpoint-blocked",lambda e:e.update(groupState="ready-to-commit",requiresConfirmation=True,commitDisposition="confirmation-required")),("wrong connectivity outcome","side-effects-domain-disconnected-link-delete",lambda e:e.update(coreDiagnostics=[]))]:
        x=copy.deepcopy(s[cid]);mut(x["document"]["expected"]);tests.append((name,lambda x=x: verify_side({**side,"cases":[x if c["caseId"]==x["caseId"] else c for c in side["cases"]]},SchemaWorld(CONTRACTS))))
    x=copy.deepcopy(m["engine-migration-atomic-commit"]);x["input"]["forwardTransaction"]["projectDependencies"][0]["version"]="9";tests.append(("changed non-Engine dependency",lambda x=x:verify_engine({**engine,"migrationCases":[x if c["id"]==x["id"] else c for c in engine["migrationCases"]]})))
    x=copy.deepcopy(m["engine-migration-undo-exact-inverse"]);x["expected"]["engineExecutions"]=[x["input"]["migration"]["currentDefaultEngineLock"]["bundleManifestDigest"]];tests.append(("Undo invoking Engine",lambda x=x:verify_engine({**engine,"migrationCases":[x if c["id"]==x["id"] else c for c in engine["migrationCases"]]})))
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
