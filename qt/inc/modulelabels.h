// ModuleLabels provides utility functions for extracting and formatting module display names
#pragma once

#include "module.h"
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
    return stringParameter(module, "display_name", module ? module->id() : QStringLiteral("Node"));
}

inline QString externalId(const Module* module) {
    QString external = stringParameter(module, "external_id");
    if (!external.isEmpty()) return external;

    return displayName(module).toLower();
}

inline QString humanizeExternalId(const QString& moduleType, const QString& rawId) {
    if (rawId.isEmpty()) return moduleType;

    if (moduleType == "XP") {
        QRegularExpression meshPattern("^xp_(\\d+)_(\\d+)$", QRegularExpression::CaseInsensitiveOption);
        auto meshMatch = meshPattern.match(rawId);
        if (meshMatch.hasMatch()) {
            return QString("XP_%1%2").arg(meshMatch.captured(1), meshMatch.captured(2));
        }

        QRegularExpression seqPattern("^xp_(\\d+)$", QRegularExpression::CaseInsensitiveOption);
        auto seqMatch = seqPattern.match(rawId);
        if (seqMatch.hasMatch()) {
            return QString("XP_%1").arg(seqMatch.captured(1).toInt(), 2, 10, QChar('0'));
        }
    }

    if (moduleType == "Endpoint") {
        QRegularExpression seqPattern("^ep_(\\d+)$", QRegularExpression::CaseInsensitiveOption);
        auto seqMatch = seqPattern.match(rawId);
        if (seqMatch.hasMatch()) {
            return QString("EP_%1").arg(seqMatch.captured(1).toInt(), 2, 10, QChar('0'));
        }
    }

    return rawId.toUpper();
}

} // namespace ModuleLabels
