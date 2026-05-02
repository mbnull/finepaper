# IP Internal Graph Topology Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Qt editor work on one selected IP package at a time, add Mesh/Ring topology preset creation, and migrate RaveNoC from a single black-box module to editable internal graph nodes.

**Architecture:** Keep plugin directories as IP packages and use `PluginDescriptor` as the runtime package metadata. Add active-IP filtering to the palette and generator flow, parse package topology presets from `plugin.json`, and implement a Qt topology builder that creates ordinary `Graph` modules/connections from preset metadata. RaveNoC will expose internal `RaveTile`/attachment module types and its Ruby generator will infer RaveNoC configuration from that internal graph while retaining legacy single-`RaveNoC` input compatibility.

**Tech Stack:** C++23/Qt 6, QtNodes, xmake, Ruby/Minitest, YAML/JSON spec generation, Verilator smoke tests.

---

## File Structure

- Modify `qt/inc/plugins/plugindescriptor.h` to add topology preset metadata structs.
- Modify `qt/src/plugins/pluginregistry.cpp` to parse `topology_presets` from `plugin.json`.
- Modify `qt/inc/modules/moduleregistry.h` and `qt/src/modules/moduleregistry.cpp` to list module types by plugin and graph group within a plugin.
- Modify `qt/inc/panels/palette.h` and `qt/src/panels/palette.cpp` to filter visible module types by active IP package.
- Create `qt/inc/topology/topologypresetbuilder.h` and `qt/src/topology/topologypresetbuilder.cpp` for non-UI graph creation.
- Create `qt/test/topology_preset_test.cpp` and update `qt/xmake.lua` for the new unit target.
- Modify `qt/inc/app/mainwindow.h` and `qt/src/app/mainwindow.cpp` to add active IP and topology preset controls.
- Modify `spec_generator/lib/spec_generator.rb` and `spec_generator/test/spec_generator_test.rb` to accept and emit extension `topology_presets`.
- Modify `plugins/noc/plugin.json`, `spec/noc/ravenoc.yml`, `spec/noc/views/*.xml`, and generated `plugins/ravenoc/*` metadata.
- Modify `plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb` and tests to consume internal graph nodes.

---

## Runtime Preset Shape

Use this runtime manifest shape in `plugin.json`:

```json
{
  "topology_presets": [
    {
      "id": "mesh",
      "label": "Mesh",
      "kind": "mesh",
      "router_module": "XP",
      "id_pattern": "xp_{row}_{col}",
      "ports": {
        "east": "east",
        "west": "west",
        "north": "north",
        "south": "south"
      },
      "parameters": {
        "rows": { "label": "Rows", "default": 2, "min": 1, "max": 16 },
        "cols": { "label": "Columns", "default": 2, "min": 1, "max": 16 }
      }
    },
    {
      "id": "ring",
      "label": "Ring",
      "kind": "ring",
      "router_module": "XP",
      "id_pattern": "xp_{index}",
      "ports": {
        "east": "east",
        "west": "west"
      },
      "parameters": {
        "nodes": { "label": "Nodes", "default": 4, "min": 2, "max": 64 }
      }
    }
  ]
}
```

The source YAML shape for generated extensions mirrors this structure under top-level `topology_presets`.

---

### Task 1: Active IP Package Filtering

**Files:**
- Modify: `qt/inc/modules/moduleregistry.h`
- Modify: `qt/src/modules/moduleregistry.cpp`
- Modify: `qt/inc/panels/palette.h`
- Modify: `qt/src/panels/palette.cpp`
- Test: `qt/test/plugin_test.cpp`

- [ ] **Step 1: Write failing ModuleRegistry filtering test**

Add this test to `qt/test/plugin_test.cpp` before `testGeneratorArgumentsSubstituteInputAndOutput()`:

```cpp
void testModuleRegistryListsTypesByPlugin() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);

    ModuleType nocType;
    nocType.name = QStringLiteral("XP");
    nocType.pluginId = QStringLiteral("finepaper.noc");
    nocType.graphGroup = QStringLiteral("xps");
    require(registry.registerType(nocType), "noc type should register");

    ModuleType ravenType;
    ravenType.name = QStringLiteral("RaveTile");
    ravenType.pluginId = QStringLiteral("finepaper.ravenoc");
    ravenType.graphGroup = QStringLiteral("xps");
    require(registry.registerType(ravenType), "ravenoc type should register");

    const QStringList nocTypes = registry.availableTypesForPlugin(QStringLiteral("finepaper.noc"));
    const QStringList ravenTypes = registry.availableTypesForPlugin(QStringLiteral("finepaper.ravenoc"));

    require(nocTypes == QStringList{QStringLiteral("XP")},
            "NoC active IP should only list NoC module types");
    require(ravenTypes == QStringList{QStringLiteral("RaveTile")},
            "RaveNoC active IP should only list RaveNoC module types");

    const ModuleType* ravenRouter =
        registry.getTypeForGraphGroup(QStringLiteral("finepaper.ravenoc"), QStringLiteral("xps"));
    require(ravenRouter && ravenRouter->name == QStringLiteral("RaveTile"),
            "graph group lookup should be scoped by plugin id");
}
```

Call it from `main()`:

```cpp
testModuleRegistryListsTypesByPlugin();
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd qt
xmake build plugin_test
xmake run plugin_test
```

Expected: build fails because `availableTypesForPlugin` and scoped `getTypeForGraphGroup` do not exist.

- [ ] **Step 3: Add ModuleRegistry filtering APIs**

In `qt/inc/modules/moduleregistry.h`, add:

```cpp
    // Returns type names owned by one plugin, sorted for stable UI listing.
    QStringList availableTypesForPlugin(const QString& pluginId) const;
    // Looks up the first type in a graph group owned by one plugin.
    const ModuleType* getTypeForGraphGroup(const QString& pluginId, const QString& graphGroup) const;
```

In `qt/src/modules/moduleregistry.cpp`, add:

```cpp
const ModuleType* ModuleRegistry::getTypeForGraphGroup(const QString& pluginId,
                                                       const QString& graphGroup) const {
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it) {
        if (it.value().pluginId == pluginId && it.value().graphGroup == graphGroup) {
            return &it.value();
        }
    }
    return nullptr;
}

QStringList ModuleRegistry::availableTypesForPlugin(const QString& pluginId) const {
    QStringList types;
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it) {
        if (it.value().pluginId == pluginId) {
            types.append(it.key());
        }
    }
    types.sort();
    return types;
}
```

- [ ] **Step 4: Add Palette active plugin filtering**

In `qt/inc/panels/palette.h`, add public API:

```cpp
    void setActivePluginId(const QString& pluginId);
    QString activePluginId() const { return m_activePluginId; }
```

Add member:

```cpp
    QString m_activePluginId;
```

In `qt/src/panels/palette.cpp`, update `populateModuleTypes()`:

```cpp
void Palette::populateModuleTypes() {
    m_listWidget->clear();

    const QStringList types = m_activePluginId.isEmpty()
        ? ModuleRegistry::instance().availableTypes()
        : ModuleRegistry::instance().availableTypesForPlugin(m_activePluginId);

    for (const QString& type : types) {
        const ModuleType* moduleType = ModuleRegistry::instance().getType(type);
        auto* item = new QListWidgetItem(ModuleTypeMetadata::paletteLabel(moduleType));
        item->setData(Qt::UserRole, type);
        m_listWidget->addItem(item);
    }
}
```

Add:

```cpp
void Palette::setActivePluginId(const QString& pluginId) {
    if (m_activePluginId == pluginId) {
        return;
    }
    m_activePluginId = pluginId;
    populateModuleTypes();
}
```

- [ ] **Step 5: Run test to verify it passes**

Run:

```bash
cd qt
xmake build plugin_test
xmake run plugin_test
```

Expected: `plugin_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/modules/moduleregistry.h qt/src/modules/moduleregistry.cpp \
        qt/inc/panels/palette.h qt/src/panels/palette.cpp qt/test/plugin_test.cpp
git commit -m "feat(qt): filter module palette by active ip package"
```

---

### Task 2: Topology Preset Manifest Metadata

**Files:**
- Modify: `qt/inc/plugins/plugindescriptor.h`
- Modify: `qt/src/plugins/pluginregistry.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `plugins/noc/plugin.json`

- [ ] **Step 1: Write failing plugin manifest preset test**

In `qt/test/plugin_test.cpp`, add this to `testPluginManifestLoadsRelativePaths()` after generator assertions:

```cpp
    require(plugins.first().topologyPresets.size() == 2,
            "topology presets should load from manifest");
    require(plugins.first().topologyPresets.first().id == QStringLiteral("mesh"),
            "first topology preset id should load");
    require(plugins.first().topologyPresets.first().kind == QStringLiteral("mesh"),
            "first topology preset kind should load");
    require(plugins.first().topologyPresets.first().routerModule == QStringLiteral("XP"),
            "topology router module should load");
    require(plugins.first().topologyPresets.first().parameters.value(QStringLiteral("rows")).defaultValue == 2,
            "topology rows default should load");
```

Replace the JSON in that test with:

```json
"topology_presets": [
  {
    "id": "mesh",
    "label": "Mesh",
    "kind": "mesh",
    "router_module": "XP",
    "id_pattern": "xp_{row}_{col}",
    "ports": {"east": "east", "west": "west", "north": "north", "south": "south"},
    "parameters": {
      "rows": {"label": "Rows", "default": 2, "min": 1, "max": 16},
      "cols": {"label": "Columns", "default": 2, "min": 1, "max": 16}
    }
  },
  {
    "id": "ring",
    "label": "Ring",
    "kind": "ring",
    "router_module": "XP",
    "id_pattern": "xp_{index}",
    "ports": {"east": "east", "west": "west"},
    "parameters": {
      "nodes": {"label": "Nodes", "default": 4, "min": 2, "max": 64}
    }
  }
]
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd qt
xmake build plugin_test
```

Expected: build fails because `PluginDescriptor::topologyPresets` is missing.

- [ ] **Step 3: Add topology preset descriptor types**

In `qt/inc/plugins/plugindescriptor.h`, add includes:

```cpp
#include <QHash>
#include <QVector>
```

Add before `PluginDescriptor`:

```cpp
struct TopologyPresetParameterDescriptor {
    QString label;
    int defaultValue = 0;
    int minimumValue = 0;
    int maximumValue = 0;
};

struct TopologyPresetDescriptor {
    QString id;
    QString label;
    QString kind;
    QString routerModule;
    QString idPattern;
    QHash<QString, QString> ports;
    QHash<QString, TopologyPresetParameterDescriptor> parameters;
};
```

Add to `PluginDescriptor`:

```cpp
    QVector<TopologyPresetDescriptor> topologyPresets;
```

- [ ] **Step 4: Parse topology presets in PluginRegistry**

In `qt/src/plugins/pluginregistry.cpp`, add helper functions near `stringArray()`:

```cpp
TopologyPresetParameterDescriptor topologyParameter(const QJsonObject& object) {
    TopologyPresetParameterDescriptor parameter;
    parameter.label = object.value(QStringLiteral("label")).toString();
    parameter.defaultValue = object.value(QStringLiteral("default")).toInt();
    parameter.minimumValue = object.value(QStringLiteral("min")).toInt(parameter.defaultValue);
    parameter.maximumValue = object.value(QStringLiteral("max")).toInt(parameter.defaultValue);
    return parameter;
}

QVector<TopologyPresetDescriptor> topologyPresetsFromJson(const QJsonValue& value) {
    QVector<TopologyPresetDescriptor> presets;
    if (!value.isArray()) {
        return presets;
    }

    for (const QJsonValue& item : value.toArray()) {
        const QJsonObject object = item.toObject();
        TopologyPresetDescriptor preset;
        preset.id = object.value(QStringLiteral("id")).toString().trimmed();
        preset.label = object.value(QStringLiteral("label")).toString().trimmed();
        preset.kind = object.value(QStringLiteral("kind")).toString().trimmed();
        preset.routerModule = object.value(QStringLiteral("router_module")).toString().trimmed();
        preset.idPattern = object.value(QStringLiteral("id_pattern")).toString().trimmed();

        const QJsonObject ports = object.value(QStringLiteral("ports")).toObject();
        for (auto it = ports.constBegin(); it != ports.constEnd(); ++it) {
            if (it.value().isString()) {
                preset.ports.insert(it.key(), it.value().toString());
            }
        }

        const QJsonObject parameters = object.value(QStringLiteral("parameters")).toObject();
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            preset.parameters.insert(it.key(), topologyParameter(it.value().toObject()));
        }

        if (!preset.id.isEmpty() && !preset.kind.isEmpty() && !preset.routerModule.isEmpty()) {
            if (preset.label.isEmpty()) {
                preset.label = preset.id;
            }
            presets.append(preset);
        }
    }
    return presets;
}
```

In `loadManifest()`, assign:

```cpp
    descriptor.topologyPresets = topologyPresetsFromJson(object.value(QStringLiteral("topology_presets")));
```

- [ ] **Step 5: Add NoC runtime presets**

Modify `plugins/noc/plugin.json` to include the Mesh and Ring runtime shape shown in the "Runtime Preset Shape" section.

- [ ] **Step 6: Run Qt test**

Run:

```bash
cd qt
xmake build plugin_test
xmake run plugin_test
```

Expected: `plugin_test passed`.

- [ ] **Step 7: Write failing spec-generator topology preset test**

In `spec_generator/test/spec_generator_test.rb`, extend `test_generates_ravenoc_extension_runtime_bundle`:

```ruby
      presets = plugin_json.fetch('topology_presets')
      assert_equal 1, presets.size
      assert_equal 'mesh', presets.first.fetch('id')
      assert_equal 'RaveTile', presets.first.fetch('router_module')
      assert_equal 'rave_{row}_{col}', presets.first.fetch('id_pattern')
      assert_equal 2, presets.first.fetch('parameters').fetch('rows').fetch('default')
```

In `ravenoc_extension_yaml`, add top-level YAML before `modules:`:

```yaml
      topology_presets:
        - id: mesh
          label: Mesh
          kind: mesh
          router_module: RaveTile
          id_pattern: rave_{row}_{col}
          ports: { east: east, west: west, north: north, south: south }
          parameters:
            rows: { label: Rows, default: 2, min: 1, max: 16 }
            cols: { label: Columns, default: 2, min: 1, max: 16 }
```

- [ ] **Step 8: Run spec-generator test to verify it fails**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: failure because `topology_presets` is unknown or not emitted.

- [ ] **Step 9: Implement extension topology preset parsing and emitting**

In `spec_generator/lib/spec_generator.rb`, update constants:

```ruby
EXTENSION_TOP_LEVEL_KEYS = %w[schema kind extension runtime topology_presets modules].freeze
TOPOLOGY_PRESET_KEYS = %w[id label kind router_module id_pattern ports parameters].freeze
TOPOLOGY_PRESET_PARAMETER_KEYS = %w[label default min max].freeze
```

In `ExtensionParser#parse`, after `validate_runtime(...)`, add:

```ruby
      validate_topology_presets(data.fetch('topology_presets', []))
```

Add parser methods:

```ruby
    def validate_topology_presets(presets)
      raise SpecError, 'topology_presets must be a list' unless presets.is_a?(Array)

      presets.each do |preset|
        raise SpecError, 'topology preset must be a map' unless preset.is_a?(Hash)

        validate_keys!(preset, TOPOLOGY_PRESET_KEYS, "topology preset #{preset['id'] || '<unnamed>'}")
        %w[id label kind router_module id_pattern].each do |key|
          raise SpecError, "topology preset #{key} must be a string" unless preset[key].is_a?(String)
        end
        raise SpecError, "topology preset #{preset['id']} ports must be a map" unless preset['ports'].is_a?(Hash)
        preset['ports'].each do |name, value|
          raise SpecError, "topology preset #{preset['id']} port #{name} must be a string" unless value.is_a?(String)
        end
        raise SpecError, "topology preset #{preset['id']} parameters must be a map" unless preset['parameters'].is_a?(Hash)
        preset['parameters'].each do |name, parameter|
          raise SpecError, "topology preset #{preset['id']} parameter #{name} must be a map" unless parameter.is_a?(Hash)
          validate_keys!(parameter, TOPOLOGY_PRESET_PARAMETER_KEYS, "topology preset #{preset['id']} parameter #{name}")
          raise SpecError, "topology preset #{preset['id']} parameter #{name} label must be a string" unless parameter['label'].is_a?(String)
          %w[default min max].each do |key|
            raise SpecError, "topology preset #{preset['id']} parameter #{name} #{key} must be an integer" unless parameter[key].is_a?(Integer)
          end
        end
      end
    end
```

In `ExtensionBundleEmitter#plugin_json`, add:

```ruby
          topology_presets: @spec.fetch('topology_presets', [])
```

Place it before `native:` so runtime manifests remain easy to scan.

- [ ] **Step 10: Run tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
cd qt
xmake build plugin_test
xmake run plugin_test
```

Expected: both pass.

- [ ] **Step 11: Commit**

```bash
git add qt/inc/plugins/plugindescriptor.h qt/src/plugins/pluginregistry.cpp \
        qt/test/plugin_test.cpp plugins/noc/plugin.json \
        spec_generator/lib/spec_generator.rb spec_generator/test/spec_generator_test.rb
git commit -m "feat(ip): load topology preset metadata"
```

---

### Task 3: Qt Topology Preset Builder

**Files:**
- Create: `qt/inc/topology/topologypresetbuilder.h`
- Create: `qt/src/topology/topologypresetbuilder.cpp`
- Create: `qt/test/topology_preset_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing topology builder test**

Create `qt/test/topology_preset_test.cpp`:

```cpp
#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "plugins/plugindescriptor.h"
#include "topology/topologypresetbuilder.h"

#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ModuleType routerType(const QString& name, const QString& pluginId) {
    ModuleType type;
    type.name = name;
    type.pluginId = pluginId;
    type.graphGroup = QStringLiteral("xps");
    type.defaultPorts = {
        Port(QStringLiteral("north"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("North"), {}, QStringLiteral("router"), QStringLiteral("router_link"), QStringLiteral("north")),
        Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("East"), {}, QStringLiteral("router"), QStringLiteral("router_link"), QStringLiteral("east")),
        Port(QStringLiteral("south"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("South"), {}, QStringLiteral("router"), QStringLiteral("router_link"), QStringLiteral("south")),
        Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("West"), {}, QStringLiteral("router"), QStringLiteral("router_link"), QStringLiteral("west"))
    };
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultParameters.insert(QStringLiteral("display_name"), Parameter(QStringLiteral("display_name"), QString{}));
    type.defaultParameters.insert(QStringLiteral("external_id"), Parameter(QStringLiteral("external_id"), QString{}));

    ModuleInterfaceMetadata north;
    north.id = QStringLiteral("north");
    north.bus = QStringLiteral("router_link");
    north.role = QStringLiteral("target");
    north.compatibleRoles = {QStringLiteral("initiator")};
    type.interfaceMetadata.insert(north.id, north);

    ModuleInterfaceMetadata east;
    east.id = QStringLiteral("east");
    east.bus = QStringLiteral("router_link");
    east.role = QStringLiteral("initiator");
    east.compatibleRoles = {QStringLiteral("target")};
    type.interfaceMetadata.insert(east.id, east);

    ModuleInterfaceMetadata south = east;
    south.id = QStringLiteral("south");
    type.interfaceMetadata.insert(south.id, south);

    ModuleInterfaceMetadata west = north;
    west.id = QStringLiteral("west");
    type.interfaceMetadata.insert(west.id, west);

    return type;
}

TopologyPresetDescriptor meshPreset() {
    TopologyPresetDescriptor preset;
    preset.id = QStringLiteral("mesh");
    preset.label = QStringLiteral("Mesh");
    preset.kind = QStringLiteral("mesh");
    preset.routerModule = QStringLiteral("XP");
    preset.idPattern = QStringLiteral("xp_{row}_{col}");
    preset.ports.insert(QStringLiteral("east"), QStringLiteral("east"));
    preset.ports.insert(QStringLiteral("west"), QStringLiteral("west"));
    preset.ports.insert(QStringLiteral("north"), QStringLiteral("north"));
    preset.ports.insert(QStringLiteral("south"), QStringLiteral("south"));
    return preset;
}

TopologyPresetDescriptor ringPreset() {
    TopologyPresetDescriptor preset;
    preset.id = QStringLiteral("ring");
    preset.label = QStringLiteral("Ring");
    preset.kind = QStringLiteral("ring");
    preset.routerModule = QStringLiteral("XP");
    preset.idPattern = QStringLiteral("xp_{index}");
    preset.ports.insert(QStringLiteral("east"), QStringLiteral("east"));
    preset.ports.insert(QStringLiteral("west"), QStringLiteral("west"));
    return preset;
}

void testMeshPresetCreatesEditableGraph() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.pluginId = QStringLiteral("finepaper.noc");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 3);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 6, "2x3 mesh should create six routers");
    require(graph.connections().size() == 7, "2x3 mesh should create seven links");
    require(graph.getModule(QStringLiteral("xp_0_0")) != nullptr, "mesh should create deterministic node ids");
    require(graph.isValidConnection(PortRef{QStringLiteral("xp_0_0"), QStringLiteral("east")},
                                    PortRef{QStringLiteral("xp_0_1"), QStringLiteral("west")}) == false,
            "created east/west ports should be occupied by normal graph connections");
}

void testRingPresetCreatesClosedLoop() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.pluginId = QStringLiteral("finepaper.noc");
    request.preset = ringPreset();
    request.parameters.insert(QStringLiteral("nodes"), 4);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "ring should create four routers");
    require(graph.connections().size() == 4, "ring should close the loop");
    require(graph.getModule(QStringLiteral("xp_3")) != nullptr, "ring should create deterministic ids");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testMeshPresetCreatesEditableGraph();
        testRingPresetCreatesClosedLoop();
    } catch (const std::exception& error) {
        std::cerr << "topology_preset_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "topology_preset_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add xmake target and verify failure**

In `qt/xmake.lua`, add:

```lua
add_qt_test_target("topology_preset_test", "test/topology_preset_test.cpp", {
    "src/**/topologypresetbuilder.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/pluginregistry.cpp",
    "src/**/frameworkpaths.cpp",
    "inc/**/topologypresetbuilder.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/plugindescriptor.h"
})
```

Run:

```bash
cd qt
xmake build topology_preset_test
```

Expected: build fails because `topology/topologypresetbuilder.h` does not exist.

- [ ] **Step 3: Implement topology builder header**

Create `qt/inc/topology/topologypresetbuilder.h`:

```cpp
#pragma once

#include "plugins/plugindescriptor.h"

#include <QHash>
#include <QString>
#include <QStringList>

class Graph;
class ModuleRegistry;

struct TopologyPresetRequest {
    QString pluginId;
    TopologyPresetDescriptor preset;
    QHash<QString, int> parameters;
};

struct TopologyPresetResult {
    bool success = false;
    QString error;
    QStringList moduleIds;
    QStringList connectionIds;
};

class TopologyPresetBuilder {
public:
    static TopologyPresetResult apply(Graph* graph,
                                      const ModuleRegistry& registry,
                                      const TopologyPresetRequest& request);
};
```

- [ ] **Step 4: Implement topology builder source**

Create `qt/src/topology/topologypresetbuilder.cpp`:

```cpp
#include "topology/topologypresetbuilder.h"

#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "modules/moduleregistry.h"

#include <memory>

namespace {

TopologyPresetResult failure(const QString& error) {
    TopologyPresetResult result;
    result.error = error;
    return result;
}

QString replaceToken(QString pattern, const QString& token, int value) {
    pattern.replace(QStringLiteral("{") + token + QStringLiteral("}"), QString::number(value));
    return pattern;
}

QString meshNodeId(const QString& pattern, int row, int col) {
    return replaceToken(replaceToken(pattern, QStringLiteral("row"), row), QStringLiteral("col"), col);
}

QString ringNodeId(const QString& pattern, int index) {
    return replaceToken(pattern, QStringLiteral("index"), index);
}

std::unique_ptr<Module> instantiateModule(const ModuleType& type,
                                          const QString& id,
                                          int row,
                                          int col) {
    auto module = std::make_unique<Module>(id, type.name);
    for (const Port& port : type.defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    if (module->parameters().contains(QStringLiteral("x"))) {
        module->setParameter(QStringLiteral("x"), col);
    }
    if (module->parameters().contains(QStringLiteral("y"))) {
        module->setParameter(QStringLiteral("y"), row);
    }
    if (module->parameters().contains(QStringLiteral("display_name"))) {
        module->setParameter(QStringLiteral("display_name"), id);
    }
    if (module->parameters().contains(QStringLiteral("external_id"))) {
        module->setParameter(QStringLiteral("external_id"), id);
    }
    return module;
}

bool addLink(Graph* graph,
             TopologyPresetResult& result,
             const QString& id,
             const QString& sourceModule,
             const QString& sourcePort,
             const QString& targetModule,
             const QString& targetPort) {
    const PortRef source{sourceModule, sourcePort};
    const PortRef target{targetModule, targetPort};
    if (!graph->isValidConnection(source, target)) {
        result.error = QStringLiteral("Generated invalid connection: %1").arg(id);
        return false;
    }
    graph->addConnection(std::make_unique<Connection>(id, source, target));
    result.connectionIds.append(id);
    return true;
}

TopologyPresetResult createMesh(Graph* graph,
                                const ModuleType& routerType,
                                const TopologyPresetRequest& request) {
    const int rows = request.parameters.value(QStringLiteral("rows"), 2);
    const int cols = request.parameters.value(QStringLiteral("cols"), 2);
    if (rows < 1 || cols < 1) {
        return failure(QStringLiteral("Mesh rows and columns must be positive"));
    }

    TopologyPresetResult result;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QString id = meshNodeId(request.preset.idPattern, row, col);
            if (graph->getModule(id)) {
                return failure(QStringLiteral("Module already exists: %1").arg(id));
            }
            if (!graph->addModule(instantiateModule(routerType, id, row, col))) {
                return failure(QStringLiteral("Could not add module: %1").arg(id));
            }
            result.moduleIds.append(id);
        }
    }

    const QString east = request.preset.ports.value(QStringLiteral("east"));
    const QString west = request.preset.ports.value(QStringLiteral("west"));
    const QString north = request.preset.ports.value(QStringLiteral("north"));
    const QString south = request.preset.ports.value(QStringLiteral("south"));

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const QString current = meshNodeId(request.preset.idPattern, row, col);
            if (col + 1 < cols) {
                const QString right = meshNodeId(request.preset.idPattern, row, col + 1);
                if (!addLink(graph, result, current + QStringLiteral("_east"), current, east, right, west)) {
                    return result;
                }
            }
            if (row + 1 < rows) {
                const QString below = meshNodeId(request.preset.idPattern, row + 1, col);
                if (!addLink(graph, result, current + QStringLiteral("_south"), current, south, below, north)) {
                    return result;
                }
            }
        }
    }

    result.success = true;
    return result;
}

TopologyPresetResult createRing(Graph* graph,
                                const ModuleType& routerType,
                                const TopologyPresetRequest& request) {
    const int nodes = request.parameters.value(QStringLiteral("nodes"), 4);
    if (nodes < 2) {
        return failure(QStringLiteral("Ring nodes must be at least 2"));
    }

    TopologyPresetResult result;
    for (int index = 0; index < nodes; ++index) {
        const QString id = ringNodeId(request.preset.idPattern, index);
        if (graph->getModule(id)) {
            return failure(QStringLiteral("Module already exists: %1").arg(id));
        }
        if (!graph->addModule(instantiateModule(routerType, id, 0, index))) {
            return failure(QStringLiteral("Could not add module: %1").arg(id));
        }
        result.moduleIds.append(id);
    }

    const QString east = request.preset.ports.value(QStringLiteral("east"));
    const QString west = request.preset.ports.value(QStringLiteral("west"));
    for (int index = 0; index < nodes; ++index) {
        const QString current = ringNodeId(request.preset.idPattern, index);
        const QString next = ringNodeId(request.preset.idPattern, (index + 1) % nodes);
        if (!addLink(graph, result, current + QStringLiteral("_next"), current, east, next, west)) {
            return result;
        }
    }

    result.success = true;
    return result;
}

} // namespace

TopologyPresetResult TopologyPresetBuilder::apply(Graph* graph,
                                                  const ModuleRegistry& registry,
                                                  const TopologyPresetRequest& request) {
    if (!graph) {
        return failure(QStringLiteral("Graph is required"));
    }
    const ModuleType* routerType = registry.getType(request.preset.routerModule);
    if (!routerType || routerType->pluginId != request.pluginId) {
        return failure(QStringLiteral("Router module %1 is not part of active IP %2")
                           .arg(request.preset.routerModule, request.pluginId));
    }
    if (request.preset.kind == QStringLiteral("mesh")) {
        return createMesh(graph, *routerType, request);
    }
    if (request.preset.kind == QStringLiteral("ring")) {
        return createRing(graph, *routerType, request);
    }
    return failure(QStringLiteral("Unsupported topology preset kind: %1").arg(request.preset.kind));
}
```

- [ ] **Step 5: Run topology preset test**

Run:

```bash
cd qt
xmake build topology_preset_test
xmake run topology_preset_test
```

Expected: `topology_preset_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/topology/topologypresetbuilder.h qt/src/topology/topologypresetbuilder.cpp \
        qt/test/topology_preset_test.cpp qt/xmake.lua
git commit -m "feat(qt): create graph topology presets"
```

---

### Task 4: Qt Active IP and Topology Controls

**Files:**
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/topology/topologypresetbuilder.cpp` if UI validation finds missing errors

- [ ] **Step 1: Add MainWindow members and slots**

In `qt/inc/app/mainwindow.h`, forward declare:

```cpp
class QComboBox;
class QMenu;
```

Add slots:

```cpp
    void activeIpChanged(int index);
    void createTopologyPreset();
```

Add private helpers:

```cpp
    void populateActiveIpSelector();
    void setActivePluginId(const QString& pluginId);
    void rebuildTopologyMenu();
```

Add members:

```cpp
    QComboBox* m_activeIpCombo;
    QMenu* m_topologyMenu;
    QString m_activePluginId;
```

- [ ] **Step 2: Initialize members**

In `MainWindow` constructor initializer list in `qt/src/app/mainwindow.cpp`, add:

```cpp
      m_activeIpCombo(nullptr),
      m_topologyMenu(nullptr),
```

- [ ] **Step 3: Add includes**

In `qt/src/app/mainwindow.cpp`, add:

```cpp
#include "topology/topologypresetbuilder.h"
#include <algorithm>
#include <QComboBox>
#include <QInputDialog>
#include <QToolButton>
```

- [ ] **Step 4: Add active IP selector setup**

In `setupActions()`, after creating `mainToolBar`, add:

```cpp
    m_activeIpCombo = new QComboBox(this);
    m_activeIpCombo->setObjectName(QStringLiteral("activeIpCombo"));
    mainToolBar->addWidget(m_activeIpCombo);
    connect(m_activeIpCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::activeIpChanged);
    populateActiveIpSelector();
```

Add helper implementation:

```cpp
void MainWindow::populateActiveIpSelector() {
    if (!m_activeIpCombo) {
        return;
    }
    m_activeIpCombo->blockSignals(true);
    m_activeIpCombo->clear();

    for (const PluginDescriptor& plugin : PluginRegistry::instance().plugins()) {
        if (ModuleRegistry::instance().availableTypesForPlugin(plugin.id).isEmpty()) {
            continue;
        }
        m_activeIpCombo->addItem(plugin.name, plugin.id);
    }

    m_activeIpCombo->blockSignals(false);
    if (m_activeIpCombo->count() > 0) {
        setActivePluginId(m_activeIpCombo->itemData(0).toString());
        m_activeIpCombo->setCurrentIndex(0);
    }
}
```

Add:

```cpp
void MainWindow::setActivePluginId(const QString& pluginId) {
    if (m_activePluginId == pluginId) {
        return;
    }
    m_activePluginId = pluginId;
    if (m_palette) {
        m_palette->setActivePluginId(pluginId);
    }
    rebuildTopologyMenu();
}
```

Add slot:

```cpp
void MainWindow::activeIpChanged(int index) {
    if (!m_activeIpCombo || index < 0) {
        return;
    }
    const QString nextPluginId = m_activeIpCombo->itemData(index).toString();
    if (nextPluginId == m_activePluginId) {
        return;
    }
    if (!m_graph->modules().empty()) {
        QMessageBox::warning(this,
                             "Active IP",
                             "Start a new empty design before switching the active IP package.");
        const int previousIndex = m_activeIpCombo->findData(m_activePluginId);
        if (previousIndex >= 0) {
            m_activeIpCombo->blockSignals(true);
            m_activeIpCombo->setCurrentIndex(previousIndex);
            m_activeIpCombo->blockSignals(false);
        }
        return;
    }
    setActivePluginId(nextPluginId);
}
```

- [ ] **Step 5: Add topology menu**

In `setupActions()`, after `mainToolBar->addAction(m_arrangeAction);`, add:

```cpp
    m_topologyMenu = new QMenu("Topology", this);
    auto* topologyButton = new QToolButton(this);
    topologyButton->setText("Topology");
    topologyButton->setPopupMode(QToolButton::InstantPopup);
    topologyButton->setMenu(m_topologyMenu);
    mainToolBar->addWidget(topologyButton);
    rebuildTopologyMenu();
```

Add helper:

```cpp
void MainWindow::rebuildTopologyMenu() {
    if (!m_topologyMenu) {
        return;
    }
    m_topologyMenu->clear();
    const PluginDescriptor* plugin = PluginRegistry::instance().plugin(m_activePluginId);
    if (!plugin) {
        return;
    }

    for (const TopologyPresetDescriptor& preset : plugin->topologyPresets) {
        QAction* action = m_topologyMenu->addAction(preset.label);
        action->setData(preset.id);
        connect(action, &QAction::triggered, this, &MainWindow::createTopologyPreset);
    }
}
```

Add slot:

```cpp
void MainWindow::createTopologyPreset() {
    auto* action = qobject_cast<QAction*>(sender());
    const PluginDescriptor* plugin = PluginRegistry::instance().plugin(m_activePluginId);
    if (!action || !plugin) {
        return;
    }

    const QString presetId = action->data().toString();
    auto it = std::find_if(plugin->topologyPresets.cbegin(), plugin->topologyPresets.cend(),
                           [&](const TopologyPresetDescriptor& preset) {
                               return preset.id == presetId;
                           });
    if (it == plugin->topologyPresets.cend()) {
        return;
    }

    TopologyPresetRequest request;
    request.pluginId = plugin->id;
    request.preset = *it;

    for (auto paramIt = it->parameters.cbegin(); paramIt != it->parameters.cend(); ++paramIt) {
        bool ok = false;
        const TopologyPresetParameterDescriptor parameter = paramIt.value();
        const int value = QInputDialog::getInt(this,
                                               it->label,
                                               parameter.label,
                                               parameter.defaultValue,
                                               parameter.minimumValue,
                                               parameter.maximumValue,
                                               1,
                                               &ok);
        if (!ok) {
            return;
        }
        request.parameters.insert(paramIt.key(), value);
    }

    const TopologyPresetResult result =
        TopologyPresetBuilder::apply(m_graph, ModuleRegistry::instance(), request);
    if (!result.success) {
        QMessageBox::warning(this, "Topology", result.error);
        return;
    }
    scheduleDocumentStateRefresh();
}
```

- [ ] **Step 6: Build Qt app**

Run:

```bash
cd qt
xmake build qt
```

Expected: build succeeds.

- [ ] **Step 7: Run focused Qt tests**

Run:

```bash
cd qt
xmake build plugin_test
xmake run plugin_test
xmake build topology_preset_test
xmake run topology_preset_test
```

Expected: both tests pass.

- [ ] **Step 8: Commit**

```bash
git add qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp
git commit -m "feat(qt): add active ip topology controls"
```

---

### Task 5: RaveNoC Internal Graph Metadata

**Files:**
- Modify: `spec/noc/ravenoc.yml`
- Create: `spec/noc/views/RaveTile.xml`
- Create: `spec/noc/views/RaveEndpoint.xml`
- Modify generated: `plugins/ravenoc/plugin.json`
- Modify generated: `plugins/ravenoc/modules.xml`
- Create generated: `plugins/ravenoc/graphics/RaveTile.xml`
- Create generated: `plugins/ravenoc/graphics/RaveEndpoint.xml`
- Delete generated if obsolete: `plugins/ravenoc/graphics/RaveNoC.xml`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `qt/test/plugin_test.cpp`

- [ ] **Step 1: Update spec-generator test expectation for RaveNoC internal nodes**

In `spec_generator/test/spec_generator_test.rb`, change the RaveNoC extension test assertions:

```ruby
      assert_includes modules_xml, '<module name="RaveTile" palette_label="Rave Tile" graph_group="xps"'
      assert_includes modules_xml, '<module name="RaveEndpoint" palette_label="Rave Endpoint" graph_group="endpoints"'
      assert_includes modules_xml, '<port id="east" direction="inout" type="bus" bus_type="ravenoc_router_link" role="router" name="East" description="East RaveNoC router link" interface="east" />'
      assert_includes modules_xml, '<choice value="xy" label="XY" />'

      rave_tile_graphics = File.read(File.join(dir, 'plugins/ravenoc/graphics/RaveTile.xml'))
      assert_includes rave_tile_graphics, '<module-graphics type="RaveTile">'
      assert_includes rave_tile_graphics, '<anchor ref="east"'
```

Remove assertions that require a single `RaveNoC` module.

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: failure because RaveNoC spec still emits `RaveNoC`.

- [ ] **Step 3: Replace RaveNoC spec modules with internal modules**

In `spec/noc/ravenoc.yml`, replace `modules: RaveNoC:` with:

```yaml
topology_presets:
  - id: mesh
    label: Mesh
    kind: mesh
    router_module: RaveTile
    id_pattern: rave_{row}_{col}
    ports: { east: east, west: west, north: north, south: south }
    parameters:
      rows: { label: Rows, default: 2, min: 1, max: 16 }
      cols: { label: Columns, default: 2, min: 1, max: 16 }
modules:
  RaveTile:
    palette_label: Rave Tile
    graph_group: xps
    description: Editable RaveNoC router tile.
    identity:
      external_id_prefix: rave
      display_prefix: RT
      width: 2
      supports_mesh_coordinates: true
    parameters:
      x: { type: int, default: 0, configurable: false, description: Mesh X coordinate. }
      y: { type: int, default: 0, configurable: false, description: Mesh Y coordinate. }
      display_name: { type: string, default: '', label: Display name, description: Name shown on the canvas. }
      external_id: { type: string, default: '', label: External ID, description: Stable generated artifact identifier. }
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
    interfaces:
      north: { label: North, bus: ravenoc_router_link, role: target, connects_to: initiator, match: [], port: { id: north, direction: inout, type: bus, bus_type: ravenoc_router_link, role: router, name: North, description: North RaveNoC router link } }
      east: { label: East, bus: ravenoc_router_link, role: initiator, connects_to: target, match: [], port: { id: east, direction: inout, type: bus, bus_type: ravenoc_router_link, role: router, name: East, description: East RaveNoC router link } }
      south: { label: South, bus: ravenoc_router_link, role: initiator, connects_to: target, match: [], port: { id: south, direction: inout, type: bus, bus_type: ravenoc_router_link, role: router, name: South, description: South RaveNoC router link } }
      west: { label: West, bus: ravenoc_router_link, role: target, connects_to: initiator, match: [], port: { id: west, direction: inout, type: bus, bus_type: ravenoc_router_link, role: router, name: West, description: West RaveNoC router link } }
      local0: { label: Local 0, bus: ravenoc_ni_link, role: target, connects_to: initiator, match: [data_width], port: { id: local0, direction: inout, type: bus, bus_type: ravenoc_ni_link, role: attachment, name: Local 0, description: Local RaveNoC endpoint slot 0 } }
  RaveEndpoint:
    palette_label: Rave Endpoint
    graph_group: endpoints
    description: Editable RaveNoC AXI endpoint attachment.
    identity:
      external_id_prefix: rep
      display_prefix: REP
      width: 2
      supports_mesh_coordinates: false
    parameters:
      display_name: { type: string, default: '', label: Display name, description: Name shown on the canvas. }
      external_id: { type: string, default: '', label: External ID, description: Stable generated artifact identifier. }
      data_width: { type: int, enum: [32, 64, 128, 256, 512], default: 32, label: Data width, description: AXI data width in bits. }
    interfaces:
      noc:
        label: NoC
        bus: ravenoc_ni_link
        role: initiator
        connects_to: target
        match: [data_width]
        port: { id: noc, direction: inout, type: bus, bus_type: ravenoc_ni_link, role: attachment, name: NoC, description: RaveNoC attachment interface }
```

- [ ] **Step 4: Add RaveTile view**

Create `spec/noc/views/RaveTile.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<module-view schema="v1" module="RaveTile">
  <graphics layout="mesh_router" node_color="#b7d7f0" supports_collapse="true">
    <expanded min_width="136" height="116" caption_left="30" caption_top="6" port_inset="16" />
    <collapsed min_width="104" height="92" caption_left="30" caption_top="26" endpoint_inset="18" />
    <arrangement endpoint_offset_x="156" mesh_spacing_x="220" mesh_spacing_y="168" />
  </graphics>
  <anchors>
    <anchor ref="north" x="68" y="0" normal_x="0" normal_y="-1" label="N" label_x="68" label_y="18" />
    <anchor ref="east" x="136" y="58" normal_x="1" normal_y="0" label="E" label_x="112" label_y="58" />
    <anchor ref="south" x="68" y="116" normal_x="0" normal_y="1" label="S" label_x="68" label_y="98" />
    <anchor ref="west" x="0" y="58" normal_x="-1" normal_y="0" label="W" label_x="24" label_y="58" />
    <anchor ref="local0" x="0" y="30" normal_x="-1" normal_y="0" label="Local 0" label_x="32" label_y="30" />
  </anchors>
</module-view>
```

- [ ] **Step 5: Add RaveEndpoint view**

Create `spec/noc/views/RaveEndpoint.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<module-view schema="v1" module="RaveEndpoint">
  <graphics layout="endpoint" node_color="#d6f4b6">
    <expanded min_width="120" height="54" caption_left="8" caption_top="6" />
    <arrangement loose_endpoint_spacing_x="168" loose_endpoint_spacing_y="84" loose_endpoint_margin_y="116" />
  </graphics>
  <anchors>
    <anchor ref="noc" x="120" y="27" normal_x="1" normal_y="0" label="NoC" label_x="92" label_y="27" />
  </anchors>
</module-view>
```

- [ ] **Step 6: Regenerate RaveNoC bundle**

Run:

```bash
ruby spec_generator/bin/spec-gen --extension spec/noc/ravenoc.yml --views spec/noc/views --bundle plugins/ravenoc
```

Expected: generated `plugins/ravenoc/modules.xml`, `plugin.json`, and `graphics/RaveTile.xml` / `graphics/RaveEndpoint.xml`.

- [ ] **Step 7: Update Qt RaveNoC metadata test**

In `qt/test/plugin_test.cpp`, replace `testRepositoryRaveNoCPluginMetadataLoads()` module checks with:

```cpp
    const ModuleType* tileType = registry.getType(QStringLiteral("RaveTile"));
    require(tileType != nullptr, "RaveTile module type should load");
    require(tileType->pluginId == QStringLiteral("finepaper.ravenoc"),
            "RaveTile should keep plugin ownership");
    require(tileType->graphGroup == QStringLiteral("xps"),
            "RaveTile should participate in router-style internal editing");
    require(tileType->defaultParameters.contains(QStringLiteral("routing_algorithm")),
            "RaveTile routing algorithm parameter should load");

    const ModuleType* endpointType = registry.getType(QStringLiteral("RaveEndpoint"));
    require(endpointType != nullptr, "RaveEndpoint module type should load");
    require(endpointType->pluginId == QStringLiteral("finepaper.ravenoc"),
            "RaveEndpoint should keep plugin ownership");
    require(endpointType->graphGroup == QStringLiteral("endpoints"),
            "RaveEndpoint should participate in endpoint-style internal editing");
```

- [ ] **Step 8: Run tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
cd qt
xmake build plugin_test
xmake run plugin_test
```

Expected: all pass.

- [ ] **Step 9: Commit**

```bash
git add spec/noc/ravenoc.yml spec/noc/views/RaveTile.xml spec/noc/views/RaveEndpoint.xml \
        plugins/ravenoc/plugin.json plugins/ravenoc/modules.xml plugins/ravenoc/graphics \
        spec_generator/test/spec_generator_test.rb qt/test/plugin_test.cpp
git commit -m "feat(ravenoc): expose editable internal graph modules"
```

---

### Task 6: RaveNoC Generator Internal Graph Support

**Files:**
- Modify: `plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb`
- Modify: `plugins/ravenoc/generator/test/test_generator.rb`
- Modify: `plugins/ravenoc/generator/test/test_smoke.rb` if smoke fixture changes
- Modify or create: `plugins/ravenoc/generator/examples/internal_mesh_2x2.json`

- [ ] **Step 1: Add internal graph example**

Create `plugins/ravenoc/generator/examples/internal_mesh_2x2.json`:

```json
{
  "schema": "finepaper-plugin-graph-v1",
  "name": "ravenoc_internal_2x2",
  "modules": [
    {
      "id": "rave_0_0",
      "plugin": "finepaper.ravenoc",
      "type": "RaveTile",
      "parameters": {
        "x": 0,
        "y": 0,
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
    },
    { "id": "rave_0_1", "plugin": "finepaper.ravenoc", "type": "RaveTile", "parameters": { "x": 1, "y": 0 } },
    { "id": "rave_1_0", "plugin": "finepaper.ravenoc", "type": "RaveTile", "parameters": { "x": 0, "y": 1 } },
    { "id": "rave_1_1", "plugin": "finepaper.ravenoc", "type": "RaveTile", "parameters": { "x": 1, "y": 1 } }
  ],
  "connections": [
    { "id": "rave_0_0_east", "source": { "module": "rave_0_0", "port": "east" }, "target": { "module": "rave_0_1", "port": "west" } },
    { "id": "rave_1_0_east", "source": { "module": "rave_1_0", "port": "east" }, "target": { "module": "rave_1_1", "port": "west" } },
    { "id": "rave_0_0_south", "source": { "module": "rave_0_0", "port": "south" }, "target": { "module": "rave_1_0", "port": "north" } },
    { "id": "rave_0_1_south", "source": { "module": "rave_0_1", "port": "south" }, "target": { "module": "rave_1_1", "port": "north" } }
  ]
}
```

- [ ] **Step 2: Write failing generator test**

In `plugins/ravenoc/generator/test/test_generator.rb`, add:

```ruby
  def test_generates_from_internal_ravetile_graph
    Dir.mktmpdir do |dir|
      input = File.join(__dir__, '..', 'examples', 'internal_mesh_2x2.json')
      out = File.join(dir, 'out')
      vendor = File.expand_path('../../vendor/ravenoc', __dir__)

      stdout, stderr, status = run_generator(input, out, vendor)

      assert status.success?, stderr
      assert_includes stdout, 'Generated RaveNoC integration'
      config = File.read(File.join(out, 'ravenoc_config.svh'))
      assert_includes config, '`define NOC_CFG_SZ_ROWS 2'
      assert_includes config, '`define NOC_CFG_SZ_COLS 2'

      manifest = JSON.parse(File.read(File.join(out, 'manifest.json')))
      assert_equal 'internal_graph', manifest.fetch('module').fetch('type')
      assert_equal 4, manifest.fetch('module').fetch('tiles')
    end
  end
```

- [ ] **Step 3: Run test to verify it fails**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_generator.rb
```

Expected: failure because generator still expects exactly one `RaveNoC` module.

- [ ] **Step 4: Implement internal graph parsing with legacy fallback**

In `plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb`, replace `single_ravenoc_module` with:

```ruby
  def ravenoc_module_record(graph)
    legacy = graph.fetch('modules', []).select do |mod|
      mod['plugin'] == 'finepaper.ravenoc' && mod['type'] == 'RaveNoC'
    end
    return legacy.first if legacy.size == 1

    tiles = graph.fetch('modules', []).select do |mod|
      mod['plugin'] == 'finepaper.ravenoc' && mod['type'] == 'RaveTile'
    end
    raise GenerationError, "expected RaveNoC module or RaveTile graph, found none" if tiles.empty?

    internal_graph_module_record(graph, tiles)
  end
```

Add:

```ruby
  def internal_graph_module_record(graph, tiles)
    coordinates = tiles.map do |tile|
      params = tile.fetch('parameters', {})
      x = params.fetch('x')
      y = params.fetch('y')
      raise GenerationError, "RaveTile #{tile['id']} x/y must be integers" unless x.is_a?(Integer) && y.is_a?(Integer)
      [tile['id'], x, y]
    end

    cols = coordinates.map { |(_, x, _)| x }.max + 1
    rows = coordinates.map { |(_, _, y)| y }.max + 1
    expected = rows * cols
    raise GenerationError, "RaveTile graph must be rectangular, expected #{expected} tiles, found #{tiles.size}" unless tiles.size == expected

    occupied = coordinates.map { |(_, x, y)| [x, y] }
    (0...rows).each do |row|
      (0...cols).each do |col|
        raise GenerationError, "missing RaveTile at #{col},#{row}" unless occupied.include?([col, row])
      end
    end

    first_tile = tiles.min_by { |tile| [tile.fetch('parameters', {}).fetch('y'), tile.fetch('parameters', {}).fetch('x')] }
    parameters = first_tile.fetch('parameters', {}).merge('rows' => rows, 'cols' => cols)
    {
      'id' => graph.fetch('name', 'ravenoc_internal_graph'),
      'type' => 'internal_graph',
      'parameters' => parameters,
      'tiles' => tiles
    }
  end
```

Update `generate`:

```ruby
    module_record = ravenoc_module_record(graph)
```

Update `write_manifest`:

```ruby
      module: {
        id: module_record['id'],
        type: module_record['type'],
        tiles: module_record.fetch('tiles', []).size
      },
```

- [ ] **Step 5: Run RaveNoC generator tests**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_generator.rb
```

Expected: all RaveNoC generator tests pass.

- [ ] **Step 6: Run RaveNoC smoke**

Run:

```bash
ruby plugins/ravenoc/generator/test/test_smoke.rb
```

Expected: `1 runs, 4 assertions, 0 failures`.

- [ ] **Step 7: Commit**

```bash
git add plugins/ravenoc/generator/src/ruby/ravenoc_generator.rb \
        plugins/ravenoc/generator/test/test_generator.rb \
        plugins/ravenoc/generator/examples/internal_mesh_2x2.json
git commit -m "feat(ravenoc): generate from editable internal graph"
```

---

### Task 7: End-to-End Preset Runtime Integration

**Files:**
- Modify: `spec/noc/ravenoc.yml`
- Modify generated: `plugins/ravenoc/plugin.json`
- Modify generated: `plugins/ravenoc/modules.xml`
- Modify generated graphics under `plugins/ravenoc/graphics/`
- Modify: `qt/test/topology_preset_test.cpp`

- [ ] **Step 1: Add repository RaveNoC preset test**

In `qt/test/topology_preset_test.cpp`, add:

```cpp
#include "plugins/pluginregistry.h"
#include <QDir>
#include <QFileInfo>
```

Add helper:

```cpp
QString repositoryPluginPath(const QString& relativePluginPath) {
    const QStringList candidates = {
        QDir::current().filePath(relativePluginPath),
        QDir::current().filePath(QStringLiteral("../") + relativePluginPath),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../../../../") + relativePluginPath)
    };
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isDir()) {
            return info.absoluteFilePath();
        }
    }
    return QFileInfo(candidates.first()).absoluteFilePath();
}
```

Add test:

```cpp
void testRepositoryRaveNoCMeshPresetCreatesInternalTiles() {
    const QString pluginRoot = repositoryPluginPath(QStringLiteral("plugins/ravenoc"));
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({pluginRoot});
    require(plugins.size() == 1, "RaveNoC plugin should be discovered");
    require(!plugins.first().topologyPresets.isEmpty(), "RaveNoC should expose topology presets");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);

    const auto meshIt = std::find_if(plugins.first().topologyPresets.cbegin(),
                                     plugins.first().topologyPresets.cend(),
                                     [](const TopologyPresetDescriptor& preset) {
                                         return preset.id == QStringLiteral("mesh");
                                     });
    require(meshIt != plugins.first().topologyPresets.cend(), "RaveNoC mesh preset should exist");

    Graph graph;
    TopologyPresetRequest request;
    request.pluginId = plugins.first().id;
    request.preset = *meshIt;
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "RaveNoC 2x2 mesh should create four editable tiles");
    require(graph.connections().size() == 4, "RaveNoC 2x2 mesh should create four router links");
    require(graph.getModule(QStringLiteral("rave_0_0")) != nullptr, "RaveNoC mesh node id should be deterministic");
}
```

Call it from `main()`:

```cpp
testRepositoryRaveNoCMeshPresetCreatesInternalTiles();
```

- [ ] **Step 2: Run test to verify failure if metadata is not regenerated**

Run:

```bash
cd qt
xmake build topology_preset_test
xmake run topology_preset_test
```

Expected: pass only after Task 5 generated RaveNoC preset metadata. If it fails, regenerate with Step 3.

- [ ] **Step 3: Regenerate RaveNoC runtime bundle**

Run:

```bash
ruby spec_generator/bin/spec-gen --extension spec/noc/ravenoc.yml --views spec/noc/views --bundle plugins/ravenoc
```

- [ ] **Step 4: Run full focused checks**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
cd qt
xmake build topology_preset_test
xmake run topology_preset_test
xmake build plugin_test
xmake run plugin_test
cd ..
ruby plugins/ravenoc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_smoke.rb
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add spec/noc/ravenoc.yml plugins/ravenoc/plugin.json plugins/ravenoc/modules.xml \
        plugins/ravenoc/graphics qt/test/topology_preset_test.cpp
git commit -m "test(ravenoc): verify mesh preset internal graph flow"
```

---

### Task 8: Full Regression

**Files:**
- No source edits unless a regression fails.

- [ ] **Step 1: Run Ruby spec-generator tests**

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: `0 failures, 0 errors`.

- [ ] **Step 2: Run Ruby generator tests**

```bash
ruby plugins/noc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_smoke.rb
```

Expected: all pass.

- [ ] **Step 3: Run Qt tests**

```bash
cd qt
xmake build graph_test
xmake run graph_test
xmake build plugin_test
xmake run plugin_test
xmake build projectdocument_test
xmake run projectdocument_test
xmake build topology_preset_test
xmake run topology_preset_test
```

Expected: every test prints `<target> passed`.

- [ ] **Step 4: Check worktree state**

```bash
git status --short
git diff --exit-code
```

Expected: no unstaged tracked changes. Only intentionally untracked user files may remain in the parent workspace; this implementation worktree should be clean before merge.

- [ ] **Step 5: Finish branch**

Use `superpowers:verification-before-completion`, then `superpowers:finishing-a-development-branch`.
