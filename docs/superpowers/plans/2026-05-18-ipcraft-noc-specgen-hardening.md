# Ipcraft NoC Specgen Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden the existing NoC package flow so `finepaper-noc`, `opennoc`, and `ravenoc` are driven by `ipcore.yml`, view XML, generated `ipcraft.json`, metadata-based Qt UX, and a framework-owned common generator path.

**Architecture:** Keep Qt runtime package loading centered on `ipcraft.json`; keep `ipcore.yml` parsing inside `specgen`; keep `.fpproj` as the saved editor document; export `ipcraft.noc.project.v1` only as command input. Add manifest metadata for display labels, topology semantics, generation configuration, and package diagnostics, then migrate frontend behavior and generator behavior to consume that metadata.

**Tech Stack:** Ruby 3/Minitest for `spec_generator` and common generator tests, C++23/Qt Widgets/Qt JSON/XML APIs for editor behavior, xmake Qt test targets, existing package generator fixtures for parity, `ipcraft.noc.project.v1` JSON command input.

---

## File Structure

Specgen:

- Modify: `spec_generator/lib/spec_generator.rb` for schema validation, manifest emission, normalized drift checks, display/topology/generation metadata.
- Modify: `spec_generator/bin/spec-gen` for check/build/generation validation entry points if new flags are needed.
- Modify: `spec_generator/test/spec_generator_test.rb` for package-source, drift, display binding, topology join, and generation metadata tests.

Package source:

- Modify: `ipcores/finepaper-noc/ipcore.yml`
- Modify: `ipcores/opennoc/ipcore.yml`
- Modify: `ipcores/ravenoc/ipcore.yml`
- Regenerate: `ipcores/finepaper-noc/ipcraft.json`
- Regenerate: `ipcores/opennoc/ipcraft.json`
- Regenerate: `ipcores/ravenoc/ipcraft.json`
- Modify views only when required by manifest/view join validation: `ipcores/*/views/*.xml`

Qt manifest/runtime:

- Modify: `qt/inc/ipcraft/ipcraftmanifest.h`
- Modify: `qt/src/ipcraft/ipcraftmanifest.cpp`
- Modify: `qt/src/ipcraft/ipcraftmanifestreader.cpp`
- Modify: `qt/src/ipcraft/ipcraftregistry.cpp`
- Modify tests: `qt/test/ipcraftmanifest_test.cpp`, `qt/test/ipcatalogservice_test.cpp`

Qt UX and behavior:

- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Create: `qt/inc/panels/ipcorepathsdialog.h`
- Create: `qt/src/panels/ipcorepathsdialog.cpp`
- Modify: `qt/xmake.lua`
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/inc/common/portlayout.h`
- Modify: `qt/src/topology/topologypresetbuilder.cpp`
- Modify tests: `qt/test/connectionruleservice_test.cpp`, `qt/test/topology_preset_test.cpp`, `qt/test/ipcatalogpanel_test.cpp`

Command input and generation:

- Modify: `qt/src/ipcore/ipcoregraphexporter.cpp`
- Modify tests: `qt/test/ipcoregraphexporter_test.cpp`, `qt/test/projectgenerationrunner_test.cpp`
- Create: `ipcraft_generator/bin/ipcraft-generate`
- Create: `ipcraft_generator/lib/ipcraft_generator.rb`
- Create: `ipcraft_generator/test/ipcraft_generator_test.rb`
- Modify package command declarations in `ipcores/*/ipcore.yml` after parity tests are passing.

## Implementation Order

Do not start the common generator migration until specgen metadata, Qt package loading, connection labels, and topology metadata tests are passing. Keep package-local generators as parity references until each package has framework-owned output tests.

## Task 1: Specgen Manifest Metadata Contract

**Files:**

- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Write failing tests for display and topology metadata**

Add tests to `SpecGeneratorTest`:

```ruby
def test_emits_display_label_binding
  Dir.mktmpdir do |dir|
    package_root = write_ipcraft_package_source(
      dir,
      yaml: ipcraft_package_yaml.sub(
        "parameters:\n            x:",
        "display:\n            label_parameter: display_name\n          parameters:\n            display_name: { type: string, default: XP }\n            x:"
      )
    )

    build_ipcraft_manifest(package_root)
    manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))
    xp = manifest.fetch('modules').find { |mod| mod.fetch('id') == 'xp' }

    assert_equal({ 'label_parameter' => 'display_name' }, xp.fetch('display'))
  end
end

def test_rejects_display_label_binding_missing_parameter
  Dir.mktmpdir do |dir|
    package_root = write_ipcraft_package_source(
      dir,
      yaml: ipcraft_package_yaml.sub(
        "parameters:\n            x:",
        "display:\n            label_parameter: missing_label\n          parameters:\n            x:"
      )
    )

    error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
    assert_match(/module xp display.label_parameter references unknown parameter missing_label/, error.message)
  end
end
```

Add topology-side join tests:

```ruby
def test_emits_topology_side_metadata
  Dir.mktmpdir do |dir|
    yaml = ipcraft_package_yaml.sub(
      "modes: [chi_interconnect]\n              accepts:",
      "topology:\n                side: east\n                opposite: west\n              modes: [chi_interconnect]\n              accepts:"
    )
    package_root = write_ipcraft_package_source(dir, yaml: yaml)

    build_ipcraft_manifest(package_root)
    manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))
    xp = manifest.fetch('modules').find { |mod| mod.fetch('id') == 'xp' }
    rnf0 = xp.fetch('interfaces').find { |interface| interface.fetch('id') == 'rnf0' }

    assert_equal({ 'side' => 'east', 'opposite' => 'west' }, rnf0.fetch('topology'))
  end
end

def test_rejects_topology_opposite_that_is_not_a_module_interface
  Dir.mktmpdir do |dir|
    yaml = ipcraft_package_yaml.sub(
      "modes: [chi_interconnect]\n              accepts:",
      "topology:\n                side: east\n                opposite: missing_west\n              modes: [chi_interconnect]\n              accepts:"
    )
    package_root = write_ipcraft_package_source(dir, yaml: yaml)

    error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
    assert_match(/xp.rnf0 topology.opposite references unknown interface missing_west/, error.message)
  end
end
```

- [ ] **Step 2: Run the failing tests**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: the new tests fail because `display` and interface `topology` are unknown fields.

- [ ] **Step 3: Implement display and topology schema**

In `spec_generator/lib/spec_generator.rb`, extend constants and normalization:

```ruby
IPCRAFT_MODULE_KEYS = %w[id name description graph_role attach display parameters interfaces].freeze
IPCRAFT_MODULE_DISPLAY_KEYS = %w[label_parameter short_label_parameter].freeze
IPCRAFT_INTERFACE_KEYS = %w[id name label modes accepts multi_connection ipxact parameters topology].freeze
IPCRAFT_INTERFACE_TOPOLOGY_KEYS = %w[side opposite role].freeze
```

Add normalizers:

```ruby
def normalize_module_display(module_id, display, parameters)
  return nil unless display
  raise SpecError, "module #{module_id}.display must be a map" unless display.is_a?(Hash)
  validate_keys!(display, IPCRAFT_MODULE_DISPLAY_KEYS, "module #{module_id}.display")

  normalized = {}
  %w[label_parameter short_label_parameter].each do |key|
    next unless display.key?(key)
    value = required_string(display, key, "module #{module_id}.display.#{key}")
    raise SpecError, "module #{module_id} display.#{key} references unknown parameter #{value}" unless parameters.key?(value)
    normalized[key] = value
  end
  normalized
end

def normalize_interface_topology(module_id, interface_id, topology, known_interfaces)
  return nil unless topology
  raise SpecError, "#{module_id}.#{interface_id}.topology must be a map" unless topology.is_a?(Hash)
  validate_keys!(topology, IPCRAFT_INTERFACE_TOPOLOGY_KEYS, "#{module_id}.#{interface_id}.topology")

  normalized = {}
  normalized['side'] = required_string(topology, 'side', "#{module_id}.#{interface_id}.topology.side") if topology.key?('side')
  if topology.key?('opposite')
    opposite = required_string(topology, 'opposite', "#{module_id}.#{interface_id}.topology.opposite")
    raise SpecError, "#{module_id}.#{interface_id} topology.opposite references unknown interface #{opposite}" unless known_interfaces.include?(opposite)
    normalized['opposite'] = opposite
  end
  normalized['role'] = required_string(topology, 'role', "#{module_id}.#{interface_id}.topology.role") if topology.key?('role')
  normalized
end
```

Wire `display` into module emission after parameters are normalized. Normalize interfaces in two passes so `topology.opposite` can check all interface IDs in the same module.

- [ ] **Step 4: Run specgen tests**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: all specgen tests pass.

- [ ] **Step 5: Commit**

```bash
git add spec_generator/lib/spec_generator.rb spec_generator/test/spec_generator_test.rb
git commit -m "feat: add ipcraft display and topology metadata"
```

## Task 2: Generation Metadata Spike and Schema

**Files:**

- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `ipcores/finepaper-noc/ipcore.yml`
- Modify: `ipcores/opennoc/ipcore.yml`
- Modify: `ipcores/ravenoc/ipcore.yml`

- [ ] **Step 1: Add failing tests for bounded generation metadata**

Add a fixture test:

```ruby
def test_emits_generation_metadata
  Dir.mktmpdir do |dir|
    yaml = ipcraft_package_yaml.sub(
      "commands:\n",
      "generation:\n  engine: ipcraft.common.v1\n  outputs:\n    - id: manifest\n      kind: json\n      path: manifest.json\n  module_mappings:\n    xp: XP\ncommands:\n"
    )
    package_root = write_ipcraft_package_source(dir, yaml: yaml)

    build_ipcraft_manifest(package_root)
    manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))

    assert_equal 'ipcraft.common.v1', manifest.fetch('generation').fetch('engine')
    assert_equal 'manifest.json', manifest.fetch('generation').fetch('outputs').first.fetch('path')
    assert_equal({ 'xp' => 'XP' }, manifest.fetch('generation').fetch('module_mappings'))
  end
end
```

Add rejection tests for unresolved module mappings and path traversal:

```ruby
def test_rejects_generation_module_mapping_for_unknown_module
  Dir.mktmpdir do |dir|
    yaml = ipcraft_package_yaml.sub(
      "commands:\n",
      "generation:\n  engine: ipcraft.common.v1\n  module_mappings:\n    missing_module: XP\ncommands:\n"
    )
    package_root = write_ipcraft_package_source(dir, yaml: yaml)

    error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
    assert_match(/generation.module_mappings references unknown module missing_module/, error.message)
  end
end

def test_rejects_generation_output_path_outside_package
  Dir.mktmpdir do |dir|
    yaml = ipcraft_package_yaml.sub(
      "commands:\n",
      "generation:\n  engine: ipcraft.common.v1\n  outputs:\n    - id: bad\n      kind: json\n      path: ../manifest.json\ncommands:\n"
    )
    package_root = write_ipcraft_package_source(dir, yaml: yaml)

    error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
    assert_match(/generation output bad path escapes package root/, error.message)
  end
end
```

Add a command schema test proving package commands can call a framework-owned tool without escaping the package root:

```ruby
def test_emits_framework_tool_command
  Dir.mktmpdir do |dir|
    yaml = ipcraft_package_yaml.sub(
      "generate:\n      executable: generator/bin/generate\n",
      "generate:\n      framework_tool: ipcraft-generate\n"
    )
    package_root = write_ipcraft_package_source(dir, yaml: yaml)

    build_ipcraft_manifest(package_root)
    manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))
    command = manifest.fetch('commands').fetch('generate')

    assert_equal 'ipcraft-generate', command.fetch('framework_tool')
    refute command.key?('executable')
  end
end
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: failure because `generation` is unknown.

- [ ] **Step 3: Implement the first generation metadata subset**

In `spec_generator/lib/spec_generator.rb`, extend top-level keys:

```ruby
IPCRAFT_PACKAGE_TOP_LEVEL_KEYS = %w[
  schema id name version plugin extensions ipxact parameters connection_classes modules views topologies generation commands
].freeze
IPCRAFT_GENERATION_KEYS = %w[engine outputs templates file_copies vendor_requirements commands module_mappings coordinate_bindings attachment_bindings parameter_projections].freeze
IPCRAFT_GENERATION_OUTPUT_KEYS = %w[id kind path].freeze
IPCRAFT_COMMAND_KEYS = %w[executable framework_tool input_schema args].freeze
```

Add `normalize_generation` that accepts only:

- `engine: ipcraft.common.v1`
- `outputs`: list of `{ id, kind, path }`
- `templates`: map of string to package-local path
- `file_copies`: list of package-local `from` and relative output `to`
- `vendor_requirements`: list of package-local paths
- `commands`: list of external command descriptors with explicit args
- `module_mappings`: map whose keys are known module IDs
- `coordinate_bindings`: map whose values are known parameter names on that module
- `attachment_bindings`: map whose values reference known interfaces
- `parameter_projections`: map whose values reference package or module parameters

Use the existing `validate_package_local_path!` for package-local inputs and a new `validate_relative_output_path!` for generated outputs.

Update `normalize_commands` so each command has exactly one of `executable` or `framework_tool`. Keep `executable` package-local. Accept only `framework_tool: ipcraft-generate` in this phase; emit it unchanged to `ipcraft.json`.

- [ ] **Step 4: Add package metadata spikes**

Add minimal `generation` sections to the three package sources without changing their command declarations yet.

For `ipcores/opennoc/ipcore.yml`, include:

```yaml
generation:
  engine: ipcraft.common.v1
  module_mappings:
    OpenNoCXP: XP
    OpenNoCRNF: RNF
    OpenNoCRNI: RNI
    OpenNoCHNF: HNF
    OpenNoCHNI: HNI
    OpenNoCSNF: SNF
  coordinate_bindings:
    OpenNoCXP: { col: mesh_col, row: mesh_row }
  outputs:
    - id: mesh_json
      kind: json
      path: opennoc_mesh.json
```

For `ipcores/ravenoc/ipcore.yml`, include:

```yaml
generation:
  engine: ipcraft.common.v1
  module_mappings:
    RaveTile: tile
    RaveEndpoint: endpoint
  coordinate_bindings:
    RaveTile: { col: mesh_col, row: mesh_row }
  outputs:
    - id: config_header
      kind: template
      path: ravenoc_config.svh
```

For `ipcores/finepaper-noc/ipcore.yml`, include:

```yaml
generation:
  engine: ipcraft.common.v1
  module_mappings:
    XP: router
    Endpoint: endpoint
  coordinate_bindings:
    XP: { col: mesh_col, row: mesh_row }
  outputs:
    - id: top
      kind: template
      path: top.v
```

- [ ] **Step 5: Run tests and drift check**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
ruby spec_generator/bin/spec-gen --check
```

Expected before manifest regeneration: `spec-gen --check` fails with content mismatches for the three `ipcraft.json` files.

- [ ] **Step 6: Regenerate manifests**

Run:

```bash
ruby spec_generator/bin/spec-gen
ruby spec_generator/bin/spec-gen --check
```

Expected: first command prints `Generated repository ipcraft manifests`; second command prints `Repository ipcraft manifests are up to date`.

- [ ] **Step 7: Commit**

```bash
git add spec_generator/lib/spec_generator.rb spec_generator/test/spec_generator_test.rb ipcores/finepaper-noc/ipcore.yml ipcores/opennoc/ipcore.yml ipcores/ravenoc/ipcore.yml ipcores/finepaper-noc/ipcraft.json ipcores/opennoc/ipcraft.json ipcores/ravenoc/ipcraft.json
git commit -m "feat: add bounded NoC generation metadata"
```

## Task 3: Manifest Reader and Catalog Diagnostics

**Files:**

- Modify: `qt/inc/ipcraft/ipcraftmanifest.h`
- Modify: `qt/src/ipcraft/ipcraftmanifest.cpp`
- Modify: `qt/src/ipcraft/ipcraftmanifestreader.cpp`
- Modify: `qt/src/ipcraft/ipcraftregistry.cpp`
- Modify: `qt/test/ipcraftmanifest_test.cpp`
- Modify: `qt/test/ipcatalogservice_test.cpp`

- [ ] **Step 1: Write failing manifest tests**

Add to `qt/test/ipcraftmanifest_test.cpp`:

```cpp
void testManifestReaderParsesDisplayTopologyAndGeneration() {
    const QJsonObject manifest = minimalManifest();
    QJsonObject module = manifestModule(QStringLiteral("Tile"));
    module.insert(QStringLiteral("display"), QJsonObject{
        {QStringLiteral("label_parameter"), QStringLiteral("display_name")},
        {QStringLiteral("short_label_parameter"), QStringLiteral("short_name")}
    });
    module.insert(QStringLiteral("interfaces"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("link_out")},
            {QStringLiteral("label"), QStringLiteral("Out")},
            {QStringLiteral("modes"), QJsonArray{QStringLiteral("initiator")}},
            {QStringLiteral("accepts"), QJsonArray{accept(QStringLiteral("mesh_link"), QStringLiteral("source"))}},
            {QStringLiteral("topology"), QJsonObject{
                {QStringLiteral("side"), QStringLiteral("east")},
                {QStringLiteral("opposite"), QStringLiteral("link_in")}
            }}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("link_in")},
            {QStringLiteral("label"), QStringLiteral("In")},
            {QStringLiteral("modes"), QJsonArray{QStringLiteral("target")}},
            {QStringLiteral("accepts"), QJsonArray{accept(QStringLiteral("mesh_link"), QStringLiteral("sink"))}}
        }
    });

    QJsonObject document = manifest;
    document.insert(QStringLiteral("modules"), QJsonArray{module});
    document.insert(QStringLiteral("generation"), QJsonObject{
        {QStringLiteral("engine"), QStringLiteral("ipcraft.common.v1")},
        {QStringLiteral("module_mappings"), QJsonObject{{QStringLiteral("Tile"), QStringLiteral("tile")}}}
    });

    const IpcraftManifestReadResult result = readManifest(document);

    require(result.success, "manifest with metadata should parse");
    const IpcraftModuleDescriptor* parsed = result.manifest.module(QStringLiteral("Tile"));
    require(parsed && parsed->displayLabelParameter == QStringLiteral("display_name"),
            "display label binding should parse");
    require(parsed->interfaceDescriptor(QStringLiteral("link_out"))->topologySide == QStringLiteral("east"),
            "topology side should parse");
    require(result.manifest.generation.engine == QStringLiteral("ipcraft.common.v1"),
            "generation engine should parse");
}
```

Add to `qt/test/ipcatalogservice_test.cpp`:

```cpp
void testDuplicatePackageIdsAreDiagnosed() {
    QTemporaryDir first;
    QTemporaryDir second;
    writeMinimalPackage(first.path(), QStringLiteral("org.example.dup"));
    writeMinimalPackage(second.path(), QStringLiteral("org.example.dup"));

    const IpcraftRegistryLoadResult result =
        loadIpcraftPackageManifestsWithDiagnostics({first.path(), second.path()});

    require(result.manifests.empty(), "duplicate package IDs should not silently select one package");
    require(result.diagnostics.size() == 1, "duplicate package ID should produce one diagnostic");
    require(result.diagnostics.first().message.contains(QStringLiteral("Duplicate package id org.example.dup")),
            "diagnostic should name duplicate package id");
}
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
xmake -P qt build ipcraftmanifest_test ipcatalogservice_test
xmake -P qt run ipcraftmanifest_test
xmake -P qt run ipcatalogservice_test
```

Expected: tests fail to compile or fail assertions because the new fields and diagnostics helper do not exist.

- [ ] **Step 3: Extend manifest data types**

Add fields in `qt/inc/ipcraft/ipcraftmanifest.h`:

```cpp
struct IpcraftInterfaceTopology {
    QString side;
    QString oppositeInterfaceId;
    QString role;
};

struct IpcraftGenerationDescriptor {
    QString engine;
    QJsonObject metadata;
};
```

Add to existing descriptors:

```cpp
struct IpcraftInterfaceDescriptor {
    ...
    IpcraftInterfaceTopology topology;
};

struct IpcraftModuleDescriptor {
    ...
    QString displayLabelParameter;
    QString shortLabelParameter;
};

struct IpcraftPackageManifest {
    ...
    IpcraftGenerationDescriptor generation;
};
```

- [ ] **Step 4: Parse fields and duplicate package IDs**

In `qt/src/ipcraft/ipcraftmanifestreader.cpp`, parse `display`, `interfaces[].topology`, and `generation`.

In `qt/src/ipcraft/ipcraftregistry.cpp`, add a result type or helper that returns both manifests and diagnostics:

```cpp
struct IpcraftRegistryLoadResult {
    QVector<IpcraftPackageManifest> manifests;
    QVector<IpcraftDiagnostic> diagnostics;
};
```

Reject duplicate manifest IDs across all configured roots by omitting all packages with the duplicated ID and adding a diagnostic naming each root.

- [ ] **Step 5: Run focused tests**

Run:

```bash
xmake -P qt build ipcraftmanifest_test ipcatalogservice_test
xmake -P qt run ipcraftmanifest_test
xmake -P qt run ipcatalogservice_test
```

Expected: both tests print their `*_test passed` lines.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/ipcraft/ipcraftmanifest.h qt/src/ipcraft/ipcraftmanifest.cpp qt/src/ipcraft/ipcraftmanifestreader.cpp qt/src/ipcraft/ipcraftregistry.cpp qt/test/ipcraftmanifest_test.cpp qt/test/ipcatalogservice_test.cpp qt/xmake.lua
git commit -m "feat: parse ipcraft UX and generation metadata"
```

## Task 4: IP Package Root Management UI

**Files:**

- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Create: `qt/inc/panels/ipcorepathsdialog.h`
- Create: `qt/src/panels/ipcorepathsdialog.cpp`
- Modify: `qt/xmake.lua`
- Modify: `qt/test/ipcatalogpanel_test.cpp`
- Modify: `qt/test/appsettings_test.cpp`

- [ ] **Step 1: Add dialog tests where practical**

Add a non-visual test to `qt/test/appsettings_test.cpp`:

```cpp
void testIpcorePathsDeduplicateAndPersist() {
    QTemporaryDir root;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ipcore_paths_test_org"));
    QCoreApplication::setApplicationName(QStringLiteral("ipcore_paths_test_app"));

    AppSettings settings;
    settings.setIpcorePaths({QStringLiteral("/tmp/a"), QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b")});

    const QStringList paths = settings.ipcorePaths();
    require(paths.size() == 2, "ipcore paths should deduplicate");
    require(paths.at(0).endsWith(QStringLiteral("/tmp/a")), "first path should persist");
    require(paths.at(1).endsWith(QStringLiteral("/tmp/b")), "second path should persist");
}
```

Add a catalog panel or main window seam test that calls a new non-UI reload method if available:

```cpp
void testCatalogReloadUsesConfiguredPackageRoots() {
    QTemporaryDir settingsRoot;
    QTemporaryDir packageRoot;
    writeMinimalPackage(packageRoot.path(), QStringLiteral("org.example.reload"));
    configureSettingsRoot(settingsRoot.path());
    AppSettings().setIpcorePaths({packageRoot.path()});

    IpCatalogService service = IpCatalogService::fromRuntimeRegistries();

    require(service.entry(QStringLiteral("org.example.reload")).has_value(),
            "catalog reload should use configured package roots");
}
```

- [ ] **Step 2: Run focused tests**

Run:

```bash
xmake -P qt build appsettings_test ipcatalogpanel_test
xmake -P qt run appsettings_test
xmake -P qt run ipcatalogpanel_test
```

Expected: existing tests pass; new reload test fails until the reload seam is added.

- [ ] **Step 3: Implement `IpcorePathsDialog`**

Create `qt/inc/panels/ipcorepathsdialog.h`:

```cpp
#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;
class QPushButton;

class IpcorePathsDialog : public QDialog {
    Q_OBJECT
public:
    explicit IpcorePathsDialog(QWidget* parent = nullptr);

    void setPaths(const QStringList& paths);
    QStringList paths() const;
    void setDiagnostics(const QStringList& diagnostics);

private slots:
    void addPath();
    void removeSelectedPath();

private:
    QListWidget* m_paths = nullptr;
    QListWidget* m_diagnostics = nullptr;
    QPushButton* m_removeButton = nullptr;
};
```

Create `qt/src/panels/ipcorepathsdialog.cpp` with a `QVBoxLayout`, a paths list, Add/Remove buttons, a diagnostics list, `QFileDialog::getExistingDirectory`, and deduplication in `setPaths`.

- [ ] **Step 4: Wire menu action and reload**

In `MainWindow::setupActions`, add a Tools menu action:

```cpp
auto* manageIpcorePathsAction = new QAction("IP Core Packages...", this);
manageIpcorePathsAction->setToolTip("Add, remove, and reload IP core package roots.");
connect(manageIpcorePathsAction, &QAction::triggered, this, &MainWindow::manageIpcorePackageRoots);
toolsMenu->addSeparator();
toolsMenu->addAction(manageIpcorePathsAction);
```

Add private methods in `MainWindow`:

```cpp
void manageIpcorePackageRoots();
void reloadIpcoreCatalog();
```

`reloadIpcoreCatalog()` should rebuild `IpCatalogService` from configured roots, reload module registry package data, update `IpCatalogPanel`, append diagnostics to `LogPanel`, and call `rebuildTopologyMenu()`.

- [ ] **Step 5: Run UI-related tests**

Run:

```bash
xmake -P qt build appsettings_test ipcatalogservice_test ipcatalogpanel_test
xmake -P qt run appsettings_test
xmake -P qt run ipcatalogservice_test
xmake -P qt run ipcatalogpanel_test
```

Expected: all focused tests pass.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp qt/inc/panels/ipcorepathsdialog.h qt/src/panels/ipcorepathsdialog.cpp qt/test/appsettings_test.cpp qt/test/ipcatalogpanel_test.cpp qt/xmake.lua
git commit -m "feat: manage IP core package roots in Qt"
```

## Task 5: Display-Oriented Connection Labels

**Files:**

- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/inc/modules/modulelabels.h`
- Modify: `qt/src/modules/moduleregistry.cpp`
- Modify: `qt/test/connectionruleservice_test.cpp`

- [ ] **Step 1: Add failing label tests**

Add to `qt/test/connectionruleservice_test.cpp`:

```cpp
void testConnectionOptionsPreferDisplayNamesAndInterfaceLabels() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registerDisplayNamedTypes(registry);

    Graph graph;
    auto xp = makeDisplayNamedModule(QStringLiteral("uuid_xp"), QStringLiteral("RouterTile"), QStringLiteral("XP A"));
    auto ep = makeDisplayNamedModule(QStringLiteral("uuid_ep"), QStringLiteral("Endpoint"), QStringLiteral("DMA 0"));
    graph.addModule(std::move(xp));
    graph.addModule(std::move(ep));

    ConnectionRuleService service(&graph, {}, {displayNamedManifest()});
    const ConnectionCheckResult result = service.check(ConnectionRequest::portToPort(
        PortRef{QStringLiteral("uuid_ep"), QStringLiteral("noc")},
        PortRef{QStringLiteral("uuid_xp"), QStringLiteral("local")},
        ConnectionRequestKind::Interactive));

    require(result.hasSingleOption(), "display-name test should have one option");
    require(result.options.first().label.contains(QStringLiteral("DMA 0")),
            "option should show endpoint display name");
    require(result.options.first().label.contains(QStringLiteral("XP A")),
            "option should show router display name");
    require(!result.options.first().label.contains(QStringLiteral("uuid_ep")),
            "option main label should not expose endpoint runtime ID");
}
```

Add duplicate label test:

```cpp
void testDuplicateConnectionOptionLabelsUseShortLabelBeforeIds() {
    Graph graph = graphWithTwoDisplayIdenticalEndpoints();
    ConnectionRuleService service(&graph, {}, {displayNamedManifestWithShortLabels()});

    const ConnectionCheckResult result = service.check(nodeToPortRequest());

    require(result.status == ConnectionCheckStatus::NeedsSelection,
            "duplicate visible options should require selection");
    require(result.options.at(0).label.contains(QStringLiteral("slot 0")) ||
            result.options.at(1).label.contains(QStringLiteral("slot 0")),
            "short label should disambiguate duplicate display names");
}
```

- [ ] **Step 2: Run focused test**

Run:

```bash
xmake -P qt build connectionruleservice_test
xmake -P qt run connectionruleservice_test
```

Expected: new tests fail because labels are still built from runtime module IDs and port IDs.

- [ ] **Step 3: Implement display label helper**

In `qt/inc/modules/modulelabels.h`, add:

```cpp
inline QString userFacingName(const Module* module) {
    const ModuleType* type = ModuleTypeMetadata::type(module);
    const QString binding = type ? type->displayLabelParameter : QString();
    if (!binding.isEmpty()) {
        const QString value = stringParameter(module, binding, {});
        if (!value.trimmed().isEmpty()) return value.trimmed();
    }
    return displayName(module);
}
```

Add `shortDisambiguator(const Module*)` using manifest short label binding when available.

In `ConnectionRuleService::buildOptions`, replace:

```cpp
option.label = QStringLiteral("%1.%2 -> %3.%4").arg(...);
```

with a helper that formats:

```text
<source display>.<source interface label> -> <target display>.<target interface label>
```

Append a secondary ` [<short>]` suffix only when duplicate labels exist.

- [ ] **Step 4: Run label tests**

Run:

```bash
xmake -P qt build connectionruleservice_test
xmake -P qt run connectionruleservice_test
```

Expected: `connectionruleservice_test passed`.

- [ ] **Step 5: Commit**

```bash
git add qt/src/connection/connectionruleservice.cpp qt/inc/modules/modulelabels.h qt/src/modules/moduleregistry.cpp qt/test/connectionruleservice_test.cpp
git commit -m "fix: show display labels in connection choices"
```

## Task 6: Metadata-Driven Topology and Port Layout

**Files:**

- Modify: `qt/inc/common/portlayout.h`
- Modify: `qt/src/topology/topologypresetbuilder.cpp`
- Modify: `qt/src/modules/moduleregistry.cpp`
- Modify: `qt/test/topology_preset_test.cpp`

- [ ] **Step 1: Add failing topology binding test**

Add to `qt/test/topology_preset_test.cpp`:

```cpp
void testMeshPresetUsesManifestSideMetadataWithoutDirectionalNames() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const IpcraftPackageManifest manifest = manifestSideMetadataPackage(
        QStringLiteral("org.example.side_metadata"),
        QStringLiteral("Tile"));
    require(registry.loadIpcraftPackages({manifest}), "side metadata package should load");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("org.example.side_metadata");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = manifestTopologyPreset(manifest, QStringLiteral("mesh"));
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.connections().size() == 1, "metadata mesh should create one east-west link");
    const Connection* connection = graph.connections().front().get();
    require(connection->source().portId == QStringLiteral("mesh_out"),
            "source port should come from topology side metadata");
    require(connection->target().portId == QStringLiteral("mesh_in"),
            "target port should come from topology opposite metadata");
}
```

The test package should use interface IDs `mesh_out` and `mesh_in`, not `east` and `west`.

- [ ] **Step 2: Run topology test**

Run:

```bash
xmake -P qt build topology_preset_test
xmake -P qt run topology_preset_test
```

Expected: failure until `TopologyPresetBuilder` can resolve topology sides from metadata instead of preset port names alone.

- [ ] **Step 3: Propagate topology metadata to `ModuleType`**

Add fields to `ModuleInterfaceMetadata` if they are not already present:

```cpp
QString topologySide;
QString oppositeInterfaceId;
QString topologyRole;
```

In `ModuleRegistry::loadIpcraftPackages`, copy manifest interface topology fields into `ModuleInterfaceMetadata`.

- [ ] **Step 4: Update `PortLayout` helper paths**

Add overloads that use metadata:

```cpp
inline QString semanticSide(const Port& port, const ModuleInterfaceMetadata* metadata) {
    if (metadata && !metadata->topologySide.isEmpty()) return metadata->topologySide;
    return hintedSide(port);
}

inline QString oppositeSide(const QString& side, const ModuleInterfaceMetadata* metadata) {
    if (metadata && !metadata->oppositeInterfaceId.isEmpty()) return metadata->oppositeInterfaceId;
    return oppositeRouterSide(side);
}
```

Keep existing helpers as legacy fallback.

- [ ] **Step 5: Update topology preset builder**

Change mesh link resolution:

- Prefer `request.preset.ports` explicit mapping when present.
- If a direction key is missing, resolve the interface with matching `topologySide`.
- Resolve the target interface through `oppositeInterfaceId` when present.
- Fall back to legacy `north/east/south/west` only when metadata is absent.

- [ ] **Step 6: Run topology tests**

Run:

```bash
xmake -P qt build topology_preset_test connectionruleservice_test
xmake -P qt run topology_preset_test
xmake -P qt run connectionruleservice_test
```

Expected: both tests pass.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/common/portlayout.h qt/src/topology/topologypresetbuilder.cpp qt/src/modules/moduleregistry.cpp qt/test/topology_preset_test.cpp
git commit -m "feat: drive topology presets from manifest metadata"
```

## Task 7: Command Input Export Boundary

**Files:**

- Modify: `qt/src/ipcore/ipcoregraphexporter.cpp`
- Modify: `qt/test/ipcoregraphexporter_test.cpp`
- Modify: `qt/test/projectgenerationrunner_test.cpp`

- [ ] **Step 1: Add failing export tests**

Add to `qt/test/ipcoregraphexporter_test.cpp`:

```cpp
void testIpcraftExportUsesManifestModuleIdsAndDisplayParameters() {
    Graph graph;
    addPackageScopedModule(graph,
                           QStringLiteral("runtime_uuid"),
                           QStringLiteral("org.example.noc::Tile"),
                           QStringLiteral("org.example.noc"),
                           QStringLiteral("noc_0"),
                           QJsonObject{{QStringLiteral("display_name"), QStringLiteral("Tile A")}});

    IpCoreGraphExportRequest request = ipcraftExportRequest(graph, QStringLiteral("org.example.noc"));
    const IpCoreGraphExportResult result = IpCoreGraphExporter::exportGraph(request);

    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject root = result.document.object();
    const QJsonObject instance = root.value(QStringLiteral("instances")).toArray().first().toObject();
    require(instance.value(QStringLiteral("module")).toString() == QStringLiteral("Tile"),
            "export should use manifest module id");
    require(instance.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("display_name")).toString() == QStringLiteral("Tile A"),
            "export should include dynamic display parameter");
}
```

Add a filename/log wording test to `projectgenerationrunner_test` ensuring generated command input paths contain `command-input` or `ipcraft-input`, not `.fpproj`.

- [ ] **Step 2: Run export tests**

Run:

```bash
xmake -P qt build ipcoregraphexporter_test projectgenerationrunner_test
xmake -P qt run ipcoregraphexporter_test
xmake -P qt run projectgenerationrunner_test
```

Expected: failures identify missing export fields or confusing command input naming.

- [ ] **Step 3: Harden exporter**

Ensure `IpCoreGraphExporter::exportGraph`:

- always emits `schema: ipcraft.noc.project.v1` for commands declaring that schema;
- uses manifest module IDs through `ModuleTypeMetadata::moduleId`;
- includes all module parameters, including display label parameters;
- includes normalized connection `interfaces`, `class`, `status`, and `alternatives`;
- never reads or serializes `.fpproj` directly.

- [ ] **Step 4: Run tests**

Run:

```bash
xmake -P qt build ipcoregraphexporter_test projectgenerationrunner_test
xmake -P qt run ipcoregraphexporter_test
xmake -P qt run projectgenerationrunner_test
```

Expected: both tests pass.

- [ ] **Step 5: Commit**

```bash
git add qt/src/ipcore/ipcoregraphexporter.cpp qt/test/ipcoregraphexporter_test.cpp qt/test/projectgenerationrunner_test.cpp
git commit -m "fix: keep command input export distinct from project files"
```

## Task 8: Common Generator CLI Skeleton

**Files:**

- Create: `ipcraft_generator/bin/ipcraft-generate`
- Create: `ipcraft_generator/lib/ipcraft_generator.rb`
- Create: `ipcraft_generator/test/ipcraft_generator_test.rb`

- [ ] **Step 1: Add failing CLI tests**

Create `ipcraft_generator/test/ipcraft_generator_test.rb`:

```ruby
$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'ipcraft_generator'

class IpcraftGeneratorTest < Minitest::Test
  ROOT = File.expand_path('..', __dir__)
  CLI = File.join(ROOT, 'bin/ipcraft-generate')

  def test_cli_requires_manifest_input_and_output
    stdout, stderr, status = Open3.capture3(RbConfig.ruby, CLI)

    refute status.success?
    assert_empty stdout
    assert_includes stderr, 'error: --manifest is required'
  end

  def test_loads_manifest_project_and_writes_output_manifest
    Dir.mktmpdir do |dir|
      manifest_path = File.join(dir, 'ipcraft.json')
      input_path = File.join(dir, 'input.json')
      output = File.join(dir, 'out')
      File.write(manifest_path, JSON.pretty_generate(minimal_manifest))
      File.write(input_path, JSON.pretty_generate(minimal_project))

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby, CLI,
        '--manifest', manifest_path,
        '--input', input_path,
        '--output', output
      )

      assert status.success?, stderr
      assert_includes stdout, "Generated ipcraft output in #{output}"
      assert File.file?(File.join(output, 'manifest.json'))
    end
  end

  private

  def minimal_manifest
    {
      'schema' => 'ipcraft.manifest.v1',
      'id' => 'org.example.noc',
      'name' => 'Example',
      'generation' => {
        'engine' => 'ipcraft.common.v1',
        'outputs' => [{ 'id' => 'manifest', 'kind' => 'json', 'path' => 'manifest.json' }]
      }
    }
  end

  def minimal_project
    {
      'schema' => 'ipcraft.noc.project.v1',
      'package' => 'org.example.noc',
      'instances' => [],
      'connections' => []
    }
  end
end
```

- [ ] **Step 2: Run generator test**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: failure because files do not exist.

- [ ] **Step 3: Implement CLI skeleton**

Create `ipcraft_generator/bin/ipcraft-generate`:

```ruby
#!/usr/bin/env ruby

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'ipcraft_generator'

begin
  IpcraftGenerator::CLI.run(ARGV)
rescue IpcraftGenerator::Error => error
  warn "error: #{error.message}"
  exit 1
end
```

Create `ipcraft_generator/lib/ipcraft_generator.rb` with:

```ruby
require 'fileutils'
require 'json'
require 'optparse'

module IpcraftGenerator
  class Error < StandardError; end

  class CLI
    def self.run(argv)
      options = {}
      OptionParser.new do |parser|
        parser.on('--manifest PATH') { |value| options[:manifest] = value }
        parser.on('--input PATH') { |value| options[:input] = value }
        parser.on('--output DIR') { |value| options[:output] = value }
      end.parse!(argv)

      raise Error, '--manifest is required' unless options[:manifest]
      raise Error, '--input is required' unless options[:input]
      raise Error, '--output is required' unless options[:output]

      Generator.new(options).generate
      puts "Generated ipcraft output in #{options.fetch(:output)}"
    end
  end

  class Generator
    def initialize(manifest:, input:, output:)
      @manifest_path = manifest
      @input_path = input
      @output_dir = output
    end

    def generate
      manifest = JSON.parse(File.read(@manifest_path))
      input = JSON.parse(File.read(@input_path))
      validate!(manifest, input)
      FileUtils.mkdir_p(@output_dir)
      File.write(File.join(@output_dir, 'manifest.json'), JSON.pretty_generate({
        ipcore: manifest.fetch('id'),
        schema: input.fetch('schema'),
        instance_count: input.fetch('instances', []).size,
        connection_count: input.fetch('connections', []).size
      }) + "\n")
    end

    private

    def validate!(manifest, input)
      raise Error, 'manifest schema must be ipcraft.manifest.v1' unless manifest['schema'] == 'ipcraft.manifest.v1'
      raise Error, 'input schema must be ipcraft.noc.project.v1' unless input['schema'] == 'ipcraft.noc.project.v1'
      raise Error, 'input package does not match manifest id' unless input['package'] == manifest['id']
    end
  end
end
```

- [ ] **Step 4: Run generator test**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: test passes.

- [ ] **Step 5: Commit**

```bash
git add ipcraft_generator/bin/ipcraft-generate ipcraft_generator/lib/ipcraft_generator.rb ipcraft_generator/test/ipcraft_generator_test.rb
git commit -m "feat: add common ipcraft generator CLI"
```

## Task 9: Finepaper NoC Common Generator Parity

**Files:**

- Modify: `ipcraft_generator/lib/ipcraft_generator.rb`
- Modify: `ipcraft_generator/test/ipcraft_generator_test.rb`

- [ ] **Step 1: Add parity test for finepaper-noc**

Add test:

```ruby
def test_generates_finepaper_noc_structural_outputs
  Dir.mktmpdir do |dir|
    input = File.join(dir, 'input.json')
    output = File.join(dir, 'out')
    File.write(input, JSON.pretty_generate(finepaper_noc_project))

    IpcraftGenerator::Generator.new(
      manifest: repo_path('ipcores/finepaper-noc/ipcraft.json'),
      input: input,
      output: output
    ).generate

    assert File.file?(File.join(output, 'manifest.json'))
    assert File.file?(File.join(output, 'filelist.f'))
    assert File.file?(File.join(output, 'rtl/top.v'))
    manifest = JSON.parse(File.read(File.join(output, 'manifest.json')))
    assert_equal 'finepaper.noc', manifest.fetch('ipcore')
    assert_equal 4, manifest.fetch('routers')
  end
end
```

- [ ] **Step 2: Run test and confirm failure**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: failure because only generic `manifest.json` is written.

- [ ] **Step 3: Implement finepaper NoC output mode**

Implement a small generation dispatcher based on `manifest['id']` or `generation.module_mappings`:

- collect router instances mapped to `XP`;
- collect endpoint instances mapped to `Endpoint`;
- write `rtl/top.v` from a minimal ERB template or existing template rendering;
- write `filelist.f`;
- write `manifest.json` with router and endpoint counts.

Do not remove `ipcores/finepaper-noc/generator` yet.

- [ ] **Step 4: Run parity test**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: generator tests pass.

- [ ] **Step 5: Commit**

```bash
git add ipcraft_generator/lib/ipcraft_generator.rb ipcraft_generator/test/ipcraft_generator_test.rb
git commit -m "feat: generate finepaper NoC through common generator"
```

## Task 10: OpenNoC Common Generator Parity

**Files:**

- Modify: `ipcraft_generator/lib/ipcraft_generator.rb`
- Modify: `ipcraft_generator/test/ipcraft_generator_test.rb`

- [ ] **Step 1: Add OpenNoC projection parity test**

Add test:

```ruby
def test_generates_opennoc_mesh_projection_without_vendor
  Dir.mktmpdir do |dir|
    input = File.join(dir, 'input.json')
    output = File.join(dir, 'out')
    File.write(input, JSON.pretty_generate(opennoc_2x2_project))

    IpcraftGenerator::Generator.new(
      manifest: repo_path('ipcores/opennoc/ipcraft.json'),
      input: input,
      output: output
    ).generate

    mesh = JSON.parse(File.read(File.join(output, 'opennoc_mesh.json')))
    assert_equal({ 'X' => 0, 'Y' => 0, 'P0' => 'RNF', 'P1' => 'RNI' }, mesh.fetch('XP0_0'))
    assert_equal({ 'X' => 1, 'Y' => 0, 'P0' => 'HNF', 'P1' => 'NONE' }, mesh.fetch('XP1_0'))
    manifest = JSON.parse(File.read(File.join(output, 'manifest.json')))
    assert_equal 'finepaper.opennoc', manifest.fetch('ipcore')
    assert_equal 2, manifest.fetch('rows')
    assert_equal 2, manifest.fetch('cols')
  end
end
```

- [ ] **Step 2: Run test and confirm failure**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: OpenNoC projection test fails.

- [ ] **Step 3: Implement OpenNoC projection**

Use manifest `generation.module_mappings` to map:

```ruby
{
  'OpenNoCRNF' => 'RNF',
  'OpenNoCRNI' => 'RNI',
  'OpenNoCHNF' => 'HNF',
  'OpenNoCHNI' => 'HNI',
  'OpenNoCSNF' => 'SNF'
}
```

Collect XP coordinates from `mesh_col` and `mesh_row`; validate rectangular mesh; project agent connections to `P0` and `P1`; write `opennoc_mesh.json` and `manifest.json`. If vendor requirements are present and missing, emit an actionable error only when the selected output mode requires invoking vendor tools.

- [ ] **Step 4: Run OpenNoC generator tests**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: all common generator tests pass.

- [ ] **Step 5: Commit**

```bash
git add ipcraft_generator/lib/ipcraft_generator.rb ipcraft_generator/test/ipcraft_generator_test.rb
git commit -m "feat: project OpenNoC meshes in common generator"
```

## Task 11: RaveNoC Common Generator Parity

**Files:**

- Modify: `ipcraft_generator/lib/ipcraft_generator.rb`
- Modify: `ipcraft_generator/test/ipcraft_generator_test.rb`

- [ ] **Step 1: Add RaveNoC projection parity test**

Add test:

```ruby
def test_generates_ravenoc_config_and_manifest
  Dir.mktmpdir do |dir|
    input = File.join(dir, 'input.json')
    output = File.join(dir, 'out')
    File.write(input, JSON.pretty_generate(ravenoc_2x2_project))

    IpcraftGenerator::Generator.new(
      manifest: repo_path('ipcores/ravenoc/ipcraft.json'),
      input: input,
      output: output
    ).generate

    config = File.read(File.join(output, 'ravenoc_config.svh'))
    assert_includes config, '`define NOC_CFG_SZ_ROWS 2'
    assert_includes config, '`define NOC_CFG_SZ_COLS 2'
    filelist = File.read(File.join(output, 'ravenoc_filelist.f'))
    assert_includes filelist, 'ravenoc_top.sv'
    manifest = JSON.parse(File.read(File.join(output, 'manifest.json')))
    assert_equal 'finepaper.ravenoc', manifest.fetch('ipcore')
    assert_equal 4, manifest.fetch('tiles')
  end
end
```

- [ ] **Step 2: Run test and confirm failure**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: RaveNoC test fails.

- [ ] **Step 3: Implement RaveNoC projection**

Use project instance parameters for RaveNoC macro values. Compute rows/cols from `RaveTile` coordinates. Write:

- `ravenoc_config.svh`
- `ravenoc_filelist.f`
- `manifest.json`

Copy or render minimal wrapper artifacts only where parity tests require them.

- [ ] **Step 4: Run generator tests**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: all common generator tests pass.

- [ ] **Step 5: Commit**

```bash
git add ipcraft_generator/lib/ipcraft_generator.rb ipcraft_generator/test/ipcraft_generator_test.rb
git commit -m "feat: project RaveNoC config in common generator"
```

## Task 12: Switch Package Commands to Common Generator

**Files:**

- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `qt/inc/app/projectgenerationrunner.h`
- Modify: `qt/src/app/projectgenerationrunner.cpp`
- Modify: `qt/test/projectgenerationrunner_test.cpp`
- Modify: `ipcores/finepaper-noc/ipcore.yml`
- Modify: `ipcores/opennoc/ipcore.yml`
- Modify: `ipcores/ravenoc/ipcore.yml`
- Regenerate: `ipcores/*/ipcraft.json`
- Modify: package generator tests only where they assert command paths.

- [ ] **Step 1: Add failing framework-tool runner test**

Add to `qt/test/projectgenerationrunner_test.cpp`:

```cpp
void testGenerationRunnerResolvesFrameworkToolCommand() {
    QTemporaryDir workspace;
    QTemporaryDir toolRoot;
    const QString toolPath = toolRoot.filePath(QStringLiteral("ipcraft-generate"));
    writeExecutableScript(toolPath, QStringLiteral(
        "#!/usr/bin/env ruby\n"
        "require 'fileutils'\n"
        "out = ARGV[ARGV.index('--output') + 1]\n"
        "FileUtils.mkdir_p(out)\n"
        "File.write(File.join(out, 'manifest.json'), \"{}\\n\")\n"
    ));

    ProjectGenerationRunner runner;
    runner.setFrameworkToolSearchPaths({toolRoot.path()});
    ProjectGenerationRequest request = minimalGenerationRequest(workspace.path());
    request.command.frameworkTool = QStringLiteral("ipcraft-generate");
    request.command.inputSchema = QStringLiteral("ipcraft.noc.project.v1");

    const ProjectGenerationResult result = runner.run(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(QFile::exists(workspace.filePath(QStringLiteral("out/manifest.json"))),
            "framework tool should run and write output");
}
```

Run:

```bash
xmake -P qt build projectgenerationrunner_test
xmake -P qt run projectgenerationrunner_test
```

Expected: test fails because `ProjectGenerationRunner` only resolves package-local executable commands.

- [ ] **Step 2: Implement framework-tool resolution**

Add a framework tool field to the command descriptor parsed from `ipcraft.json`:

```cpp
struct IpcraftCommandDescriptor {
    QString executable;
    QString frameworkTool;
    QString inputSchema;
    QStringList args;
};
```

In `ProjectGenerationRunner`, resolve commands with this rule:

```cpp
QString ProjectGenerationRunner::resolveProgram(const ProjectGenerationRequest& request) const {
    if (!request.command.frameworkTool.isEmpty()) {
        for (const QString& root : m_frameworkToolSearchPaths) {
            const QString candidate = QDir(root).filePath(request.command.frameworkTool);
            if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isExecutable()) return candidate;
        }
        return {};
    }
    return QDir(request.packageRoot).filePath(request.command.executable);
}
```

The default search paths should include the development repo path `ipcraft_generator/bin` when running from the source tree and the installed tools path when available. If no matching framework tool is found, return an error naming the missing tool and the searched paths.

- [ ] **Step 3: Update command declarations**

Change each package `commands.generate` executable to:

```yaml
generate:
  framework_tool: ipcraft-generate
  input_schema: ipcraft.noc.project.v1
  args: ["--manifest", "{manifest}", "--input", "{input}", "--output", "{output}"]
```

Keep `commands.validate` on existing package-local DRC until a common validator is specified.

- [ ] **Step 4: Regenerate manifests**

Run:

```bash
ruby spec_generator/bin/spec-gen
ruby spec_generator/bin/spec-gen --check
```

Expected: manifests regenerate and drift check passes.

- [ ] **Step 5: Run generator and runner tests**

Run:

```bash
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
xmake -P qt build projectgenerationrunner_test
xmake -P qt run projectgenerationrunner_test
```

Expected: Ruby generator/specgen tests pass and `projectgenerationrunner_test passed`.

- [ ] **Step 6: Run package-local tests as references**

Run:

```bash
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/opennoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
```

Expected: package-local tests still pass or fail only on assertions that are intentionally updated to recognize the common command path.

- [ ] **Step 7: Commit**

```bash
git add spec_generator/lib/spec_generator.rb spec_generator/test/spec_generator_test.rb qt/inc/app/projectgenerationrunner.h qt/src/app/projectgenerationrunner.cpp qt/test/projectgenerationrunner_test.cpp ipcores/finepaper-noc/ipcore.yml ipcores/opennoc/ipcore.yml ipcores/ravenoc/ipcore.yml ipcores/finepaper-noc/ipcraft.json ipcores/opennoc/ipcraft.json ipcores/ravenoc/ipcraft.json ipcores/*/generator/test/test_generator.rb
git commit -m "feat: route NoC generation through common generator"
```

## Task 13: Final Verification and Documentation Handoff

**Files:**

- Modify only if needed: `spec_generator/README.md`
- Modify only if needed: `docs/superpowers/specs/2026-05-18-ipcraft-noc-specgen-hardening-design.md`

- [ ] **Step 1: Run full focused verification**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
ruby spec_generator/bin/spec-gen --check
xmake -P qt build appsettings_test ipcatalogservice_test ipcraftmanifest_test connectionruleservice_test topology_preset_test ipcoregraphexporter_test projectgenerationrunner_test
xmake -P qt run appsettings_test
xmake -P qt run ipcatalogservice_test
xmake -P qt run ipcraftmanifest_test
xmake -P qt run connectionruleservice_test
xmake -P qt run topology_preset_test
xmake -P qt run ipcoregraphexporter_test
xmake -P qt run projectgenerationrunner_test
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/opennoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
```

Expected: all listed commands exit 0. If a vendor-backed smoke test is unavailable because vendor files are absent, record the skip output and do not claim vendor-backed verification.

- [ ] **Step 2: Inspect for excluded acceptance material**

Run:

```bash
rg -n "C[M]N|T[B]D|TO[D]O|FIXM[E]|reci[p]e|project[.]json|backend plugi[n]|implement late[r]|fill in detail[s]|appropriate error handlin[g]|similar to Tas[k]" docs/superpowers/specs/2026-05-18-ipcraft-noc-specgen-hardening-design.md docs/superpowers/plans/2026-05-18-ipcraft-noc-specgen-hardening.md spec_generator ipcraft_generator qt ipcores
```

Expected: no forbidden terms in the design/plan docs. Code may contain unrelated legacy terms only if they are pre-existing or part of compatibility tests; review any hits before finalizing.

- [ ] **Step 3: Summarize implementation state**

Prepare a concise handoff noting:

- package roots can be configured and reloaded from Qt;
- connection choices use display labels;
- topology metadata is manifest-driven for the tested paths;
- `ipcraft.noc.project.v1` remains command input, not saved project format;
- common generator parity coverage exists for the three current packages;
- independent function/support/test list is intentionally deferred until after this implementation.

- [ ] **Step 4: Commit final documentation updates if any**

If README or spec wording changed:

```bash
git add spec_generator/README.md docs/superpowers/specs/2026-05-18-ipcraft-noc-specgen-hardening-design.md
git commit -m "docs: update NoC specgen hardening handoff"
```

If no documentation changed, do not create an empty commit.

## Self-Review Checklist

- Spec coverage: tasks cover manifest source-of-truth, UX package roots, display labels, frontend hardcoding cleanup, command input boundary, common generator migration, parity tests, and deferred third-party acceptance material.
- Scope control: this plan does not implement a new external NoC IP and does not write the independent acceptance checklist.
- TDD: each behavior task starts with focused failing tests and has an expected failing command.
- Drift control: `ipcraft.json` remains committed and regenerated by `specgen`.
- Compatibility: package-local generators remain until common generator parity tests exist.
- Terminology: this plan uses `generation metadata` and `command input`; it avoids ambiguous generator ownership wording.
