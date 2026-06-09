// ModuleLabels provides utility functions for extracting and formatting module display names
#pragma once

#include "graph/module.h"
#include "modules/moduletypemetadata.h"
#include <QRegularExpression>

namespace ModuleLabels {

inline QString humanizeIdentifier(const QString& identifier) {
    if (identifier.isEmpty()) {
        return {};
    }

    QString text = identifier;
    text.replace('-', ' ');
    text.replace('_', ' ');

    bool capitalizeNext = true;
    for (int index = 0; index < text.size(); ++index) {
        if (text[index].isSpace()) {
            capitalizeNext = true;
            continue;
        }

        if (capitalizeNext) {
            text[index] = text[index].toUpper();
            capitalizeNext = false;
        }
    }

    return text;
}

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

inline QString staticModuleName(const Module* module) {
    const ModuleType* type = ModuleTypeMetadata::type(module);
    if (type) {
        const QString label = ModuleTypeMetadata::paletteLabel(type).trimmed();
        if (!label.isEmpty()) {
            return label;
        }

        const QString moduleId = ModuleTypeMetadata::moduleId(type).trimmed();
        if (!moduleId.isEmpty()) {
            return moduleId;
        }
    }

    if (!module) {
        return {};
    }

    const QString moduleType = module->type().trimmed();
    return moduleType.isEmpty() ? module->id() : moduleType;
}

inline QString userFacingName(const Module* module) {
    const ModuleType* type = ModuleTypeMetadata::type(module);
    const QString binding = type ? type->displayLabelParameter.trimmed() : QString();
    if (!binding.isEmpty()) {
        const QString value = stringParameter(module, binding).trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }

    return staticModuleName(module);
}

inline QString shortDisambiguator(const Module* module) {
    const ModuleType* type = ModuleTypeMetadata::type(module);
    const QString binding = type ? type->shortLabelParameter.trimmed() : QString();
    if (binding.isEmpty()) {
        return {};
    }

    return stringParameter(module, binding).trimmed();
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
            return QString("%1_%2%3")
                .arg(displayPrefix)
                .arg(meshMatch.captured(1))
                .arg(meshMatch.captured(2));
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
