# RaveNoC Plugin Adaptation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add RaveNoC as a standalone Finepaper IP plugin that can be configured in the UI, generated through a generic plugin graph export, and syntax-checked with Verilator.

**Architecture:** Keep RaveNoC outside core logic. Add a generic generator input flavor to Finepaper, keep the existing legacy NoC export as the default for `finepaper.noc`, and let `finepaper.ravenoc` consume a plugin-owned generic graph JSON. Implement the RaveNoC generator with Ruby + ERB to match the existing Finepaper generator style; RaveNoC upstream provides RTL/FuseSoC metadata, not a Finepaper-specific generator.

**Tech Stack:** C++23/Qt for editor export and plugin metadata, XML plugin bundles, Ruby/ERB for the RaveNoC generator, git submodule for upstream RTL, Verilator for smoke verification.

---

## File Structure

- Modify `qt/inc/graph/graph.h`: add a generic plugin graph JSON flavor.
- Modify `qt/src/graph/graph.cpp`: implement generic module/connection serialization without XP/Endpoint assumptions.
- Modify `qt/test/graph_test.cpp`: add tests proving non-XP plugin modules survive generic export.
- Modify `qt/inc/plugins/plugindescriptor.h`: expose generator input format in descriptors and resolved commands.
- Modify `qt/src/plugins/pluginregistry.cpp`: parse `generator.input_format`.
- Modify `qt/inc/plugins/generatorrunner.h`: carry `inputFormat` to callers.
- Modify `qt/src/plugins/generatorrunner.cpp`: propagate plugin generator input format.
- Modify `qt/src/app/mainwindow.cpp`: write generic JSON when the selected plugin requests it.
- Modify `qt/test/plugin_test.cpp`: cover manifest parsing and generator resolution for `generic_graph_v1`.
- Create `plugins/ravenoc/plugin.json`: plugin manifest.
- Create `plugins/ravenoc/modules.xml`: `RaveNoC` module metadata and parameters.
- Create `plugins/ravenoc/graphics/RaveNoC.xml`: simple plugin-owned node visuals.
- Create `plugins/ravenoc/generator/bin/generate`: Ruby entrypoint.
- Create `plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb`: Ruby generator implementation.
- Create `plugins/ravenoc/generator/template/ravenoc_config.svh.erb`: config macro template.
- Create `plugins/ravenoc/generator/template/ravenoc_demo_top.sv.erb`: wrapper template.
- Create `plugins/ravenoc/generator/template/ravenoc_filelist.f.erb`: Verilator filelist template.
- Create `plugins/ravenoc/generator/template/verify.sh.erb`: smoke script template.
- Create `plugins/ravenoc/generator/test/test_generator.rb`: generator unit tests.
- Add submodule `plugins/ravenoc/vendor/ravenoc`.
- Create or update `.gitmodules`: submodule metadata created by `git submodule add`.

## Task 1: Generic Plugin Graph Export

**Files:**
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Write the failing test**

Append this test to `qt/test/graph_test.cpp`, near the JSON export tests:

```cpp
void testGenericPluginExportKeepsNonNocModules() {
    ModuleType accelType;
    accelType.name = QStringLiteral("GenericAccel");
    accelType.pluginId = QStringLiteral("finepaper.generic");
    ModuleRegistry::instance().registerType(accelType);

    Graph graph;
    auto module = makeModule(
        "accel_internal",
        "GenericAccel",
        {
            Port("cfg", Port::Direction::Input, "axi_lite", "CFG", {}, "control", "axi_lite"),
            Port("irq", Port::Direction::Output, "interrupt", "IRQ", {}, "status", "interrupt")
        });
    module->setParameter("display_name", QString("Generic Accel"));
    module->setParameter("x", 15);
    module->setParameter("y", 25);
    module->setParameter("width", 64);

    require(graph.addModule(std::move(module)), "failed to add generic module");

    const QJsonObject root =
        graph.toJsonDocument("generic_design", GraphJsonFlavor::Plugin).object();

    require(root["schema"].toString() == "finepaper-plugin-graph-v1",
            "generic plugin export should identify its schema");
    require(root["name"].toString() == "generic_design",
            "generic plugin export should include the design name");

    const QJsonArray modules = root["modules"].toArray();
    require(modules.size() == 1, "generic plugin export should include non-NoC module");
    const QJsonObject exportedModule = modules.first().toObject();
    require(exportedModule["id"].toString() == "accel_internal",
            "generic plugin export should preserve internal module id");
    require(exportedModule["plugin"].toString() == "finepaper.generic",
            "generic plugin export should include plugin owner");
    require(exportedModule["type"].toString() == "GenericAccel",
            "generic plugin export should include module type");
    require(exportedModule["parameters"].toObject()["width"].toInt() == 64,
            "generic plugin export should include module parameters");

    const QJsonArray ports = exportedModule["ports"].toArray();
    require(ports.size() == 2, "generic plugin export should include ports");
    require(ports.first().toObject()["id"].toString() == "cfg",
            "generic plugin export should include port ids");
}
```

Add the call in `main()` before `testFrameworkExportOmitsEditorOnlyCollapsedField();`:

```cpp
testGenericPluginExportKeepsNonNocModules();
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
xmake build graph_test
xmake run graph_test
```

Expected: build fails because `GraphJsonFlavor::Plugin` is not defined.

- [ ] **Step 3: Add the enum value**

Modify `qt/inc/graph/graph.h`:

```cpp
enum class GraphJsonFlavor {
    Editor,
    Framework,
    Plugin
};
```

- [ ] **Step 4: Implement generic export**

In `qt/src/graph/graph.cpp`, add these helpers near `parameterToJson()`:

```cpp
QString directionToJsonString(Port::Direction direction) {
    if (direction == Port::Direction::Input) return QStringLiteral("input");
    if (direction == Port::Direction::Output) return QStringLiteral("output");
    return QStringLiteral("inout");
}

QJsonObject portToGenericJson(const Port& port) {
    QJsonObject object;
    object["id"] = port.id();
    object["direction"] = directionToJsonString(port.direction());
    object["type"] = port.type();
    object["name"] = port.name();
    if (!port.description().isEmpty()) object["description"] = port.description();
    if (!port.role().isEmpty()) object["role"] = port.role();
    if (!port.busType().isEmpty()) object["bus_type"] = port.busType();
    if (!port.interfaceId().isEmpty()) object["interface"] = port.interfaceId();
    return object;
}

QJsonObject parametersToGenericJson(const Module* module) {
    QJsonObject parameters;
    if (!module) return parameters;
    for (auto it = module->parameters().constBegin(); it != module->parameters().constEnd(); ++it) {
        parameters.insert(it.key(), parameterToJson(it.value().value()));
    }
    return parameters;
}
```

At the start of `Graph::toJsonDocument()`, after clearing `externalToInternalIds`, add:

```cpp
    if (flavor == GraphJsonFlavor::Plugin) {
        QJsonArray modules;
        QJsonArray connections;

        for (const auto& module : m_modules) {
            const ModuleType* type = ModuleRegistry::instance().getType(module->type());

            QJsonObject object;
            object["id"] = module->id();
            object["plugin"] = type ? type->pluginId : QString();
            object["type"] = module->type();
            object["parameters"] = parametersToGenericJson(module.get());

            QJsonArray ports;
            for (const Port& port : module->ports()) {
                ports.append(portToGenericJson(port));
            }
            object["ports"] = ports;
            modules.append(object);
        }

        for (const auto& connection : m_connections) {
            QJsonObject object;
            object["id"] = connection->id();
            object["source"] = QJsonObject{
                {QStringLiteral("module"), connection->source().moduleId},
                {QStringLiteral("port"), connection->source().portId}
            };
            object["target"] = QJsonObject{
                {QStringLiteral("module"), connection->target().moduleId},
                {QStringLiteral("port"), connection->target().portId}
            };
            connections.append(object);
        }

        QJsonObject root;
        root["schema"] = QStringLiteral("finepaper-plugin-graph-v1");
        root["name"] = designName.isEmpty() ? QStringLiteral("design") : designName;
        root["modules"] = modules;
        root["connections"] = connections;
        return QJsonDocument(root);
    }
```

- [ ] **Step 5: Run GREEN verification**

Run:

```bash
xmake build graph_test
xmake run graph_test
```

Expected: `graph_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/graph/graph.h qt/src/graph/graph.cpp qt/test/graph_test.cpp
git commit -m "feat(qt): export generic plugin graph json"
```

## Task 2: Generator Input Format Metadata

**Files:**
- Modify: `qt/inc/plugins/plugindescriptor.h`
- Modify: `qt/src/plugins/pluginregistry.cpp`
- Modify: `qt/inc/plugins/generatorrunner.h`
- Modify: `qt/src/plugins/generatorrunner.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing plugin metadata tests**

In `qt/test/plugin_test.cpp`, update the manifest in `testPluginManifestLoadsRelativePaths()` so the generator contains input format:

```json
"generator": {
  "command": "ruby",
  "input_format": "generic_graph_v1",
  "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}"]
}
```

Add this assertion after `require(plugins.first().generator.hasGenerator(), "generator should be retained");`:

```cpp
require(plugins.first().generator.inputFormat == QStringLiteral("generic_graph_v1"),
        "generator input format should load");
```

Add a new test:

```cpp
void testGeneratorRunnerPropagatesInputFormat() {
    Graph graph;

    ModuleType type;
    type.name = QStringLiteral("FormatIp");
    type.pluginId = QStringLiteral("finepaper.format");
    ModuleRegistry::instance().registerType(type);

    auto module = std::make_unique<Module>(QStringLiteral("format_node"), QStringLiteral("FormatIp"));
    require(graph.addModule(std::move(module)), "failed to add format module");

    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.format");
    plugin.rootPath = QStringLiteral("/tmp/finepaper-format-plugin");
    plugin.generator.command = QStringLiteral("ruby");
    plugin.generator.inputFormat = QStringLiteral("generic_graph_v1");
    plugin.generator.args = {QStringLiteral("generator/bin/generate")};

    const GeneratorCommand command =
        GeneratorRunner::resolveForGraph(&graph, {plugin}, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(command.valid, "generator command should resolve");
    require(command.inputFormat == QStringLiteral("generic_graph_v1"),
            "resolved command should carry input format");
}
```

Add the call in `main()` before diagnostics tests:

```cpp
testGeneratorRunnerPropagatesInputFormat();
```

- [ ] **Step 2: Run RED**

Run:

```bash
xmake build plugin_test
```

Expected: build fails because `PluginGeneratorDescriptor::inputFormat`, `GeneratorCommand::inputFormat`, and `Graph` symbols are missing from the plugin test target.

- [ ] **Step 3: Extend descriptor structs**

Modify `qt/inc/plugins/plugindescriptor.h`:

```cpp
struct PluginGeneratorDescriptor {
    QString command;
    QString inputFormat = QStringLiteral("legacy_noc_json");
    QStringList args;

    bool hasGenerator() const { return !command.trimmed().isEmpty(); }
    bool usesGenericGraphInput() const {
        return inputFormat == QStringLiteral("generic_graph_v1");
    }
    QStringList arguments(const QString& inputPath, const QString& outputDirectory) const {
        QStringList resolved;
        resolved.reserve(args.size());
        for (QString arg : args) {
            arg.replace(QStringLiteral("{input}"), inputPath);
            arg.replace(QStringLiteral("{output}"), outputDirectory);
            resolved.append(arg);
        }
        return resolved;
    }
};
```

Modify `qt/inc/plugins/generatorrunner.h`:

```cpp
struct GeneratorCommand {
    bool valid = false;
    QString errorMessage;
    QString pluginId;
    QString workingDirectory;
    QString command;
    QString inputFormat = QStringLiteral("legacy_noc_json");
    QStringList arguments;
};
```

- [ ] **Step 4: Parse and propagate input format**

In `qt/src/plugins/pluginregistry.cpp`, after parsing `generator.command`, add:

```cpp
    descriptor.generator.inputFormat =
        generator.value(QStringLiteral("input_format")).toString(QStringLiteral("legacy_noc_json")).trimmed();
    if (descriptor.generator.inputFormat.isEmpty()) {
        descriptor.generator.inputFormat = QStringLiteral("legacy_noc_json");
    }
```

In `qt/src/plugins/generatorrunner.cpp`, before returning the valid command, set:

```cpp
    command.inputFormat = plugin->generator.inputFormat;
```

- [ ] **Step 5: Write generic JSON in MainWindow when requested**

In `qt/src/app/mainwindow.cpp`, replace:

```cpp
jsonFile.write(m_graph->toJsonDocument(designName, GraphJsonFlavor::Framework).toJson());
```

with:

```cpp
    const GraphJsonFlavor exportFlavor =
        generatorCommand.inputFormat == QStringLiteral("generic_graph_v1")
            ? GraphJsonFlavor::Plugin
            : GraphJsonFlavor::Framework;
    jsonFile.write(m_graph->toJsonDocument(designName, exportFlavor).toJson());
```

- [ ] **Step 6: Include graph sources in plugin_test target**

In `qt/xmake.lua`, add these files to `plugin_test` extra files:

```lua
    "src/**/generatorrunner.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "inc/**/generatorrunner.h",
    "inc/**/graph.h",
    "inc/**/module.h",
```

- [ ] **Step 7: Run GREEN verification**

Run:

```bash
xmake build plugin_test
xmake run plugin_test
xmake build graph_test
xmake run graph_test
```

Expected: both tests print `passed`.

- [ ] **Step 8: Commit**

```bash
git add qt/inc/plugins/plugindescriptor.h qt/src/plugins/pluginregistry.cpp qt/inc/plugins/generatorrunner.h qt/src/plugins/generatorrunner.cpp qt/src/app/mainwindow.cpp qt/test/plugin_test.cpp qt/xmake.lua
git commit -m "feat(qt): support generic plugin generator input"
```

## Task 3: RaveNoC Plugin Metadata

**Files:**
- Create: `plugins/ravenoc/plugin.json`
- Create: `plugins/ravenoc/modules.xml`
- Create: `plugins/ravenoc/graphics/RaveNoC.xml`
- Modify: `qt/test/plugin_test.cpp`

- [ ] **Step 1: Write failing metadata test**

Append this test to `qt/test/plugin_test.cpp`:

```cpp
void testRepositoryRaveNoCPluginMetadataLoads() {
    const QString pluginRoot = QFileInfo(QStringLiteral("../plugins/ravenoc")).absoluteFilePath();
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({pluginRoot});

    require(plugins.size() == 1, "RaveNoC plugin should be discovered");
    require(plugins.first().id == QStringLiteral("finepaper.ravenoc"),
            "RaveNoC plugin id should load");
    require(plugins.first().generator.command == QStringLiteral("ruby"),
            "RaveNoC plugin should use Ruby generator");
    require(plugins.first().generator.inputFormat == QStringLiteral("generic_graph_v1"),
            "RaveNoC plugin should request generic graph input");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);

    const ModuleType* type = registry.getType(QStringLiteral("RaveNoC"));
    require(type != nullptr, "RaveNoC module type should load");
    require(type->pluginId == QStringLiteral("finepaper.ravenoc"),
            "RaveNoC module should keep plugin ownership");
    require(type->graphGroup != QStringLiteral("xps") &&
                type->graphGroup != QStringLiteral("endpoints"),
            "RaveNoC must not reuse XP/Endpoint graph groups");
    require(type->defaultParameters.contains(QStringLiteral("rows")),
            "RaveNoC rows parameter should load");
    require(type->defaultParameters.contains(QStringLiteral("routing_algorithm")),
            "RaveNoC routing algorithm parameter should load");
    require(type->parameterMetadata.value(QStringLiteral("routing_algorithm")).choices.size() == 2,
            "RaveNoC routing algorithm choices should load");
}
```

Add the call in `main()`:

```cpp
testRepositoryRaveNoCPluginMetadataLoads();
```

- [ ] **Step 2: Run RED**

Run:

```bash
xmake build plugin_test
xmake run plugin_test
```

Expected: runtime failure with `RaveNoC plugin should be discovered`.

- [ ] **Step 3: Create plugin manifest**

Create `plugins/ravenoc/plugin.json`:

```json
{
  "id": "finepaper.ravenoc",
  "name": "RaveNoC",
  "version": "1.0",
  "modules": "modules.xml",
  "graphics": "graphics",
  "generator": {
    "command": "ruby",
    "input_format": "generic_graph_v1",
    "args": [
      "generator/bin/generate",
      "-i",
      "{input}",
      "-o",
      "{output}",
      "-t",
      "generator/template"
    ]
  },
  "native": {
    "enabled": false,
    "library": ""
  }
}
```

- [ ] **Step 4: Create RaveNoC module bundle**

Create `plugins/ravenoc/modules.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<module-bundle>
  <module name="RaveNoC" palette_label="RaveNoC" graph_group="ip_core"
          description="Configurable RaveNoC mesh fabric backed by upstream RTL.">
    <identity external_id_prefix="ravenoc" display_prefix="RNoC" width="2" supports_mesh_coordinates="false" />
    <interfaces>
      <interface id="axi_mosi" label="AXI MOSI" bus="axi4_struct_array" role="target" connects_to="initiator" match="data_width" />
      <interface id="axi_miso" label="AXI MISO" bus="axi4_struct_array" role="initiator" connects_to="target" match="data_width" />
      <interface id="irq" label="IRQ" bus="interrupt_array" role="initiator" connects_to="target" match="" />
      <interface id="clock_reset" label="Clock/Reset" bus="clock_reset" role="target" connects_to="initiator" match="" />
    </interfaces>
    <ports>
      <port id="axi_mosi" direction="input" type="bus" bus_type="axi4_struct_array" role="target" name="AXI MOSI" description="AXI request array input" interface="axi_mosi" />
      <port id="axi_miso" direction="output" type="bus" bus_type="axi4_struct_array" role="initiator" name="AXI MISO" description="AXI response array output" interface="axi_miso" />
      <port id="irq" direction="output" type="interrupt" bus_type="interrupt_array" role="status" name="IRQ" description="RaveNoC interrupt array" interface="irq" />
      <port id="clock_reset" direction="input" type="clock_reset" bus_type="clock_reset" role="clock" name="CLK/RST" description="AXI and NoC clock/reset inputs" interface="clock_reset" />
    </ports>
    <parameters>
      <parameter name="display_name" type="string" default="RaveNoC" label="Display name" description="Name shown on the canvas." />
      <parameter name="external_id" type="string" default="ravenoc_00" label="External ID" description="Stable generated artifact identifier." />
      <parameter name="x" type="int" default="0" configurable="false" description="Canvas X position." />
      <parameter name="y" type="int" default="0" configurable="false" description="Canvas Y position." />
      <parameter name="rows" type="int" default="2" min="1" max="16" label="Rows" description="RaveNoC mesh row count." />
      <parameter name="cols" type="int" default="2" min="1" max="16" label="Columns" description="RaveNoC mesh column count." />
      <parameter name="flit_data_width" type="int" default="32" min="8" max="512" label="Flit data width" description="FLIT_DATA_WIDTH macro value." />
      <parameter name="flit_type_width" type="int" default="2" min="1" max="8" label="Flit type width" description="FLIT_TP_WIDTH macro value." />
      <parameter name="flit_buffer_depth" type="int" default="2" min="1" max="1024" label="Flit buffer depth" description="FLIT_BUFF macro value; must be a power of two." />
      <parameter name="virtual_channels" type="int" default="3" min="1" max="16" label="Virtual channels" description="N_VIRT_CHN macro value." />
      <parameter name="routing_algorithm" type="string" default="xy" label="Routing algorithm" description="ROUTING_ALG macro value.">
        <choices>
          <choice value="xy" label="XY" />
          <choice value="yx" label="YX" />
        </choices>
      </parameter>
      <parameter name="priority" type="string" default="zero_high" label="Priority" description="H_PRIORITY macro value.">
        <choices>
          <choice value="zero_high" label="Zero High" />
          <choice value="zero_low" label="Zero Low" />
        </choices>
      </parameter>
      <parameter name="max_packet_flits" type="int" default="256" min="1" max="4096" label="Max packet flits" description="MAX_SZ_PKT macro value." />
      <parameter name="axi_addr_width" type="int" default="32" min="8" max="64" label="AXI address width" description="AXI_ADDR_WIDTH macro value." />
      <parameter name="axi_data_width" type="int" default="32" min="8" max="512" label="AXI data width" description="AXI_DATA_WIDTH macro value." />
      <parameter name="axi_cdc_required" type="string" default="all" label="AXI CDC required" description="AXI_CDC_REQ policy: all, none, or bit mask." />
      <parameter name="bypass_cdc" type="bool" default="false" label="Bypass CDC in smoke" description="Drive generated bypass_cdc vector high in the smoke wrapper." />
    </parameters>
  </module>
</module-bundle>
```

- [ ] **Step 5: Create graphics overlay**

Create `plugins/ravenoc/graphics/RaveNoC.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<module-graphics type="RaveNoC">
  <graphics layout="fallback" node_color="#b7d7f0">
    <expanded min_width="168" height="92" caption_left="10" caption_top="8" />
  </graphics>
  <anchors>
    <anchor ref="clock_reset" x="84" y="0" normal_x="0" normal_y="-1" label="CLK/RST" label_x="84" label_y="18" />
    <anchor ref="axi_mosi" x="0" y="40" normal_x="-1" normal_y="0" label="AXI MOSI" label_x="44" label_y="40" />
    <anchor ref="axi_miso" x="168" y="40" normal_x="1" normal_y="0" label="AXI MISO" label_x="124" label_y="40" />
    <anchor ref="irq" x="168" y="72" normal_x="1" normal_y="0" label="IRQ" label_x="140" label_y="72" />
  </anchors>
</module-graphics>
```

- [ ] **Step 6: Run GREEN verification**

Run:

```bash
xmake build plugin_test
xmake run plugin_test
```

Expected: `plugin_test passed`.

- [ ] **Step 7: Commit**

```bash
git add plugins/ravenoc/plugin.json plugins/ravenoc/modules.xml plugins/ravenoc/graphics/RaveNoC.xml qt/test/plugin_test.cpp
git commit -m "feat(ravenoc): add plugin metadata"
```

## Task 4: Ruby/ERB RaveNoC Generator

**Files:**
- Create: `plugins/ravenoc/generator/bin/generate`
- Create: `plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb`
- Create: `plugins/ravenoc/generator/template/ravenoc_config.svh.erb`
- Create: `plugins/ravenoc/generator/template/ravenoc_demo_top.sv.erb`
- Create: `plugins/ravenoc/generator/template/ravenoc_filelist.f.erb`
- Create: `plugins/ravenoc/generator/template/verify.sh.erb`
- Create: `plugins/ravenoc/generator/test/test_generator.rb`

- [ ] **Step 1: Write failing generator tests**

Create `plugins/ravenoc/generator/test/test_generator.rb`:

```ruby
$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'fileutils'
require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'

class RaveNoCGeneratorTest < Minitest::Test
  GENERATOR = File.expand_path('../bin/generate', __dir__)

  def test_generates_config_filelist_wrapper_verify_and_manifest
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'graph.json', valid_graph)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)
      out = File.join(dir, 'out')

      stdout, stderr, status = run_generator(input, out, vendor)

      assert status.success?, stderr
      assert_includes stdout, 'Generated RaveNoC integration'
      assert_includes File.read(File.join(out, 'ravenoc_config.svh')), '`define NOC_CFG_SZ_ROWS 2'
      assert_includes File.read(File.join(out, 'ravenoc_config.svh')), '`define ROUTING_ALG XYAlg'
      assert_includes File.read(File.join(out, 'ravenoc_demo_top.sv')), 'module ravenoc_demo_top'
      assert_includes File.read(File.join(out, 'ravenoc_demo_top.sv')), 'ravenoc #('
      assert_includes File.read(File.join(out, 'ravenoc_filelist.f')), '+define+NOC_CFG_SZ_ROWS=2'
      assert_includes File.read(File.join(out, 'ravenoc_filelist.f')), 'src/ravenoc.sv'
      assert_includes File.read(File.join(out, 'ravenoc_filelist.f')), 'ravenoc_demo_top.sv'
      assert File.executable?(File.join(out, 'verify.sh')), 'verify.sh should be executable'
      assert_includes File.read(File.join(out, 'verify.sh')), '--lint-only'

      manifest = JSON.parse(File.read(File.join(out, 'manifest.json')))
      assert_equal 'finepaper.ravenoc', manifest.fetch('plugin')
      assert_equal 'ravenoc_node', manifest.fetch('module').fetch('id')
      assert_equal 2, manifest.fetch('parameters').fetch('rows')
    end
  end

  def test_rejects_illegal_single_node_mesh
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph.fetch('modules').first.fetch('parameters')['rows'] = 1
      graph.fetch('modules').first.fetch('parameters')['cols'] = 1
      input = write_json(dir, 'graph.json', graph)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)

      _stdout, stderr, status = run_generator(input, File.join(dir, 'out'), vendor)

      refute status.success?
      assert_includes stderr, '1x1 is not a legal RaveNoC mesh'
    end
  end

  def test_rejects_non_power_of_two_buffer_depth
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph.fetch('modules').first.fetch('parameters')['flit_buffer_depth'] = 3
      input = write_json(dir, 'graph.json', graph)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)

      _stdout, stderr, status = run_generator(input, File.join(dir, 'out'), vendor)

      refute status.success?
      assert_includes stderr, 'flit_buffer_depth must be a power of two'
    end
  end

  def test_reports_missing_vendor_source
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'graph.json', valid_graph)

      _stdout, stderr, status = run_generator(input, File.join(dir, 'out'), File.join(dir, 'missing'))

      refute status.success?
      assert_includes stderr, 'RaveNoC vendor source is missing'
    end
  end

  private

  def run_generator(input, output, vendor)
    Open3.capture3(
      RbConfig.ruby,
      GENERATOR,
      '-i', input,
      '-o', output,
      '-t', File.expand_path('../template', __dir__),
      '--vendor', vendor
    )
  end

  def write_json(root, relative, data)
    path = File.join(root, relative)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, JSON.pretty_generate(data))
    path
  end

  def valid_graph
    {
      'schema' => 'finepaper-plugin-graph-v1',
      'name' => 'demo',
      'modules' => [
        {
          'id' => 'ravenoc_node',
          'plugin' => 'finepaper.ravenoc',
          'type' => 'RaveNoC',
          'parameters' => {
            'rows' => 2,
            'cols' => 2,
            'flit_data_width' => 32,
            'flit_type_width' => 2,
            'flit_buffer_depth' => 2,
            'virtual_channels' => 3,
            'routing_algorithm' => 'xy',
            'priority' => 'zero_high',
            'max_packet_flits' => 256,
            'axi_addr_width' => 32,
            'axi_data_width' => 32,
            'axi_cdc_required' => 'all',
            'bypass_cdc' => false
          }
        }
      ],
      'connections' => []
    }
  end

  def make_fake_vendor(root)
    required_vendor_files.each do |relative|
      path = File.join(root, relative)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, "// fake vendor file\n")
    end
  end

  def required_vendor_files
    [
      'bus_arch_sv_pkg/amba_axi_pkg.sv',
      'src/include/ravenoc_axi_fnc.svh',
      'src/include/ravenoc_defines.svh',
      'src/include/ravenoc_structs.svh',
      'src/include/ravenoc_pkg.sv',
      'src/ni/axi_csr.sv',
      'src/ni/axi_slave_if.sv',
      'src/ni/router_wrapper.sv',
      'src/ni/async_gp_fifo.sv',
      'src/ni/cdc_pkt.sv',
      'src/ni/pkt_proc.sv',
      'src/router/fifo.sv',
      'src/router/output_module.sv',
      'src/router/router_if.sv',
      'src/router/router_ravenoc.sv',
      'src/router/rr_arbiter.sv',
      'src/router/vc_buffer.sv',
      'src/router/input_router.sv',
      'src/router/input_module.sv',
      'src/router/input_datapath.sv',
      'src/ravenoc.sv'
    ]
  end
end
```

- [ ] **Step 2: Run RED**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_generator.rb
```

Expected: failure because `plugins/ravenoc/generator/bin/generate` does not exist.

- [ ] **Step 3: Create Ruby entrypoint**

Create `plugins/ravenoc/generator/bin/generate`:

```ruby
#!/usr/bin/env ruby

$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'optparse'
require 'ravenoc_generator'

options = {
  template_dir: File.expand_path('../template', __dir__),
  vendor_dir: File.expand_path('../../vendor/ravenoc', __dir__)
}

OptionParser.new do |opts|
  opts.on('-i INPUT') { |value| options[:input] = value }
  opts.on('-o OUTPUT') { |value| options[:output] = value }
  opts.on('-t TEMPLATE_DIR') { |value| options[:template_dir] = value }
  opts.on('--vendor PATH') { |value| options[:vendor_dir] = value }
end.parse!

begin
  RaveNoCGenerator.new(
    input_path: options.fetch(:input),
    output_dir: options.fetch(:output),
    template_dir: options.fetch(:template_dir),
    vendor_dir: options.fetch(:vendor_dir)
  ).generate
rescue KeyError => error
  warn "error: missing required option #{error.key}"
  exit 1
rescue RaveNoCGenerator::GenerationError => error
  warn "error: #{error.message}"
  exit 1
end
```

Make it executable:

```bash
chmod +x plugins/ravenoc/generator/bin/generate
```

- [ ] **Step 4: Create ERB templates**

Create `plugins/ravenoc/generator/template/ravenoc_config.svh.erb`:

```erb
`ifndef FINEPAPER_RAVENOC_CONFIG_SVH
`define FINEPAPER_RAVENOC_CONFIG_SVH
<% define_values.each do |name, value| -%>
`define <%= name %> <%= value %>
<% end -%>
`endif
```

Create `plugins/ravenoc/generator/template/ravenoc_demo_top.sv.erb`:

```erb
`include "ravenoc_config.svh"

module ravenoc_demo_top;
  import amba_axi_pkg::*;
  import ravenoc_pkg::*;

  logic [NoCSize-1:0] clk_axi;
  logic               clk_noc;
  logic [NoCSize-1:0] arst_axi;
  logic               arst_noc;
  s_axi_mosi_t [NoCSize-1:0] axi_mosi_if;
  s_axi_miso_t [NoCSize-1:0] axi_miso_if;
  s_irq_ni_t   [NoCSize-1:0] irqs;
  logic [NoCSize-1:0] bypass_cdc;

  assign clk_axi = '0;
  assign clk_noc = 1'b0;
  assign arst_axi = '1;
  assign arst_noc = 1'b1;
  assign axi_mosi_if = '0;
  assign bypass_cdc = {NoCSize{<%= bypass_cdc_literal %>}};

  ravenoc #(
    .AXI_CDC_REQ(<%= axi_cdc_literal %>)
  ) u_ravenoc (
    .clk_axi(clk_axi),
    .clk_noc(clk_noc),
    .arst_axi(arst_axi),
    .arst_noc(arst_noc),
    .axi_mosi_if(axi_mosi_if),
    .axi_miso_if(axi_miso_if),
    .irqs(irqs),
    .bypass_cdc(bypass_cdc)
  );
endmodule
```

Create `plugins/ravenoc/generator/template/ravenoc_filelist.f.erb`:

```erb
+incdir+<%= output_dir %>
+incdir+<%= File.join(vendor_dir, 'src/include') %>
<% define_values.each do |name, value| -%>
+define+<%= name %>=<%= value %>
<% end -%>
<% vendor_files.each do |relative| -%>
<%= File.join(vendor_dir, relative) %>
<% end -%>
<%= File.join(output_dir, 'ravenoc_demo_top.sv') %>
```

Create `plugins/ravenoc/generator/template/verify.sh.erb`:

```erb
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
verilator --lint-only --sv --Wno-UNOPTFLAT -f ravenoc_filelist.f --top-module ravenoc_demo_top
```

- [ ] **Step 5: Implement Ruby generator**

Create `plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb`:

```ruby
require 'erb'
require 'fileutils'
require 'json'
require 'open3'

class RaveNoCGenerator
  class GenerationError < StandardError; end

  REQUIRED_VENDOR_FILES = [
    'bus_arch_sv_pkg/amba_axi_pkg.sv',
    'src/include/ravenoc_axi_fnc.svh',
    'src/include/ravenoc_defines.svh',
    'src/include/ravenoc_structs.svh',
    'src/include/ravenoc_pkg.sv',
    'src/ni/axi_csr.sv',
    'src/ni/axi_slave_if.sv',
    'src/ni/router_wrapper.sv',
    'src/ni/async_gp_fifo.sv',
    'src/ni/cdc_pkt.sv',
    'src/ni/pkt_proc.sv',
    'src/router/fifo.sv',
    'src/router/output_module.sv',
    'src/router/router_if.sv',
    'src/router/router_ravenoc.sv',
    'src/router/rr_arbiter.sv',
    'src/router/vc_buffer.sv',
    'src/router/input_router.sv',
    'src/router/input_module.sv',
    'src/router/input_datapath.sv',
    'src/ravenoc.sv'
  ].freeze

  DEFAULTS = {
    'rows' => 2,
    'cols' => 2,
    'flit_data_width' => 32,
    'flit_type_width' => 2,
    'flit_buffer_depth' => 2,
    'virtual_channels' => 3,
    'routing_algorithm' => 'xy',
    'priority' => 'zero_high',
    'max_packet_flits' => 256,
    'axi_addr_width' => 32,
    'axi_data_width' => 32,
    'axi_cdc_required' => 'all',
    'bypass_cdc' => false
  }.freeze

  ROUTING_MAP = {
    'xy' => 'XYAlg',
    'yx' => 'YXAlg'
  }.freeze

  PRIORITY_MAP = {
    'zero_high' => 'ZeroHighPrior',
    'zero_low' => 'ZeroLowPrior'
  }.freeze

  DEFINE_NAMES = {
    'rows' => 'NOC_CFG_SZ_ROWS',
    'cols' => 'NOC_CFG_SZ_COLS',
    'flit_data_width' => 'FLIT_DATA_WIDTH',
    'flit_type_width' => 'FLIT_TP_WIDTH',
    'flit_buffer_depth' => 'FLIT_BUFF',
    'virtual_channels' => 'N_VIRT_CHN',
    'routing_algorithm' => 'ROUTING_ALG',
    'priority' => 'H_PRIORITY',
    'max_packet_flits' => 'MAX_SZ_PKT',
    'axi_addr_width' => 'AXI_ADDR_WIDTH',
    'axi_data_width' => 'AXI_DATA_WIDTH'
  }.freeze

  attr_reader :input_path, :output_dir, :template_dir, :vendor_dir

  def initialize(input_path:, output_dir:, template_dir:, vendor_dir:)
    @input_path = input_path
    @output_dir = output_dir
    @template_dir = template_dir
    @vendor_dir = vendor_dir
  end

  def generate
    graph = read_graph
    module_record = single_ravenoc_module(graph)
    parameters = DEFAULTS.merge(module_record.fetch('parameters', {}))
    validate_vendor!
    validate_parameters!(parameters)

    FileUtils.mkdir_p(output_dir)
    render('ravenoc_config.svh.erb', File.join(output_dir, 'ravenoc_config.svh'), binding_for(module_record, parameters))
    render('ravenoc_demo_top.sv.erb', File.join(output_dir, 'ravenoc_demo_top.sv'), binding_for(module_record, parameters))
    render('ravenoc_filelist.f.erb', File.join(output_dir, 'ravenoc_filelist.f'), binding_for(module_record, parameters))
    render('verify.sh.erb', File.join(output_dir, 'verify.sh'), binding_for(module_record, parameters))
    FileUtils.chmod('+x', File.join(output_dir, 'verify.sh'))
    write_manifest(module_record, parameters)
    puts "Generated RaveNoC integration in #{output_dir}"
  end

  private

  def read_graph
    data = JSON.parse(File.read(input_path))
    raise GenerationError, 'expected schema finepaper-plugin-graph-v1' unless data['schema'] == 'finepaper-plugin-graph-v1'

    data
  rescue Errno::ENOENT
    raise GenerationError, "input graph not found: #{input_path}"
  rescue JSON::ParserError => error
    raise GenerationError, "invalid JSON input: #{error.message}"
  end

  def single_ravenoc_module(graph)
    modules = graph.fetch('modules', []).select do |mod|
      mod['plugin'] == 'finepaper.ravenoc' && mod['type'] == 'RaveNoC'
    end
    raise GenerationError, "expected exactly one RaveNoC module, found #{modules.size}" unless modules.size == 1

    modules.first
  end

  def validate_vendor!
    missing = REQUIRED_VENDOR_FILES.find { |relative| !File.file?(File.join(vendor_dir, relative)) }
    return unless missing

    raise GenerationError,
          "RaveNoC vendor source is missing or incomplete. Run: git submodule update --init --recursive. Missing: #{missing}"
  end

  def positive_integer!(parameters, name)
    value = parameters[name]
    raise GenerationError, "#{name} must be a positive integer" unless value.is_a?(Integer) && value.positive?

    value
  end

  def validate_parameters!(parameters)
    rows = positive_integer!(parameters, 'rows')
    cols = positive_integer!(parameters, 'cols')
    raise GenerationError, '1x1 is not a legal RaveNoC mesh' if rows == 1 && cols == 1

    buffer_depth = positive_integer!(parameters, 'flit_buffer_depth')
    raise GenerationError, 'flit_buffer_depth must be a power of two' unless (buffer_depth & (buffer_depth - 1)).zero?

    %w[flit_data_width flit_type_width virtual_channels max_packet_flits axi_addr_width axi_data_width].each do |name|
      positive_integer!(parameters, name)
    end
    raise GenerationError, 'routing_algorithm must be xy or yx' unless ROUTING_MAP.key?(parameters['routing_algorithm'])
    raise GenerationError, 'priority must be zero_high or zero_low' unless PRIORITY_MAP.key?(parameters['priority'])
    validate_axi_cdc_required!(parameters)
  end

  def validate_axi_cdc_required!(parameters)
    noc_size = parameters.fetch('rows') * parameters.fetch('cols')
    value = parameters.fetch('axi_cdc_required', 'all').to_s.strip.downcase.delete('_')
    return if %w[all none].include?(value)
    return if value.match?(/\A[01]+\z/) && value.length == noc_size

    raise GenerationError, "axi_cdc_required must be all, none, or a #{noc_size}-bit binary mask"
  end

  def define_values(parameters)
    DEFINE_NAMES.to_h do |parameter_name, define_name|
      value = parameters.fetch(parameter_name)
      value = ROUTING_MAP.fetch(value) if parameter_name == 'routing_algorithm'
      value = PRIORITY_MAP.fetch(value) if parameter_name == 'priority'
      [define_name, value]
    end
  end

  def axi_cdc_literal(parameters)
    noc_size = parameters.fetch('rows') * parameters.fetch('cols')
    value = parameters.fetch('axi_cdc_required', 'all').to_s.strip.downcase
    return "{#{noc_size}{1'b1}}" if value == 'all'
    return "{#{noc_size}{1'b0}}" if value == 'none'

    "#{noc_size}'b#{value.delete('_')}"
  end

  def bypass_cdc_literal(parameters)
    parameters.fetch('bypass_cdc') ? "1'b1" : "1'b0"
  end

  def vendor_files
    REQUIRED_VENDOR_FILES
  end

  def render(template_name, output_path, template_binding)
    template = File.read(File.join(template_dir, template_name))
    File.write(output_path, ERB.new(template, trim_mode: '-').result(template_binding))
  end

  def binding_for(module_record, parameters)
    define_values = define_values(parameters)
    axi_cdc_literal = axi_cdc_literal(parameters)
    bypass_cdc_literal = bypass_cdc_literal(parameters)
    vendor_files = vendor_files()
    output_dir = self.output_dir
    vendor_dir = self.vendor_dir
    binding
  end

  def source_commit
    stdout, _stderr, status = Open3.capture3('git', '-C', vendor_dir, 'rev-parse', 'HEAD')
    status.success? ? stdout.strip : 'unknown'
  rescue StandardError
    'unknown'
  end

  def write_manifest(module_record, parameters)
    manifest = {
      plugin: 'finepaper.ravenoc',
      source: {
        repository: 'https://github.com/aignacio/ravenoc.git',
        commit: source_commit
      },
      module: {
        id: module_record['id'],
        type: module_record['type']
      },
      parameters: parameters,
      verification: {
        command: 'bash verify.sh'
      }
    }
    File.write(File.join(output_dir, 'manifest.json'), "#{JSON.pretty_generate(manifest)}\n")
  end
end
```

- [ ] **Step 6: Run GREEN generator tests**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_generator.rb
```

Expected: `4 runs` and `0 failures`.

- [ ] **Step 7: Commit**

```bash
git add plugins/ravenoc/generator/bin/generate plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb plugins/ravenoc/generator/template/ravenoc_config.svh.erb plugins/ravenoc/generator/template/ravenoc_demo_top.sv.erb plugins/ravenoc/generator/template/ravenoc_filelist.f.erb plugins/ravenoc/generator/template/verify.sh.erb plugins/ravenoc/generator/test/test_generator.rb
git commit -m "feat(ravenoc): generate integration artifacts"
```

## Task 5: Add RaveNoC Upstream Submodule

**Files:**
- Modify: `.gitmodules`
- Create gitlink: `plugins/ravenoc/vendor/ravenoc`

- [ ] **Step 1: Add submodule**

Run:

```bash
git submodule add https://github.com/aignacio/ravenoc.git plugins/ravenoc/vendor/ravenoc
```

Expected: Git creates `.gitmodules` and a gitlink at `plugins/ravenoc/vendor/ravenoc`.

- [ ] **Step 2: Verify source files exist**

Run:

```bash
test -f plugins/ravenoc/vendor/ravenoc/src/ravenoc.sv
test -f plugins/ravenoc/vendor/ravenoc/ravenoc.core
test -f plugins/ravenoc/vendor/ravenoc/src/include/ravenoc_defines.svh
```

Expected: all commands exit 0.

- [ ] **Step 3: Run generator tests against real submodule**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_generator.rb
```

Expected: `0 failures`.

- [ ] **Step 4: Commit**

```bash
git add .gitmodules plugins/ravenoc/vendor/ravenoc
git commit -m "chore(ravenoc): vendor upstream rtl as submodule"
```

## Task 6: End-to-End RaveNoC Verilator Smoke

**Files:**
- Create: `plugins/ravenoc/generator/examples/default_2x2.json`
- Create: `plugins/ravenoc/generator/test/test_smoke.rb`
- Modify: `plugins/ravenoc/generator/template/verify.sh.erb` only when Step 3 records a concrete Verilator lint flag requirement.

- [ ] **Step 1: Create example input**

Create `plugins/ravenoc/generator/examples/default_2x2.json`:

```json
{
  "schema": "finepaper-plugin-graph-v1",
  "name": "ravenoc_default_2x2",
  "modules": [
    {
      "id": "ravenoc_00",
      "plugin": "finepaper.ravenoc",
      "type": "RaveNoC",
      "parameters": {
        "rows": 2,
        "cols": 2,
        "flit_data_width": 32,
        "flit_type_width": 2,
        "flit_buffer_depth": 2,
        "virtual_channels": 3,
        "routing_algorithm": "xy",
        "priority": "zero_high",
        "max_packet_flits": 256,
        "axi_addr_width": 32,
        "axi_data_width": 32,
        "axi_cdc_required": "all",
        "bypass_cdc": false
      }
    }
  ],
  "connections": []
}
```

- [ ] **Step 2: Write smoke test**

Create `plugins/ravenoc/generator/test/test_smoke.rb`:

```ruby
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'

class RaveNoCSmokeTest < Minitest::Test
  PLUGIN_ROOT = File.expand_path('../..', __dir__)
  GENERATOR = File.expand_path('../bin/generate', __dir__)
  EXAMPLE = File.expand_path('../examples/default_2x2.json', __dir__)
  VENDOR = File.join(PLUGIN_ROOT, 'vendor/ravenoc')

  def test_default_2x2_verilator_lint
    skip 'verilator is not installed' unless system('which verilator > /dev/null 2>&1')
    skip 'RaveNoC submodule is not initialized' unless File.file?(File.join(VENDOR, 'src/ravenoc.sv'))

    Dir.mktmpdir do |dir|
      out = File.join(dir, 'out')
      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        GENERATOR,
        '-i', EXAMPLE,
        '-o', out,
        '-t', File.expand_path('../template', __dir__)
      )
      assert status.success?, stderr
      assert_includes stdout, 'Generated RaveNoC integration'

      _verify_stdout, verify_stderr, verify_status = Open3.capture3('bash', 'verify.sh', chdir: out)
      assert verify_status.success?, verify_stderr
    end
  end
end
```

- [ ] **Step 3: Run smoke and verify behavior**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_smoke.rb
```

Expected when Verilator and submodule are present: `1 runs` and `0 failures`.

If Verilator reports upstream syntax/lint issues, add only the narrow flags needed to `plugins/ravenoc/generator/template/verify.sh.erb`, then rerun this smoke. Do not add suppressions unrelated to observed output.

- [ ] **Step 4: Run all RaveNoC generator tests**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_smoke.rb
```

Expected: both test files finish with `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add plugins/ravenoc/generator/examples/default_2x2.json plugins/ravenoc/generator/test/test_smoke.rb plugins/ravenoc/generator/template/verify.sh.erb
git commit -m "test(ravenoc): verify generated rtl with verilator"
```

## Task 7: Full Regression Pass

**Files:**
- No planned source edits.

- [ ] **Step 1: Run Qt focused tests**

Run:

```bash
xmake build graph_test
xmake run graph_test
xmake build plugin_test
xmake run plugin_test
xmake build projectdocument_test
xmake run projectdocument_test
```

Expected:

```text
graph_test passed
plugin_test passed
projectdocument_test passed
```

- [ ] **Step 2: Run RaveNoC generator tests**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_smoke.rb
```

Expected: both return `0 failures`.

- [ ] **Step 3: Run existing Ruby generator tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
ruby plugins/noc/generator/test/test_generator.rb
```

Expected: both return 0 failures.

- [ ] **Step 4: Check git state**

Run:

```bash
git status --short
```

Expected: only intentionally untracked local files remain, or no output.

- [ ] **Step 5: Confirm no final source edits are pending**

Run:

```bash
git diff --exit-code
```

Expected: no output. If this reports a diff, inspect it and either commit the specific Task 7 adjustment with an exact file list or move that adjustment back into the task that introduced it.
