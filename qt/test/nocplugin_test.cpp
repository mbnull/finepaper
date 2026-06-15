#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/pluginhost.h"
#include "app/serviceregistry.h"
#include "app/workbenchservice.h"
#include "noc/nocplugin.h"
#include "noc/nocsemanticmodel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QStringList nocExtensionPoints() {
    return {
        QStringLiteral("ui.inspectorSection"),
        QStringLiteral("editor.tool"),
        QStringLiteral("connection.ruleProvider"),
        QStringLiteral("tool.flowInputProjector"),
        QStringLiteral("artifact.presenter"),
    };
}

CapabilityHandlerDescriptor requireNocHandler(const CapabilityRegistry& capabilities) {
    for (const CapabilityHandlerDescriptor& handler : capabilities.handlers()) {
        if (handler.capabilityId == QStringLiteral("noc.v1")) {
            return handler;
        }
    }
    throw std::runtime_error("NoC plugin should register noc.v1 handler");
}

ExtensionContribution requireSingleContribution(const ExtensionPointRegistry& extensionPoints,
                                                const QString& extensionPoint) {
    const QVector<ExtensionContribution> contributions =
        extensionPoints.contributions(extensionPoint);
    if (contributions.size() != 1) {
        throw std::runtime_error(QStringLiteral("%1 should have exactly one NoC contribution")
                                     .arg(extensionPoint)
                                     .toStdString());
    }
    return contributions.first();
}

void testNoCPluginRegistersNocCapabilityAndDescriptorContributions() {
    WorkbenchService workbench;
    ServiceRegistry services;
    ExtensionPointRegistry extensionPoints;
    CapabilityRegistry capabilities;
    AppContext context;
    context.services = &services;
    context.extensionPoints = &extensionPoints;
    context.capabilities = &capabilities;
    context.workbench = &workbench;

    PluginHost host(context);
    require(host.registerPlugin(createNoCPlugin()), "NoC plugin should register with host");

    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "NoC plugin should activate");

    const CapabilityHandlerDescriptor handler = requireNocHandler(capabilities);
    require(handler.ownerPluginId == QStringLiteral("finepaper.noc"),
            "NoC handler owner plugin id should be stable");
    require(handler.extensionPoints == nocExtensionPoints(),
            "NoC handler should declare all semantic extension points");

    for (const QString& extensionPoint : nocExtensionPoints()) {
        const ExtensionContribution contribution =
            requireSingleContribution(extensionPoints, extensionPoint);
        require(contribution.ownerPluginId == QStringLiteral("finepaper.noc"),
                "NoC contribution owner plugin id should be stable");
        require(contribution.descriptor.value(QStringLiteral("capabilityId")).toString() ==
                    QStringLiteral("noc.v1"),
                "NoC contribution descriptor should name the handled capability");
        require(!contribution.descriptor.value(QStringLiteral("contributionKind")).toString().isEmpty(),
                "NoC contribution descriptor should name the contribution kind");
    }
}

void testNoCSemanticModelReadsRolesWithoutConcreteNames() {
    QJsonObject descriptor{
        {QStringLiteral("roles"), QJsonArray{
             QJsonObject{{QStringLiteral("module"), QStringLiteral("VendorSwitch")},
                         {QStringLiteral("semantic"), QStringLiteral("router")}},
             QJsonObject{{QStringLiteral("module"), QStringLiteral("VendorHost")},
                         {QStringLiteral("semantic"), QStringLiteral("endpoint")}},
             QJsonObject{{QStringLiteral("module"), QStringLiteral("IgnoredEmptyRole")}},
         }}
    };

    const NoCSemanticModel model = NoCSemanticModel::fromJson(descriptor);
    require(!model.isEmpty(), "semantic model should contain descriptor roles");
    require(model.semanticRoleForModule(QStringLiteral("VendorSwitch")) == QStringLiteral("router"),
            "router role should come from descriptor data");
    require(model.semanticRoleForModule(QStringLiteral("VendorHost")) == QStringLiteral("endpoint"),
            "endpoint role should come from descriptor data");
    require(model.semanticRoleForModule(QStringLiteral("IgnoredEmptyRole")).isEmpty(),
            "entries without semantic roles should be ignored");
    require(model.semanticRoleForModule(QStringLiteral("UnknownModule")).isEmpty(),
            "unknown modules should not produce concrete fallback roles");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testNoCPluginRegistersNocCapabilityAndDescriptorContributions();
        testNoCSemanticModelReadsRolesWithoutConcreteNames();
    } catch (const std::exception& error) {
        std::cerr << "nocplugin_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "nocplugin_test passed\n";
    return 0;
}
