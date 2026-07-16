# Appendix D — Current-to-Target Migration and Cutover Map

**Normative status:** V1 Revision 5 Core contract; cutover boundary frozen by the Gate 0 Revision 5 record.

## D1. Migration Strategy

Gates 0–F use isolated coexistence with mutually exclusive process-startup composition roots. Gate G removes legacy.

```text
legacy path
  .fpproj + ProjectDocument + legacy MainWindow/Graph/CommandManager

new path --next-workbench
  .nocproj + ProjectDesign V1 + DesignSession + Default Engine/new Workbench
```

The new path must never translate into legacy state to reuse a legacy Service.

The current `qt` target compiles `src/**.cpp`, so source-directory separation alone cannot prove link isolation. Gate 0/A first split the build graph into at least:

```text
ipcraft_noc_domain
ipcraft_noc_application
ipcraft_legacy_application
ipcraft_startup_launcher
```

The thin launcher may link both mutually exclusive composition roots before Gate G. New-path libraries may not link legacy application/authority libraries. Architecture tests inspect the library dependency graph and prohibited symbols/includes, not merely runtime selection.

## D2. Status Vocabulary

| Status | Meaning |
|---|---|
| `reuse` | Stateless implementation may be used unchanged after dependency audit. |
| `extract` | Pure/stateless lower-level behavior may be moved behind a new port; old stateful API is not reused. |
| `adapt` | Component remains but consumes the new normative contract directly. |
| `replace` | Existing responsibility remains, but implementation/model is rewritten for the new path. |
| `freeze` | Legacy-only; build/crash fixes only, no product behavior. |
| `delete-g` | Removed only after Gate G acceptance. |

## D3. Model and Persistence Map

| Current area | Current role | Gate A–F | Target | Gate G |
|---|---|---|---|---|
| `qt/inc/ipcraft/core/project_design.h` | broad pre-release ProjectDesign used by legacy services | `freeze`; new path may use a clearly versioned temporary namespace/type | exact Appendix A `ProjectDesign V1` | legacy type removed and new type may take canonical name |
| `schemas/ipcraft.project.v1.schema.json` | one conflicting ProjectDesign root | `freeze` legacy | new `ipcraft.project-design.v1` schema in new path | old schema deleted |
| `ProjectDocument` (`qt/inc/project/projectdocument.h`) | legacy project authority | `freeze` | none | `delete-g` |
| `ProjectReader/Writer` for `.fpproj` | legacy persistence | `freeze` | independent `.nocproj` repository | `delete-g` |
| `ProjectDesignSerializer` supplement bridge | bidirectional legacy bridge | `freeze`; prohibited in new path | none | `delete-g` |
| `ProjectService` | owns both ProjectDocument and ProjectDesign | `freeze`; prohibited in new path | `DesignSession` + repository port | `delete-g` |
| `qt/inc/ipcraft/core/project_patch.h` and implementation | narrow add/set_config Patch | `replace` for new path | Appendix B Patch engine | old API/tests deleted |

## D4. State, Editing, and Projection Map

| Current area | Gate A–F treatment | Target | Gate G |
|---|---|---|---|
| `DesignEditingService` | freeze; prohibited | typed Application commands + DesignSession history | delete |
| `ProjectStateService` | freeze; prohibited | no equivalent authority | delete |
| `ProjectIpService` | freeze; prohibited | NoC Profile use cases over DesignSession | delete |
| legacy `CommandManager` | freeze; prohibited | one new typed-command history | delete |
| legacy `Graph` as authority | freeze; prohibited | projection-only Canvas model | delete authority code; pure renderer may survive only after audit |
| `EditorProjectionService` | freeze; prohibited | new incremental Projection Layer | delete |
| `NodeEditorWidget` | legacy-only freeze | new Workbench Canvas; may reuse third-party QtNodes rendering primitives but not state model | delete or reduce to pure view |
| `PropertyPanel` | legacy-only freeze | schema-driven Inspector | delete/replace |

Static architecture tests for the new path must reject includes or link references to:

```text
project/projectdocument.h
project/projectservice.h
project/projectdesignserializer.h
graph/graph.h (authority API)
commands/commandmanager.h
project/designeditingservice.h
project/projectstateservice.h
project/projectipservice.h
```

## D5. Application and UI Map

| Current area | Gate A–F | Target | Gate G |
|---|---|---|---|
| legacy `MainWindow` | freeze under normal/legacy startup | new composition root and Workbench under `--next-workbench` | delete legacy MainWindow or remove old composition |
| IP Catalog dock | freeze in legacy | Package selection only in new wizard; developer inspector separate | delete legacy dock behavior |
| Activity Log dock | freeze | Problems + Output + support logging | delete legacy log UI |
| developer Package Inspector | may be adapted only under startup developer mode | separate developer layout | remove obsolete capability browser |

The internal startup option is not a product feature. Gate G removes it and makes the new composition root unconditional.

## D6. Package and Contract Runtime Map

| Current area | Gate A–F | Target | Gate G |
|---|---|---|---|
| `ipcraft.package.v1` reader | freeze legacy | new `ipcraft.noc-package.v1` reader | delete old reader if no other supported profile uses it |
| built-in Default Engine code | legacy/ad-hoc behavior only | extract/replace as independently installable exact `ipcraft.engine-bundle.v1` behind `engineHostContractVersion` | no unbundled fallback Engine remains |
| `PackageService` filesystem discovery | `extract/adapt` after stateless audit | discovers new manifests and computes locks/digests | remove legacy manifest projections |
| `IpCatalogService` | `adapt` | produces wizard cards from new Package contract | remove instance-add semantics |
| internal PluginHost/registries | keep only as internal composition if they do not enter project/domain contracts | startup composition mechanism, not Package public ABI | delete unused registries |
| XML/custom Package views | freeze legacy | V1 schema-driven Inspector and built-in presentation primitives | delete legacy public view injection |
| `unknownSections` preservation | freeze legacy | namespaced generic/opaque fallback only | delete broad loose preservation |

Shipped migration order:

1. `finepaper.noc` — first headless/UX reference Package.
2. `finepaper.ravenoc` — second declarative Package.
3. `finepaper.opennoc` — third declarative Package.
4. `vendor.meshnoc` — fixture only; does not count as shipped Gate F Package.

## D7. Tooling Map

| Current area | Gate A–F | Target | Gate G |
|---|---|---|---|
| `FlowRunner` low-level process handling | `extract` safe process/staging primitives only | `RunCoordinator` + ToolRunner ports | remove legacy record/config adapters |
| `ProjectGenerationRunner` | freeze; prohibited | Snapshot/Tool manifest generation and run promotion | delete |
| `ProjectValidationRunner` and external runner | freeze; prohibited | Core Structural DRC + semantic DRC ToolRunner | delete |
| current emitted-inputs/GraphConfig writers | freeze legacy | Appendix C canonical ProjectDesign Snapshot/tool input | delete if not used by new tools |
| output deletion/promotion logic | do not reuse without audit | run-specific staging + rollback-safe promotion | delete unsafe old path |

Reusable utilities must be extracted below Application ports and must not accept `ProjectDocument`, `ProjectIpInstanceRecord`, legacy Graph, or legacy project paths.

Using a temporary versioned new-path namespace during A–F is migration isolation, not authorization for a second NoC IR: only the new type may implement Appendix A and only the new path may consume it. No adapter or synchronization between old and new ProjectDesign types is allowed.

## D8. Test Map

During Gates A–F:

- legacy build/smoke tests remain only to ensure frozen path still builds;
- new tests link only the new path;
- no test may synchronize new and legacy models;
- architecture scans enforce prohibited dependencies.

At Gate G:

- tests whose sole purpose is `.fpproj`, supplement bridges, legacy instance projection, legacy Graph authority, old undo, or legacy MainWindow behavior are deleted with their code;
- Package source/generator tests may be migrated if they remain meaningful under Appendix C;
- blinded acceptance and all new Gate tests become mandatory release tests.

## D9. Allowed Legacy Changes

Allowed before Gate G:

- build fixes caused by shared toolchain/compiler changes;
- critical crash/security fixes;
- minimal adaptation to extracted stateless utilities.

Forbidden:

- new legacy UI features;
- new `.fpproj` fields;
- new Package capabilities through the old reader;
- new synchronization between DesignSession and any legacy service;
- implementing each typed command a second time in legacy CommandManager;
- converting `.nocproj` to legacy records to reuse generation or validation.

## D10. Cutover Completion Criteria

Gate G is complete only when repository scans prove:

- no production composition instantiates legacy authority;
- no production save path writes `.fpproj`;
- no production model stores `ipcraft.projectDesignSupplement.v1`;
- only one formal authoritative command-history manager exists in production; Draft/Group local stacks are interaction-only;
- Graph/QtNodes is projection-only or removed;
- the internal startup flag and legacy composition root are gone;
- all three shipped Packages and blinded acceptance pass on the new path.
