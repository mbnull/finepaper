// ModuleTypeMetadata centralizes editor/runtime behavior derived from ModuleType.
#pragma once

#include "module.h"
#include "moduleregistry.h"
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

inline bool isInGraphGroup(const Module* module, QStringView graphGroupName) {
    return graphGroup(module) == graphGroupName;
}

inline QString nodeColor(const Module* module) {
    const ModuleType* moduleType = type(module);
    return moduleType ? moduleType->nodeColor : QString();
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

} // namespace ModuleTypeMetadata
