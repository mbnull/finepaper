#!/usr/bin/env python3
"""NON-NORMATIVE deterministic authoring generator for Engine/Host vectors."""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path

from rfc8785 import canonical_json as _rfc8785_json, sha256_digest as _rfc8785_digest

ROOT = Path(__file__).resolve().parents[3]
VECTORS = ROOT / "docs/contracts/vectors"
D = {c: "sha256:" + c * 64 for c in "abcdef"}


def dump(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def cj(value: object) -> str:
    return _rfc8785_json(value)


def digest(value: object) -> str:
    return _rfc8785_digest(value)


def lock(d: str = D["a"], version: str = "1.0.0", compat: str = "1") -> dict:
    return {"lockId":"dep.default-engine","kind":"default-engine","id":"ipcraft.default-noc-engine","version":version,
            "bundleManifestDigest":d,"engineCompatibilityVersion":compat,"engineHostContractVersion":"ipcraft.engine-host.v1",
            "hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1","supportedPlatformAbis":["linux-x86_64-gnu-v1"]}


def manifest(version: str = "1.0.0", compat: str = "1") -> dict:
    return {"schema":"ipcraft.engine-bundle.v1","id":"ipcraft.default-noc-engine","version":version,
            "engineHostContractVersion":"ipcraft.engine-host.v1","engineCompatibilityVersion":compat,
            "hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1","migrationFromCompatibilityVersions":["1"],
            "supportedPlatformAbis":["linux-x86_64-gnu-v1"],"entrypoint":"lib/libipcraft_noc_engine.so"}


def bundle(d: str = D["a"], **kw: object) -> dict:
    out = {"bundleManifestDigest":d,"verifiedBundleManifestDigest":d,"installed":True,"revoked":False,"contentVerified":True,"source":"store","manifest":manifest()}
    out.update(kw)
    return out


def resolution_case(case_id: str, description: str, exact: dict | None = None, alternatives: list | None = None,
                    outcome: str = "exact", code: str | None = None, selected: str | None = D["a"], upgrade: bool = False) -> dict:
    return {"id":case_id,"description":description,"input":{"projectLock":lock(),"installedBundles":[] if exact is None else [exact],
            "currentPlatformAbi":"linux-x86_64-gnu-v1","supportedEngineHostContracts":["ipcraft.engine-host.v1"],
            "supportedHostSideEffectContracts":["ipcraft.noc-side-effects.v1"],"alternativeBundles":alternatives or []},
            "expected":{"outcome":outcome,"selectedBundleManifestDigest":selected,"diagnosticCode":code,"upgradeAvailable":upgrade,"retainedInContentAddressedStore":False}}


def resolution_cases() -> list[dict]:
    cases = [resolution_case("engine-lock-exact-available","Exact verified Bundle resolves.",bundle()),
             resolution_case("engine-lock-missing","Exact Bundle is absent.",None,outcome="degraded-inspect",code="engine.bundle_missing",selected=None),
             resolution_case("engine-lock-revoked","Exact Bundle is revoked.",bundle(revoked=True),outcome="degraded-inspect",code="engine.bundle_revoked",selected=None),
             resolution_case("engine-lock-corrupt","Exact Bundle bytes fail verification.",bundle(contentVerified=False),outcome="degraded-inspect",code="engine.bundle_mismatch",selected=None),
             resolution_case("engine-lock-digest-mismatch","Installed bytes recompute to another digest.",bundle(verifiedBundleManifestDigest=D["b"]),outcome="degraded-inspect",code="engine.bundle_mismatch",selected=None),
             resolution_case("engine-lock-same-compatibility-different-digest","Compatible alternative never substitutes.",None,[bundle(D["b"])],"degraded-inspect","engine.bundle_missing",None),
             resolution_case("engine-lock-no-builtin-fallback","A built-in alternative is not a fallback.",None,[{**bundle(D["b"]),"source":"builtin"}],"degraded-inspect","engine.bundle_missing",None),
             resolution_case("engine-lock-newer-target-discovered","Newer target is only an overlay.",bundle(),[bundle(D["b"],manifest=manifest("2.0.0","2"))],upgrade=True),
             resolution_case("unsupported-but-valid-bundle-retained","Valid unsupported-platform Bundle remains retained.",bundle(),outcome="degraded-inspect",code="engine.platform_unsupported",selected=None)]
    cases[-1]["input"]["currentPlatformAbi"] = "windows-x86_64-msvc-v1"
    cases[-1]["expected"]["retainedInContentAddressedStore"] = True
    for cid, field, value in [
        ("engine-lock-manifest-metadata-mismatch","version","1.0.1"),
        ("engine-lock-manifest-id-mismatch","id","vendor.other"),
        ("engine-lock-manifest-host-mismatch","engineHostContractVersion","ipcraft.engine-host.v2"),
        ("engine-lock-manifest-side-effect-mismatch","hostSideEffectContractVersion","ipcraft.noc-side-effects.v2"),
        ("engine-lock-manifest-compatibility-mismatch","engineCompatibilityVersion","2"),
        ("engine-lock-manifest-platform-metadata-mismatch","supportedPlatformAbis",["other-abi"])]:
        m=manifest(); m[field]=value
        cases.append(resolution_case(cid,f"Manifest {field} disagrees with the exact lock.",bundle(manifest=m),outcome="degraded-inspect",code="engine.bundle_mismatch",selected=None))
    p=resolution_case("engine-lock-platform-incompatible","Exact manifest does not support current platform.",bundle(),outcome="degraded-inspect",code="engine.platform_unsupported",selected=None)
    p["input"]["currentPlatformAbi"]="other-abi"; cases.append(p)
    h=resolution_case("engine-lock-host-abi-incompatible","Persisted Host ABI is unsupported.",bundle(),outcome="degraded-inspect",code="engine.host_contract_unsupported",selected=None)
    h["input"]["supportedEngineHostContracts"]=[]; cases.append(h)
    s=resolution_case("engine-lock-side-effect-contract-incompatible","Persisted Host side-effect contract is unsupported.",bundle(),outcome="degraded-inspect",code="host.side_effect_contract_unsupported",selected=None)
    s["input"]["supportedHostSideEffectContracts"]=[]; cases.append(s)
    return cases


def applicability(d: str=D["a"], base_revision:int=7, base_digest:str=D["d"]) -> dict:
    return {"schema":"ipcraft.reconcile-applicability.v1","groupId":"group.migration","requestGeneration":1,"topologyInputRevision":4,
            "topologyInputDigest":D["c"],"baseDerivedStateRevision":base_revision,"baseDerivedStateDigest":base_digest,"baseAuthoritativeDesignDigest":D["e"],
            "structureAuthority":{"kind":"default-engine","lockId":"dep.default-engine","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":d},
            "packageBundleDigest":D["f"],"reconcileDependencySetDigest":D["c"],"defaultEngineLockId":"dep.default-engine",
            "defaultEngineBundleDigest":d,"engineHostContractVersion":"ipcraft.engine-host.v1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1"}


def derivation(target: dict, state_digest:str=D["c"], revision:int=8) -> dict:
    return {"topologyInputRevision":5,"topologyInputDigest":D["b"],"derivedStateRevision":revision,"derivedStateDigest":state_digest,
            "packageBundleDigest":D["f"],"reconcileDependencySetDigest":D["b"],"defaultEngineLockId":target["lockId"],
            "defaultEngineBundleDigest":target["bundleManifestDigest"],"engineHostContractVersion":target["engineHostContractVersion"],
            "hostSideEffectContractVersion":target["hostSideEffectContractVersion"],"structureAuthority":{"kind":"default-engine","lockId":target["lockId"],
            "identity":target["id"],"version":target["version"],"bundleDigest":target["bundleManifestDigest"]},"engineCompatibilityVersion":target["engineCompatibilityVersion"]}


def migration_record() -> dict:
    return make_migration_input(False)


def migration_cases() -> list[dict]:
    def c(i,d,base=None,mut=None,exp=None):
        x=copy.deepcopy(base or migration_record()); (mut or (lambda _:None))(x)
        return {"id":i,"description":d,"input":x,"expected":exp or {"offered":True,"groupState":"ready-to-commit","requiresConfirmation":True,"atomicCommit":True,"engineExecutions":[D["b"]]}}
    def atomic_prior_count(x):
        x["beforeSnapshot"]["engineInvocationCount"]=3;x["inverseTransaction"]["restoreEngineInvocationCount"]=3;x["forwardTransaction"]["resultEngineInvocationCount"]=4;x["afterSnapshot"]["engineInvocationCount"]=4
    return [c("engine-migration-compatible-target-offered","Target declares source compatibility."),
            c("engine-migration-incompatible-target-not-offered","Target omits source compatibility; discovery retains only unchanged current state.",base=make_migration_discovery_input(),exp={"offered":False,"diagnosticCode":"engine.migration_incompatible","engineExecutions":[]}),
            c("engine-migration-atomic-commit","Lock, Derived State, side effects, mapping, and provenance commit atomically.",mut=atomic_prior_count),
            c("engine-migration-blocked-by-package-relation","Universal relation blocking priority is derived from the causal relation endpoint.",base=make_migration_input(True),exp={"offered":True,"groupState":"blocked","requiresConfirmation":False,"atomicCommit":False,"engineExecutions":[D["b"]]}),
            c("engine-migration-undo-exact-inverse","Undo restores the exact stored inverse without Engine execution.",mut=lambda x:x.update(action="undo"),exp={"offered":True,"restoredSnapshot":"beforeSnapshot","stableHostIds":True,"engineExecutions":[],"resultMode":"normal"}),
            c("engine-migration-undo-source-missing-degraded","Undo restores exact state then degrades if the source Bundle is missing.",mut=lambda x:x.update(action="undo",sourceBundleAvailable=False),exp={"offered":True,"restoredSnapshot":"beforeSnapshot","stableHostIds":True,"engineExecutions":[],"resultMode":"degraded-inspect","diagnosticCode":"engine.bundle_missing"})]


def freshness_cases() -> list[dict]:
    base={"currentAuthoritativeDesignDigest":D["a"],"formallySavedProjectDigest":D["a"],"currentDependencySetDigest":D["b"],
          "currentDefaultEngineBundleDigest":D["c"],"currentEngineHostContractVersion":"ipcraft.engine-host.v1","currentHostSideEffectContractVersion":"ipcraft.noc-side-effects.v1",
          "pendingTopologyGroup":False,"draftOverlayCount":0,"promotedManifest":{"snapshotDigest":D["a"],"dependencySetDigest":D["b"],"defaultEngineBundleDigest":D["c"],"engineHostContractVersion":"ipcraft.engine-host.v1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1"}}
    def c(i,changes,state,reasons):
        x=copy.deepcopy(base); x.update(changes); return {"id":i,"description":i.replace("-"," "),"input":x,"expected":{"state":state,"staleReasons":reasons}}
    return [c("output-freshness-saved-current-equal",{},"current-canonical",[]),
            c("output-freshness-accepted-unsaved-edit",{"currentAuthoritativeDesignDigest":D["d"]},"last-successful-stale",["authoritative-design-changed","not-formally-saved"]),
            c("output-freshness-pending-topology",{"pendingTopologyGroup":True},"last-successful-stale",["pending-topology"]),
            c("output-freshness-draft-overlay",{"draftOverlayCount":2},"last-successful-stale",["draft-overlay"]),
            c("output-freshness-engine-digest-mismatch",{"currentDefaultEngineBundleDigest":D["d"]},"last-successful-stale",["dependency-changed"]),
            c("output-freshness-engine-host-contract-mismatch",{"currentEngineHostContractVersion":"ipcraft.engine-host.v2"},"last-successful-stale",["dependency-changed"]),
            c("output-freshness-side-effect-contract-mismatch",{"currentHostSideEffectContractVersion":"ipcraft.noc-side-effects.v2"},"last-successful-stale",["dependency-changed"]),
            c("output-freshness-no-promotion",{"promotedManifest":None},"none",[])]


def router(i:str,row:int,col:int)->dict: return {"id":i,"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":row,"column":col},"properties":{}}
def link(i:str,a:str,b:str)->dict: return {"id":i,"templateKey":"mesh-link","identityCompatibilityVersion":1,"endpointA":a,"endpointB":b,"axis":"horizontal","properties":{}}
def slot(i:str,r:str)->dict: return {"id":i,"routerId":r,"templateKey":"local-0","identityCompatibilityVersion":1,"displayOrder":0,"label":"Local 0","allowedContracts":[],"properties":{}}
def package_entity(i:str)->dict: return {"id":i,"typeKey":"endpoint","data":{"name":i}}
def domain(i,t,default): return {"id":i,"typeKey":t,"name":i,"isDefault":default,"config":{"frequencyMHz":100}}
def member(i,d,r): return {"id":i,"domainId":d,"routerId":r}
def resolved(kind,id): return {"state":"resolved","subject":{"kind":kind,"id":id}}


def side_input(ops:list[dict]) -> dict:
    dtype=lambda k:{"key":k,"label":k.title(),"defaultName":k+"-default","visual":{"fill":"#123456","border":"#abcdef","pattern":"solid"},"configuration":{"fields":[]}}
    decl=lambda k,allow:{"typeKey":k,"ownership":"user","topologyDriving":False,"sources":{"kinds":["router"],"minimum":1,"maximum":1},"targets":{"kinds":["package-entity"],"minimum":1,"maximum":1},"unresolvedAllowed":allow,"schema":{}}
    return {"domainTypes":[dtype("clock"),dtype("power")],"relationDeclarations":[decl("route.allowed",True),decl("route.blocked",False)],
      "currentDerivedState":{"schema":"ipcraft.derived-state.v1","topologyId":"topology.main","routers":[router("router.a",0,0),router("router.b",0,1),router("router.c",0,2)],
      "structuralLinks":[link("link.ab","router.a","router.b"),link("link.bc","router.b","router.c")],"accessSlots":[slot("slot.a","router.a"),slot("slot.b","router.b")],"packageEntities":[package_entity("entity.a")],"packageRelations":[]},
      "domains":[domain("domain.clock.default","clock",True),domain("domain.power.default","power",True)],
      "domainMemberships":[member("membership.ca","domain.clock.default","router.a"),member("membership.cb","domain.clock.default","router.b"),member("membership.cc","domain.clock.default","router.c"),member("membership.pa","domain.power.default","router.a"),member("membership.pb","domain.power.default","router.b"),member("membership.pc","domain.power.default","router.c")],
      "attachments":[{"id":"attachment.a","interfaceId":"interface.a","state":"resolved","routerId":"router.a","slotId":"slot.a"}],
      "packageRelations":[{"id":"relation.allowed","typeKey":"route.allowed","sources":[resolved("router","router.a")],"targets":[resolved("package-entity","entity.a")],"data":{"weight":1},"extensions":[]},{"id":"relation.blocked","typeKey":"route.blocked","sources":[resolved("router","router.b")],"targets":[resolved("package-entity","entity.a")],"data":{"weight":2},"extensions":[]}],
      "authorityPatch":{"patchId":"patch.authority","source":{"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":D["a"]},"operations":ops}}


def ref_token(ref): return "id:"+ref["id"] if "id" in ref else "localRef:"+ref["localRef"]


def evaluate_side(inp:dict)->dict:
    ops=[]; impacts=[]; tomb=[]; diagnostics=[]
    authority=inp["authorityPatch"]["operations"]
    deleted_r={o["id"] for o in authority if o["op"]=="deleteEntity" and o["entityKind"]=="router"}
    deleted_s={o["id"] for o in authority if o["op"]=="deleteEntity" and o["entityKind"]=="access-slot"}
    created=[o for o in authority if o["op"]=="createEntity" and o["entityKind"]=="router"]
    defaults={d["typeKey"]:d for d in inp["domains"] if d["isDefault"]}
    seq=1
    for dtype,o in sorted(((t["key"],o) for t in inp["domainTypes"] for o in created),key=lambda x:(x[0],ref_token({"localRef":x[1]["localRef"]}))):
        ops.append({"op":"createRelation","relationKind":"domain-membership","localRef":f"application:{seq:06d}","value":{"domainRef":{"id":defaults[dtype]["id"]},"routerRef":{"localRef":o["localRef"]}}}); seq+=1
    for m in sorted(inp["domainMemberships"],key=lambda x:x["id"]):
        if m["routerId"] in deleted_r: ops.append({"op":"deleteRelation","relationKind":"domain-membership","id":m["id"]})
    for a in sorted(inp["attachments"],key=lambda x:x["id"]):
        if a["state"]=="resolved" and (a["routerId"] in deleted_r or a["slotId"] in deleted_s):
            ops.append({"op":"updateRelation","relationKind":"attachment","id":a["id"],"set":{"state":"unresolved","intendedTarget":{"routerRef":{"id":a["routerId"]},"slotRef":{"id":a["slotId"]}},"reasonCode":"attachment.target_removed"},"unset":["routerRef","slotRef"]})
            impacts.append({"code":"attachment.target_removed","severity":"warning","dataLoss":False,"subjects":[{"kind":"attachment","id":a["id"]}],"details":{"routerId":a["routerId"],"slotId":a["slotId"]},"resolution":"reattach-or-detach"})
    decl={d["typeKey"]:d for d in inp["relationDeclarations"]}
    for rel in sorted(inp["packageRelations"],key=lambda x:x["id"]):
        hit=any(e["state"]=="resolved" and e["subject"]["kind"]=="router" and e["subject"]["id"] in deleted_r for e in rel["sources"]+rel["targets"])
        if not hit: continue
        if decl[rel["typeKey"]]["unresolvedAllowed"]:
            def conv(e):
                if e["state"]=="resolved" and e["subject"]["kind"]=="router" and e["subject"]["id"] in deleted_r:
                    return {"state":"unresolved","intendedSubject":{"kind":e["subject"]["kind"],"ref":{"id":e["subject"]["id"]}},"reasonCode":"relation.target_removed"}
                if e["state"]=="resolved": return {"state":"resolved","subject":{"kind":e["subject"]["kind"],"ref":{"id":e["subject"]["id"]}}}
                return {"state":"unresolved","intendedSubject":{"kind":e["intendedSubject"]["kind"],"ref":{"id":e["intendedSubject"]["id"]}},"reasonCode":e["reasonCode"]}
            ops.append({"op":"updateRelation","relationKind":"package-relation","id":rel["id"],"set":{"sources":[conv(e) for e in rel["sources"]],"targets":[conv(e) for e in rel["targets"]]},"unset":[]})
            impacts.append({"code":"package_relation.endpoint_unresolved","severity":"warning","dataLoss":False,"subjects":[{"kind":"package-relation","id":rel["id"]}],"details":{"typeKey":rel["typeKey"]},"resolution":"reattach-or-delete-relation"})
        else: impacts.append({"code":"package_relation.endpoint_blocks_candidate","severity":"error","dataLoss":False,"subjects":[{"kind":"package-relation","id":rel["id"]}],"details":{"typeKey":rel["typeKey"]},"resolution":"discard-and-repair"})
    remaining_members=[(m["domainId"],"id:"+m["routerId"]) for m in inp["domainMemberships"] if m["routerId"] not in deleted_r]
    remaining_members += [(o["value"]["domainRef"]["id"],ref_token(o["value"]["routerRef"])) for o in ops if o["op"]=="createRelation" and o["relationKind"]=="domain-membership"]
    links={l["id"]:("id:"+l["endpointA"],"id:"+l["endpointB"]) for l in inp["currentDerivedState"]["structuralLinks"]}
    for operation in authority:
        if operation["op"]=="deleteEntity" and operation["entityKind"]=="structural-link": links.pop(operation["id"],None)
        elif operation["op"]=="createEntity" and operation["entityKind"]=="structural-link": links[operation["localRef"]]=(ref_token(operation["value"]["endpointA"]),ref_token(operation["value"]["endpointB"]))
        elif operation["op"]=="updateEntity" and operation["entityKind"]=="structural-link":
            old=links[operation["id"]]; links[operation["id"]]=(ref_token(operation["set"].get("endpointA",{"id":old[0][3:]})),ref_token(operation["set"].get("endpointB",{"id":old[1][3:]})))
    links={key:value for key,value in links.items() if all(not (token.startswith("id:") and token[3:] in deleted_r) for token in value)}
    for d in sorted(inp["domains"],key=lambda x:x["id"]):
        members={router for domain_id,router in remaining_members if domain_id==d["id"]}
        if not members and not d["isDefault"]:
            ops.append({"op":"deleteEntity","entityKind":"domain","id":d["id"]}); tomb.append({"subjectKind":"domain","id":d["id"],"value":d})
            impacts.append({"code":"domain.non_default_deleted","severity":"warning","dataLoss":True,"subjects":[{"kind":"domain","id":d["id"]}],"details":{"discardedConfig":True},"resolution":"confirm-or-discard"}); continue
        if len(members)>1:
            edges=list(links.values())
            seen={next(iter(members))}
            while True:
                more={b for a,b in edges if a in seen and b in members}|{a for a,b in edges if b in seen and a in members}
                if more<=seen: break
                seen|=more
            if seen!=members:
                imp={"code":"domain.disconnected","severity":"error","dataLoss":False,"subjects":[{"kind":"domain","id":d["id"]}],"details":{"memberCount":len(members)},"resolution":"repair-domain"}; impacts.append(imp)
                diagnostics.append({"ruleId":"domain.disconnected","severity":"error","message":"Domain is disconnected.","blocking":True,"subjects":imp["subjects"],"properties":[]})
    impacts.sort(key=lambda x:(x["code"],x["severity"],x["dataLoss"],cj(x["subjects"]),cj(x["details"]),x["resolution"]))
    codes={x["code"] for x in impacts}
    if "package_relation.endpoint_blocks_candidate" in codes: state,confirm,disp="blocked",False,"blocked"
    elif codes & {"domain.non_default_deleted","engine_migration.dependency_replaced"}: state,confirm,disp="ready-to-commit",True,"confirmation-required"
    else: state,confirm,disp="auto-commit",False,"auto-commit"
    creates=[o["localRef"] for o in inp["authorityPatch"]["operations"] if o["op"] in ("createEntity","createRelation")]+[o["localRef"] for o in ops if o["op"] in ("createEntity","createRelation")]
    return {"applicationPatch":{"patchId":"patch.application","source":{"kind":"application-reconcile","identity":"host","version":"1"},"operations":ops},"tombstones":sorted(tomb,key=lambda x:(x["subjectKind"],x["id"])),"allocationOrder":sorted(creates),"impactReport":{"schema":"ipcraft.topology-impact-report.v1","impacts":impacts},"coreDiagnostics":diagnostics,"groupState":state,"requiresConfirmation":confirm,"commitDisposition":disp}


def materialize_ref(ref:dict,mapping:dict)->str:
    return ref["id"] if "id" in ref else mapping[ref["localRef"]]


def normalize_derived_authoring(value:dict)->dict:
    result=copy.deepcopy(value)
    for key in ("routers","structuralLinks","accessSlots","packageEntities","packageRelations"):result[key].sort(key=lambda x:x["id"])
    for item in result["accessSlots"]:
        item["allowedContracts"].sort(key=lambda x:x["contractLockId"])
        for allowed in item["allowedContracts"]:allowed["roles"].sort()
    def endpoint_key(endpoint):
        if endpoint["state"]=="resolved":return (0,endpoint["subject"]["kind"],"id:"+endpoint["subject"]["id"])
        return (1,endpoint["intendedSubject"]["kind"],"id:"+endpoint["intendedSubject"]["id"],endpoint["reasonCode"])
    for item in result["packageRelations"]:
        item["sources"].sort(key=endpoint_key);item["targets"].sort(key=endpoint_key);item["extensions"].sort(key=lambda x:(x["ownerLockId"],x["schema"],x["version"]))
    return result


def derived_digest_authoring(value:dict)->str:
    return digest(normalize_derived_authoring(value))


def materialize_derived_authoring(current:dict,authority:dict,mapping:dict)->dict:
    wrapper={"dependencies":[],"derivedState":copy.deepcopy(current),"derivation":{},"hostSideEffects":{"domains":[],"domainMemberships":[],"attachments":[],"packageRelations":[]},"hostIds":{"localRefToHostId":{}},"engineInvocationCount":0}
    tx={"authorityPatch":authority,"applicationPatch":{"operations":[]},"localRefToHostId":mapping,"targetDependencies":[],"targetDerivation":{},"resultEngineInvocationCount":0,"engineInvocations":[]}
    return apply_forward_authoring(wrapper,tx)["derivedState"]


def apply_forward_authoring(before:dict,tx:dict)->dict:
    result=copy.deepcopy(before); mapping=tx["localRefToHostId"]; derived=result["derivedState"]
    arrays={"router":"routers","structural-link":"structuralLinks","access-slot":"accessSlots"}
    for op in tx["authorityPatch"]["operations"]:
        if op["entityKind"] not in arrays: continue
        array=derived[arrays[op["entityKind"]]]
        if op["op"]=="deleteEntity": array[:]=[item for item in array if item["id"]!=op["id"]]
        elif op["op"]=="createEntity":
            value=copy.deepcopy(op["value"]); value["id"]=mapping[op["localRef"]]
            if op["entityKind"]=="structural-link": value["endpointA"],value["endpointB"]=materialize_ref(value["endpointA"],mapping),materialize_ref(value["endpointB"],mapping)
            if op["entityKind"]=="access-slot": value["routerId"]=materialize_ref(value.pop("routerRef"),mapping)
            array.append(value)
    for key in ("routers","structuralLinks","accessSlots"): derived[key].sort(key=lambda x:x["id"])
    host=result["hostSideEffects"]
    for op in tx["applicationPatch"]["operations"]:
        if op["op"]=="createRelation" and op["relationKind"]=="domain-membership": host["domainMemberships"].append({"id":mapping[op["localRef"]],"domainId":materialize_ref(op["value"]["domainRef"],mapping),"routerId":materialize_ref(op["value"]["routerRef"],mapping)})
        elif op["op"]=="deleteRelation" and op["relationKind"]=="domain-membership": host["domainMemberships"]=[x for x in host["domainMemberships"] if x["id"]!=op["id"]]
        elif op["op"]=="updateRelation" and op["relationKind"]=="attachment":
            item=next(x for x in host["attachments"] if x["id"]==op["id"]); item.clear(); item.update({"id":op["id"],"interfaceId":"interface.c","state":"unresolved","intendedTarget":{"routerId":op["set"]["intendedTarget"]["routerRef"]["id"],"slotId":op["set"]["intendedTarget"]["slotRef"]["id"]},"reasonCode":op["set"]["reasonCode"]})
        elif op["op"]=="updateRelation" and op["relationKind"]=="package-relation":
            item=next(x for x in host["packageRelations"] if x["id"]==op["id"])
            def persisted(e):
                if e["state"]=="resolved": return {"state":"resolved","subject":{"kind":e["subject"]["kind"],"id":materialize_ref(e["subject"]["ref"],mapping)}}
                return {"state":"unresolved","intendedSubject":{"kind":e["intendedSubject"]["kind"],"id":materialize_ref(e["intendedSubject"]["ref"],mapping)},"reasonCode":e["reasonCode"]}
            item["sources"],item["targets"]=[persisted(e) for e in op["set"]["sources"]],[persisted(e) for e in op["set"]["targets"]]
        elif op["op"]=="deleteEntity" and op["entityKind"]=="domain": host["domains"]=[x for x in host["domains"] if x["id"]!=op["id"]]
    for key in host: host[key].sort(key=lambda x:x["id"])
    result["dependencies"]=copy.deepcopy(tx["targetDependencies"]);result["derivation"]=copy.deepcopy(tx["targetDerivation"]);result["hostIds"]={"localRefToHostId":copy.deepcopy(mapping)};result["engineInvocationCount"]=tx["resultEngineInvocationCount"]
    return result


def make_migration_input(blocked:bool)->dict:
    current,target=lock(),lock(D["b"],"2.0.0","2"); noc={"lockId":"dep.noc","kind":"noc-package","id":"vendor.noc","version":"1.0.0","bundleManifestDigest":D["f"]}
    create_router={"op":"createEntity","entityKind":"router","localRef":"authority:router-d","value":{"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":1,"column":0},"properties":{"generation":"target"}}}
    create_link={"op":"createEntity","entityKind":"structural-link","localRef":"authority:link-bd","value":{"templateKey":"mesh-link","identityCompatibilityVersion":1,"endpointA":{"id":"router.b"},"endpointB":{"localRef":"authority:router-d"},"axis":"vertical","properties":{"generation":"target"}}}
    create_slot={"op":"createEntity","entityKind":"access-slot","localRef":"authority:slot-d","value":{"routerRef":{"localRef":"authority:router-d"},"templateKey":"local-0","identityCompatibilityVersion":1,"displayOrder":0,"label":"Target Local","allowedContracts":[],"properties":{"generation":"target"}}}
    authority={"patchId":"patch.target-engine","source":{"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"2.0.0","bundleDigest":D["b"]},"operations":[{"op":"deleteEntity","entityKind":"structural-link","id":"link.bc"},{"op":"deleteEntity","entityKind":"access-slot","id":"slot.c"},{"op":"deleteEntity","entityKind":"router","id":"router.c"},create_router,create_link,create_slot]}
    causal=side_input(authority["operations"]);causal["authorityPatch"]=copy.deepcopy(authority);causal["currentDerivedState"]["accessSlots"].append(slot("slot.c","router.c"));causal["attachments"]=[{"id":"attachment.c","interfaceId":"interface.c","state":"resolved","routerId":"router.c","slotId":"slot.c"}];causal["packageRelations"]=[]
    if blocked:
        causal["attachments"]=[]
        causal["packageRelations"]=[{"id":"relation.blocked","typeKey":"route.blocked","sources":[resolved("router","router.c")],"targets":[resolved("package-entity","entity.a")],"data":{"weight":9},"extensions":[]}]
    mapping_values={"authority:link-bd":"host-link-bd","authority:router-d":"host-router-d","authority:slot-d":"host-slot-d","application:000001":"host-membership-clock-d","application:000002":"host-membership-power-d"};mapping={key:mapping_values[key] for key in sorted(mapping_values)}
    before_state=copy.deepcopy(causal["currentDerivedState"]);before_digest=derived_digest_authoring(before_state);after_state=materialize_derived_authoring(before_state,authority,mapping);after_digest=derived_digest_authoring(after_state)
    old_der={**derivation(current,before_digest,7),"topologyInputRevision":4,"defaultEngineBundleDigest":D["a"],"engineCompatibilityVersion":"1","structureAuthority":{**derivation(current,before_digest,7)["structureAuthority"],"version":"1.0.0","bundleDigest":D["a"]}}
    target_der=derivation(target,after_digest,8);app_value=applicability(D["a"],old_der["derivedStateRevision"],before_digest)
    side=evaluate_side(causal);target_deps=sorted([noc,target],key=lambda x:x["lockId"])
    application=copy.deepcopy(side["applicationPatch"]);application["patchId"]="patch.migration";application["source"]={"kind":"application-migration","identity":"host","version":"1"};application["operations"] += [{"op":"updateEntity","entityKind":"project","id":"project.main","set":{"dependencies":target_deps},"unset":[]},{"op":"updateEntity","entityKind":"topology","id":"topology.main","set":{"derivation":target_der},"unset":[]}]
    migration={"currentDefaultEngineLock":current,"targetDefaultEngineLock":target};migration_impact={"code":"engine_migration.dependency_replaced","severity":"warning","dataLoss":False,"subjects":[{"kind":"project","id":"project.main"}],"details":{"lockId":"dep.default-engine","currentBundleDigest":D["a"],"targetBundleDigest":D["b"]},"resolution":"confirm-or-discard"}
    impacts=copy.deepcopy(side["impactReport"]["impacts"])+[migration_impact];impacts.sort(key=lambda x:(x["code"],x["severity"],x["dataLoss"],cj(x["subjects"]),cj(x["details"]),x["resolution"]))
    deleted={("structural-link","link.bc"):link("link.bc","router.b","router.c"),("access-slot","slot.c"):slot("slot.c","router.c"),("router","router.c"):router("router.c",0,2)}
    for m in causal["domainMemberships"]:
        if m["routerId"]=="router.c": deleted[("domain-membership",m["id"])]=m
    tombstones=[{"subjectKind":kind,"id":id,"value":value} for (kind,id),value in sorted(deleted.items())]
    allocation=sorted(mapping)
    candidate={"schema":"ipcraft.candidate-transaction.v1","transactionId":"tx.migration","kind":"default-engine-migration","applicability":app_value,"topologyIntent":{"schema":"ipcraft.topology-intent.v1","componentId":"component.noc","topologyId":"topology.main","topologyKind":"mesh","globalConfig":{"columns":2,"rows":2},"packageEntities":[],"packageRelations":[]},"authorityPatch":authority,"applicationPatch":application,"migration":migration,"tombstones":tombstones,"allocationOrder":allocation,"impactReport":{"schema":"ipcraft.topology-impact-report.v1","impacts":impacts}}
    candidate["candidateDigest"]=digest(candidate)
    before={"dependencies":sorted([noc,current],key=lambda x:x["lockId"]),"derivedState":before_state,"derivation":old_der,"hostSideEffects":{"domains":copy.deepcopy(causal["domains"]),"domainMemberships":copy.deepcopy(causal["domainMemberships"]),"attachments":copy.deepcopy(causal["attachments"]),"packageRelations":copy.deepcopy(causal["packageRelations"])},"hostIds":{"localRefToHostId":{}},"engineInvocationCount":0}
    forward={"authorityPatch":authority,"applicationPatch":application,"tombstones":tombstones,"allocationOrder":allocation,"localRefToHostId":mapping,"targetDependencies":target_deps,"targetDerivation":target_der,"resultEngineInvocationCount":1,"engineInvocations":[]}
    after=apply_forward_authoring(before,forward)
    inverse={"restoreDependencies":copy.deepcopy(before["dependencies"]),"restoreDerivedState":copy.deepcopy(before["derivedState"]),"restoreDerivation":copy.deepcopy(before["derivation"]),"restoreHostSideEffects":copy.deepcopy(before["hostSideEffects"]),"restoreHostIds":copy.deepcopy(before["hostIds"]),"restoreEngineInvocationCount":before["engineInvocationCount"],"engineInvocations":[]}
    return {"caseKind":"offered","action":"migrate","sourceBundleAvailable":True,"migration":migration,"applicability":app_value,"currentManifest":manifest(),"targetManifest":manifest("2.0.0","2"),"sideEffectInput":causal,"beforeSnapshot":before,"afterSnapshot":after,"candidate":candidate,"forwardTransaction":forward,"inverseTransaction":inverse,"migrationEngineInvocations":[D["b"]]}


def make_migration_discovery_input()->dict:
    offered=make_migration_input(False);target_manifest=copy.deepcopy(offered["targetManifest"]);target_manifest["migrationFromCompatibilityVersions"]=["9"]
    return {"caseKind":"discovery","currentDefaultEngineLock":copy.deepcopy(offered["migration"]["currentDefaultEngineLock"]),"targetDefaultEngineLock":copy.deepcopy(offered["migration"]["targetDefaultEngineLock"]),"currentManifest":copy.deepcopy(offered["currentManifest"]),"targetManifest":target_manifest,"supportedEngineHostContracts":["ipcraft.engine-host.v1"],"supportedHostSideEffectContracts":["ipcraft.noc-side-effects.v1"],"currentPlatformAbi":"linux-x86_64-gnu-v1","currentSnapshot":copy.deepcopy(offered["beforeSnapshot"])}


def side_cases()->list[dict]:
    create=lambda lr,r,c:{"op":"createEntity","entityKind":"router","localRef":lr,"value":{"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":r,"column":c},"properties":{}}}
    create_link=lambda lr,a,b:{"op":"createEntity","entityKind":"structural-link","localRef":lr,"value":{"templateKey":"mesh-link","identityCompatibilityVersion":1,"endpointA":a,"endpointB":b,"axis":"vertical","properties":{}}}
    delete=lambda kind,i:{"op":"deleteEntity","entityKind":kind,"id":i}
    update_link=lambda i,a,b:{"op":"updateEntity","entityKind":"structural-link","id":i,"set":{"endpointA":a,"endpointB":b},"unset":[]}
    specs=[("side-effects-created-router-default-memberships",[create("authority:router-z",1,0),create_link("authority:link-z",{"id":"router.c"},{"localRef":"authority:router-z"})],lambda x:None),
      ("side-effects-created-routers-membership-order",[create("authority:router-z",1,0),create("authority:router-a",1,1),create_link("authority:link-a",{"id":"router.c"},{"localRef":"authority:router-a"}),create_link("authority:link-z",{"localRef":"authority:router-a"},{"localRef":"authority:router-z"})],lambda x:None),
      ("side-effects-deleted-router-memberships",[delete("router","router.c")],lambda x:x.update(packageRelations=[])),
      ("side-effects-deleted-router-attachment-unresolved",[delete("router","router.a")],lambda x:x.update(packageRelations=[])),
      ("side-effects-deleted-slot-attachment-unresolved",[delete("access-slot","slot.a")],lambda x:x.update(packageRelations=[])),
      ("side-effects-package-relation-endpoint-unresolved",[delete("router","router.a")],lambda x:x.update(attachments=[],packageRelations=[x["packageRelations"][0]])),
      ("side-effects-package-relation-endpoint-blocked",[delete("router","router.b")],lambda x:x.update(attachments=[],packageRelations=[x["packageRelations"][1]])),
      ("side-effects-empty-non-default-domain-tombstone",[delete("router","router.a")],lambda x:(x.update(attachments=[],packageRelations=[]),x["domains"].append(domain("domain.clock.aux","clock",False)),x["domainMemberships"].append(member("membership.auxa","domain.clock.aux","router.a")))),
      ("side-effects-empty-default-domain-preserved",[delete("router","router.a"),delete("router","router.b"),delete("router","router.c")],lambda x:x.update(attachments=[],packageRelations=[],domains=[x["domains"][0]],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"]=="domain.clock.default"])),
      ("side-effects-domain-disconnected-router-delete",[delete("router","router.b")],lambda x:x.update(attachments=[],packageRelations=[],domainTypes=[x["domainTypes"][0]],domains=[x["domains"][0]],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"]=="domain.clock.default"])),
      ("side-effects-domain-disconnected-link-delete",[delete("structural-link","link.ab")],lambda x:x.update(attachments=[],packageRelations=[],domainTypes=[x["domainTypes"][0]],domains=[x["domains"][0]],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"]=="domain.clock.default"])),
      ("side-effects-domain-disconnected-link-update",[update_link("link.bc",{"id":"router.a"},{"id":"router.b"})],lambda x:x.update(attachments=[],packageRelations=[],domainTypes=[x["domainTypes"][0]],domains=[x["domains"][0]],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"]=="domain.clock.default"])),
      ("side-effects-domain-disconnected-membership-placement",[create("authority:router-isolated",2,2)],lambda x:x.update(attachments=[],packageRelations=[],domainTypes=[x["domainTypes"][0]],domains=[x["domains"][0]],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"]=="domain.clock.default"])),
      ("side-effects-combined-deterministic-order",[create("authority:router-z",1,0),create_link("authority:link-z",{"id":"router.c"},{"localRef":"authority:router-z"}),delete("router","router.a"),delete("access-slot","slot.a"),delete("structural-link","link.bc")],lambda x:x.update(packageRelations=[x["packageRelations"][0]]))]
    out=[]
    for cid,ops,mut in specs:
        inp=side_input(ops); mut(inp); doc={"schema":"ipcraft.noc-side-effects.v1","contractVersion":"ipcraft.noc-side-effects.v1","input":inp,"expected":evaluate_side(inp)}
        out.append({"caseId":cid,"description":cid.replace("side-effects-","").replace("-"," "),"document":doc,"expectedCanonicalDigest":digest(doc["expected"])})
    return out


def main()->int:
    ap=argparse.ArgumentParser(); ap.add_argument("--output-dir",type=Path,default=VECTORS); a=ap.parse_args()
    engine={"schema":"ipcraft.default-engine-behavior-vectors.v1","canonicalization":"RFC8785-after-Appendix-F-set-projection","resolutionCases":resolution_cases(),"migrationCases":migration_cases(),"freshnessCases":freshness_cases()}
    side={"schema":"ipcraft.host-side-effect-behavior-vectors.v1","contractVersion":"ipcraft.noc-side-effects.v1","canonicalization":"RFC8785-after-Appendix-F-set-projection","cases":side_cases()}
    dump(a.output_dir/"default-engine-lock-v1.json",engine); dump(a.output_dir/"host-side-effects-v1.json",side)
    print(f"generated {len(engine['resolutionCases'])} resolution, {len(engine['migrationCases'])} migration, {len(engine['freshnessCases'])} freshness, {len(side['cases'])} side-effect cases")
    return 0


if __name__=="__main__": raise SystemExit(main())
