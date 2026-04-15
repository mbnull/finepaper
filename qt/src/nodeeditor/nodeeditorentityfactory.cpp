#include "nodeeditor/nodeeditorentityfactory.h"

#include "graph/graph.h"
#include "graph/module.h"
#include "modules/modulelabels.h"
#include "modules/moduletypemetadata.h"
#include "modules/moduleregistry.h"

#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace {

int nextModuleIndex(const Graph* graph, const QString& moduleType, const QString& externalIdPrefix) {
    QSet<int> used;
    QRegularExpression pattern("^" + QRegularExpression::escape(externalIdPrefix) + "_(\\d+)$",
                               QRegularExpression::CaseInsensitiveOption);

    for (const auto& module : graph->modules()) {
        if (module->type() != moduleType) {
            continue;
        }

        const auto match = pattern.match(ModuleLabels::externalId(module.get()));
        if (match.hasMatch()) {
            used.insert(match.captured(1).toInt());
        }
    }

    int index = 0;
    while (used.contains(index)) {
        ++index;
    }
    return index;
}

void assignModuleIdentity(Graph* graph, Module* module) {
    if (!graph || !module) {
        return;
    }

    const QString externalPrefix = ModuleTypeMetadata::externalIdPrefix(module);
    const QString displayPrefix = ModuleTypeMetadata::displayPrefix(module);
    if (externalPrefix.isEmpty() || displayPrefix.isEmpty()) {
        return;
    }

    const int index = nextModuleIndex(graph, module->type(), externalPrefix);
    const int width = ModuleTypeMetadata::identityWidth(module);
    module->setParameter("display_name", QString("%1_%2").arg(displayPrefix).arg(index, width, 10, QChar('0')));
    module->setParameter("external_id", QString("%1_%2").arg(externalPrefix).arg(index, width, 10, QChar('0')));
}

} // namespace

namespace NodeEditorEntityFactory {

QString generateEntityId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
}

std::unique_ptr<Module> createModule(Graph* graph, const QString& moduleId, const QString& moduleType) {
    const ModuleType* type = ModuleRegistry::instance().getType(moduleType);
    if (!type) {
        return {};
    }

    auto module = std::make_unique<Module>(moduleId, moduleType);
    for (const auto& port : type->defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type->defaultParameters.constBegin(); it != type->defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }

    assignModuleIdentity(graph, module.get());
    return module;
}

} // namespace NodeEditorEntityFactory
