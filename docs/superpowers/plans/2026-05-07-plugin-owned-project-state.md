# Plugin-Owned Project State Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move plugin-owned project/global state out of `Graph` and into project/plugin services while preserving existing `.fpproj` files and generator behavior.

**Architecture:** `Graph` remains the topology model. `ProjectDocument` stores opaque plugin state records, `ProjectStateService` owns editable plugin state for the open document, and plugin adapters expose parameter metadata plus state migration without making the core app understand plugin semantics.

**Tech Stack:** C++23, Qt 6 widgets, existing xmake targets, existing JSON project reader/writer, existing command manager.

---

## Spec Validation

The spec at `docs/superpowers/specs/2026-05-07-plugin-owned-project-state-design.md` is implementation-ready for a single staged project. The plan covers each acceptance criterion:

- Known plugin state round-trip: Task 1 and Task 2.
- Missing plugin state preservation: Task 1 and Task 2.
- `Graph` free of plugin-specific IP semantics: Task 6.
- Plugin global parameters rendered through provider metadata: Task 3 and Task 4.
- Plugin edits undoable and dirty-tracked: Task 5.
- Generation and validation receive graph plus plugin-owned state: Task 6.
- Existing IP instance files migrate without data loss: Task 1, Task 2, and Task 6.

The main sequencing risk is keeping old `.fpproj` files readable while removing `Graph::ipInstance()`. The plan handles that by adding plugin state persistence first, migrating the current `ip_instances` shape into plugin state, then removing graph-owned IP state only after UI and generation use the service.

## File Structure

- Create `qt/inc/project/pluginstate.h`: shared structs for plugin state records and typed field values.
- Create `qt/src/project/pluginstate.cpp`: helpers for JSON path updates and value conversion.
- Modify `qt/inc/project/projectdocument.h`: add `ProjectPluginStateRecord` and `QVector<ProjectPluginStateRecord> pluginStates`.
- Modify `qt/src/project/projectreader.cpp`: read `plugin_state` and migrate legacy `ip_instances`.
- Modify `qt/src/project/projectwriter.cpp`: write deterministic `plugin_state` and preserve legacy `ip_instances` only during migration.
- Create `qt/inc/project/projectstateservice.h`: state service API used by UI, commands, save/load, and generation.
- Create `qt/src/project/projectstateservice.cpp`: in-memory plugin state ownership outside `Graph`.
- Create `qt/inc/plugins/pluginprojectadapter.h`: adapter interfaces and metadata structs.
- Create `qt/src/plugins/manifestpluginprojectadapter.cpp`: first adapter backed by current `plugin.json` instance parameter metadata.
- Modify `qt/inc/plugins/plugindescriptor.h`: add state schema fields and parameter section metadata.
- Modify `qt/src/plugins/pluginregistry.cpp`: parse new manifest fields and construct default adapters.
- Create `qt/inc/commands/setpluginstateparametercommand.h`.
- Create `qt/src/commands/setpluginstateparametercommand.cpp`.
- Modify `qt/inc/app/mainwindow.h` and `qt/src/app/mainwindow.cpp`: own `ProjectStateService`, wire load/save/generate/dirty tracking.
- Modify `qt/inc/panels/propertypanel.h` and `qt/src/panels/propertypanel.cpp`: render plugin state sections instead of `Graph::ipInstance()`.
- Modify `qt/inc/plugins/generatorrunner.h` and `qt/src/plugins/generatorrunner.cpp`: pass plugin state into plugin graph JSON or native adapter calls.
- Modify `qt/inc/validation/drcrunner.h` and `qt/src/validation/drcrunner.cpp`: pass plugin state into DRC.
- Modify tests: `qt/test/projectdocument_test.cpp`, `qt/test/propertypanel_test.cpp`, `qt/test/plugin_test.cpp`, `qt/test/graph_test.cpp`, `qt/test/validation_test.cpp`.
- Modify `qt/xmake.lua`: add new sources to app and affected test targets.

---

### Task 1: Persist Opaque Plugin State In ProjectDocument

**Files:**
- Create: `qt/inc/project/pluginstate.h`
- Create: `qt/src/project/pluginstate.cpp`
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/projectwriter.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing project state round-trip tests**

Add this test to `qt/test/projectdocument_test.cpp`:

```cpp
void testProjectPreservesOpaquePluginState() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64},
            {QStringLiteral("routing_algorithm"), QStringLiteral("xy")}
        }}
    };
    document.pluginStates.push_back(state);

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("plugin_state.fpproj"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(path, document);
    require(writeResult.success, "project with plugin state should write");

    const ProjectReadResult readResult = ProjectReader::readFile(path);
    require(readResult.success, "project with plugin state should read");
    require(readResult.document.pluginStates.size() == 1,
            "plugin state record should round-trip");
    const ProjectPluginStateRecord& restored = readResult.document.pluginStates.first();
    require(restored.pluginId == QStringLiteral("finepaper.ravenoc"),
            "plugin state plugin id should round-trip");
    require(restored.instanceId == QStringLiteral("ravenoc_0"),
            "plugin state instance id should round-trip");
    require(restored.schema == QStringLiteral("ravenoc-project-state-v1"),
            "plugin state schema should round-trip");
    require(restored.state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "opaque plugin state JSON should round-trip");
}
```

Add this call in `main()` after `testProjectRoundTripRestoresModulesParametersAndConnections()`:

```cpp
testProjectPreservesOpaquePluginState();
```

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
xmake -P qt -r projectdocument_test
```

Expected: compile fails because `ProjectPluginStateRecord` and `ProjectDocument::pluginStates` do not exist.

- [ ] **Step 3: Add plugin state document types**

Create `qt/inc/project/pluginstate.h`:

```cpp
// Shared project plugin state records.
#pragma once

#include <QJsonObject>
#include <QString>

struct ProjectPluginStateRecord {
    QString pluginId;
    QString instanceId;
    QString schema;
    QJsonObject state;
};
```

Update `qt/inc/project/projectdocument.h`:

```cpp
#include "project/pluginstate.h"
```

Add this field to `ProjectDocument`:

```cpp
QVector<ProjectPluginStateRecord> pluginStates;
```

- [ ] **Step 4: Read plugin_state from project JSON**

In `qt/src/project/projectreader.cpp`, after reading `plugins`, add:

```cpp
const QJsonValue pluginStateValue = root.value(QStringLiteral("plugin_state"));
if (!pluginStateValue.isUndefined() && !pluginStateValue.isArray()) {
    return failure(QStringLiteral("Project plugin_state must be an array"));
}
for (const QJsonValue& value : pluginStateValue.toArray()) {
    const QJsonObject object = value.toObject();
    ProjectPluginStateRecord state;
    state.pluginId = object.value(QStringLiteral("plugin")).toString();
    state.instanceId = object.value(QStringLiteral("instance")).toString();
    state.schema = object.value(QStringLiteral("schema")).toString();
    state.state = object.value(QStringLiteral("state")).toObject();
    document.pluginStates.push_back(state);
}
```

- [ ] **Step 5: Write plugin_state to project JSON**

In `qt/src/project/projectwriter.cpp`, before writing the graph object, add:

```cpp
QJsonArray pluginStates;
for (const ProjectPluginStateRecord& state : document.pluginStates) {
    QJsonObject object;
    object.insert(QStringLiteral("plugin"), state.pluginId);
    object.insert(QStringLiteral("instance"), state.instanceId);
    object.insert(QStringLiteral("schema"), state.schema);
    object.insert(QStringLiteral("state"), sortedObject(state.state));
    pluginStates.append(object);
}
root.insert(QStringLiteral("plugin_state"), pluginStates);
```

- [ ] **Step 6: Add xmake source/header entries**

In the `projectdocument_test` target in `qt/xmake.lua`, add:

```lua
"inc/**/pluginstate.h",
```

- [ ] **Step 7: Run test and verify pass**

Run:

```bash
xmake -P qt -r projectdocument_test
xmake run -P qt projectdocument_test
```

Expected: both commands pass and `projectdocument_test passed` is printed.

- [ ] **Step 8: Commit**

```bash
git add qt/inc/project/pluginstate.h qt/inc/project/projectdocument.h qt/src/project/projectreader.cpp qt/src/project/projectwriter.cpp qt/test/projectdocument_test.cpp qt/xmake.lua
git commit -m "feat: persist plugin project state"
```

---

### Task 2: Add ProjectStateService Outside Graph

**Files:**
- Create: `qt/inc/project/projectstateservice.h`
- Create: `qt/src/project/projectstateservice.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing service test**

Add this test to `qt/test/projectdocument_test.cpp`:

```cpp
void testProjectStateServiceUpdatesPluginStateWithoutGraph() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32}
        }}
    };
    document.pluginStates.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(service.setParameter(QStringLiteral("finepaper.ravenoc"),
                                 QStringLiteral("ravenoc_0"),
                                 QStringLiteral("global_parameters"),
                                 QStringLiteral("flit_data_width"),
                                 64),
            "plugin state parameter update should succeed");

    ProjectDocument saved = validProjectDocument();
    service.writeToDocument(saved);
    require(saved.pluginStates.size() == 1,
            "service should write one plugin state record");
    require(saved.pluginStates.first()
                .state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "service should write updated plugin parameter");
}
```

Add:

```cpp
#include "project/projectstateservice.h"
```

Call the test in `main()` after `testProjectPreservesOpaquePluginState()`:

```cpp
testProjectStateServiceUpdatesPluginStateWithoutGraph();
```

- [ ] **Step 2: Run test and verify failure**

```bash
xmake -P qt -r projectdocument_test
```

Expected: compile fails because `ProjectStateService` does not exist.

- [ ] **Step 3: Create ProjectStateService API**

Create `qt/inc/project/projectstateservice.h`:

```cpp
// ProjectStateService owns editable plugin project state outside Graph.
#pragma once

#include "project/pluginstate.h"
#include "project/projectdocument.h"

#include <QJsonValue>
#include <QVector>

class ProjectStateService {
public:
    void clear();
    void loadFromDocument(const ProjectDocument& document);
    void writeToDocument(ProjectDocument& document) const;
    const QVector<ProjectPluginStateRecord>& pluginStates() const { return m_pluginStates; }

    bool setParameter(const QString& pluginId,
                      const QString& instanceId,
                      const QString& section,
                      const QString& name,
                      const QJsonValue& value);
    QJsonValue parameter(const QString& pluginId,
                         const QString& instanceId,
                         const QString& section,
                         const QString& name) const;

private:
    QVector<ProjectPluginStateRecord> m_pluginStates;
};
```

- [ ] **Step 4: Implement ProjectStateService**

Create `qt/src/project/projectstateservice.cpp`:

```cpp
// ProjectStateService stores plugin-owned project state outside Graph.
#include "project/projectstateservice.h"

void ProjectStateService::clear() {
    m_pluginStates.clear();
}

void ProjectStateService::loadFromDocument(const ProjectDocument& document) {
    m_pluginStates = document.pluginStates;
}

void ProjectStateService::writeToDocument(ProjectDocument& document) const {
    document.pluginStates = m_pluginStates;
}

bool ProjectStateService::setParameter(const QString& pluginId,
                                       const QString& instanceId,
                                       const QString& section,
                                       const QString& name,
                                       const QJsonValue& value) {
    for (ProjectPluginStateRecord& record : m_pluginStates) {
        if (record.pluginId != pluginId || record.instanceId != instanceId) {
            continue;
        }

        QJsonObject sectionObject = record.state.value(section).toObject();
        sectionObject.insert(name, value);
        record.state.insert(section, sectionObject);
        return true;
    }
    return false;
}

QJsonValue ProjectStateService::parameter(const QString& pluginId,
                                          const QString& instanceId,
                                          const QString& section,
                                          const QString& name) const {
    for (const ProjectPluginStateRecord& record : m_pluginStates) {
        if (record.pluginId == pluginId && record.instanceId == instanceId) {
            return record.state.value(section).toObject().value(name);
        }
    }
    return {};
}
```

- [ ] **Step 5: Add xmake entries**

In `projectdocument_test` target:

```lua
"src/**/projectstateservice.cpp",
"inc/**/projectstateservice.h",
```

- [ ] **Step 6: Run test and verify pass**

```bash
xmake -P qt -r projectdocument_test
xmake run -P qt projectdocument_test
```

Expected: `projectdocument_test passed`.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/project/projectstateservice.h qt/src/project/projectstateservice.cpp qt/test/projectdocument_test.cpp qt/xmake.lua
git commit -m "feat: add plugin project state service"
```

---

### Task 3: Add Plugin Parameter Provider Adapter

**Files:**
- Create: `qt/inc/plugins/pluginprojectadapter.h`
- Create: `qt/src/plugins/manifestpluginprojectadapter.cpp`
- Modify: `qt/inc/plugins/plugindescriptor.h`
- Modify: `qt/src/plugins/pluginregistry.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing adapter test**

Add to `qt/test/plugin_test.cpp`:

```cpp
void testManifestPluginAdapterExposesGlobalParameterSection() {
    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.ravenoc");
    plugin.name = QStringLiteral("RaveNoC");
    PluginInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    width.configurable = true;
    plugin.instanceParameters.insert(width.name, width);

    ManifestPluginProjectAdapter adapter(plugin);
    const QVector<PluginParameterSection> sections = adapter.parameterSections();
    require(sections.size() == 1, "adapter should expose one global parameter section");
    require(sections.first().pluginId == QStringLiteral("finepaper.ravenoc"),
            "section should retain plugin id");
    require(sections.first().fields.size() == 1,
            "section should expose one field");
    require(sections.first().fields.first().name == QStringLiteral("flit_data_width"),
            "section field should match manifest parameter");
}
```

Add:

```cpp
#include "plugins/pluginprojectadapter.h"
```

Call it in `main()` after existing plugin registry tests:

```cpp
testManifestPluginAdapterExposesGlobalParameterSection();
```

- [ ] **Step 2: Run test and verify failure**

```bash
xmake -P qt -r plugin_test
```

Expected: compile fails because `ManifestPluginProjectAdapter` and metadata structs do not exist.

- [ ] **Step 3: Create adapter metadata**

Create `qt/inc/plugins/pluginprojectadapter.h`:

```cpp
// Plugin project adapters expose plugin-owned project state to the core UI.
#pragma once

#include "graph/parameter.h"
#include "plugins/plugindescriptor.h"

#include <QString>
#include <QVector>

struct PluginParameterField {
    QString name;
    QString label;
    QString description;
    QString type;
    Parameter::Value defaultValue = QString();
    QVector<PluginInstanceParameterChoice> choices;
    bool configurable = true;
};

struct PluginParameterSection {
    QString pluginId;
    QString instanceId;
    QString id;
    QString label;
    bool expandedByDefault = true;
    QVector<PluginParameterField> fields;
};

class IPluginProjectAdapter {
public:
    virtual ~IPluginProjectAdapter() = default;
    virtual QVector<PluginParameterSection> parameterSections() const = 0;
};

class ManifestPluginProjectAdapter final : public IPluginProjectAdapter {
public:
    explicit ManifestPluginProjectAdapter(PluginDescriptor plugin);
    QVector<PluginParameterSection> parameterSections() const override;

private:
    PluginDescriptor m_plugin;
};
```

- [ ] **Step 4: Implement manifest adapter**

Create `qt/src/plugins/manifestpluginprojectadapter.cpp`:

```cpp
// ManifestPluginProjectAdapter exposes plugin.json instance_parameters as project parameters.
#include "plugins/pluginprojectadapter.h"

ManifestPluginProjectAdapter::ManifestPluginProjectAdapter(PluginDescriptor plugin)
    : m_plugin(std::move(plugin)) {}

QVector<PluginParameterSection> ManifestPluginProjectAdapter::parameterSections() const {
    PluginParameterSection section;
    section.pluginId = m_plugin.id;
    section.instanceId = m_plugin.id.section(QLatin1Char('.'), -1) + QStringLiteral("_0");
    section.id = QStringLiteral("global_parameters");
    section.label = m_plugin.name.isEmpty() ? m_plugin.id : m_plugin.name;
    section.expandedByDefault = true;

    QStringList names = m_plugin.instanceParameters.keys();
    names.sort();
    for (const QString& name : names) {
        const PluginInstanceParameterDescriptor& descriptor = m_plugin.instanceParameters.value(name);
        PluginParameterField field;
        field.name = descriptor.name;
        field.label = descriptor.label.isEmpty() ? descriptor.name : descriptor.label;
        field.description = descriptor.description;
        field.type = descriptor.type;
        field.defaultValue = descriptor.defaultValue;
        field.choices = descriptor.choices;
        field.configurable = descriptor.configurable;
        section.fields.push_back(field);
    }

    return section.fields.isEmpty() ? QVector<PluginParameterSection>{} : QVector<PluginParameterSection>{section};
}
```

- [ ] **Step 5: Add xmake entries**

In `plugin_test` target:

```lua
"src/**/manifestpluginprojectadapter.cpp",
"inc/**/pluginprojectadapter.h",
```

- [ ] **Step 6: Run test and verify pass**

```bash
xmake -P qt -r plugin_test
xmake run -P qt plugin_test
```

Expected: `plugin_test passed`.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/plugins/pluginprojectadapter.h qt/src/plugins/manifestpluginprojectadapter.cpp qt/test/plugin_test.cpp qt/xmake.lua
git commit -m "feat: expose plugin project parameter metadata"
```

---

### Task 4: Move Global Parameter UI To ProjectStateService

**Files:**
- Modify: `qt/inc/panels/propertypanel.h`
- Modify: `qt/src/panels/propertypanel.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/propertypanel_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing property panel test**

Update `qt/test/propertypanel_test.cpp` to construct a `ProjectStateService` and `ManifestPluginProjectAdapter`, then verify the panel reads state without `Graph::configureIpInstance()`:

```cpp
void testUnselectedPanelShowsPluginProjectParameters() {
    Graph graph;
    ProjectStateService stateService;
    ProjectDocument document;
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32}
        }}
    };
    document.pluginStates.push_back(state);
    stateService.loadFromDocument(document);

    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.ravenoc");
    plugin.name = QStringLiteral("RaveNoC");
    PluginInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    plugin.instanceParameters.insert(width.name, width);
    ManifestPluginProjectAdapter adapter(plugin);

    CommandManager commandManager;
    PropertyPanel panel(&graph, &stateService, {&adapter}, &commandManager);
    panel.setSelectedModule(QString());

    require(hasLabel(panel, QStringLiteral("RaveNoC")),
            "property panel should show plugin parameter section");
    require(hasLabel(panel, QStringLiteral("Flit data width")),
            "property panel should show plugin parameter field");
}
```

- [ ] **Step 2: Run test and verify failure**

```bash
xmake -P qt -r propertypanel_test
```

Expected: compile fails because `PropertyPanel` does not accept project state service or adapters.

- [ ] **Step 3: Update PropertyPanel constructor**

In `qt/inc/panels/propertypanel.h`, add forward declarations:

```cpp
class ProjectStateService;
class IPluginProjectAdapter;
```

Replace constructor with:

```cpp
PropertyPanel(Graph* graph,
              ProjectStateService* stateService,
              QVector<IPluginProjectAdapter*> pluginAdapters,
              CommandManager* commandManager,
              QWidget* parent = nullptr);
```

Add members:

```cpp
ProjectStateService* m_stateService;
QVector<IPluginProjectAdapter*> m_pluginAdapters;
```

- [ ] **Step 4: Render plugin state sections when no module is selected**

In `qt/src/panels/propertypanel.cpp`, replace the no-selected-module IP block with:

```cpp
if (!m_selectedModule) {
    for (const IPluginProjectAdapter* adapter : m_pluginAdapters) {
        for (const PluginParameterSection& section : adapter->parameterSections()) {
            auto* header = new QLabel(section.label, this);
            QFont font = header->font();
            font.setBold(true);
            header->setFont(font);
            m_formLayout->addRow(header);

            for (const PluginParameterField& field : section.fields) {
                const QJsonValue stored = m_stateService->parameter(
                    section.pluginId, section.instanceId, section.id, field.name);
                QWidget* widget = createPluginParameterWidget(section, field, stored);
                if (!widget) {
                    continue;
                }
                QLabel* rowLabel = new QLabel(field.label, this);
                rowLabel->setToolTip(field.description);
                widget->setToolTip(field.description);
                m_formLayout->addRow(rowLabel, widget);
                m_ipParameterWidgets.insert(section.pluginId + QStringLiteral("/") + field.name, widget);
            }
        }
    }
    return;
}
```

Add a private helper in the same file:

```cpp
QWidget* PropertyPanel::createPluginParameterWidget(const PluginParameterSection& section,
                                                    const PluginParameterField& field,
                                                    const QJsonValue& storedValue) {
    if (field.type == QStringLiteral("int")) {
        auto* spinBox = new QSpinBox(this);
        spinBox->setRange(INT_MIN, INT_MAX);
        spinBox->setValue(storedValue.toInt(std::get<int>(field.defaultValue)));
        spinBox->setEnabled(field.configurable);
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, section, field](int value) {
            auto command = std::make_unique<SetPluginStateParameterCommand>(
                m_stateService, section.pluginId, section.instanceId, section.id, field.name, value);
            m_commandManager->executeCommand(std::move(command));
        });
        return spinBox;
    }
    return nullptr;
}
```

- [ ] **Step 5: Wire MainWindow ownership**

In `qt/inc/app/mainwindow.h`, add:

```cpp
class ProjectStateService;
class IPluginProjectAdapter;
```

Add members:

```cpp
std::unique_ptr<ProjectStateService> m_projectStateService;
QVector<std::unique_ptr<IPluginProjectAdapter>> m_pluginProjectAdapters;
```

In `MainWindow` constructor, initialize:

```cpp
m_projectStateService(std::make_unique<ProjectStateService>()),
```

In `setupPanels()`, build adapter pointer list and pass it:

```cpp
QVector<IPluginProjectAdapter*> adapters;
for (const auto& adapter : m_pluginProjectAdapters) {
    adapters.push_back(adapter.get());
}
m_propertyPanel = new PropertyPanel(m_graph, m_projectStateService.get(), adapters, m_commandManager.get(), this);
```

- [ ] **Step 6: Run property panel test**

```bash
xmake -P qt -r propertypanel_test
xmake run -P qt propertypanel_test
```

Expected: `propertypanel_test passed`.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/panels/propertypanel.h qt/src/panels/propertypanel.cpp qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp qt/test/propertypanel_test.cpp qt/xmake.lua
git commit -m "feat: render plugin project parameters"
```

---

### Task 5: Add Undoable Plugin State Parameter Command

**Files:**
- Create: `qt/inc/commands/setpluginstateparametercommand.h`
- Create: `qt/src/commands/setpluginstateparametercommand.cpp`
- Modify: `qt/test/propertypanel_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing undo test**

Extend `testUnselectedPanelShowsPluginProjectParameters()`:

```cpp
QSpinBox* spinBox = panel.findChild<QSpinBox*>();
require(spinBox != nullptr, "plugin int parameter should use spin box");
spinBox->setValue(64);
require(commandManager.currentStateId() == 1,
        "plugin parameter edit should enter command history");
require(stateService.parameter(QStringLiteral("finepaper.ravenoc"),
                               QStringLiteral("ravenoc_0"),
                               QStringLiteral("global_parameters"),
                               QStringLiteral("flit_data_width")).toInt() == 64,
        "plugin parameter edit should update state service");

commandManager.undo();
require(stateService.parameter(QStringLiteral("finepaper.ravenoc"),
                               QStringLiteral("ravenoc_0"),
                               QStringLiteral("global_parameters"),
                               QStringLiteral("flit_data_width")).toInt() == 32,
        "undo should restore previous plugin parameter value");
```

- [ ] **Step 2: Run test and verify failure**

```bash
xmake -P qt -r propertypanel_test
```

Expected: compile fails because `SetPluginStateParameterCommand` does not exist.

- [ ] **Step 3: Add command header**

Create `qt/inc/commands/setpluginstateparametercommand.h`:

```cpp
// SetPluginStateParameterCommand changes one plugin-owned parameter with undo support.
#pragma once

#include "commands/command.h"
#include "project/projectstateservice.h"

#include <QJsonValue>
#include <QString>

class SetPluginStateParameterCommand final : public Command {
public:
    SetPluginStateParameterCommand(ProjectStateService* stateService,
                                   QString pluginId,
                                   QString instanceId,
                                   QString section,
                                   QString name,
                                   QJsonValue newValue);
    void execute() override;
    void undo() override;

private:
    ProjectStateService* m_stateService;
    QString m_pluginId;
    QString m_instanceId;
    QString m_section;
    QString m_name;
    QJsonValue m_newValue;
    QJsonValue m_oldValue;
};
```

- [ ] **Step 4: Add command implementation**

Create `qt/src/commands/setpluginstateparametercommand.cpp`:

```cpp
// SetPluginStateParameterCommand applies undoable plugin state edits.
#include "commands/setpluginstateparametercommand.h"

SetPluginStateParameterCommand::SetPluginStateParameterCommand(ProjectStateService* stateService,
                                                               QString pluginId,
                                                               QString instanceId,
                                                               QString section,
                                                               QString name,
                                                               QJsonValue newValue)
    : m_stateService(stateService),
      m_pluginId(std::move(pluginId)),
      m_instanceId(std::move(instanceId)),
      m_section(std::move(section)),
      m_name(std::move(name)),
      m_newValue(std::move(newValue)) {}

void SetPluginStateParameterCommand::execute() {
    m_oldValue = m_stateService->parameter(m_pluginId, m_instanceId, m_section, m_name);
    if (m_oldValue == m_newValue) {
        return;
    }
    if (m_stateService->setParameter(m_pluginId, m_instanceId, m_section, m_name, m_newValue)) {
        m_executed = true;
    }
}

void SetPluginStateParameterCommand::undo() {
    m_stateService->setParameter(m_pluginId, m_instanceId, m_section, m_name, m_oldValue);
}
```

- [ ] **Step 5: Add xmake entries**

In the app and `propertypanel_test` targets:

```lua
add_files("src/commands/setpluginstateparametercommand.cpp")
add_headerfiles("inc/commands/setpluginstateparametercommand.h")
```

- [ ] **Step 6: Run test and verify pass**

```bash
xmake -P qt -r propertypanel_test
xmake run -P qt propertypanel_test
```

Expected: `propertypanel_test passed`.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/commands/setpluginstateparametercommand.h qt/src/commands/setpluginstateparametercommand.cpp qt/test/propertypanel_test.cpp qt/xmake.lua
git commit -m "feat: make plugin state edits undoable"
```

---

### Task 6: Migrate Current IP Instance Path To Plugin State

**Files:**
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/src/plugins/generatorrunner.cpp`
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/test/validation_test.cpp`

- [ ] **Step 1: Write failing migration test**

Add to `qt/test/projectdocument_test.cpp`:

```cpp
void testLegacyIpInstanceMigratesToPluginState() {
    ProjectDocument document = validProjectDocument();
    document.ipInstances.push_back(ProjectIpInstanceRecord{
        QStringLiteral("ravenoc_0"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("noc"),
        QStringLiteral("RaveNoC"),
        QJsonObject{{QStringLiteral("flit_data_width"), 64}}
    });

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("legacy_ip.fpproj"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(path, document);
    require(writeResult.success, "legacy ip project should write");

    const ProjectReadResult readResult = ProjectReader::readFile(path);
    require(readResult.success, "legacy ip project should read");
    require(readResult.document.pluginStates.size() == 1,
            "legacy IP instance should migrate to plugin state");
    require(readResult.document.pluginStates.first().pluginId == QStringLiteral("finepaper.ravenoc"),
            "migrated plugin state should retain plugin id");
    require(readResult.document.pluginStates.first()
                .state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "migrated plugin state should retain parameter value");
}
```

- [ ] **Step 2: Run test and verify failure**

```bash
xmake -P qt -r projectdocument_test
```

Expected: test fails because legacy `ip_instances` are not migrated to `pluginStates`.

- [ ] **Step 3: Migrate legacy IP instance records while reading**

In `qt/src/project/projectreader.cpp`, after reading `ip_instances`, add:

```cpp
if (document.pluginStates.isEmpty()) {
    for (const ProjectIpInstanceRecord& ipInstance : document.ipInstances) {
        ProjectPluginStateRecord state;
        state.pluginId = ipInstance.pluginId;
        state.instanceId = ipInstance.id;
        state.schema = ipInstance.pluginId + QStringLiteral("-project-state-v1");
        state.state = QJsonObject{
            {QStringLiteral("kind"), ipInstance.kind},
            {QStringLiteral("type"), ipInstance.type},
            {QStringLiteral("global_parameters"), ipInstance.parameters}
        };
        document.pluginStates.push_back(state);
    }
}
```

- [ ] **Step 4: Stop populating Graph IP state during project load**

In `qt/src/project/graphprojectserializer.cpp`, remove `graph.configureIpInstance(...)` from `populateGraph()`. Keep module and connection load unchanged.

- [ ] **Step 5: Update generation and validation to accept ProjectStateService**

Change generator inputs by adding state JSON to plugin graph JSON at the MainWindow call site:

```cpp
QJsonDocument graphDocument = m_graph->toJsonDocument(designName, exportFlavor);
QJsonObject root = graphDocument.object();
root.insert(QStringLiteral("plugin_state"), pluginStateArray(m_projectStateService->pluginStates()));
jsonFile.write(QJsonDocument(root).toJson());
```

Implement `pluginStateArray()` in `generationartifacts.cpp`:

```cpp
QJsonArray pluginStateArray(const QVector<ProjectPluginStateRecord>& records) {
    QJsonArray array;
    for (const ProjectPluginStateRecord& record : records) {
        QJsonObject object;
        object.insert(QStringLiteral("plugin"), record.pluginId);
        object.insert(QStringLiteral("instance"), record.instanceId);
        object.insert(QStringLiteral("schema"), record.schema);
        object.insert(QStringLiteral("state"), record.state);
        array.append(object);
    }
    return array;
}
```

- [ ] **Step 6: Remove Graph IP instance API after all callers move**

Remove from `qt/inc/graph/graph.h`:

```cpp
const std::optional<GraphIpInstance>& ipInstance() const;
void configureIpInstance(...);
bool setIpInstanceParameter(...);
```

Remove corresponding implementations and `m_ipInstance` from `qt/src/graph/graph.cpp`.

- [ ] **Step 7: Run focused tests**

```bash
xmake -P qt -r projectdocument_test
xmake run -P qt projectdocument_test
xmake -P qt -r propertypanel_test
xmake run -P qt propertypanel_test
xmake -P qt -r graph_test
xmake run -P qt graph_test
xmake -P qt -r validation_test
xmake run -P qt validation_test
```

Expected: all listed tests pass.

- [ ] **Step 8: Build app**

```bash
xmake -P qt -r qt
```

Expected: `build ok`.

- [ ] **Step 9: Commit**

```bash
git add qt/src/project/graphprojectserializer.cpp qt/src/project/projectreader.cpp qt/src/app/mainwindow.cpp qt/src/validation/drcrunner.cpp qt/src/plugins/generatorrunner.cpp qt/inc/graph/graph.h qt/src/graph/graph.cpp qt/test/projectdocument_test.cpp qt/test/graph_test.cpp qt/test/validation_test.cpp
git commit -m "refactor: move IP project state out of graph"
```

---

## Final Verification

Run:

```bash
xmake -P qt -r qt
xmake -P qt -g test
xmake run -P qt projectdocument_test
xmake run -P qt propertypanel_test
xmake run -P qt plugin_test
xmake run -P qt graph_test
xmake run -P qt validation_test
```

Expected:

- App target prints `build ok`.
- Test group prints `build ok`.
- Each `xmake run` command prints `<target> passed`.

Manual smoke test:

1. Launch the app with `xmake -P qt run qt`.
2. Create or open a RaveNoC project.
3. Confirm global plugin parameters appear with no module selected.
4. Edit a global parameter.
5. Confirm Undo restores the old value.
6. Save, reopen, and confirm the value persists.
7. Generate Verilog and confirm the output directory contains `.json`, `.fpproj`, and generated Verilog.
