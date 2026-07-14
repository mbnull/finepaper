#!/usr/bin/env python3
"""NON-NORMATIVE deterministic authoring generator for Engine/Host vectors."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
VECTORS = ROOT / "docs/contracts/vectors"
D = {c: "sha256:" + c * 64 for c in "abcdef"}


def dump(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def cj(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def digest(value: object) -> str:
    return "sha256:" + hashlib.sha256(cj(value).encode()).hexdigest()


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
    out = {"bundleManifestDigest":d,"verifiedBundleManifestDigest":d,"installed":True,"revoked":False,"contentVerified":True,"manifest":manifest()}
    out.update(kw)
    return out


def resolution_case(case_id: str, description: str, exact: dict | None = None, alternatives: list | None = None,
                    outcome: str = "exact", code: str | None = None, selected: str | None = D["a"], upgrade: bool = False) -> dict:
    return {"id":case_id,"description":description,"input":{"projectLock":lock(),"installedBundles":[] if exact is None else [exact],
            "currentPlatformAbi":"linux-x86_64-gnu-v1","supportedEngineHostContracts":["ipcraft.engine-host.v1"],
            "supportedHostSideEffectContracts":["ipcraft.noc-side-effects.v1"],"alternativeBundles":alternatives or []},
            "expected":{"outcome":outcome,"selectedBundleManifestDigest":selected,"diagnosticCode":code,"upgradeAvailable":upgrade}}


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


def applicability(d: str=D["a"]) -> dict:
    return {"schema":"ipcraft.reconcile-applicability.v1","groupId":"group.migration","requestGeneration":1,"topologyInputRevision":4,
            "topologyInputDigest":D["c"],"baseDerivedStateRevision":7,"baseDerivedStateDigest":D["d"],"baseAuthoritativeDesignDigest":D["e"],
            "structureAuthority":{"kind":"default-engine","lockId":"dep.default-engine","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":d},
            "packageBundleDigest":D["f"],"reconcileDependencySetDigest":D["c"],"defaultEngineLockId":"dep.default-engine",
            "defaultEngineBundleDigest":d,"engineHostContractVersion":"ipcraft.engine-host.v1","hostSideEffectContractVersion":"ipcraft.noc-side-effects.v1"}


def derivation(target: dict) -> dict:
    return {"topologyInputRevision":5,"topologyInputDigest":D["b"],"derivedStateRevision":8,"derivedStateDigest":D["c"],
            "packageBundleDigest":D["f"],"reconcileDependencySetDigest":D["b"],"defaultEngineLockId":target["lockId"],
            "defaultEngineBundleDigest":target["bundleManifestDigest"],"engineHostContractVersion":target["engineHostContractVersion"],
            "hostSideEffectContractVersion":target["hostSideEffectContractVersion"],"structureAuthority":{"kind":"default-engine","lockId":target["lockId"],
            "identity":target["id"],"version":target["version"],"bundleDigest":target["bundleManifestDigest"]},"engineCompatibilityVersion":target["engineCompatibilityVersion"]}


def migration_record() -> dict:
    current,target=lock(),lock(D["b"],"2.0.0","2")
    deps=[{"lockId":"dep.noc","kind":"noc-package","id":"vendor.noc","version":"1.0.0","bundleManifestDigest":D["f"]},target]
    old_der={**derivation(current),"topologyInputRevision":4,"derivedStateRevision":7,"defaultEngineBundleDigest":D["a"],"structureAuthority":{**derivation(current)["structureAuthority"],"bundleDigest":D["a"]}}
    forward={"projectDependencies":deps,"topologyDerivation":derivation(target),"derivedState":{"router.r0":"host-router-001"},"hostSideEffects":{"membership.default.r0":"host-membership-001"},"localRefToHostId":{"authority:router-0":"host-router-001","application:000001":"host-membership-001"}}
    inverse={"projectDependencies":[deps[0],current],"topologyDerivation":old_der,"derivedState":{"router.r0":"host-router-001"},"hostSideEffects":{"membership.default.r0":"host-membership-001"},"localRefToHostId":forward["localRefToHostId"]}
    migration={"currentDefaultEngineLock":current,"targetDefaultEngineLock":target}
    impact_value={"code":"engine_migration.dependency_replaced","severity":"warning","dataLoss":False,"subjects":[{"kind":"project","id":"project.main"}],"details":{"lockId":"dep.default-engine","currentBundleDigest":D["a"],"targetBundleDigest":D["b"]},"resolution":"confirm-or-discard"}
    candidate={"schema":"ipcraft.candidate-transaction.v1","transactionId":"tx.migration","kind":"default-engine-migration","applicability":applicability(),
      "topologyIntent":{"schema":"ipcraft.topology-intent.v1","componentId":"component.noc","topologyId":"topology.main","topologyKind":"mesh","globalConfig":{"columns":3,"rows":2},"packageEntities":[],"packageRelations":[]},
      "authorityPatch":{"patchId":"patch.target-engine","source":{"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"2.0.0","bundleDigest":D["b"]},"operations":[]},
      "applicationPatch":{"patchId":"patch.migration","source":{"kind":"application-migration","identity":"host","version":"1"},"operations":[{"op":"updateEntity","entityKind":"project","id":"project.main","set":{"dependencies":deps},"unset":[]},{"op":"updateEntity","entityKind":"topology","id":"topology.main","set":{"derivation":derivation(target)},"unset":[]}]},
      "migration":migration,"tombstones":[],"allocationOrder":[],"impactReport":{"schema":"ipcraft.topology-impact-report.v1","impacts":[impact_value]}}
    candidate["candidateDigest"]=digest(candidate)
    return {"migration":migration,"candidate":candidate,"applicability":applicability(),"targetManifest":manifest("2.0.0","2"),
            "baseDependencies":[deps[0],current],"forwardTransaction":forward,"inverseTransaction":inverse,
            "dependencyProvenance":{"unchangedLockIds":["dep.noc"],"replacedLockId":"dep.default-engine"},"derivationProvenance":{"current":old_der,"target":derivation(target)},
            "impactProvenance":{"code":"engine_migration.dependency_replaced","lockId":"dep.default-engine","currentBundleDigest":D["a"],"targetBundleDigest":D["b"]}}


def migration_cases() -> list[dict]:
    base=migration_record()
    def c(i,d,mut=None,exp=None):
        x=copy.deepcopy(base); (mut or (lambda _:None))(x)
        return {"id":i,"description":d,"input":x,"expected":exp or {"offered":True,"groupState":"ready-to-commit","requiresConfirmation":True,"atomicCommit":True,"engineExecutions":[D["b"]]}}
    return [c("engine-migration-compatible-target-offered","Target declares source compatibility."),
            c("engine-migration-incompatible-target-not-offered","Target omits source compatibility.",lambda x:x["targetManifest"].update(migrationFromCompatibilityVersions=["9"]),{"offered":False,"diagnosticCode":"engine.migration_incompatible","engineExecutions":[]}),
            c("engine-migration-atomic-commit","Lock, Derived State, side effects, mapping, and provenance commit atomically."),
            c("engine-migration-blocked-by-package-relation","Universal relation blocking priority wins.",lambda x:x.update(blockingImpact="package_relation.endpoint_blocks_candidate"),{"offered":True,"groupState":"blocked","requiresConfirmation":False,"atomicCommit":False,"engineExecutions":[D["b"]]}),
            c("engine-migration-undo-exact-inverse","Undo restores exact stored inverse without Engine execution.",lambda x:x.update(action="undo"),{"offered":True,"restoredTransaction":"inverseTransaction","stableHostIds":True,"engineExecutions":[],"resultMode":"normal"}),
            c("engine-migration-undo-source-missing-degraded","Undo restores exact state then degrades if source Bundle is missing.",lambda x:x.update(action="undo",sourceBundleAvailable=False),{"offered":True,"restoredTransaction":"inverseTransaction","stableHostIds":True,"engineExecutions":[],"resultMode":"degraded-inspect","diagnosticCode":"engine.bundle_missing"})]


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
def domain(i,t,default): return {"id":i,"typeKey":t,"name":i,"isDefault":default,"config":{"frequencyMHz":100}}
def member(i,d,r): return {"id":i,"domainId":d,"routerId":r}
def resolved(kind,id): return {"state":"resolved","subject":{"kind":kind,"id":id}}


def side_input(ops:list[dict]) -> dict:
    dtype=lambda k:{"key":k,"label":k.title(),"defaultName":k+"-default","visual":{"fill":"#123456","border":"#abcdef","pattern":"solid"},"configuration":{"fields":[]}}
    decl=lambda k,allow:{"typeKey":k,"ownership":"user","topologyDriving":False,"sources":{"kinds":["router"],"minimum":1,"maximum":1},"targets":{"kinds":["package-entity"],"minimum":1,"maximum":1},"unresolvedAllowed":allow,"schema":{}}
    return {"domainTypes":[dtype("clock"),dtype("power")],"relationDeclarations":[decl("route.allowed",True),decl("route.blocked",False)],
      "currentDerivedState":{"schema":"ipcraft.derived-state.v1","topologyId":"topology.main","routers":[router("router.a",0,0),router("router.b",0,1),router("router.c",0,2)],
      "structuralLinks":[link("link.ab","router.a","router.b"),link("link.bc","router.b","router.c")],"accessSlots":[slot("slot.a","router.a"),slot("slot.b","router.b")],"packageEntities":[],"packageRelations":[]},
      "domains":[domain("domain.clock.default","clock",True),domain("domain.power.default","power",True),domain("domain.clock.aux","clock",False)],
      "domainMemberships":[member("membership.ca","domain.clock.default","router.a"),member("membership.cb","domain.clock.default","router.b"),member("membership.cc","domain.clock.default","router.c"),member("membership.pa","domain.power.default","router.a"),member("membership.pb","domain.power.default","router.b"),member("membership.pc","domain.power.default","router.c"),member("membership.auxa","domain.clock.aux","router.a"),member("membership.auxb","domain.clock.aux","router.b")],
      "attachments":[{"id":"attachment.a","interfaceId":"interface.a","state":"resolved","routerId":"router.a","slotId":"slot.a"}],
      "packageRelations":[{"id":"relation.allowed","typeKey":"route.allowed","sources":[resolved("router","router.a")],"targets":[resolved("package-entity","entity.a")],"data":{"weight":1},"extensions":[]},{"id":"relation.blocked","typeKey":"route.blocked","sources":[resolved("router","router.b")],"targets":[resolved("package-entity","entity.a")],"data":{"weight":2},"extensions":[]}],
      "authorityPatch":{"patchId":"patch.authority","source":{"kind":"default-engine","identity":"ipcraft.default-noc-engine","version":"1.0.0","bundleDigest":D["a"]},"operations":ops}}


def ref_token(ref): return "id:"+ref["id"] if "id" in ref else "localRef:"+ref["localRef"]


def evaluate_side(inp:dict)->dict:
    ops=[]; impacts=[]; tomb=[]; diagnostics=[]
    deleted_r={o["id"] for o in inp["authorityPatch"]["operations"] if o["op"]=="deleteEntity" and o["entityKind"]=="router"}
    deleted_s={o["id"] for o in inp["authorityPatch"]["operations"] if o["op"]=="deleteEntity" and o["entityKind"]=="access-slot"}
    deleted_l={o["id"] for o in inp["authorityPatch"]["operations"] if o["op"]=="deleteEntity" and o["entityKind"]=="structural-link"}
    created=[o for o in inp["authorityPatch"]["operations"] if o["op"]=="createEntity" and o["entityKind"]=="router"]
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
    remaining_members=[m for m in inp["domainMemberships"] if m["routerId"] not in deleted_r]
    for d in sorted(inp["domains"],key=lambda x:x["id"]):
        members={m["routerId"] for m in remaining_members if m["domainId"]==d["id"]}
        if not members and not d["isDefault"]:
            ops.append({"op":"deleteEntity","entityKind":"domain","id":d["id"]}); tomb.append({"subjectKind":"domain","id":d["id"],"value":d})
            impacts.append({"code":"domain.non_default_deleted","severity":"warning","dataLoss":True,"subjects":[{"kind":"domain","id":d["id"]}],"details":{"discardedConfig":True},"resolution":"confirm-or-discard"}); continue
        if len(members)>1:
            edges=[]
            for l in inp["currentDerivedState"]["structuralLinks"]:
                if l["id"] not in deleted_l and l["endpointA"] not in deleted_r and l["endpointB"] not in deleted_r: edges.append((l["endpointA"],l["endpointB"]))
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


def side_cases()->list[dict]:
    create=lambda lr,r,c:{"op":"createEntity","entityKind":"router","localRef":lr,"value":{"templateKey":"mesh-router","identityCompatibilityVersion":1,"coordinate":{"row":r,"column":c},"properties":{}}}
    delete=lambda kind,i:{"op":"deleteEntity","entityKind":kind,"id":i}
    specs=[("side-effects-created-router-default-memberships",[create("authority:router-z",1,0)],lambda x:None),
      ("side-effects-created-routers-membership-order",[create("authority:router-z",1,0),create("authority:router-a",1,1)],lambda x:None),
      ("side-effects-deleted-router-memberships",[delete("router","router.c")],lambda x:x.update(packageRelations=[])),
      ("side-effects-deleted-router-attachment-unresolved",[delete("router","router.a")],lambda x:x.update(packageRelations=[])),
      ("side-effects-deleted-slot-attachment-unresolved",[delete("access-slot","slot.a")],lambda x:x.update(packageRelations=[])),
      ("side-effects-package-relation-endpoint-unresolved",[delete("router","router.a")],lambda x:x.update(attachments=[],packageRelations=[x["packageRelations"][0]])),
      ("side-effects-package-relation-endpoint-blocked",[delete("router","router.b")],lambda x:x.update(attachments=[],packageRelations=[x["packageRelations"][1]])),
      ("side-effects-empty-non-default-domain-tombstone",[delete("router","router.a"),delete("router","router.b")],lambda x:x.update(attachments=[],packageRelations=[])),
      ("side-effects-empty-default-domain-preserved",[delete("router","router.a"),delete("router","router.b"),delete("router","router.c")],lambda x:x.update(attachments=[],packageRelations=[],domains=[x["domains"][0]],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"]=="domain.clock.default"])),
      ("side-effects-domain-disconnected-router-delete",[delete("router","router.b")],lambda x:x.update(attachments=[],packageRelations=[],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"]!="domain.clock.aux"])),
      ("side-effects-domain-disconnected-link-delete",[delete("structural-link","link.ab")],lambda x:x.update(attachments=[],packageRelations=[],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"]!="domain.clock.aux"])),
      ("side-effects-domain-disconnected-membership-placement",[],lambda x:x.update(attachments=[],packageRelations=[],domainMemberships=[m for m in x["domainMemberships"] if m["domainId"] not in ("domain.clock.aux",) and not (m["domainId"]=="domain.clock.default" and m["routerId"]=="router.b")])),
      ("side-effects-combined-deterministic-order",[create("authority:router-z",1,0),delete("router","router.a"),delete("access-slot","slot.a"),delete("structural-link","link.bc")],lambda x:x.update(packageRelations=[x["packageRelations"][0]]))]
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
