// Topology preset interaction handler implementation.
#include "app/topologypresetinteractionhandler.h"

#include "app/interactionids.h"
#include "app/pluginids.h"
#include "commands/commandmanager.h"
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"
#include "topology/topologypresetbuilder.h"
#include "workspace/activeworkspacecontroller.h"

#include <QDebug>
#include <QInputDialog>
#include <QMessageBox>
#include <algorithm>
#include <memory>
#include <optional>

namespace {

struct OrderedPresetParameter {
    QString name;
    QString label;
};

bool contextMatches(const ActiveWorkspaceContext& activeContext,
                    const QString& ipcoreId,
                    const QString& instanceId) {
    return activeContext.record.ipcoreId == ipcoreId &&
           activeContext.record.instanceId == instanceId;
}

std::optional<TopologyPresetDescriptor> findPreset(const IpCatalogEntry& entry,
                                                   const QString& presetId) {
    const auto presetIt = std::find_if(entry.topologyPresets.cbegin(),
                                       entry.topologyPresets.cend(),
                                       [&](const TopologyPresetDescriptor& preset) {
                                           return preset.id == presetId;
                                       });
    if (presetIt == entry.topologyPresets.cend()) {
        return std::nullopt;
    }
    return *presetIt;
}

QVector<OrderedPresetParameter> sortedPresetParameters(
    const TopologyPresetDescriptor& preset) {
    QVector<OrderedPresetParameter> orderedParameters;
    orderedParameters.reserve(preset.parameters.size());
    for (auto parameterIt = preset.parameters.cbegin();
         parameterIt != preset.parameters.cend();
         ++parameterIt) {
        OrderedPresetParameter parameter;
        parameter.name = parameterIt.key();
        parameter.label = parameterIt.value().label.trimmed().isEmpty()
            ? parameterIt.key()
            : parameterIt.value().label;
        orderedParameters.push_back(parameter);
    }

    std::sort(orderedParameters.begin(),
              orderedParameters.end(),
              [](const auto& left, const auto& right) {
                  const int labelOrder =
                      QString::compare(left.label, right.label, Qt::CaseInsensitive);
                  if (labelOrder != 0) {
                      return labelOrder < 0;
                  }
                  return left.name < right.name;
              });
    return orderedParameters;
}

std::optional<QHash<QString, int>> promptPresetParameters(
    QWidget* hostWidget,
    const TopologyPresetDescriptor& preset) {
    QHash<QString, int> parameters;
    for (const OrderedPresetParameter& orderedParameter : sortedPresetParameters(preset)) {
        const auto parameterIt = preset.parameters.constFind(orderedParameter.name);
        if (parameterIt == preset.parameters.constEnd()) {
            continue;
        }

        const TopologyPresetParameterDescriptor& parameter = parameterIt.value();
        bool ok = false;
        const int value = QInputDialog::getInt(hostWidget,
                                               preset.label,
                                               parameter.label,
                                               parameter.defaultValue,
                                               parameter.minimumValue,
                                               parameter.maximumValue,
                                               1,
                                               &ok);
        if (!ok) {
            return std::nullopt;
        }
        parameters.insert(orderedParameter.name, value);
    }
    return parameters;
}

} // namespace

TopologyPresetInteractionHandler::TopologyPresetInteractionHandler(
    QWidget* hostWidget,
    Graph* graph,
    CommandManager* commandManager,
    ActiveWorkspaceController* workspaceController,
    EditorMutationTarget* editorMutationTarget)
    : m_hostWidget(hostWidget),
      m_graph(graph),
      m_commandManager(commandManager),
      m_workspaceController(workspaceController),
      m_editorMutationTarget(editorMutationTarget) {}

bool TopologyPresetInteractionHandler::registerHandlers(
    PluginInteractionRegistry& interactions) const {
    PluginInteractionHandlerDescriptor topologyHandler;
    topologyHandler.id = app::interactionids::nocTopologyPresetHandler();
    topologyHandler.ownerPluginId = app::pluginids::nocPlugin();
    topologyHandler.interactionKind = app::interactionids::topologyPreset();
    topologyHandler.handler = [this](const PluginInteractionDescriptor& interaction,
                                     const PluginInteractionContext& context) {
        return handleInteraction(interaction, context);
    };
    return interactions.registerHandler(topologyHandler);
}

PluginInteractionResult TopologyPresetInteractionHandler::handleInteraction(
    const PluginInteractionDescriptor& interaction,
    const PluginInteractionContext& context) const {
    PluginInteractionResult result;
    result.handled = true;

    if (!m_graph || !m_commandManager || !m_workspaceController) {
        result.message = QStringLiteral("Topology interaction handler is not ready.");
        return result;
    }

    const QString presetId = interaction.descriptor.value(QStringLiteral("presetId"))
                                 .toString()
                                 .trimmed();
    if (context.ipcoreId.trimmed().isEmpty() ||
        context.instanceId.trimmed().isEmpty() ||
        presetId.isEmpty()) {
        result.message = QStringLiteral("Topology interaction request is incomplete.");
        return result;
    }

    const std::optional<ActiveWorkspaceContext> activeContext =
        m_workspaceController->activeContext();
    if (!activeContext.has_value() ||
        !contextMatches(*activeContext, context.ipcoreId, context.instanceId)) {
        result.message = QStringLiteral("Topology interaction does not match the active IP instance.");
        return result;
    }

    const std::optional<TopologyPresetDescriptor> preset =
        findPreset(activeContext->entry, presetId);
    if (!preset.has_value()) {
        result.message = QStringLiteral("Topology interaction does not name a known preset.");
        return result;
    }

    TopologyPresetRequest request;
    request.ipcoreId = context.ipcoreId;
    request.instanceId = context.instanceId;
    request.preset = *preset;

    const std::optional<QHash<QString, int>> parameters =
        promptPresetParameters(m_hostWidget, *preset);
    if (!parameters.has_value()) {
        result.success = true;
        return result;
    }
    request.parameters = *parameters;

    const std::optional<ActiveWorkspaceContext> contextBeforeExecute =
        m_workspaceController->activeContext();
    if (!contextBeforeExecute.has_value() ||
        !contextMatches(*contextBeforeExecute, context.ipcoreId, context.instanceId)) {
        result.message = QStringLiteral("Topology interaction changed active IP instance before execution.");
        return result;
    }

    Q_UNUSED(request);
    result.message = QStringLiteral(
        "Topology presets require the design-level patch planner.");
    QMessageBox::warning(m_hostWidget, QStringLiteral("Topology"), result.message);
    return result;
}
