// ModuleTypeMetadata centralizes editor/runtime behavior derived from ModuleType.
#pragma once

#include "graph/module.h"
#include "modules/moduleregistry.h"
#include <QStringView>

namespace ModuleTypeMetadata {

inline const ModuleType* type(const QString& typeName) {
    return ModuleRegistry::instance().getType(typeName);
}

inline const ModuleType* type(const Module* module) {
    return module ? type(module->type()) : nullptr;
}

inline QString paletteLabel(const ModuleType* moduleType) {
    if (!moduleType) return {};
    return moduleType->paletteLabel.isEmpty() ? moduleType->name : moduleType->paletteLabel;
}

inline QString paletteLabel(const Module* module) {
    return paletteLabel(type(module));
}

inline QString editorLayout(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType && !moduleType->editorLayout.isEmpty()
        ? moduleType->editorLayout
        : QStringLiteral("default");
}

inline bool hasEditorLayout(const Module* module, QStringView layout) {
    return editorLayout(module) == layout;
}

inline QString graphGroup(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->graphGroup : QString();
}

inline QString description(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->description : QString();
}

inline bool isInGraphGroup(const Module* module, QStringView graphGroupName) {
    return graphGroup(module) == graphGroupName;
}

inline QString nodeColor(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->nodeColor : QString();
}

inline int expandedNodeMinWidth(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->expandedNodeMinWidth : ModuleType{}.expandedNodeMinWidth;
}

inline int expandedNodeHeight(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->expandedNodeHeight : ModuleType{}.expandedNodeHeight;
}

inline int collapsedNodeMinWidth(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->collapsedNodeMinWidth : expandedNodeMinWidth(module);
}

inline int collapsedNodeHeight(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->collapsedNodeHeight : expandedNodeHeight(module);
}

inline qreal captionLeftInset(const Module* module, bool collapsed) {
    const ModuleType* moduleType = type(module);
    if (!moduleType) {
        const ModuleType defaults;
        return collapsed ? defaults.collapsedCaptionLeftInset : defaults.expandedCaptionLeftInset;
    }
    return collapsed ? moduleType->collapsedCaptionLeftInset : moduleType->expandedCaptionLeftInset;
}

inline qreal captionTopInset(const Module* module, bool collapsed) {
    const ModuleType* moduleType = type(module);
    if (!moduleType) {
        const ModuleType defaults;
        return collapsed ? defaults.collapsedCaptionTopInset : defaults.expandedCaptionTopInset;
    }
    return collapsed ? moduleType->collapsedCaptionTopInset : moduleType->expandedCaptionTopInset;
}

inline qreal expandedPortInset(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->expandedPortInset : ModuleType{}.expandedPortInset;
}

inline qreal collapsedEndpointPortInset(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->collapsedEndpointPortInset : ModuleType{}.collapsedEndpointPortInset;
}

inline int linkedEndpointOffsetX(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->linkedEndpointOffsetX : ModuleType{}.linkedEndpointOffsetX;
}

inline int meshSpacingX(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->meshSpacingX : ModuleType{}.meshSpacingX;
}

inline int meshSpacingY(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->meshSpacingY : ModuleType{}.meshSpacingY;
}

inline int looseEndpointSpacingX(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->looseEndpointSpacingX : ModuleType{}.looseEndpointSpacingX;
}

inline int looseEndpointSpacingY(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->looseEndpointSpacingY : ModuleType{}.looseEndpointSpacingY;
}

inline int looseEndpointMarginY(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->looseEndpointMarginY : ModuleType{}.looseEndpointMarginY;
}

inline bool supportsCollapse(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType && moduleType->supportsCollapse;
}

inline QString externalIdPrefix(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->externalIdPrefix : QString();
}

inline QString displayPrefix(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->displayPrefix : QString();
}

inline int identityWidth(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->identityWidth : 2;
}

inline bool supportsMeshCoordinates(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType && moduleType->supportsMeshCoordinates;
}

inline const QVector<ModuleConfigField>& configFields(const Module* module) {
    static const QVector<ModuleConfigField> emptyFields;
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->configFields : emptyFields;
}

inline const ModuleParameterMetadata* parameterMetadata(const Module* module, const QString& parameterName) {
    const ModuleType* moduleType = type(module);
    if (!moduleType) {
        return nullptr;
    }

    const auto it = moduleType->parameterMetadata.find(parameterName);
    return it != moduleType->parameterMetadata.end() ? &it.value() : nullptr;
}

} // namespace ModuleTypeMetadata
