// ModuleLabels provides utility functions for extracting and formatting module display names
#pragma once

#include "graph/module.h"
#include "modules/moduletypemetadata.h"
#include <QRegularExpression>

namespace ModuleLabels {

inline QString stringParameter(const Module* module, const QString& name, const QString& fallback = {}) {
    if (!module) return fallback;

    auto it = module->parameters().find(name);
    if (it == module->parameters().end()) return fallback;

    const auto& value = it.value().value();
    if (auto* text = std::get_if<QString>(&value)) {
        return *text;
    }

    return fallback;
}

inline QString displayName(const Module* module) {
    return stringParameter(module, "display_name", module ? module->id() : QString());
}

inline QString externalId(const Module* module) {
    QString external = stringParameter(module, "external_id");
    if (!external.isEmpty()) return external;

    return displayName(module).toLower();
}

inline QString humanizeExternalId(const QString& moduleType, const QString& rawId) {
    const ModuleType* type = ModuleTypeMetadata::type(moduleType);
    if (rawId.isEmpty()) return type ? ModuleTypeMetadata::paletteLabel(type) : moduleType;

    const QString externalPrefix = type ? type->externalIdPrefix : QString();
    const QString displayPrefix = type ? type->displayPrefix : QString();
    const int identityWidth = type ? type->identityWidth : 2;

    if (type && type->supportsMeshCoordinates && !externalPrefix.isEmpty() && !displayPrefix.isEmpty()) {
        QRegularExpression meshPattern("^" + QRegularExpression::escape(externalPrefix) + "_(\\d+)_(\\d+)$",
                                       QRegularExpression::CaseInsensitiveOption);
        auto meshMatch = meshPattern.match(rawId);
        if (meshMatch.hasMatch()) {
            return QString("%1_%2%3").arg(displayPrefix, meshMatch.captured(1), meshMatch.captured(2));
        }
    }

    if (!externalPrefix.isEmpty() && !displayPrefix.isEmpty()) {
        QRegularExpression seqPattern("^" + QRegularExpression::escape(externalPrefix) + "_(\\d+)$",
                                      QRegularExpression::CaseInsensitiveOption);
        auto seqMatch = seqPattern.match(rawId);
        if (seqMatch.hasMatch()) {
            return QString("%1_%2").arg(displayPrefix).arg(seqMatch.captured(1).toInt(), identityWidth, 10, QChar('0'));
        }
    }

    return rawId.toUpper();
}

} // namespace ModuleLabels
