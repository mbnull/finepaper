#include "noc/nocplugin.h"

#include "app/appcontext.h"
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/pluginhost.h"

#include <QJsonObject>

#include <array>
#include <stdexcept>

namespace {

struct ContributionDefinition {
    const char* extensionPoint;
    const char* idSuffix;
    const char* label;
    const char* contributionKind;
};

constexpr std::array<ContributionDefinition, 5> kContributions{{
    {"ui.inspectorSection", "inspector-section", "NoC Inspector", "inspectorSection"},
    {"editor.tool", "editor-tool", "NoC Editor Tool", "editorTool"},
    {"connection.ruleProvider", "connection-rule-provider", "NoC Connection Rules", "ruleProvider"},
    {"tool.flowInputProjector", "flow-input-projector", "NoC Flow Input Projector", "flowInputProjector"},
    {"artifact.presenter", "artifact-presenter", "NoC Artifact Presenter", "artifactPresenter"},
}};

QString capabilityId() {
    return QStringLiteral("noc.v1");
}

QString pluginId() {
    return QStringLiteral("finepaper.") + QStringLiteral("noc");
}

QStringList extensionPoints() {
    QStringList result;
    result.reserve(static_cast<qsizetype>(kContributions.size()));
    for (const ContributionDefinition& definition : kContributions) {
        result.append(QString::fromLatin1(definition.extensionPoint));
    }
    return result;
}

QJsonObject descriptorFor(const ContributionDefinition& definition) {
    return QJsonObject{
        {QStringLiteral("capabilityId"), capabilityId()},
        {QStringLiteral("contributionKind"), QString::fromLatin1(definition.contributionKind)},
        {QStringLiteral("extensionPoint"), QString::fromLatin1(definition.extensionPoint)},
    };
}

class NoCPlugin final : public IAppPlugin {
public:
    QString id() const override {
        return pluginId();
    }

    void activate(AppContext& context) override {
        if (!context.capabilities) {
            throw std::runtime_error("CapabilityRegistry is required before activating NoCPlugin.");
        }
        if (!context.extensionPoints) {
            throw std::runtime_error("ExtensionPointRegistry is required before activating NoCPlugin.");
        }

        CapabilityHandlerDescriptor handler;
        handler.capabilityId = capabilityId();
        handler.ownerPluginId = pluginId();
        handler.extensionPoints = extensionPoints();
        if (!context.capabilities->registerHandler(handler)) {
            throw std::runtime_error("NoC capability handler could not be registered.");
        }

        for (const ContributionDefinition& definition : kContributions) {
            ExtensionContribution contribution;
            contribution.id = pluginId() + QStringLiteral(".") +
                QString::fromLatin1(definition.idSuffix);
            contribution.extensionPoint = QString::fromLatin1(definition.extensionPoint);
            contribution.ownerPluginId = pluginId();
            contribution.label = QString::fromLatin1(definition.label);
            contribution.descriptor = descriptorFor(definition);
            if (!context.extensionPoints->registerContribution(contribution)) {
                throw std::runtime_error("NoC extension contribution could not be registered.");
            }
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createNoCPlugin() {
    return std::make_unique<NoCPlugin>();
}
