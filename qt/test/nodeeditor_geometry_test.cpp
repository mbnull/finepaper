// Node editor geometry tests for interface-anchor scaling and router layout.
#include "commands/commandmanager.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"
#include "nodeeditor/endpointattachmentlayout.h"
#include "nodeeditor/graphnodegeometry.h"
#include "nodeeditor/graphnodemodel.h"
#include "nodeeditor/nodeeditorwidget.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFile>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPointF>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

constexpr auto ScopedModuleMime = "application/x-finepaper-module";

IpCoreRuntimeDescriptor nodeEditorRavenocDescriptor() {
    IpCoreRuntimeDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.ravenoc");
    descriptor.name = QStringLiteral("RaveNoC");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("noc");
    return descriptor;
}

IpCoreRuntimeDescriptor nodeEditorFabricDescriptor() {
    IpCoreRuntimeDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.fabric");
    descriptor.name = QStringLiteral("Fabric");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("fabric");
    return descriptor;
}

IpCoreRuntimeDescriptor attachmentPresentationDescriptor() {
    IpCoreRuntimeDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.attachmenttest");
    descriptor.name = QStringLiteral("Attachment Test");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("noc");
    return descriptor;
}

ModuleType scopedEditorType(const QString& name, const QString& ipcoreId) {
    ModuleType type;
    type.name = name;
    type.ipcoreId = ipcoreId;
    type.paletteLabel = name;
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultPorts.push_back(Port(QStringLiteral("out"),
                                     Port::Direction::Output,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Out")));
    return type;
}

ModuleInterfaceMetadata attachmentInterface(const QString& id,
                                            const QString& role,
                                            const QString& connectsTo) {
    ModuleInterfaceMetadata metadata;
    metadata.id = id;
    metadata.label = id;
    metadata.bus = QStringLiteral("attachment_link");
    metadata.role = role;
    metadata.compatibleRoles = {connectsTo};
    metadata.cardinality = QStringLiteral("one");
    metadata.autocompleteGroup = QStringLiteral("endpoint_attachment");
    return metadata;
}

ModuleType attachmentHostType() {
    ModuleType type;
    type.name = QStringLiteral("AttachmentHostTile");
    type.ipcoreId = QStringLiteral("finepaper.attachmenttest");
    type.paletteLabel = QStringLiteral("Attachment Host");
    type.graphGroup = QStringLiteral("xps");
    type.graphRole = QStringLiteral("host");
    type.editorLayout = QStringLiteral("mesh_router");
    type.supportsCollapse = true;
    type.expandedNodeMinWidth = 136;
    type.expandedNodeHeight = 116;
    type.collapsedNodeMinWidth = 104;
    type.collapsedNodeHeight = 92;
    type.defaultPorts.push_back(Port(QStringLiteral("local"),
                                     Port::Direction::Input,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Local"),
                                     {},
                                     QStringLiteral("attachment"),
                                     QStringLiteral("attachment_link"),
                                     QStringLiteral("local")));
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultParameters.insert(QStringLiteral("collapsed"), Parameter(QStringLiteral("collapsed"), true));
    type.interfaceMetadata.insert(QStringLiteral("local"),
                                  attachmentInterface(QStringLiteral("local"),
                                                      QStringLiteral("target"),
                                                      QStringLiteral("initiator")));
    return type;
}

ModuleType attachmentEndpointType() {
    ModuleType type;
    type.name = QStringLiteral("AttachmentEndpointNode");
    type.ipcoreId = QStringLiteral("finepaper.attachmenttest");
    type.paletteLabel = QStringLiteral("Attachment Endpoint");
    type.graphGroup = QStringLiteral("endpoints");
    type.graphRole = QStringLiteral("attached");
    type.editorLayout = QStringLiteral("endpoint");
    type.expandedNodeMinWidth = 104;
    type.expandedNodeHeight = 54;
    type.defaultPorts.push_back(Port(QStringLiteral("noc"),
                                     Port::Direction::Output,
                                     QStringLiteral("bus"),
                                     QStringLiteral("NoC"),
                                     {},
                                     QStringLiteral("attachment"),
                                     QStringLiteral("attachment_link"),
                                     QStringLiteral("noc")));
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.interfaceMetadata.insert(QStringLiteral("noc"),
                                  attachmentInterface(QStringLiteral("noc"),
                                                      QStringLiteral("initiator"),
                                                      QStringLiteral("target")));
    return type;
}

ModuleType manifestAttachmentHostType() {
    ModuleType type = attachmentHostType();
    type.name = QStringLiteral("ManifestAttachmentHostTile");
    type.graphRole = QStringLiteral("host");
    type.graphGroup = QStringLiteral("tiles");
    type.editorLayout = QStringLiteral("tile_socket");
    type.defaultPorts.clear();
    type.defaultPorts.push_back(Port(QStringLiteral("p0"),
                                     Port::Direction::Input,
                                     QStringLiteral("bus"),
                                     QStringLiteral("P0"),
                                     {},
                                     QStringLiteral("fabric_socket"),
                                     QStringLiteral("attachment_link"),
                                     QStringLiteral("p0")));
    type.interfaceMetadata.clear();
    type.interfaceMetadata.insert(QStringLiteral("p0"),
                                  attachmentInterface(QStringLiteral("p0"),
                                                      QStringLiteral("target"),
                                                      QStringLiteral("initiator")));
    type.interfaceAnchors.clear();
    type.interfaceAnchors.insert(QStringLiteral("p0"),
                                 ModuleInterfaceAnchor{QStringLiteral("p0"),
                                                       68.0,
                                                       116.0,
                                                       0.0,
                                                       1.0,
                                                       QStringLiteral("P0"),
                                                       56.0,
                                                       96.0});
    return type;
}

ModuleType manifestAttachedAgentType() {
    ModuleType type = attachmentEndpointType();
    type.name = QStringLiteral("ManifestAttachedAgentNode");
    type.graphRole = QStringLiteral("attached");
    type.graphGroup = QStringLiteral("agents");
    type.editorLayout = QStringLiteral("agent_socket");
    type.defaultPorts.clear();
    type.defaultPorts.push_back(Port(QStringLiteral("chi"),
                                     Port::Direction::Output,
                                     QStringLiteral("bus"),
                                     QStringLiteral("CHI"),
                                     {},
                                     QStringLiteral("fabric_socket"),
                                     QStringLiteral("attachment_link"),
                                     QStringLiteral("chi")));
    type.interfaceMetadata.clear();
    type.interfaceMetadata.insert(QStringLiteral("chi"),
                                  attachmentInterface(QStringLiteral("chi"),
                                                      QStringLiteral("initiator"),
                                                      QStringLiteral("target")));
    type.interfaceAnchors.clear();
    type.interfaceAnchors.insert(QStringLiteral("chi"),
                                 ModuleInterfaceAnchor{QStringLiteral("chi"),
                                                       104.0,
                                                       27.0,
                                                       1.0,
                                                       0.0,
                                                       QStringLiteral("CHI"),
                                                       76.0,
                                                       40.0});
    return type;
}

ModuleType visualOnlyAttachmentHostType() {
    ModuleType type = attachmentHostType();
    type.name = QStringLiteral("VisualOnlyAttachmentHostTile");
    type.interfaceMetadata.clear();
    return type;
}

ModuleType visualOnlyAttachedEndpointType() {
    ModuleType type = attachmentEndpointType();
    type.name = QStringLiteral("VisualOnlyAttachedEndpointNode");
    type.interfaceMetadata.clear();
    return type;
}

std::unique_ptr<Module> moduleFromType(const ModuleType& type,
                                       const QString& moduleId,
                                       const QString& instanceId) {
    auto module = std::make_unique<Module>(moduleId, type.name);
    module->setIpcoreId(type.ipcoreId);
    module->setInstanceId(instanceId);
    for (const Port& port : type.defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    return module;
}

struct ScopedNodeEditorHarness {
    Graph graph;
    CommandManager commandManager;
    ModuleRegistry registry{ModuleRegistry::LoadMode::Empty};
    IpCoreRuntimeDescriptor ravenoc = nodeEditorRavenocDescriptor();
    IpCoreRuntimeDescriptor fabric = nodeEditorFabricDescriptor();
    IpCatalogService catalog;
    ProjectStateService stateService;
    ProjectIpService projectIpService;
    ActiveWorkspaceController workspaceController;
    NodeEditorWidget editor;

    ScopedNodeEditorHarness()
        : catalog(QList<IpCoreRuntimeDescriptor>{ravenoc, fabric}, &registry),
          projectIpService(&stateService),
          workspaceController(&projectIpService, &catalog),
          editor(&graph, &stateService, &workspaceController, &commandManager) {
        require(registry.registerType(scopedEditorType(QStringLiteral("RaveTile"),
                                                       QStringLiteral("finepaper.ravenoc"))),
                "RaveTile test type should register");
        require(registry.registerType(scopedEditorType(QStringLiteral("FabricSwitch"),
                                                       QStringLiteral("finepaper.fabric"))),
                "FabricSwitch test type should register");
        catalog = IpCatalogService(QList<IpCoreRuntimeDescriptor>{ravenoc, fabric}, &registry);
        editor.resize(320, 240);
        editor.show();
        QCoreApplication::processEvents();
    }

    IpCatalogEntry ravenocEntry() const {
        const std::optional<IpCatalogEntry> entry = catalog.entry(QStringLiteral("finepaper.ravenoc"));
        require(entry.has_value(), "RaveNoC entry should exist");
        return *entry;
    }

    void selectRavenoc() {
        const ProjectIpServiceResult result = projectIpService.createInstanceForIpcore(ravenocEntry());
        require(result.success, "RaveNoC instance should be selected");
        QCoreApplication::processEvents();
    }
};

std::unique_ptr<QMimeData> scopedModuleMime(const QString& ipcoreId,
                                            const QString& instanceId,
                                            const QString& moduleType) {
    QJsonObject object;
    object.insert(QStringLiteral("ipcore"), ipcoreId);
    object.insert(QStringLiteral("instance"), instanceId);
    object.insert(QStringLiteral("type"), moduleType);
    auto mimeData = std::make_unique<QMimeData>();
    mimeData->setData(ScopedModuleMime,
                      QJsonDocument(object).toJson(QJsonDocument::Compact));
    return mimeData;
}

bool sendScopedDrop(NodeEditorWidget& editor, QMimeData* mimeData) {
    QDragEnterEvent enter(QPoint(16, 16),
                          Qt::CopyAction,
                          mimeData,
                          Qt::LeftButton,
                          Qt::NoModifier);
    QCoreApplication::sendEvent(&editor, &enter);

    QDropEvent drop(QPointF(48, 64),
                    Qt::CopyAction,
                    mimeData,
                    Qt::LeftButton,
                    Qt::NoModifier);
    QCoreApplication::sendEvent(&editor, &drop);
    QCoreApplication::processEvents();
    return drop.isAccepted();
}

std::optional<QPointF> visibleModulePosition(NodeEditorWidget& editor, const QString& moduleId) {
    auto* view = editor.findChild<QGraphicsView*>();
    if (!view || !view->scene()) {
        return std::nullopt;
    }

    for (QGraphicsItem* item : view->scene()->items()) {
        auto* nodeGraphics = qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item);
        if (!nodeGraphics) {
            continue;
        }

        auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&nodeGraphics->graphModel());
        auto* nodeModel = graphModel
            ? graphModel->delegateModel<GraphNodeModel>(nodeGraphics->nodeId())
            : nullptr;
        if (nodeModel && nodeModel->module() && nodeModel->module()->id() == moduleId) {
            return nodeGraphics->pos();
        }
    }

    return std::nullopt;
}

void registerScaledAnchorType() {
    static bool registered = false;
    if (registered) {
        return;
    }

    ModuleType type;
    type.name = QStringLiteral("GeometryScaledXP");
    type.editorLayout = QStringLiteral("mesh_router");
    type.supportsCollapse = true;
    type.expandedNodeMinWidth = 136;
    type.expandedNodeHeight = 116;
    type.collapsedNodeMinWidth = 104;
    type.collapsedNodeHeight = 92;
    type.expandedCaptionLeftInset = 30.0;
    type.expandedCaptionTopInset = 6.0;
    type.collapsedCaptionLeftInset = 30.0;
    type.collapsedCaptionTopInset = 26.0;

    type.interfaceAnchors.insert(QStringLiteral("north"),
        ModuleInterfaceAnchor{QStringLiteral("north"), 68.0, 0.0, 0.0, -1.0, QStringLiteral("North"), 68.0, 18.0});
    type.interfaceAnchors.insert(QStringLiteral("east"),
        ModuleInterfaceAnchor{QStringLiteral("east"), 136.0, 58.0, 1.0, 0.0, QStringLiteral("East"), 112.0, 58.0});
    type.interfaceAnchors.insert(QStringLiteral("south"),
        ModuleInterfaceAnchor{QStringLiteral("south"), 68.0, 116.0, 0.0, 1.0, QStringLiteral("South"), 68.0, 98.0});
    type.interfaceAnchors.insert(QStringLiteral("west"),
        ModuleInterfaceAnchor{QStringLiteral("west"), 0.0, 58.0, -1.0, 0.0, QStringLiteral("West"), 24.0, 58.0});
    type.interfaceAnchors.insert(QStringLiteral("local0"),
        ModuleInterfaceAnchor{QStringLiteral("local0"), 0.0, 26.0, -1.0, 0.0, QStringLiteral("Local 0"), 32.0, 26.0});

    ModuleRegistry::instance().registerType(type);
    registered = true;
}

void registerEndpointTypeWithLeftDefaultAnchor() {
    static bool registered = false;
    if (registered) {
        return;
    }

    ModuleType type;
    type.name = QStringLiteral("GeometryEndpoint");
    type.editorLayout = QStringLiteral("endpoint");
    type.graphGroup = QStringLiteral("endpoints");
    type.expandedNodeMinWidth = 104;
    type.expandedNodeHeight = 54;
    type.interfaceAnchors.insert(QStringLiteral("noc"),
        ModuleInterfaceAnchor{QStringLiteral("noc"), 0.0, 27.0, -1.0, 0.0, QStringLiteral("NoC"), 26.0, 40.0});

    ModuleRegistry::instance().registerType(type);
    registered = true;
}

ModuleType registerOpenNocViewAnchoredType() {
    QTemporaryDir packageRoot;
    require(packageRoot.isValid(), "failed to create temporary OpenNoC view package");
    require(QDir(packageRoot.path()).mkpath(QStringLiteral("views")),
            "failed to create temporary view directory");

    const QString moduleId = QStringLiteral("GeometryOpenNoCXPAnchored");
    const QString viewPath = packageRoot.filePath(QStringLiteral("views/OpenNoCXP.xml"));
    QFile viewFile(viewPath);
    require(viewFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to write temporary OpenNoC view XML");
    viewFile.write(QByteArrayLiteral(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<module-view schema="v1" module="GeometryOpenNoCXPAnchored">
  <graphics layout="mesh_router" node_color="#e6edf3" supports_collapse="true">
    <expanded min_width="160" height="128" caption_left="16" caption_top="20" port_inset="18" />
    <collapsed min_width="112" height="96" caption_left="20" caption_top="28" endpoint_inset="18" />
  </graphics>
  <anchors>
    <anchor ref="p0" x="42" y="128" normal_x="0" normal_y="1" label="P0" label_x="30" label_y="106" />
    <anchor ref="p1" x="118" y="128" normal_x="0" normal_y="1" label="P1" label_x="106" label_y="106" />
  </anchors>
</module-view>
)xml"));
    viewFile.close();

    IpcraftPackageManifest manifest;
    manifest.id = QStringLiteral("finepaper.geometry.opennoc");
    manifest.connectionClasses.push_back(IpcraftConnectionClass{
        QStringLiteral("chi_node_interface"),
        QStringList{QStringLiteral("node"), QStringLiteral("interconnect")},
        false
    });

    IpcraftModuleDescriptor module;
    module.id = moduleId;
    module.name = QStringLiteral("OpenNoC XP");
    module.graphRole = QStringLiteral("host");
    module.interfaces = {
        IpcraftInterfaceDescriptor{
            QStringLiteral("p0"),
            QStringLiteral("P0"),
            {},
            QVector<IpcraftInterfaceAcceptRule>{
                IpcraftInterfaceAcceptRule{QStringLiteral("chi_node_interface"),
                                           QStringLiteral("interconnect")}
            }
        },
        IpcraftInterfaceDescriptor{
            QStringLiteral("p1"),
            QStringLiteral("P1"),
            {},
            QVector<IpcraftInterfaceAcceptRule>{
                IpcraftInterfaceAcceptRule{QStringLiteral("chi_node_interface"),
                                           QStringLiteral("interconnect")}
            }
        }
    };
    manifest.modules.push_back(module);

    IpcraftViewDescriptor view;
    view.moduleId = moduleId;
    view.filePath = QStringLiteral("views/OpenNoCXP.xml");
    view.resolvedFilePath = viewPath;
    manifest.views.push_back(view);

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.loadIpcraftPackages({manifest}), "temporary OpenNoC package should load");
    const ModuleType* loadedType = registry.getType(moduleId);
    require(loadedType != nullptr, "temporary OpenNoC module type should load");
    ModuleRegistry::instance().registerType(*loadedType);
    return *loadedType;
}

struct AttachmentZoneViewTypes {
    ModuleType host;
    ModuleType agent;
};

AttachmentZoneViewTypes registerAttachmentZoneViewTypes() {
    QTemporaryDir packageRoot;
    require(packageRoot.isValid(), "failed to create temporary attachment-zone view package");
    require(QDir(packageRoot.path()).mkpath(QStringLiteral("views")),
            "failed to create temporary attachment-zone view directory");

    const QString hostModuleId = QStringLiteral("ZoneHost");
    const QString agentModuleId = QStringLiteral("ZoneAgent");
    const QString hostViewPath = packageRoot.filePath(QStringLiteral("views/ZoneHost.xml"));
    QFile hostViewFile(hostViewPath);
    require(hostViewFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to write temporary attachment-zone host view XML");
    hostViewFile.write(QByteArrayLiteral(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<module-view schema="v1" module="ZoneHost">
  <graphics layout="zone_host_visual" node_color="#dde8f2" supports_collapse="true">
    <expanded min_width="180" height="100" caption_left="16" caption_top="12" />
    <collapsed min_width="132" height="74" caption_left="16" caption_top="12" />
    <arrangement endpoint_offset_x="70" />
  </graphics>
  <anchors>
    <anchor ref="socket" x="12" y="12" normal_x="-1" normal_y="0" label="Socket" label_x="24" label_y="24" />
  </anchors>
  <attachment-zones>
    <zone id="socket" x="160" y="64" normal_x="1" normal_y="0" label="Socket" mirror="false" />
  </attachment-zones>
</module-view>
)xml"));
    hostViewFile.close();

    const QString agentViewPath = packageRoot.filePath(QStringLiteral("views/ZoneAgent.xml"));
    QFile agentViewFile(agentViewPath);
    require(agentViewFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to write temporary attachment-zone agent view XML");
    agentViewFile.write(QByteArrayLiteral(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<module-view schema="v1" module="ZoneAgent">
  <graphics layout="zone_agent_visual" node_color="#ecf6dd">
    <expanded min_width="96" height="60" caption_left="8" caption_top="6" />
  </graphics>
  <anchors>
    <anchor ref="link" x="96" y="42" normal_x="1" normal_y="0" label="Link" label_x="68" label_y="48" />
  </anchors>
</module-view>
)xml"));
    agentViewFile.close();

    IpcraftPackageManifest manifest;
    manifest.id = QStringLiteral("finepaper.attachment_zone_view");
    manifest.connectionClasses.push_back(IpcraftConnectionClass{
        QStringLiteral("zone_link"),
        QStringList{QStringLiteral("initiator"), QStringLiteral("target")},
        false
    });

    QJsonObject hostParameters;
    hostParameters.insert(QStringLiteral("x"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("int")},
        {QStringLiteral("default"), 0},
        {QStringLiteral("configurable"), false}
    });
    hostParameters.insert(QStringLiteral("y"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("int")},
        {QStringLiteral("default"), 0},
        {QStringLiteral("configurable"), false}
    });
    hostParameters.insert(QStringLiteral("collapsed"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("bool")},
        {QStringLiteral("default"), false},
        {QStringLiteral("configurable"), false}
    });
    hostParameters.insert(QStringLiteral("display_name"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("default"), QStringLiteral("ZH")},
        {QStringLiteral("configurable"), false}
    });

    IpcraftModuleDescriptor hostModule;
    hostModule.id = hostModuleId;
    hostModule.name = QStringLiteral("Zone Host");
    hostModule.graphRole = QStringLiteral("host");
    hostModule.parameters = hostParameters;
    hostModule.interfaces = {
        IpcraftInterfaceDescriptor{
            QStringLiteral("socket"),
            QStringLiteral("Socket"),
            {},
            QVector<IpcraftInterfaceAcceptRule>{
                IpcraftInterfaceAcceptRule{QStringLiteral("zone_link"), QStringLiteral("target")}
            }
        }
    };

    QJsonObject agentParameters;
    agentParameters.insert(QStringLiteral("x"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("int")},
        {QStringLiteral("default"), 0},
        {QStringLiteral("configurable"), false}
    });
    agentParameters.insert(QStringLiteral("y"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("int")},
        {QStringLiteral("default"), 0},
        {QStringLiteral("configurable"), false}
    });
    agentParameters.insert(QStringLiteral("display_name"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("default"), QStringLiteral("ZA")},
        {QStringLiteral("configurable"), false}
    });

    IpcraftModuleDescriptor agentModule;
    agentModule.id = agentModuleId;
    agentModule.name = QStringLiteral("Zone Agent");
    agentModule.graphRole = QStringLiteral("attached");
    agentModule.parameters = agentParameters;
    agentModule.interfaces = {
        IpcraftInterfaceDescriptor{
            QStringLiteral("link"),
            QStringLiteral("Link"),
            {},
            QVector<IpcraftInterfaceAcceptRule>{
                IpcraftInterfaceAcceptRule{QStringLiteral("zone_link"), QStringLiteral("initiator")}
            }
        }
    };

    manifest.modules.push_back(hostModule);
    manifest.modules.push_back(agentModule);
    manifest.views.push_back(IpcraftViewDescriptor{
        hostModuleId,
        QStringLiteral("views/ZoneHost.xml"),
        hostViewPath
    });
    manifest.views.push_back(IpcraftViewDescriptor{
        agentModuleId,
        QStringLiteral("views/ZoneAgent.xml"),
        agentViewPath
    });

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.loadIpcraftPackages({manifest}), "temporary attachment-zone package should load");
    const ModuleType* loadedHostType = registry.getType(hostModuleId);
    const ModuleType* loadedAgentType = registry.getType(agentModuleId);
    require(loadedHostType != nullptr, "attachment-zone host module type should load");
    require(loadedAgentType != nullptr, "attachment-zone agent module type should load");
    ModuleRegistry::instance().registerType(*loadedHostType);
    ModuleRegistry::instance().registerType(*loadedAgentType);
    return AttachmentZoneViewTypes{*loadedHostType, *loadedAgentType};
}

std::unique_ptr<Module> makeRouter(bool collapsed, const QString& id = QStringLiteral("router")) {
    auto module = std::make_unique<Module>(id, QStringLiteral("GeometryScaledXP"));
    module->addPort(Port(QStringLiteral("north"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("North"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("north")));
    module->addPort(Port(QStringLiteral("east"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("East"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("east")));
    module->addPort(Port(QStringLiteral("south"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("South"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("south")));
    module->addPort(Port(QStringLiteral("west"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("West"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("west")));
    module->addPort(Port(QStringLiteral("local0"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("Local 0"), {}, QStringLiteral("attachment"),
                         QStringLiteral("ni_link"), QStringLiteral("local0")));
    module->setParameter(QStringLiteral("display_name"),
                         QStringLiteral("Very Long Router Caption For Geometry Scaling"));
    module->setParameter(QStringLiteral("collapsed"), collapsed);
    return module;
}

std::unique_ptr<Module> makeEndpoint(const QString& id = QStringLiteral("endpoint")) {
    auto module = std::make_unique<Module>(id, QStringLiteral("GeometryEndpoint"));
    module->addPort(Port(QStringLiteral("noc"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("NoC"), {}, QStringLiteral("attachment"),
                         QStringLiteral("ni_link"), QStringLiteral("noc")));
    module->setParameter(QStringLiteral("display_name"), QStringLiteral("Endpoint"));
    return module;
}

GraphNodeModel* addGraphNode(QtNodes::DataFlowGraphModel& graphModel, Module* module, QtNodes::NodeId& nodeId) {
    nodeId = graphModel.addNode(QStringLiteral("GraphNode"));
    auto* model = dynamic_cast<GraphNodeModel*>(graphModel.delegateModel<GraphNodeModel>(nodeId));
    require(model != nullptr, "graph node model should be created");
    model->setModule(module);
    return model;
}

void testExpandedMeshRouterUsesStatefulPortLayout() {
    registerScaledAnchorType();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> router = makeRouter(false);
    QtNodes::NodeId nodeId = QtNodes::InvalidNodeId;
    GraphNodeModel* nodeModel = addGraphNode(graphModel, router.get(), nodeId);
    GraphNodeGeometry geometry(graphModel);

    const QSize nodeSize = geometry.size(nodeId);
    require(nodeSize.width() > 136, "long caption should expand router width beyond view baseline");

    const QPointF north = geometry.portPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("north"), QtNodes::PortType::In));
    const QPointF east = geometry.portPosition(
        nodeId,
        QtNodes::PortType::Out,
        nodeModel->portIndex(QStringLiteral("east"), QtNodes::PortType::Out));
    const QPointF south = geometry.portPosition(
        nodeId,
        QtNodes::PortType::Out,
        nodeModel->portIndex(QStringLiteral("south"), QtNodes::PortType::Out));
    const QPointF west = geometry.portPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("west"), QtNodes::PortType::In));
    const QPointF local0 = geometry.portPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("local0"), QtNodes::PortType::In));

    require(local0.x() == 0.0, "expanded local endpoint interface should remain on the left edge");
    require(north.x() == nodeSize.width() &&
                east.x() == nodeSize.width() &&
                south.x() == nodeSize.width() &&
                west.x() == nodeSize.width(),
            "expanded mesh router interfaces should all be stacked on the right edge");
    require(north.y() < east.y() && east.y() < south.y() && south.y() < west.y(),
            "expanded mesh router interfaces should be vertically ordered on the right edge");
}

void testEndpointAttachmentLayoutUsesHostAnchorNormal() {
    const QPointF topLeft = EndpointAttachmentLayout::endpointTopLeft(
        QPointF(100.0, 50.0),
        QPointF(-1.0, 0.0),
        QPointF(104.0, 27.0),
        52.0);

    require(topLeft.x() == -56.0, "left-facing local anchor should place endpoint to the left");
    require(topLeft.y() == 23.0, "endpoint NoC anchor should align vertically with host local anchor");
}

void testEndpointInterfaceUsesHorizontalSidesOnly() {
    const QSize endpointSize(104, 54);

    const QPointF facingLeftHost =
        EndpointAttachmentLayout::endpointAnchorForHostNormal(endpointSize, QPointF(-1.0, 0.0));
    const QPointF facingRightHost =
        EndpointAttachmentLayout::endpointAnchorForHostNormal(endpointSize, QPointF(1.0, 0.0));
    const QPointF verticalHost =
        EndpointAttachmentLayout::endpointAnchorForHostNormal(endpointSize, QPointF(0.0, -1.0));

    require(facingLeftHost == QPointF(104.0, 27.0),
            "endpoint attached left of a host should expose its interface on the right side");
    require(facingRightHost == QPointF(0.0, 27.0),
            "endpoint attached right of a host should expose its interface on the left side");
    require(verticalHost == QPointF(104.0, 27.0),
            "endpoint interface should stay on a horizontal side instead of top or bottom");
}

void testEndpointInterfaceFlipsTowardHostAnchor() {
    registerScaledAnchorType();
    registerEndpointTypeWithLeftDefaultAnchor();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> router = makeRouter(false);
    std::unique_ptr<Module> endpoint = makeEndpoint();
    QtNodes::NodeId routerNodeId = QtNodes::InvalidNodeId;
    QtNodes::NodeId endpointNodeId = QtNodes::InvalidNodeId;
    GraphNodeModel* routerModel = addGraphNode(graphModel, router.get(), routerNodeId);
    GraphNodeModel* endpointModel = addGraphNode(graphModel, endpoint.get(), endpointNodeId);

    const QtNodes::PortIndex endpointPortIndex =
        endpointModel->portIndex(QStringLiteral("noc"), QtNodes::PortType::Out);
    const QtNodes::PortIndex endpointInputPortIndex =
        endpointModel->portIndex(QStringLiteral("noc"), QtNodes::PortType::In);
    const QtNodes::PortIndex routerPortIndex =
        routerModel->portIndex(QStringLiteral("local0"), QtNodes::PortType::In);
    graphModel.addConnection(QtNodes::ConnectionId{
        endpointNodeId,
        endpointPortIndex,
        routerNodeId,
        routerPortIndex
    });

    GraphNodeGeometry geometry(graphModel);
    const QSize endpointSize = geometry.size(endpointNodeId);
    const QPointF endpointPort = geometry.portPosition(
        endpointNodeId,
        QtNodes::PortType::Out,
        endpointPortIndex);

    require(endpointPort.x() == endpointSize.width(),
            "endpoint interface should flip to the side facing the host anchor");
    require(endpointPort.y() == endpointSize.height() / 2.0,
            "flipped endpoint interface should stay vertically centered for a horizontal host anchor");

    const QPointF endpointInputPort = geometry.portPosition(
        endpointNodeId,
        QtNodes::PortType::In,
        endpointInputPortIndex);
    require(endpointInputPort == endpointPort,
            "inout endpoint interface should use one visual anchor for both Qt port directions");
}

void testEndpointInterfaceFollowsRelativeNodePosition() {
    registerScaledAnchorType();
    registerEndpointTypeWithLeftDefaultAnchor();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> router = makeRouter(false);
    std::unique_ptr<Module> endpoint = makeEndpoint();
    QtNodes::NodeId routerNodeId = QtNodes::InvalidNodeId;
    QtNodes::NodeId endpointNodeId = QtNodes::InvalidNodeId;
    GraphNodeModel* routerModel = addGraphNode(graphModel, router.get(), routerNodeId);
    GraphNodeModel* endpointModel = addGraphNode(graphModel, endpoint.get(), endpointNodeId);

    graphModel.setNodeData(routerNodeId, QtNodes::NodeRole::Position, QPointF(0.0, 0.0));
    graphModel.setNodeData(endpointNodeId, QtNodes::NodeRole::Position, QPointF(260.0, 0.0));

    const QtNodes::PortIndex endpointPortIndex =
        endpointModel->portIndex(QStringLiteral("noc"), QtNodes::PortType::Out);
    const QtNodes::PortIndex routerPortIndex =
        routerModel->portIndex(QStringLiteral("local0"), QtNodes::PortType::In);
    graphModel.addConnection(QtNodes::ConnectionId{
        endpointNodeId,
        endpointPortIndex,
        routerNodeId,
        routerPortIndex
    });

    GraphNodeGeometry geometry(graphModel);
    const QPointF endpointPort = geometry.portPosition(
        endpointNodeId,
        QtNodes::PortType::Out,
        endpointPortIndex);

    require(endpointPort.x() == 0.0,
            "endpoint interface should move to the left side when the host is left of the endpoint");
}

void testStoredNodeSizeOverridesDefaultAndProvidesResizeHandle() {
    registerScaledAnchorType();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> router = makeRouter(false);
    router->setParameter(QStringLiteral("display_name"), QStringLiteral("Router"));
    router->setParameter(QStringLiteral("node_width"), 220);
    router->setParameter(QStringLiteral("node_height"), 164);
    QtNodes::NodeId nodeId = QtNodes::InvalidNodeId;
    addGraphNode(graphModel, router.get(), nodeId);

    GraphNodeGeometry geometry(graphModel);
    const QSize nodeSize = geometry.size(nodeId);
    const QRect resizeHandle = geometry.resizeHandleRect(nodeId);

    require(nodeSize.width() == 220 && nodeSize.height() == 164,
            "stored node_width/node_height should override default node geometry");
    require(!resizeHandle.isEmpty(), "resizable nodes should expose a drag handle");
    require(resizeHandle.right() <= nodeSize.width() && resizeHandle.bottom() <= nodeSize.height(),
            "resize handle should sit inside the bottom-right node bounds");
}

void testOpenNocMultipleInterfacesRenderFromViewAnchors() {
    const ModuleType type = registerOpenNocViewAnchoredType();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> xp = moduleFromType(type,
                                                QStringLiteral("opennoc_xp"),
                                                QStringLiteral("opennoc_0"));
    xp->setParameter(QStringLiteral("display_name"), QStringLiteral("XP"));
    xp->setParameter(QStringLiteral("collapsed"), false);
    QtNodes::NodeId nodeId = QtNodes::InvalidNodeId;
    GraphNodeModel* nodeModel = addGraphNode(graphModel, xp.get(), nodeId);
    GraphNodeGeometry geometry(graphModel);

    const QPointF p0 = geometry.portPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("p0"), QtNodes::PortType::In));
    const QPointF p1 = geometry.portPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("p1"), QtNodes::PortType::In));
    const QPointF p0Label = geometry.portTextPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("p0"), QtNodes::PortType::In));

    require(p0 == QPointF(42.0, 128.0),
            "OpenNoC p0 should render from its view XML anchor");
    require(p1 == QPointF(118.0, 128.0),
            "OpenNoC p1 should render from its view XML anchor instead of sharing p0 fallback");
    require(p0Label == QPointF(30.0, 106.0),
            "OpenNoC p0 label should render from its view XML label anchor");
}

void testNodeEditorWidgetOwnsConnectionRuleServiceInputs() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    IpCatalogService catalog(QList<IpCoreRuntimeDescriptor>{}, &ModuleRegistry::instance());
    ActiveWorkspaceController workspaceController(&projectIpService, &catalog);
    CommandManager commandManager;
    NodeEditorWidget widget(&graph, &stateService, &workspaceController, &commandManager);

    require(!widget.isArrangeEnabled(),
            "widget should construct with project state service dependency");
}

std::unique_ptr<Module> selectableConnectionModule(const QString& id,
                                                   const QString& type,
                                                   const QString& ipcoreId,
                                                   const QString& instanceId,
                                                   const Port& port) {
    auto module = std::make_unique<Module>(id, type);
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    module->addPort(port);
    return module;
}

void testConnectionSelectionEmitsConnectionId() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    const std::optional<ProjectIpInstanceRecord> activeInstance =
        harness.projectIpService.selectedIpInstanceRecord();
    require(activeInstance.has_value(), "test harness should select a RaveNoC instance");

    require(harness.graph.addModule(selectableConnectionModule(
                QStringLiteral("source"),
                QStringLiteral("SelectableSource"),
                activeInstance->ipcoreId,
                activeInstance->instanceId,
                Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("Out")))),
            "source module should add");
    require(harness.graph.addModule(selectableConnectionModule(
                QStringLiteral("target"),
                QStringLiteral("SelectableTarget"),
                activeInstance->ipcoreId,
                activeInstance->instanceId,
                Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("In")))),
            "target module should add");
    harness.graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("conn_1"),
        PortRef{QStringLiteral("source"), QStringLiteral("out")},
        PortRef{QStringLiteral("target"), QStringLiteral("in")}));
    QCoreApplication::processEvents();

    QString selectedConnectionId;
    QObject::connect(&harness.editor,
                     &NodeEditorWidget::connectionSelected,
                     [&selectedConnectionId](const QString& connectionId) {
                         selectedConnectionId = connectionId;
                     });

    harness.editor.highlightElement(QStringLiteral("conn_1"));
    QCoreApplication::processEvents();

    require(selectedConnectionId == QStringLiteral("conn_1"),
            "selecting a rendered connection should emit its graph connection id");
}

void testScopedDropRejectsMissingActiveInstance() {
    ScopedNodeEditorHarness harness;
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.ravenoc"),
                                     QStringLiteral("ravenoc_0"),
                                     QStringLiteral("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "drop without selected active IP instance should be rejected");
    require(harness.graph.modules().empty(),
            "drop without selected active IP instance should not create a module");
}

void testScopedDropRejectsDifferentIpcore() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.fabric"),
                                     QStringLiteral("fabric_0"),
                                     QStringLiteral("FabricSwitch"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "drop for a different IP core should be rejected");
    require(harness.graph.modules().empty(),
            "drop for a different IP core should not create a module");
}

void testScopedDropRejectsLegacyModuleTypeMime() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = std::make_unique<QMimeData>();
    const QString legacyMime = QStringLiteral("application/x-") + QStringLiteral("moduletype");
    mimeData->setData(legacyMime, QByteArray("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "legacy module MIME should be rejected");
    require(harness.graph.modules().empty(),
            "legacy module MIME should not create a module");
}

void testScopedDropCreatesOwnedModule() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.ravenoc"),
                                     QStringLiteral("ravenoc_0"),
                                     QStringLiteral("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(accepted, "matching scoped module drop should be accepted");
    require(harness.graph.modules().size() == 1, "matching scoped drop should create one module");
    const Module* module = harness.graph.modules().front().get();
    require(module->type() == QStringLiteral("RaveTile"),
            "created module should use payload module type");
    require(module->ipcoreId() == QStringLiteral("finepaper.ravenoc"),
            "created module should keep active IP-core ownership");
    require(module->instanceId() == QStringLiteral("ravenoc_0"),
            "created module should keep active instance ownership");
    require(harness.commandManager.canUndo(),
            "scoped module creation should enter command history");
}

void testActiveWorkspaceShowsOnlyModulesForSelectedInstance() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto firstMime = scopedModuleMime(QStringLiteral("finepaper.ravenoc"),
                                      QStringLiteral("ravenoc_0"),
                                      QStringLiteral("RaveTile"));
    require(sendScopedDrop(harness.editor, firstMime.get()),
            "first scoped drop should create module for ravenoc_0");
    require(harness.graph.modules().size() == 1, "first scoped drop should add one graph module");
    const QString firstModuleId = harness.graph.modules().front()->id();

    const ProjectIpServiceResult secondInstance =
        harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry());
    require(secondInstance.success, "second RaveNoC instance should be created");
    auto secondMime = scopedModuleMime(QStringLiteral("finepaper.ravenoc"),
                                       QStringLiteral("ravenoc_1"),
                                       QStringLiteral("RaveTile"));
    require(sendScopedDrop(harness.editor, secondMime.get()),
            "second scoped drop should create module for ravenoc_1");
    require(harness.graph.modules().size() == 2, "second scoped drop should add another graph module");
    const QString secondModuleId = harness.graph.modules().back()->id();

    QStringList visibleIds = harness.editor.visibleModuleIds();
    require(visibleIds == QStringList{secondModuleId},
            "active workspace should only show modules for the selected instance");

    require(harness.projectIpService.selectInstance(QStringLiteral("finepaper.ravenoc"),
                                                    QStringLiteral("ravenoc_0")),
            "first instance selection should succeed");
    QCoreApplication::processEvents();
    visibleIds = harness.editor.visibleModuleIds();
    require(visibleIds == QStringList{firstModuleId},
            "switching back to ravenoc_0 should hide ravenoc_1 modules");

    require(harness.projectIpService.selectInstance(QStringLiteral("finepaper.ravenoc"),
                                                    QStringLiteral("ravenoc_1")),
            "second instance selection should succeed");
    QCoreApplication::processEvents();
    visibleIds = harness.editor.visibleModuleIds();
    require(visibleIds == QStringList{secondModuleId},
            "switching to ravenoc_1 should hide ravenoc_0 modules");
}

void testCreateMenuTypesFollowActiveWorkspace() {
    ScopedNodeEditorHarness harness;
    require(harness.editor.availableCreateModuleTypes().isEmpty(),
            "create menu should be empty without active workspace");

    harness.selectRavenoc();

    const QStringList moduleTypes = harness.editor.availableCreateModuleTypes();
    require(moduleTypes.size() == 1, "create menu should list active workspace modules only");
    require(moduleTypes.first() == QStringLiteral("RaveTile"),
            "create menu should list RaveNoC module type");
}

void testCollapsedHostAbsorbsEndpointWhenAttachmentConnectionIsAdded() {
    Graph graph;
    CommandManager commandManager;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const ModuleType hostType = attachmentHostType();
    const ModuleType endpointType = attachmentEndpointType();
    require(registry.registerType(hostType), "attachment host should register locally");
    require(registry.registerType(endpointType), "attachment endpoint should register locally");
    ModuleRegistry::instance().registerType(hostType);
    ModuleRegistry::instance().registerType(endpointType);

    const IpCoreRuntimeDescriptor runtime = attachmentPresentationDescriptor();
    IpCatalogService catalog(QList<IpCoreRuntimeDescriptor>{runtime}, &registry);
    ActiveWorkspaceController workspaceController(&projectIpService, &catalog);
    NodeEditorWidget editor(&graph, &stateService, &workspaceController, &commandManager);
    editor.resize(360, 240);
    editor.show();

    const std::optional<IpCatalogEntry> entry = catalog.entry(runtime.id);
    require(entry.has_value(), "attachment runtime should be in catalog");
    const ProjectIpServiceResult instance = projectIpService.createInstanceForIpcore(*entry);
    require(instance.success, "attachment IP instance should be selected");

    require(graph.addModule(moduleFromType(hostType,
                                           QStringLiteral("host"),
                                           instance.record.instanceId)),
            "collapsed host should add");
    require(graph.addModule(moduleFromType(endpointType,
                                           QStringLiteral("endpoint"),
                                           instance.record.instanceId)),
            "endpoint should add");
    QCoreApplication::processEvents();

    QStringList visibleIds = editor.visibleModuleIds();
    require(visibleIds.contains(QStringLiteral("host")),
            "host should be visible before attachment");
    require(visibleIds.contains(QStringLiteral("endpoint")),
            "loose endpoint should be visible before attachment");

    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("endpoint_to_host"),
        PortRef{QStringLiteral("endpoint"), QStringLiteral("noc")},
        PortRef{QStringLiteral("host"), QStringLiteral("local")}));
    QCoreApplication::processEvents();

    require(graph.connections().size() == 1,
            "attachment connection should be stored in the graph");
    visibleIds = editor.visibleModuleIds();
    require(visibleIds.contains(QStringLiteral("host")),
            "collapsed host should remain visible after attachment");
    require(!visibleIds.contains(QStringLiteral("endpoint")),
            "collapsed host should absorb the attached endpoint visual");
}

void testCollapsedAttachedNodeMirrorsIntoHostZone() {
    Graph graph;
    CommandManager commandManager;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const ModuleType hostType = manifestAttachmentHostType();
    const ModuleType agentType = manifestAttachedAgentType();
    require(registry.registerType(hostType), "manifest attachment host should register locally");
    require(registry.registerType(agentType), "manifest attached agent should register locally");
    ModuleRegistry::instance().registerType(hostType);
    ModuleRegistry::instance().registerType(agentType);

    const IpCoreRuntimeDescriptor runtime = attachmentPresentationDescriptor();
    IpCatalogService catalog(QList<IpCoreRuntimeDescriptor>{runtime}, &registry);
    ActiveWorkspaceController workspaceController(&projectIpService, &catalog);
    NodeEditorWidget editor(&graph, &stateService, &workspaceController, &commandManager);
    editor.resize(360, 240);
    editor.show();

    const std::optional<IpCatalogEntry> entry = catalog.entry(runtime.id);
    require(entry.has_value(), "attachment runtime should be in catalog");
    const ProjectIpServiceResult instance = projectIpService.createInstanceForIpcore(*entry);
    require(instance.success, "attachment IP instance should be selected");

    require(graph.addModule(moduleFromType(hostType,
                                           QStringLiteral("host"),
                                           instance.record.instanceId)),
            "manifest collapsed host should add");
    require(graph.addModule(moduleFromType(agentType,
                                           QStringLiteral("agent"),
                                           instance.record.instanceId)),
            "manifest attached agent should add");
    Module* host = graph.getModule(QStringLiteral("host"));
    Module* agent = graph.getModule(QStringLiteral("agent"));
    require(host != nullptr && agent != nullptr, "manifest attachment modules should exist");
    host->setParameter(QStringLiteral("x"), 40);
    host->setParameter(QStringLiteral("y"), 50);
    host->setParameter(QStringLiteral("collapsed"), false);
    agent->setParameter(QStringLiteral("x"), 320);
    agent->setParameter(QStringLiteral("y"), 80);
    QCoreApplication::processEvents();

    QStringList visibleIds = editor.visibleModuleIds();
    require(visibleIds.contains(QStringLiteral("host")),
            "manifest host should be visible before attachment");
    require(visibleIds.contains(QStringLiteral("agent")),
            "manifest attached agent should be visible before attachment");

    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("agent_to_host"),
        PortRef{QStringLiteral("agent"), QStringLiteral("chi")},
        PortRef{QStringLiteral("host"), QStringLiteral("p0")}));
    QCoreApplication::processEvents();

    visibleIds = editor.visibleModuleIds();
    require(visibleIds.contains(QStringLiteral("host")),
            "expanded manifest host should remain visible after attachment");
    require(visibleIds.contains(QStringLiteral("agent")),
            "expanded manifest host should keep attached node visible");
    const std::optional<QPointF> expandedAgentPosition =
        visibleModulePosition(editor, QStringLiteral("agent"));
    require(expandedAgentPosition.has_value(), "expanded manifest attached node should have a visible position");
    require(*expandedAgentPosition == QPointF(4.0, 322.0),
            "manifest attachment metadata should place non-legacy attached node from its host interface anchor");

    host->setParameter(QStringLiteral("collapsed"), true);
    QCoreApplication::processEvents();

    visibleIds = editor.visibleModuleIds();
    require(visibleIds.contains(QStringLiteral("host")),
            "collapsed manifest host should remain visible after attachment");
    require(!visibleIds.contains(QStringLiteral("agent")),
            "collapsed manifest host should absorb attached nodes through graph role and endpoint_attachment metadata");
}

void testVisualStyleAndEndpointLikeNamesDoNotImplyAttachmentBehavior() {
    Graph graph;
    CommandManager commandManager;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const ModuleType hostType = visualOnlyAttachmentHostType();
    const ModuleType endpointType = visualOnlyAttachedEndpointType();
    require(registry.registerType(hostType), "visual-only host should register locally");
    require(registry.registerType(endpointType), "visual-only endpoint should register locally");
    ModuleRegistry::instance().registerType(hostType);
    ModuleRegistry::instance().registerType(endpointType);

    const IpCoreRuntimeDescriptor runtime = attachmentPresentationDescriptor();
    IpCatalogService catalog(QList<IpCoreRuntimeDescriptor>{runtime}, &registry);
    ActiveWorkspaceController workspaceController(&projectIpService, &catalog);
    NodeEditorWidget editor(&graph, &stateService, &workspaceController, &commandManager);
    editor.resize(360, 240);
    editor.show();

    const std::optional<IpCatalogEntry> entry = catalog.entry(runtime.id);
    require(entry.has_value(), "visual-only runtime should be in catalog");
    const ProjectIpServiceResult instance = projectIpService.createInstanceForIpcore(*entry);
    require(instance.success, "visual-only IP instance should be selected");

    require(graph.addModule(moduleFromType(hostType,
                                           QStringLiteral("visual_host"),
                                           instance.record.instanceId)),
            "visual-only collapsed host should add");
    require(graph.addModule(moduleFromType(endpointType,
                                           QStringLiteral("visual_endpoint"),
                                           instance.record.instanceId)),
            "visual-only endpoint should add");
    QCoreApplication::processEvents();

    QStringList visibleIds = editor.visibleModuleIds();
    require(visibleIds.contains(QStringLiteral("visual_host")),
            "visual-only host should be visible before connection");
    require(visibleIds.contains(QStringLiteral("visual_endpoint")),
            "visual-only endpoint should be visible before connection");

    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("visual_endpoint_to_host"),
        PortRef{QStringLiteral("visual_endpoint"), QStringLiteral("noc")},
        PortRef{QStringLiteral("visual_host"), QStringLiteral("local")}));
    QCoreApplication::processEvents();

    visibleIds = editor.visibleModuleIds();
    require(visibleIds.contains(QStringLiteral("visual_host")),
            "visual-only host should remain visible after connection");
    require(visibleIds.contains(QStringLiteral("visual_endpoint")),
            "endpoint-like port ids and visual collapse style must not hide nodes without endpoint_attachment metadata");
}

void testAttachmentZoneViewMetadataPlacesAttachedNodeWithoutMirroringWhenDisabled() {
    const AttachmentZoneViewTypes types = registerAttachmentZoneViewTypes();

    Graph graph;
    CommandManager commandManager;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(types.host), "attachment-zone host should register locally");
    require(registry.registerType(types.agent), "attachment-zone agent should register locally");

    IpCoreRuntimeDescriptor runtime = attachmentPresentationDescriptor();
    runtime.id = types.host.ipcoreId;
    runtime.name = QStringLiteral("Attachment Zone View");
    IpCatalogService catalog(QList<IpCoreRuntimeDescriptor>{runtime}, &registry);
    ActiveWorkspaceController workspaceController(&projectIpService, &catalog);
    NodeEditorWidget editor(&graph, &stateService, &workspaceController, &commandManager);
    editor.resize(420, 280);
    editor.show();

    const std::optional<IpCatalogEntry> entry = catalog.entry(runtime.id);
    require(entry.has_value(), "attachment-zone runtime should be in catalog");
    const ProjectIpServiceResult instance = projectIpService.createInstanceForIpcore(*entry);
    require(instance.success, "attachment-zone IP instance should be selected");

    require(graph.addModule(moduleFromType(types.host,
                                           QStringLiteral("zone_host"),
                                           instance.record.instanceId)),
            "attachment-zone host should add");
    require(graph.addModule(moduleFromType(types.agent,
                                           QStringLiteral("zone_agent"),
                                           instance.record.instanceId)),
            "attachment-zone agent should add");
    Module* host = graph.getModule(QStringLiteral("zone_host"));
    Module* agent = graph.getModule(QStringLiteral("zone_agent"));
    require(host != nullptr && agent != nullptr, "attachment-zone modules should exist");
    host->setParameter(QStringLiteral("x"), 25);
    host->setParameter(QStringLiteral("y"), 35);
    host->setParameter(QStringLiteral("collapsed"), false);
    agent->setParameter(QStringLiteral("x"), 400);
    agent->setParameter(QStringLiteral("y"), 400);
    QCoreApplication::processEvents();

    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("zone_agent_to_host"),
        PortRef{QStringLiteral("zone_agent"), QStringLiteral("link")},
        PortRef{QStringLiteral("zone_host"), QStringLiteral("socket")}));
    QCoreApplication::processEvents();

    const std::optional<QPointF> agentPosition =
        visibleModulePosition(editor, QStringLiteral("zone_agent"));
    require(agentPosition.has_value(), "attachment-zone agent should remain visible while host is expanded");
    require(*agentPosition == QPointF(255.0, 57.0),
            "view XML attachment-zone anchor and mirror=false metadata should drive attached-node placement");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testExpandedMeshRouterUsesStatefulPortLayout();
        testEndpointAttachmentLayoutUsesHostAnchorNormal();
        testEndpointInterfaceUsesHorizontalSidesOnly();
        testEndpointInterfaceFlipsTowardHostAnchor();
        testEndpointInterfaceFollowsRelativeNodePosition();
        testStoredNodeSizeOverridesDefaultAndProvidesResizeHandle();
        testOpenNocMultipleInterfacesRenderFromViewAnchors();
        testNodeEditorWidgetOwnsConnectionRuleServiceInputs();
        testConnectionSelectionEmitsConnectionId();
        testScopedDropRejectsMissingActiveInstance();
        testScopedDropRejectsDifferentIpcore();
        testScopedDropRejectsLegacyModuleTypeMime();
        testScopedDropCreatesOwnedModule();
        testActiveWorkspaceShowsOnlyModulesForSelectedInstance();
        testCreateMenuTypesFollowActiveWorkspace();
        testCollapsedHostAbsorbsEndpointWhenAttachmentConnectionIsAdded();
        testCollapsedAttachedNodeMirrorsIntoHostZone();
        testVisualStyleAndEndpointLikeNamesDoNotImplyAttachmentBehavior();
        testAttachmentZoneViewMetadataPlacesAttachedNodeWithoutMirroringWhenDisabled();
    } catch (const std::exception& error) {
        std::cerr << "nodeeditor_geometry_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "nodeeditor_geometry_test passed\n";
    return 0;
}
