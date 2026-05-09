# Node 9 Final V1 Architecture Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Use reasoning effort `high` for implementation work and `xhigh` for final gate review.

**Goal:** Verify the full pre-v1 mainline architecture after Nodes 1-8 and archive final readiness notes.

**Architecture:** Add one focused non-interactive Qt gate test that exercises the repository IP-core mainline flow end to end, then run the full Qt/spec/Ruby/stale-scan gate. Store readiness evidence in a tracked Node 9 readiness note and archive it with the Node 9 plan.

**Tech Stack:** C++23, Qt 6 console/widget test targets, xmake, Ruby `spec_generator`, Ruby NoC/RaveNoC generator suites, repository stale scans with `rg`.

---

## Gate Scope

Node 9 does not introduce new product features. It closes the architecture migration with executable evidence that:

- repository-generated IP cores are discoverable through `generated/ipcores`;
- the IP Catalog path can add/select a NoC IP instance;
- active workspace state exposes only that instance's module types and topology presets;
- a topology preset can create a graph owned by the selected IP core;
- project save/open round-trips graph and IP-instance state;
- validation and generation resolve through the selected IP core's command descriptors;
- generation writes Verilog artifacts and a `.fpproj` snapshot;
- old pre-v1 schema/terminology is absent from live code except explicit rejection/absence tests;
- helper artifacts `.codex/`, `.superpowers/`, and `image.png` are not committed.

## File Structure

- Create: `qt/test/v1architecturegate_test.cpp`
  - Single C++ integration-style gate for the repository Finepaper NoC flow.
- Modify: `qt/xmake.lua`
  - Add `v1architecturegate_test` target with the same production sources used by the gate path.
- Create: `docs/superpowers/readiness/2026-05-09-v1-architecture-readiness.md`
  - Final readiness notes and verification evidence.
- Modify: `docs/superpowers/plans/2026-05-09-node-9-final-v1-gate.md`
  - Track execution status.

---

## Task 9.1: Baseline Gate Inventory

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-9-final-v1-gate.md`

- [x] **Step 1: Confirm clean archive baseline**

Run:

```bash
git log --oneline --grep='archive: complete node-' -n 12
git status --short
```

Expected:

- Node 1 through Node 8 archive commits are present.
- `git status --short` shows only untracked helper artifacts before Node 9 edits:
  - `.codex`
  - `.superpowers/`
  - `image.png`

- [x] **Step 2: Confirm existing gate coverage map**

Run:

```bash
rg -n "v1architecturegate_test|ipcatalogpanel_test|projectipservice_test|topology_preset_test|projectdocument_test|ipcoregraphexporter_test|validation_test" qt/xmake.lua qt/test -g '!**/*.svg'
```

Expected:

- Existing tests cover slices of the mainline flow.
- `v1architecturegate_test` has no hits before this node adds it.

---

## Task 9.2: Add Non-Interactive V1 Mainline Gate Test

**Files:**

- Create: `qt/test/v1architecturegate_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Verify the gate target is absent**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt v1architecturegate_test
```

Expected: fails because `v1architecturegate_test` is not defined yet.

- [x] **Step 2: Add the gate test source**

Create `qt/test/v1architecturegate_test.cpp` with a single mainline flow test. The test must:

- discover `finepaper.noc` from `generated/ipcores`;
- load repository module metadata into a local `ModuleRegistry`;
- create `IpCatalogService`, `ProjectStateService`, `ProjectIpService`, and `ActiveWorkspaceController`;
- add/select the NoC instance through `ProjectIpService`;
- apply the NoC `mesh` topology preset;
- save/read/load a `.fpproj`;
- export `finepaper-ipcore-graph-v1` JSON;
- run the selected NoC DRC command through `DRCRunner`;
- run the selected NoC generator command through `GeneratorRunner`;
- write and read the generated `.fpproj` snapshot.

Use these helpers and assertions in the source:

```cpp
QString repositoryPath(const QString& relativePath);
const PluginDescriptor* findPlugin(const QList<PluginDescriptor>& plugins, const QString& id);
const TopologyPresetDescriptor* findPreset(const QVector<TopologyPresetDescriptor>& presets, const QString& id);
QString runCommand(const GeneratorCommand& command);
void writeJsonFile(const QString& path, const QJsonDocument& document);

void testRepositoryNoCMainlineFlow() {
    const QList<PluginDescriptor> plugins =
        PluginRegistry::discover({repositoryPath(QStringLiteral("generated/ipcores"))});
    const PluginDescriptor* nocPlugin = findPlugin(plugins, QStringLiteral("finepaper.noc"));
    require(nocPlugin != nullptr, "Finepaper NoC IP core should be discovered");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.loadPlugins(plugins), "repository IP modules should load");
    ModuleRegistry::instance().loadPlugins(plugins);

    IpCatalogService catalog(plugins, &registry);
    const std::optional<IpCatalogEntry> nocEntry = catalog.entry(QStringLiteral("finepaper.noc"));
    require(nocEntry.has_value(), "catalog should expose Finepaper NoC");
    require(nocEntry->moduleTypes.contains(QStringLiteral("XP")),
            "active NoC module list should include XP");
    require(nocEntry->generator.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "NoC generator should consume IP-core graph input");
    require(nocEntry->drc.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "NoC DRC should consume IP-core graph input");

    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    ActiveWorkspaceController workspace(&projectIpService, &catalog);
    const ProjectIpServiceResult created = projectIpService.ensureInstanceForIpcore(*nocEntry);
    require(created.success, created.error.toLocal8Bit().constData());
    require(workspace.state().hasActiveIp, "workspace should activate selected NoC instance");
    require(workspace.state().ipcoreId == QStringLiteral("finepaper.noc"),
            "workspace should expose selected NoC id");

    const TopologyPresetDescriptor* mesh = findPreset(workspace.state().topologyPresets,
                                                      QStringLiteral("mesh"));
    require(mesh != nullptr, "NoC workspace should expose mesh preset");
    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = workspace.state().ipcoreId;
    request.preset = *mesh;
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);
    const TopologyPresetResult topology = TopologyPresetBuilder::apply(&graph, registry, request);
    require(topology.success, topology.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "mesh topology should create four routers");
    require(graph.connections().size() == 4, "mesh topology should create four router links");

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary gate directory should be created");
    ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("v1_gate"));
    stateService.writeToDocument(document);
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("v1_gate.fpproj"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    require(writeResult.success, writeResult.error.toLocal8Bit().constData());
    const ProjectReadResult readResult = ProjectReader::readFile(projectPath);
    require(readResult.success, readResult.error.toLocal8Bit().constData());

    Graph restored;
    const GraphProjectLoadResult loadResult =
        GraphProjectSerializer::loadProject(readResult.document, restored);
    require(loadResult.success, loadResult.error.toLocal8Bit().constData());
    require(restored.modules().size() == graph.modules().size(),
            "project load should restore topology modules");
    require(restored.connections().size() == graph.connections().size(),
            "project load should restore topology connections");
    require(readResult.document.ipcoreState.size() == 1,
            "project load should preserve selected IP-instance state");

    const ProjectIpInstanceRecord& instance = stateService.ipInstanceRecords().first();
    const IpCoreGraphExportResult exportResult =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &graph,
            *nocEntry,
            instance,
            QStringLiteral("v1_gate"),
            nullptr
        });
    require(exportResult.success, exportResult.error.toLocal8Bit().constData());
    require(exportResult.document.object().value(QStringLiteral("schema")).toString() ==
                QStringLiteral("finepaper-ipcore-graph-v1"),
            "generator input should use final IP-core schema");

    DRCRunner drcRunner;
    const QList<ValidationResult> validationResults =
        drcRunner.validate(&graph, *nocEntry, instance);
    for (const ValidationResult& result : validationResults) {
        require(result.severity() != ValidationSeverity::Error,
                result.message().toLocal8Bit().constData());
    }

    const QString inputPath = QDir(tempDir.path()).filePath(QStringLiteral("v1_gate.json"));
    writeJsonFile(inputPath, exportResult.document);
    const QString outputPath = QDir(tempDir.path()).filePath(QStringLiteral("generated"));
    require(QDir().mkpath(outputPath), "generated output directory should be created");
    const GeneratorCommand generator =
        GeneratorRunner::resolveForIpcore(*nocEntry, inputPath, outputPath);
    require(generator.valid, generator.errorMessage.toLocal8Bit().constData());
    const QString generatorError = runCommand(generator);
    require(generatorError.isEmpty(), generatorError.toLocal8Bit().constData());
    require(!QDir(outputPath).entryList(QStringList{
                QStringLiteral("*.sv"),
                QStringLiteral("*.v")
            }, QDir::Files).isEmpty(),
            "generator should write RTL files");

    const GeneratedProjectSnapshotResult snapshot =
        writeGeneratedProjectSnapshot(graph,
                                      outputPath,
                                      QStringLiteral("v1_gate"),
                                      stateService.ipInstanceRecords());
    require(snapshot.success, snapshot.error.toLocal8Bit().constData());
    require(QFileInfo::exists(snapshot.path), "generated project snapshot should exist");
    const ProjectReadResult snapshotRead = ProjectReader::readFile(snapshot.path);
    require(snapshotRead.success, snapshotRead.error.toLocal8Bit().constData());
    require(snapshotRead.document.ipcoreState.size() == 1,
            "generated project snapshot should preserve IP-instance state");
}
```

- [x] **Step 3: Add the xmake target**

In `qt/xmake.lua`, append:

```lua
add_qt_test_target("v1architecturegate_test", "test/v1architecturegate_test.cpp", {
    "src/app/generationartifacts.cpp",
    "src/connection/connectionruleservice.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/ipcore/ipcoregraphexporter.cpp",
    "src/plugins/generatorrunner.cpp",
    "src/plugins/pluginregistry.cpp",
    "src/project/graphprojectserializer.cpp",
    "src/project/projectipservice.cpp",
    "src/project/projectreader.cpp",
    "src/project/projectstateservice.cpp",
    "src/project/projectwriter.cpp",
    "src/topology/topologypresetbuilder.cpp",
    "src/validation/drcrunner.cpp",
    "src/validation/validationresult.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "inc/app/generationartifacts.h",
    "inc/connection/connectionruleservice.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/ipcore/ipcoregraphexporter.h",
    "inc/plugins/generatorrunner.h",
    "inc/plugins/pluginregistry.h",
    "inc/project/graphprojectserializer.h",
    "inc/project/projectipservice.h",
    "inc/project/projectreader.h",
    "inc/project/projectstateservice.h",
    "inc/project/projectwriter.h",
    "inc/topology/topologypresetbuilder.h",
    "inc/validation/drcrunner.h",
    "inc/validation/validationresult.h"
})
```

- [x] **Step 4: Run the new gate test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt v1architecturegate_test
```

Expected: prints `v1architecturegate_test passed`.

---

## Task 9.3: Final Verification Commands

**Files:** none expected unless verification reveals a real gap.

- [x] **Step 1: Run full Qt test suite**

Run:

```bash
CCACHE_DISABLE=1 xmake test -P qt
```

Expected: every xmake Qt test target passes, including `v1architecturegate_test`.

- [x] **Step 2: Run release Qt app build**

Run:

```bash
CCACHE_DISABLE=1 xmake -P qt -r qt
```

Expected: target `qt` builds successfully.

- [x] **Step 3: Run spec generator and drift checks**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
ruby spec_generator/bin/spec-gen --check
```

Expected:

- `spec_generator_test.rb` reports 0 failures and 0 errors.
- `spec-gen --check` reports generated IP core runtime artifacts are up to date.

- [x] **Step 4: Run generator suites**

Run:

```bash
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_smoke.rb
```

Expected: all report 0 failures and 0 errors.

- [x] **Step 5: Run stale-term scans**

Run:

```bash
rg -u -n "finepaper-plugin-graph-v1|generic_graph_v1|GraphJsonFlavor|toJsonDocument\\(|FrameworkPaths|FRAMEWORK_PATH|framework/|pluginstate|SetPluginStateParameterCommand|onPluginStateParameterChanged|pluginprojectadapter|PluginProjectAdapter|PluginParameterField|PluginParameterSection|ManifestPluginProjectAdapter|IPluginProjectAdapter|activeIpCombo|paletteDock|Palette dock" \
  qt/inc qt/src qt/test spec_generator ipcores generated/ipcores \
  -g '!**/*.svg'

rg -u -n "plugin_state|ip_instances|\\\"plugin\\\"|QStringLiteral\\(\"plugin\"\\)" \
  qt/inc qt/src qt/test spec_generator ipcores generated/ipcores \
  -g '!**/*.svg'
```

Expected:

- hard live-code scan only hits absence assertions for removed UI names in
  `qt/test/ipcatalogpanel_test.cpp`, if any;
- legacy project-field scan only hits `qt/src/project/projectreader.cpp` and
  `qt/test/projectdocument_test.cpp` explicit pre-v1 rejection /
  writer-not-emitting tests.

- [x] **Step 6: Run repository hygiene checks**

Run:

```bash
git diff --check
git status --short
```

Expected:

- `git diff --check` prints no output;
- `.codex`, `.superpowers/`, and `image.png` remain untracked and are not staged.

---

## Task 9.4: Readiness Notes

**Files:**

- Create: `docs/superpowers/readiness/2026-05-09-v1-architecture-readiness.md`

- [x] **Step 1: Create readiness note**

Create `docs/superpowers/readiness/2026-05-09-v1-architecture-readiness.md` with:

```markdown
# V1 Architecture Readiness Notes

Date: 2026-05-09

## Archive Baseline

- Node 1: `d685a76` `archive: complete node-1 spec source of truth`
- Node 2: `186dcc3` `archive: complete node-2 ipcore vocabulary migration`
- Node 3: `8fcab74` `archive: complete node-3 project ip services`
- Node 4: `6068a0a` `archive: complete node-4 ip catalog ui`
- Node 5: `c0adb8c` `archive: complete node-5 scoped workspace tools`
- Node 6: `0272da1` `archive: complete node-6 connection semantics split`
- Node 7: `018ff77` `archive: complete node-7 ipcore generation boundary`
- Node 8: `2a357e8` `archive: complete node-8 historical cleanup`

## Mainline Gate

`v1architecturegate_test` verifies the repository Finepaper NoC flow:

- discover `finepaper.noc` from `generated/ipcores`;
- add/select one NoC IP instance;
- expose active workspace modules and topology presets;
- create a 2x2 mesh graph;
- save/read/load `.fpproj` with IP-instance state;
- export `finepaper-ipcore-graph-v1`;
- run DRC and generation through the selected IP core command descriptors;
- write a generated `.fpproj` snapshot beside RTL output.

## Verification Evidence

- `CCACHE_DISABLE=1 xmake run -P qt v1architecturegate_test`
- `CCACHE_DISABLE=1 xmake test -P qt`
- `CCACHE_DISABLE=1 xmake -P qt -r qt`
- `ruby spec_generator/test/spec_generator_test.rb`
- `ruby spec_generator/bin/spec-gen --check`
- `ruby ipcores/finepaper-noc/generator/test/test_generator.rb`
- `ruby ipcores/ravenoc/generator/test/test_generator.rb`
- `ruby ipcores/ravenoc/generator/test/test_smoke.rb`
- hard live-code stale scan
- legacy project-field allowlist scan
- `git diff --check`

All commands above passed during Node 9 execution.

## Residual Notes

- Runtime plugin infrastructure names remain intentionally: `PluginRegistry`, `PluginDescriptor`, `PluginCommandDescriptor`, `plugin.json`, native plugin metadata, and `FINEPAPER_PLUGIN_PATH`.
- Explicit pre-v1 `.fpproj` rejection guards remain intentionally in `ProjectReader` and `projectdocument_test`.
- No compatibility promise is made for pre-v1 project files.
```

---

## Task 9.5: Final Review, Supervisor Preflight, And Archive

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-9-final-v1-gate.md`

- [x] **Step 1: Request xhigh final gate review**

Ask an `xhigh` reviewer to inspect:

- `v1architecturegate_test` covers the mainline gate without depending on old fallback paths;
- full verification command results match Task 9.3 expectations;
- stale scans match the allowlist;
- readiness notes accurately summarize evidence and residual intentionally-kept runtime plugin infrastructure;
- `.codex`, `.superpowers/`, and `image.png` are not staged.

- [x] **Step 2: Supervisor preflight**

Send the standing supervisor:

- final verification command results;
- stale scan output and allowlist assessment;
- `git diff --stat`;
- `git status --short`;
- confirmation that helper artifacts are not staged.

- [x] **Step 3: Archive Node 9**

Run:

```bash
git status --short
git add docs/superpowers/plans/2026-05-09-node-9-final-v1-gate.md \
        docs/superpowers/readiness/2026-05-09-v1-architecture-readiness.md \
        qt/test/v1architecturegate_test.cpp \
        qt/xmake.lua
git commit -m "archive: complete node-9 v1 architecture gate"
```

Expected:

- commit succeeds;
- `.codex`, `.superpowers/`, and `image.png` remain untracked and uncommitted.

---

## Self-Review

- Spec coverage: The plan covers the umbrella Node 9 gate plus the design requirement to produce final readiness notes.
- Existing gap addressed: A new non-interactive test stitches together the existing slice tests into one mainline gate for Finepaper NoC.
- Scope control: The plan does not add new product behavior or compatibility fallback; it only adds verification and readiness documentation.
- Command correction: The stale scan excludes deleted `plugins/` path so the command itself does not fail after Node 8 cleanup.
