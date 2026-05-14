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

inline QString packageId(const ModuleType* moduleType) {
    if (!moduleType) return {};
    return moduleType->packageId.isEmpty() ? moduleType->ipcoreId : moduleType->packageId;
}

inline QString packageId(const Module* module) {
    return packageId(type(module));
}

inline QString moduleId(const ModuleType* moduleType) {
    if (!moduleType) return {};
    return moduleType->moduleId.isEmpty() ? moduleType->name : moduleType->moduleId;
}

inline QString moduleId(const Module* module) {
    return moduleId(type(module));
}

inline QString graphRole(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->graphRole : QString();
}

inline QString viewFilePath(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->viewFilePath : QString();
}

inline QString description(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->description : QString();
}

inline QString description(const ModuleType* moduleType) {
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

inline const ModuleInterfaceMetadata* interfaceMetadata(const Module* module, const QString& interfaceId) {
    const ModuleType* moduleType = type(module);
    if (!moduleType || interfaceId.isEmpty()) {
        return nullptr;
    }

    const auto it = moduleType->interfaceMetadata.find(interfaceId);
    return it != moduleType->interfaceMetadata.end() ? &it.value() : nullptr;
}

inline const ModuleInterfaceAnchor* interfaceAnchor(const Module* module, const QString& interfaceId) {
    const ModuleType* moduleType = type(module);
    if (!moduleType || interfaceId.isEmpty()) {
        return nullptr;
    }

    const auto it = moduleType->interfaceAnchors.find(interfaceId);
    return it != moduleType->interfaceAnchors.end() ? &it.value() : nullptr;
}

inline const ModuleInterfaceAnchor* interfaceAnchor(const Module* module, const Port& port) {
    const QString interfaceId = port.interfaceId().isEmpty() ? port.id() : port.interfaceId();
    return interfaceAnchor(module, interfaceId);
}

inline const ModuleAttachmentZone* attachmentZone(const Module* module, const QString& zoneId) {
    const ModuleType* moduleType = type(module);
    if (!moduleType || zoneId.isEmpty()) {
        return nullptr;
    }

    const auto it = moduleType->attachmentZones.find(zoneId);
    return it != moduleType->attachmentZones.end() ? &it.value() : nullptr;
}

inline const ModuleAttachmentZone* attachmentZone(const Module* module, const Port& port) {
    const QString interfaceId = port.interfaceId().isEmpty() ? port.id() : port.interfaceId();
    return attachmentZone(module, interfaceId);
}

inline QString interfaceLabel(const Module* module, const Port& port) {
    const ModuleInterfaceAnchor* anchor = interfaceAnchor(module, port);
    if (anchor && !anchor->label.isEmpty()) {
        return anchor->label;
    }

    const QString interfaceId = port.interfaceId().isEmpty() ? port.id() : port.interfaceId();
    const ModuleInterfaceMetadata* metadata = interfaceMetadata(module, interfaceId);
    if (metadata && !metadata->label.isEmpty()) {
        return metadata->label;
    }

    return port.name();
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
