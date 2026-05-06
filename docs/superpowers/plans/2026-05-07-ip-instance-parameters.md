# IP Instance Parameters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move RaveNoC fabric-wide parameters to IP instance parameters and expose them through spec metadata, Qt project/export data, the property panel, and the RaveNoC generator.

**Architecture:** The first implementation keeps the current single active IP editing model, but names the data as an IP instance so future multi-IP project support has a clear landing point. Plugin manifests carry `kind` and `instance_parameters`; `Graph` owns one active `IpInstance`; project/generator JSON serializes that instance separately from internal modules; RaveNoC reads fabric-wide values only from `ip_instance.parameters`.

**Tech Stack:** Ruby spec generator and RaveNoC generator, Qt/C++ graph/project/plugin/property panel code, existing xmake Qt tests, Minitest Ruby tests.

---

### Task 1: Spec Generator And RaveNoC Metadata

**Files:**
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `spec/noc/ravenoc.yml`
- Generated: `plugins/ravenoc/plugin.json`
- Generated: `plugins/ravenoc/modules.xml`

- [ ] **Step 1: Write the failing spec generator test**

Add assertions to `test_generates_ravenoc_extension_runtime_bundle`:

```ruby
assert_equal 'noc', plugin_json.fetch('kind')
instance_parameters = plugin_json.fetch('instance_parameters')
assert_equal 11, instance_parameters.size
assert_equal 32, instance_parameters.fetch('flit_data_width').fetch('default')
assert_equal %w[xy yx], instance_parameters.fetch('routing_algorithm').fetch('enum')
refute_includes modules_xml, 'name="flit_data_width"'
assert_includes modules_xml, '<parameter name="mesh_row" type="int" default="0"'
```

Add a rejection test:

```ruby
def test_rejects_extension_instance_parameter_without_default
  Dir.mktmpdir do |dir|
    yaml = ravenoc_extension_yaml.sub('default: 32, min: 8', 'min: 8')
    extension_path = write_file(dir, 'spec/noc/ravenoc.yml', yaml)
    write_file(dir, 'spec/noc/views/RaveTile.xml', rave_tile_view_xml)
    write_file(dir, 'spec/noc/views/RaveEndpoint.xml', rave_endpoint_view_xml)

    error = assert_raises(SpecGenerator::SpecError) do
      SpecGenerator.generate_extension(
        extension_path: extension_path,
        views_dir: File.join(dir, 'spec/noc/views'),
        bundle_dir: File.join(dir, 'plugins/ravenoc')
      )
    end
    assert_match(/instance parameter flit_data_width default is required/, error.message)
  end
end
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ruby spec_generator/test/spec_generator_test.rb`

Expected: FAIL because `plugin.json` does not contain `kind` or `instance_parameters`, and RaveTile still emits fabric-wide parameters.

- [ ] **Step 3: Implement spec parser/emitter support**

Update extension constants and parser:

```ruby
EXTENSION_TOP_LEVEL_KEYS = %w[schema kind extension runtime topology_presets instance_parameters modules].freeze

def parse
  data = load_yaml
  validate_top_level(data)
  validate_extension(data.fetch('extension'))
  validate_runtime(data.fetch('runtime'))
  validate_topology_presets(data.fetch('topology_presets', []))
  validate_parameters('instance', data.fetch('instance_parameters', {}), 'instance parameter')
  validate_modules(data.fetch('modules'))
  views = ViewParser.new(@views_dir, data.fetch('modules')).parse
  ParsedSpec.new(data: data, views: views)
end
```

Emit manifest fields in `ExtensionBundleEmitter#plugin_json`:

```ruby
{
  id: extension.fetch('id'),
  name: extension.fetch('name'),
  version: extension.fetch('version'),
  kind: @spec.fetch('kind'),
  instance_parameters: @spec.fetch('instance_parameters', {}),
  modules: 'modules.xml',
  graphics: 'graphics',
  ...
}
```

- [ ] **Step 4: Move RaveNoC fabric parameters in the source spec**

Add top-level `instance_parameters` to `spec/noc/ravenoc.yml` and remove these keys from `modules.RaveTile.parameters`:

```yaml
instance_parameters:
  flit_data_width: { type: int, default: 32, min: 8, max: 512, label: Flit data width, description: FLIT_DATA_WIDTH macro value. }
  flit_type_width: { type: int, default: 2, min: 1, max: 8, label: Flit type width, description: FLIT_TP_WIDTH macro value. }
  flit_buffer_depth: { type: int, default: 2, min: 1, max: 1024, label: Flit buffer depth, description: FLIT_BUFF macro value; must be a power of two. }
  virtual_channels: { type: int, default: 3, min: 1, max: 16, label: Virtual channels, description: N_VIRT_CHN macro value. }
  routing_algorithm: { type: string, enum: [xy, yx], labels: { xy: XY, yx: YX }, default: xy, label: Routing algorithm, description: ROUTING_ALG macro value. }
  priority: { type: string, enum: [zero_high, zero_low], labels: { zero_high: Zero High, zero_low: Zero Low }, default: zero_high, label: Priority, description: H_PRIORITY macro value. }
  max_packet_flits: { type: int, default: 256, min: 1, max: 4096, label: Max packet flits, description: MAX_SZ_PKT macro value. }
  axi_addr_width: { type: int, default: 32, min: 8, max: 64, label: AXI address width, description: AXI_ADDR_WIDTH macro value. }
  axi_data_width: { type: int, default: 32, min: 8, max: 512, label: AXI data width, description: AXI_DATA_WIDTH macro value. }
  axi_cdc_required: { type: string, default: all, label: AXI CDC required, description: 'AXI_CDC_REQ policy: all, none, or bit mask.' }
  bypass_cdc: { type: bool, default: false, label: Bypass CDC in smoke, description: Drive generated bypass_cdc vector high in the smoke wrapper. }
```

- [ ] **Step 5: Regenerate bundle and verify**

Run: `ruby spec_generator/bin/spec-gen --extension spec/noc/ravenoc.yml --views spec/noc/views --bundle plugins/ravenoc`

Run: `ruby spec_generator/test/spec_generator_test.rb`

Expected: PASS.

### Task 2: Qt Plugin Metadata And Graph IP Instance State

**Files:**
- Modify: `qt/inc/plugins/plugindescriptor.h`
- Modify: `qt/src/plugins/pluginregistry.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Write failing Qt metadata/export tests**

In `plugin_test.cpp`, assert RaveNoC plugin instance parameters load:

```cpp
require(plugins.first().kind == QStringLiteral("noc"),
        "RaveNoC plugin kind should load");
require(plugins.first().instanceParameters.contains(QStringLiteral("flit_data_width")),
        "RaveNoC instance flit width should load");
require(std::get<int>(plugins.first().instanceParameters.value(QStringLiteral("flit_data_width")).defaultValue) == 32,
        "RaveNoC flit width default should load");
```

In `graph_test.cpp`, add:

```cpp
void testPluginExportIncludesIpInstanceParameters() {
    Graph graph;
    graph.configureIpInstance(QStringLiteral("ravenoc_0"),
                              QStringLiteral("finepaper.ravenoc"),
                              QStringLiteral("noc"),
                              QStringLiteral("RaveNoC"),
                              {{QStringLiteral("flit_data_width"), Parameter(QStringLiteral("flit_data_width"), 64)}});
    QJsonObject root = graph.toJsonDocument(QStringLiteral("demo"), GraphJsonFlavor::Plugin).object();
    QJsonObject ip = root.value(QStringLiteral("ip_instance")).toObject();
    require(ip.value(QStringLiteral("id")).toString() == QStringLiteral("ravenoc_0"), "ip instance id should export");
    require(ip.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("flit_data_width")).toInt() == 64,
            "ip instance parameter should export");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `xmake build qt_tests && xmake run plugin_test && xmake run graph_test`

Expected: FAIL because PluginDescriptor has no `kind`/`instanceParameters`, and Graph plugin export has no `ip_instance`.

- [ ] **Step 3: Implement plugin descriptor parsing**

Add `PluginInstanceParameterDescriptor` with `name`, `type`, `defaultValue`, `label`, `description`, `choices`, `minimumValue`, `maximumValue`, and `configurable`. Parse manifest `kind` and `instance_parameters` in `PluginRegistry`.

- [ ] **Step 4: Implement Graph IP instance state**

Add `GraphIpInstance` and methods:

```cpp
void configureIpInstance(const QString& id,
                         const QString& pluginId,
                         const QString& kind,
                         const QString& type,
                         const QHash<QString, Parameter>& parameters);
bool setIpInstanceParameter(const QString& name, Parameter::Value value);
const std::optional<GraphIpInstance>& ipInstance() const;
```

Emit `ipInstanceParameterChanged(name)` when a value changes.

- [ ] **Step 5: Export plugin JSON with `ip_instance`**

Update `Graph::toJsonDocument(..., GraphJsonFlavor::Plugin)` to include:

```json
"ip_instance": {
  "id": "ravenoc_0",
  "plugin": "finepaper.ravenoc",
  "kind": "noc",
  "type": "RaveNoC",
  "parameters": {}
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `xmake build qt_tests && xmake run plugin_test && xmake run graph_test`

Expected: PASS.

### Task 3: Project Persistence And Active IP Initialization

**Files:**
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/projectwriter.cpp`
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/src/app/mainwindow.cpp`

- [ ] **Step 1: Write failing project persistence tests**

Add a project round-trip assertion that `ProjectDocument` preserves an IP instance with parameters:

```cpp
document.ipInstances.push_back(ProjectIpInstanceRecord{
    QStringLiteral("ravenoc_0"),
    QStringLiteral("finepaper.ravenoc"),
    QStringLiteral("noc"),
    QStringLiteral("RaveNoC"),
    QJsonObject{{QStringLiteral("flit_data_width"), 64}}
});
```

Add a validation test:

```cpp
ProjectDocument document = validProjectDocument();
document.ipInstances.push_back(ProjectIpInstanceRecord{QStringLiteral("noc_a"), QStringLiteral("finepaper.ravenoc"), QStringLiteral("noc"), QStringLiteral("RaveNoC"), {}});
document.ipInstances.push_back(ProjectIpInstanceRecord{QStringLiteral("noc_b"), QStringLiteral("finepaper.othernoc"), QStringLiteral("noc"), QStringLiteral("OtherNoC"), {}});
Graph graph;
const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);
require(!result.success, "second noc IP should be rejected");
require(result.error.contains(QStringLiteral("kind: noc")), "error should mention noc uniqueness");
```

- [ ] **Step 2: Run project test to verify it fails**

Run: `xmake build qt_tests && xmake run projectdocument_test`

Expected: FAIL because project records do not contain IP instances.

- [ ] **Step 3: Implement project read/write/load**

Add `ProjectIpInstanceRecord` to `ProjectDocument`. Write it under top-level `ip_instances`. Read it back. Validate at most one `kind == "noc"`. Configure `Graph` with the loaded instance before module population.

- [ ] **Step 4: Initialize active plugin instance in MainWindow**

When `setActivePluginId()` runs, configure the graph IP instance from the selected plugin manifest defaults:

```cpp
configureGraphIpInstanceFromPlugin(*plugin);
```

Use id `<last-plugin-token>_0`, kind `plugin.kind`, type `plugin.name`, and default instance parameters.

- [ ] **Step 5: Run project tests**

Run: `xmake build qt_tests && xmake run projectdocument_test`

Expected: PASS.

### Task 4: Property Panel IP Instance Sections

**Files:**
- Modify: `qt/inc/panels/propertypanel.h`
- Modify: `qt/src/panels/propertypanel.cpp`

- [ ] **Step 1: Write failing panel behavior test where practical**

If adding a widget test is too invasive for this slice, verify this through `graph_test` signal behavior and manual Qt smoke after implementation:

```cpp
bool changed = false;
QObject::connect(&graph, &Graph::ipInstanceParameterChanged, [&changed](const QString&) { changed = true; });
graph.setIpInstanceParameter(QStringLiteral("flit_data_width"), 64);
require(changed, "ip instance parameter update should notify property panel");
```

- [ ] **Step 2: Run signal test to verify it fails**

Run: `xmake build qt_tests && xmake run graph_test`

Expected: FAIL before `ipInstanceParameterChanged` exists.

- [ ] **Step 3: Add unselected property panel content**

When `m_selectedModule == nullptr` and graph has an IP instance, show a section header label and form rows for IP instance parameters. Reuse the existing controls for string/int/bool/enum values, but call `Graph::setIpInstanceParameter()` instead of `SetParameterCommand` for the first slice.

- [ ] **Step 4: Refresh on graph IP parameter changes**

Connect `Graph::ipInstanceParameterChanged` to a new `PropertyPanel::onIpInstanceParameterChanged()` slot and update the matching widget.

- [ ] **Step 5: Run tests and manual smoke**

Run: `xmake build qt_tests && xmake run graph_test`

Manual smoke: start Qt, create a RaveNoC mesh, click empty canvas, verify the property panel shows `RaveNoC / ravenoc_0` with fabric-wide parameters and RaveTile no longer shows those parameters.

### Task 5: RaveNoC Generator Reads IP Instance Parameters

**Files:**
- Modify: `plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb`
- Modify: `plugins/ravenoc/generator/test/test_generator.rb`
- Modify: `plugins/ravenoc/generator/examples/default_2x2.json`
- Modify: `plugins/ravenoc/generator/examples/internal_mesh_2x2.json`

- [ ] **Step 1: Write failing generator tests**

Change `valid_graph` and internal graph fixtures to include:

```ruby
'ip_instance' => {
  'id' => 'ravenoc_0',
  'plugin' => 'finepaper.ravenoc',
  'kind' => 'noc',
  'type' => 'RaveNoC',
  'parameters' => {
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
```

Add:

```ruby
def test_rejects_missing_ip_instance_parameter
  Dir.mktmpdir do |dir|
    graph = valid_graph
    graph.fetch('ip_instance').fetch('parameters').delete('flit_data_width')
    input = write_json(dir, 'missing_ip_param.json', graph)
    _stdout, stderr, status = run_drc(input)
    refute status.success?
    assert_includes stderr, 'missing IP instance parameter flit_data_width'
  end
end
```

- [ ] **Step 2: Run generator tests to verify failure**

Run: `ruby plugins/ravenoc/generator/test/test_generator.rb`

Expected: FAIL because generator still reads module/tile parameters.

- [ ] **Step 3: Implement generator parameter source**

Read `graph['ip_instance']['parameters']`, merge derived `rows`/`cols`, and remove first-tile fabric parameter fallback. Validate every key in `DEFAULTS` except `rows` and `cols` is present in the IP instance parameter map.

- [ ] **Step 4: Update fixtures**

Move fabric-wide settings from RaveNoC module/tile parameters into top-level `ip_instance.parameters` in RaveNoC example JSON files.

- [ ] **Step 5: Run generator tests**

Run: `ruby plugins/ravenoc/generator/test/test_generator.rb`

Expected: PASS.

### Task 6: Full Verification

**Files:**
- No new files.

- [ ] **Step 1: Run Ruby spec generator tests**

Run: `ruby spec_generator/test/spec_generator_test.rb`

Expected: PASS.

- [ ] **Step 2: Run RaveNoC generator tests**

Run: `ruby plugins/ravenoc/generator/test/test_generator.rb`

Expected: PASS.

- [ ] **Step 3: Run Qt tests**

Run: `xmake build qt_tests && xmake run plugin_test && xmake run graph_test && xmake run projectdocument_test`

Expected: PASS.

- [ ] **Step 4: Run smoke where dependencies are available**

Run: `ruby plugins/ravenoc/generator/test/test_smoke.rb`

Expected: PASS or SKIP with a clear missing-Verilator/submodule message.

---

## Self-Review

Spec coverage:

- `instance_parameters` declaration and validation are covered by Task 1.
- Per-IP parameter storage and generator export are covered by Tasks 2 and 3.
- Property panel unselected editing is covered by Task 4.
- RaveNoC fabric parameter move and generator source change are covered by Tasks 1 and 5.
- One-NoC-per-project validation is covered by Task 3.

Placeholder scan: no `TBD`, `TODO`, or unresolved placeholder steps remain.

Type consistency: the plan consistently uses `instance_parameters` in plugin/spec metadata, `ip_instance` in generator export, and `ProjectIpInstanceRecord` in project persistence.
