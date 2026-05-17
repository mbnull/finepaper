// Ipcraft manifest tests for strict package loading and registry behavior.
#include "ipcraft/ipcraftmanifestreader.h"
#include "ipcraft/ipcraftregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "failed to open test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

QString createView(QDir& packageRoot,
                   const QString& module = QStringLiteral("Module"),
                   const QString& interfaceId = QStringLiteral("bus")) {
    require(packageRoot.mkpath(QStringLiteral("views")), "failed to create views directory");
    const QString relativePath = QStringLiteral("views/") + module + QStringLiteral(".xml");
    const QByteArray xml = QStringLiteral(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<module-view schema="v1" module="%1">
  <graphics layout="endpoint" />
  <anchors>
    <anchor ref="%2" x="0" y="0" normal_x="1" normal_y="0" />
  </anchors>
</module-view>
)xml").arg(module, interfaceId).toUtf8();
    writeFile(packageRoot.filePath(relativePath), xml);
    return relativePath;
}

QString createViewXml(QDir& packageRoot,
                      const QString& module,
                      const QByteArray& xml) {
    require(packageRoot.mkpath(QStringLiteral("views")), "failed to create views directory");
    const QString relativePath = QStringLiteral("views/") + module + QStringLiteral(".xml");
    writeFile(packageRoot.filePath(relativePath), xml);
    return relativePath;
}

QByteArray minimalManifest(
    const QString& viewPath = QStringLiteral("views/Module.xml"),
    const QString& packageId = QStringLiteral("org.example.demo"),
    const QString& validateExecutablePath = QStringLiteral("tools/validate")) {
    return QStringLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "%1",
  "name": "Demo",
  "version": "1.0.0",
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "name": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["initiator"],
          "accepts": [
            { "class": "demo_link", "role": "initiator" }
          ],
          "multi_connection": false
        }
      ]
    }
  ],
  "views": [
    { "module": "Module", "file": "%2" }
  ],
  "commands": {
    "validate": {
      "executable": "%3",
      "input_schema": "ipcraft.noc.project.v1",
      "args": ["-i", "{input}"]
    },
    "generate": {
      "executable": "tools/generate",
      "input_schema": "ipcraft.noc.project.v1",
      "args": ["-i", "{input}", "-o", "{output}"]
    }
  }
})json").arg(packageId, viewPath, validateExecutablePath).toUtf8();
}

bool diagnosticsContain(const QVector<IpcraftDiagnostic>& diagnostics, const QString& text) {
    for (const IpcraftDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.path.contains(text) || diagnostic.message.contains(text)) {
            return true;
        }
    }
    return false;
}

void testLoadsMinimalPackageManifest() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")), minimalManifest());

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(result.ok, "minimal manifest should load");
    require(result.manifest.id == QStringLiteral("org.example.demo"), "manifest id should load");
    require(result.manifest.modules.size() == 1, "one module should load");
    require(result.manifest.modules.first().interfaces.size() == 1, "one interface should load");
    require(result.manifest.connectionClasses.size() == 1, "one connection class should load");
    require(result.manifest.views.size() == 1, "one view should load");
    require(QFileInfo(result.manifest.views.first().resolvedFilePath).isAbsolute(),
            "view path should be package-root resolved");
    require(result.manifest.commands.contains(QStringLiteral("validate")),
            "validate command should load");
    require(QFileInfo(result.manifest.commands.value(QStringLiteral("validate")).resolvedExecutablePath).isAbsolute(),
            "command executable path should be package-root resolved");
}

void testRejectsDuplicateJsonKeys() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.first",
  "id": "org.example.second",
  "name": "Demo",
  "version": "1.0.0",
  "modules": [],
  "views": [],
  "connection_classes": []
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "duplicate JSON keys should be rejected");
    require(!result.diagnostics.isEmpty(), "duplicate key rejection should emit diagnostics");
    require(result.diagnostics.first().message.contains(QStringLiteral("Duplicate JSON key")),
            "duplicate key diagnostic should be explicit");
}

void testRejectsDuplicateJsonKeysAfterUnicodeDecoding() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);

    QByteArray manifest = QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "\u00e9": "escaped",
  ")json");
    manifest += QByteArray::fromHex("c3a9");
    manifest += QByteArrayLiteral(R"json(": "raw",
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "name": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["initiator"],
          "accepts": [
            { "class": "demo_link", "role": "initiator" }
          ]
        }
      ]
    }
  ],
  "views": [{ "module": "Module", "file": "views/Module.xml" }]
})json");
    writeFile(root.filePath(QStringLiteral("ipcraft.json")), manifest);

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "escaped Unicode and raw UTF-8 duplicate JSON keys should be rejected");
    require(!result.diagnostics.isEmpty(), "Unicode duplicate key rejection should emit diagnostics");
    require(result.diagnostics.first().message.contains(QStringLiteral("Duplicate JSON key")),
            "Unicode duplicate key diagnostic should be explicit");
}

void testRejectsMissingCommandInputSchema() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["initiator"],
          "accepts": [{ "class": "demo_link", "role": "initiator" }]
        }
      ]
    }
  ],
  "views": [{ "module": "Module", "file": "views/Module.xml" }],
  "commands": {
    "validate": { "executable": "tools/validate" }
  }
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "commands without input_schema should be rejected");
    require(!result.diagnostics.isEmpty(), "missing input_schema should emit diagnostics");
    require(result.diagnostics.first().message.contains(QStringLiteral("input_schema")),
            "diagnostic should mention input_schema");
}

QByteArray manifestWithGenerateCommand(const QByteArray& generateCommand) {
    QByteArray manifest = minimalManifest();
    const QByteArray oldGenerateCommand = QByteArrayLiteral(R"json("generate": {
      "executable": "tools/generate",
      "input_schema": "ipcraft.noc.project.v1",
      "args": ["-i", "{input}", "-o", "{output}"]
    })json");
    require(manifest.contains(oldGenerateCommand),
            "test manifest generate command should be present");
    manifest.replace(oldGenerateCommand, generateCommand);
    return manifest;
}

void testLoadsFrameworkToolGenerateCommand() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              manifestWithGenerateCommand(QByteArrayLiteral(R"json("generate": {
      "framework_tool": "ipcraft-generate",
      "input_schema": "ipcraft.noc.project.v1",
      "args": ["--manifest", "{manifest}", "--input", "{input}", "--output", "{output}"]
    })json")));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(result.ok, "framework_tool generate command should load");
    const IpcraftCommandDescriptor command =
        result.manifest.commands.value(QStringLiteral("generate"));
    require(command.frameworkTool == QStringLiteral("ipcraft-generate"),
            "framework_tool should be preserved on the command descriptor");
    require(command.executablePath.isEmpty(),
            "framework_tool command should not populate package executable path");
    require(command.resolvedExecutablePath.isEmpty(),
            "framework_tool command should not resolve through package root");
    require(command.args.contains(QStringLiteral("{manifest}")),
            "framework_tool command args should preserve manifest placeholder");
}

void testRejectsCommandWithExecutableAndFrameworkTool() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              manifestWithGenerateCommand(QByteArrayLiteral(R"json("generate": {
      "executable": "tools/generate",
      "framework_tool": "ipcraft-generate",
      "input_schema": "ipcraft.noc.project.v1"
    })json")));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "commands with executable and framework_tool should be rejected");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("commands.generate.framework_tool")),
            "both-fields diagnostic should mention framework_tool");
}

void testRejectsCommandWithoutExecutableOrFrameworkTool() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              manifestWithGenerateCommand(QByteArrayLiteral(R"json("generate": {
      "input_schema": "ipcraft.noc.project.v1"
    })json")));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "commands without executable or framework_tool should be rejected");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("commands.generate")),
            "missing command target diagnostic should mention command path");
}

void testRejectsUnknownFrameworkToolCommand() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              manifestWithGenerateCommand(QByteArrayLiteral(R"json("generate": {
      "framework_tool": "unknown-tool",
      "input_schema": "ipcraft.noc.project.v1"
    })json")));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "unknown framework_tool should be rejected");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("unknown-tool")),
            "unknown framework_tool diagnostic should mention the tool name");
}

void testRejectsUnknownRequiredShapeFields() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["initiator"],
          "accepts": [{ "class": "demo_link", "role": "initiator" }]
        }
      ]
    }
  ],
  "views": [
    {
      "module": "Module",
      "file": "views/Module.xml",
      "required_shape": { "unknown_anchor_family": true }
    }
  ]
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "unknown required_shape fields should be rejected");
    require(!result.diagnostics.isEmpty(), "unknown required_shape should emit diagnostics");
    require(result.diagnostics.first().message.contains(QStringLiteral("required_shape")),
            "diagnostic should mention required_shape");
}

void testRejectsDottedUnknownRequiredShapeFields() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["initiator"],
          "accepts": [{ "class": "demo_link", "role": "initiator" }]
        }
      ]
    }
  ],
  "views": [
    {
      "module": "Module",
      "file": "views/Module.xml",
      "required_shape": { "bad.field": true }
    }
  ]
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "dotted unknown required_shape fields should be rejected");
    require(!result.diagnostics.isEmpty(), "dotted unknown required_shape should emit diagnostics");
    require(result.diagnostics.first().message.contains(QStringLiteral("required_shape")),
            "diagnostic should mention required_shape");
}

void testRejectsNonStringArrayEntries() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    QByteArray manifest = minimalManifest();
    manifest.replace(QByteArrayLiteral(R"json("roles": ["initiator", "target"])json"),
                     QByteArrayLiteral(R"json("roles": ["initiator", 7])json"));
    writeFile(root.filePath(QStringLiteral("ipcraft.json")), manifest);

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "string arrays should reject non-string entries");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("roles")),
            "string array diagnostic should mention the invalid field");
}

void testLoadsConnectionClassIpxactAndAttachMetadata() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "extensions": {
    "noc.v1": { "enabled": true }
  },
  "connection_classes": [
    {
      "id": "chi_node_interface",
      "roles": ["node", "interconnect"],
      "symmetric": false,
      "ipxact": {
        "node": "initiator",
        "interconnect": "target"
      }
    }
  ],
  "modules": [
    {
      "id": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["chi_interconnect"],
          "accepts": [{ "class": "chi_node_interface", "role": "interconnect" }]
        }
      ]
    },
    {
      "id": "Agent",
      "graph_role": "attached",
      "attach": { "hosts": ["Module"], "zone": "agent" },
      "interfaces": [
        {
          "id": "chi",
          "modes": ["chi_requester_node"],
          "accepts": [{ "class": "chi_node_interface", "role": "node" }]
        }
      ]
    }
  ],
  "views": [{ "module": "Module", "file": "views/Module.xml" }]
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(result.ok, "manifest with connection class ipxact and attach metadata should load");
    require(result.manifest.connectionClasses.first().ipxact.value(QStringLiteral("node")).toString() ==
                QStringLiteral("initiator"),
            "connection class IP-XACT role mapping should be preserved");
    require(result.manifest.modules.at(1).attach.value(QStringLiteral("zone")).toString() ==
                QStringLiteral("agent"),
            "module attach metadata should be preserved for attachment-zone validation");
}

void testManifestReaderParsesDisplayTopologyAndGeneration() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root, QStringLiteral("Tile"), QStringLiteral("link_out"));
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.display",
  "name": "Display",
  "version": "1.0.0",
  "connection_classes": [
    { "id": "mesh_link", "roles": ["source", "sink"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Tile",
      "name": "Tile",
      "display": {
        "label_parameter": "display_name",
        "short_label_parameter": "short_name"
      },
      "interfaces": [
        {
          "id": "link_out",
          "label": "Out",
          "modes": ["initiator"],
          "accepts": [{ "class": "mesh_link", "role": "source" }],
          "topology": {
            "side": "east",
            "opposite": "link_in",
            "role": "source"
          }
        },
        {
          "id": "link_in",
          "label": "In",
          "modes": ["target"],
          "accepts": [{ "class": "mesh_link", "role": "sink" }]
        }
      ]
    }
  ],
  "views": [
    { "module": "Tile", "file": "views/Tile.xml" }
  ],
  "generation": {
    "engine": "ipcraft.common.v1",
    "module_mappings": { "Tile": "tile" }
  }
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(result.ok, "manifest with display/topology/generation metadata should parse");
    const IpcraftModuleDescriptor* parsed = result.manifest.module(QStringLiteral("Tile"));
    require(parsed != nullptr, "Tile module should parse");
    require(parsed->displayLabelParameter == QStringLiteral("display_name"),
            "display label binding should parse");
    require(parsed->shortLabelParameter == QStringLiteral("short_name"),
            "short display label binding should parse");
    const IpcraftInterfaceDescriptor* output =
        parsed->interfaceDescriptor(QStringLiteral("link_out"));
    require(output != nullptr, "link_out interface should parse");
    require(output->topology.side == QStringLiteral("east"),
            "topology side should parse");
    require(output->topology.oppositeInterfaceId == QStringLiteral("link_in"),
            "topology opposite interface should parse");
    require(output->topology.role == QStringLiteral("source"),
            "topology role should parse");
    require(result.manifest.generation.engine == QStringLiteral("ipcraft.common.v1"),
            "generation engine should parse");
    require(result.manifest.generation.metadata.value(QStringLiteral("module_mappings"))
                .toObject()
                .value(QStringLiteral("Tile"))
                .toString() == QStringLiteral("tile"),
            "generation metadata should parse");
}

void testRejectsWrongPluginFieldTypes() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "plugin": {
    "id": "org.example.demo.plugin",
    "library": 7,
    "entry": "DemoPlugin"
  },
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["initiator"],
          "accepts": [{ "class": "demo_link", "role": "initiator" }]
        }
      ]
    }
  ],
  "views": [{ "module": "Module", "file": "views/Module.xml" }]
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "plugin fields should reject non-string values");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("plugin.library")),
            "plugin field diagnostic should mention the invalid field");
}

void testRejectsNonObjectExtensionValues() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    QByteArray manifest = minimalManifest();
    manifest.replace(QByteArrayLiteral(R"json("connection_classes")json"),
                     QByteArrayLiteral(R"json("extensions": { "noc.v1": true },
  "connection_classes")json"));
    writeFile(root.filePath(QStringLiteral("ipcraft.json")), manifest);

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "extensions entries should reject non-object values");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("extensions.noc.v1")),
            "extension diagnostic should mention the invalid extension id");
}

void testRejectsWrongTopLevelCollectionTypes() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "connection_classes": [],
  "modules": {},
  "views": [],
  "commands": [],
  "topologies": {}
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "top-level arrays and maps should reject wrong JSON types");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("modules")),
            "top-level type diagnostic should mention the invalid field");
}

void testRejectsAbsoluteViewPath() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    const QString viewPath = createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              minimalManifest(root.filePath(viewPath)));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "absolute view paths should be rejected");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("views.Module.file")),
            "absolute view path diagnostic should mention the view file");
}

void testRejectsTraversingViewPath() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir workspace(temp.path());
    require(workspace.mkpath(QStringLiteral("package")), "failed to create package directory");
    require(workspace.mkpath(QStringLiteral("outside")), "failed to create outside directory");

    QDir packageRoot(workspace.filePath(QStringLiteral("package")));
    QDir outsideRoot(workspace.filePath(QStringLiteral("outside")));
    createView(outsideRoot);
    writeFile(packageRoot.filePath(QStringLiteral("ipcraft.json")),
              minimalManifest(QStringLiteral("../outside/views/Module.xml")));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(packageRoot.absolutePath());

    require(!result.ok, "view paths using parent traversal should be rejected");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("views.Module.file")),
            "traversing view path diagnostic should mention the view file");
}

void testRejectsAbsoluteCommandExecutable() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              minimalManifest(QStringLiteral("views/Module.xml"),
                              QStringLiteral("org.example.demo"),
                              QStringLiteral("/bin/echo")));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "absolute command executable paths should be rejected");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("commands.validate.executable")),
            "absolute command path diagnostic should mention the command executable");
}

void testRejectsTraversingCommandExecutable() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              minimalManifest(QStringLiteral("views/Module.xml"),
                              QStringLiteral("org.example.demo"),
                              QStringLiteral("../tools/validate")));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(!result.ok, "command executable paths using parent traversal should be rejected");
    require(diagnosticsContain(result.diagnostics, QStringLiteral("commands.validate.executable")),
            "traversing command path diagnostic should mention the command executable");
}

void testRejectsPartialPackageRegistration() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root, QStringLiteral("Module"), QStringLiteral("missing_interface"));
    writeFile(root.filePath(QStringLiteral("ipcraft.json")), minimalManifest());

    IpcraftRegistry registry;
    const bool loaded = registry.loadPackageRoots({temp.path()});

    require(!loaded, "package with invalid view XML should fail registry load");
    require(registry.packages().isEmpty(), "failed package should not be partially registered");
    require(registry.package(QStringLiteral("org.example.demo")) == nullptr,
            "failed package id should not be registered");
    require(!registry.diagnostics().isEmpty(), "registry should expose diagnostics for failed package");
}

void testRejectsRegistryPackageWithInvalidViewInterfaceReferenceAttributes() {
    const QVector<QString> attributeNames{
        QStringLiteral("interface"),
        QStringLiteral("interface_id"),
        QStringLiteral("interface_ref")
    };

    for (const QString& attributeName : attributeNames) {
        QTemporaryDir temp;
        require(temp.isValid(), "temporary directory should be valid");

        QDir root(temp.path());
        createViewXml(
            root,
            QStringLiteral("Module"),
            QStringLiteral(R"xml(<module-view schema="v1" module="Module">
  <decorations><marker %1="missing_interface" /></decorations>
</module-view>)xml").arg(attributeName).toUtf8());
        writeFile(root.filePath(QStringLiteral("ipcraft.json")), minimalManifest());

        IpcraftRegistry registry;
        const bool loaded = registry.loadPackageRoots({temp.path()});

        require(!loaded,
                "package with invalid view XML interface reference attribute should fail registry load");
        require(registry.packages().isEmpty(),
                "failed interface-reference package should not be partially registered");
        require(diagnosticsContain(registry.diagnostics(), attributeName),
                "registry diagnostic should identify the invalid interface reference attribute");
        require(diagnosticsContain(registry.diagnostics(), QStringLiteral("missing_interface")),
                "registry diagnostic should identify the missing referenced interface");
    }
}

QByteArray attachmentZoneManifest(const QString& viewPath = QStringLiteral("views/Module.xml")) {
    return QStringLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.attach_zone",
  "name": "Attach Zone",
  "version": "1.0.0",
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "name": "Module",
      "interfaces": [
        { "id": "bus", "modes": ["target"], "accepts": [{ "class": "demo_link", "role": "target" }] }
      ]
    },
    {
      "id": "Endpoint",
      "name": "Endpoint",
      "attach": { "hosts": ["Module"], "zone": "agent_slot" },
      "interfaces": [
        { "id": "link", "modes": ["initiator"], "accepts": [{ "class": "demo_link", "role": "initiator" }] }
      ]
    }
  ],
  "views": [
    { "module": "Module", "file": "%1" }
  ]
})json").arg(viewPath).toUtf8();
}

void testRegistryAcceptsAttachmentZoneRefAlias() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createViewXml(
        root,
        QStringLiteral("Module"),
        QByteArrayLiteral(R"xml(<module-view schema="v1" module="Module">
  <anchors><anchor ref="bus" x="0" y="0" /></anchors>
  <attachment-zones><zone ref="agent_slot" x="10" y="10" /></attachment-zones>
</module-view>)xml"));
    writeFile(root.filePath(QStringLiteral("ipcraft.json")), attachmentZoneManifest());

    IpcraftRegistry registry;
    const bool loaded = registry.loadPackageRoots({temp.path()});

    require(loaded,
            "attachment-zone ref should be interpreted as a zone id, not an interface reference");
    require(registry.package(QStringLiteral("org.example.attach_zone")) != nullptr,
            "package with attachment-zone ref alias should load");
}

void testRejectsRegistryPackageWithInvalidViewAttachmentZoneReferences() {
    const QVector<QString> attributeNames{
        QStringLiteral("ref"),
        QStringLiteral("zone"),
        QStringLiteral("attach_zone"),
        QStringLiteral("attachment_zone")
    };

    for (const QString& attributeName : attributeNames) {
        QTemporaryDir temp;
        require(temp.isValid(), "temporary directory should be valid");

        QDir root(temp.path());
        createViewXml(
            root,
            QStringLiteral("Module"),
            QStringLiteral(R"xml(<module-view schema="v1" module="Module">
  <attachment-zones><zone %1="missing_zone" x="0" y="0" /></attachment-zones>
</module-view>)xml").arg(attributeName).toUtf8());
        writeFile(root.filePath(QStringLiteral("ipcraft.json")), minimalManifest());

        IpcraftRegistry registry;
        const bool loaded = registry.loadPackageRoots({temp.path()});

        require(!loaded,
                "package with invalid view XML attachment zone reference should fail registry load");
        require(registry.packages().isEmpty(),
                "failed attachment-zone package should not be partially registered");
        require(diagnosticsContain(registry.diagnostics(), attributeName),
                "registry diagnostic should identify the invalid attachment zone attribute");
        require(diagnosticsContain(registry.diagnostics(), QStringLiteral("missing_zone")),
                "registry diagnostic should identify the missing referenced attachment zone");
    }
}

void testRejectsBatchPackageRegistrationOnAnyFailure() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir workspace(temp.path());
    require(workspace.mkpath(QStringLiteral("valid")), "failed to create valid package directory");
    require(workspace.mkpath(QStringLiteral("invalid")), "failed to create invalid package directory");

    QDir validRoot(workspace.filePath(QStringLiteral("valid")));
    createView(validRoot);
    writeFile(validRoot.filePath(QStringLiteral("ipcraft.json")),
              minimalManifest(QStringLiteral("views/Module.xml"),
                              QStringLiteral("org.example.valid")));

    QDir invalidRoot(workspace.filePath(QStringLiteral("invalid")));
    createView(invalidRoot, QStringLiteral("Module"), QStringLiteral("missing_interface"));
    writeFile(invalidRoot.filePath(QStringLiteral("ipcraft.json")),
              minimalManifest(QStringLiteral("views/Module.xml"),
                              QStringLiteral("org.example.invalid")));

    IpcraftRegistry registry;
    const bool loaded = registry.loadPackageRoots({validRoot.absolutePath(), invalidRoot.absolutePath()});

    require(!loaded, "batch with any invalid package should fail registry load");
    require(registry.packages().isEmpty(), "failed batch should not register earlier valid packages");
    require(registry.package(QStringLiteral("org.example.valid")) == nullptr,
            "valid package from failed batch should not remain registered");
    require(!registry.diagnostics().isEmpty(), "failed batch should expose diagnostics");
}

void testPluginAndExtensionsAreDistinct() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    createView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "plugin": {
    "library": "plugins/libdemo.so",
    "entry": "DemoPlugin"
  },
  "extensions": {
    "noc.v1": { "enabled": true }
  },
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["initiator"],
          "accepts": [{ "class": "demo_link", "role": "initiator" }]
        }
      ]
    }
  ],
  "views": [{ "module": "Module", "file": "views/Module.xml" }]
})json"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(temp.path());

    require(result.ok, "manifest with plugin and extensions should load");
    require(result.manifest.plugin.has_value(), "plugin descriptor should be populated");
    require(result.manifest.plugin->id.isEmpty(), "plugin id should remain optional");
    require(result.manifest.plugin->libraryPath == QStringLiteral("plugins/libdemo.so"),
            "plugin library should load");
    require(result.manifest.plugin->entrypoint == QStringLiteral("DemoPlugin"),
            "plugin entry should load into plugin metadata");
    require(result.manifest.extensions.contains(QStringLiteral("noc.v1")),
            "extension descriptor should be populated");
    require(result.manifest.extensions.value(QStringLiteral("noc.v1")).id == QStringLiteral("noc.v1"),
            "extension id should load independently of plugin metadata");
    require(result.manifest.plugin->libraryPath != result.manifest.extensions.value(QStringLiteral("noc.v1")).id,
            "plugin and extension descriptors should remain distinct");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testLoadsMinimalPackageManifest();
        testRejectsDuplicateJsonKeys();
        testRejectsDuplicateJsonKeysAfterUnicodeDecoding();
        testRejectsMissingCommandInputSchema();
        testLoadsFrameworkToolGenerateCommand();
        testRejectsCommandWithExecutableAndFrameworkTool();
        testRejectsCommandWithoutExecutableOrFrameworkTool();
        testRejectsUnknownFrameworkToolCommand();
        testRejectsUnknownRequiredShapeFields();
        testRejectsDottedUnknownRequiredShapeFields();
        testRejectsNonStringArrayEntries();
        testLoadsConnectionClassIpxactAndAttachMetadata();
        testManifestReaderParsesDisplayTopologyAndGeneration();
        testRejectsWrongPluginFieldTypes();
        testRejectsNonObjectExtensionValues();
        testRejectsWrongTopLevelCollectionTypes();
        testRejectsAbsoluteViewPath();
        testRejectsTraversingViewPath();
        testRejectsAbsoluteCommandExecutable();
        testRejectsTraversingCommandExecutable();
        testRejectsPartialPackageRegistration();
        testRejectsRegistryPackageWithInvalidViewInterfaceReferenceAttributes();
        testRegistryAcceptsAttachmentZoneRefAlias();
        testRejectsRegistryPackageWithInvalidViewAttachmentZoneReferences();
        testRejectsBatchPackageRegistrationOnAnyFailure();
        testPluginAndExtensionsAreDistinct();
    } catch (const std::exception& error) {
        std::cerr << "ipcraftmanifest_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcraftmanifest_test passed\n";
    return 0;
}
