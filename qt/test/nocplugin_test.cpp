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

#include <array>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct ExpectedNoCContribution {
    const char* extensionPoint;
    const char* id;
    const char* contributionKind;
};

constexpr std::array<ExpectedNoCContribution, 5> kExpectedNoCContributions{{
    {"ui.inspectorSection", "finepaper.noc.inspector-section", "inspectorSection"},
    {"editor.tool", "finepaper.noc.editor-tool", "editorTool"},
    {"connection.ruleProvider", "finepaper.noc.connection-rule-provider", "ruleProvider"},
    {"tool.flowInputProjector", "finepaper.noc.flow-input-projector", "flowInputProjector"},
    {"artifact.presenter", "finepaper.noc.artifact-presenter", "artifactPresenter"},
}};

QStringList nocExtensionPoints() {
    QStringList result;
    result.reserve(static_cast<qsizetype>(kExpectedNoCContributions.size()));
    for (const ExpectedNoCContribution& contribution : kExpectedNoCContributions) {
        result.append(QString::fromLatin1(contribution.extensionPoint));
    }
    return result;
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

    const QVector<ExtensionContribution> allContributions = extensionPoints.allContributions();
    require(allContributions.size() == static_cast<qsizetype>(kExpectedNoCContributions.size()),
            "NoC plugin should register exactly five contributions");

    for (qsizetype index = 0; index < allContributions.size(); ++index) {
        const ExpectedNoCContribution& expected =
            kExpectedNoCContributions.at(static_cast<std::size_t>(index));
        const QString extensionPoint = QString::fromLatin1(expected.extensionPoint);
        const QString id = QString::fromLatin1(expected.id);
        const QString contributionKind = QString::fromLatin1(expected.contributionKind);
        const ExtensionContribution contribution =
            requireSingleContribution(extensionPoints, extensionPoint);
        require(allContributions.at(index).id == id,
                "NoC contribution id order should be externally stable");
        require(contribution.id == id, "NoC contribution id should be externally stable");
        require(contribution.extensionPoint == extensionPoint,
                "NoC contribution extension point should be externally stable");
        require(contribution.ownerPluginId == QStringLiteral("finepaper.noc"),
                "NoC contribution owner plugin id should be stable");
        require(contribution.descriptor.value(QStringLiteral("capabilityId")).toString() ==
                    QStringLiteral("noc.v1"),
                "NoC contribution descriptor should name the handled capability");
        require(contribution.descriptor.value(QStringLiteral("extensionPoint")).toString() ==
                    extensionPoint,
                "NoC contribution descriptor should name the extension point");
        require(contribution.descriptor.value(QStringLiteral("contributionKind")).toString() ==
                    contributionKind,
                "NoC contribution descriptor should map to the expected kind");
    }
}

void testNoCSemanticModelReadsRolesWithoutConcreteNames() {
    const QJsonObject descriptor = {
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
