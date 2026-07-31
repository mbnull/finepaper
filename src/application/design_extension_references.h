#pragma once

#include "noc/model.h"
#include "package/package.h"

#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace finepaper {

inline constexpr quint64
    kMaximumDesignExtensionDomainReferenceEvaluationSteps = 1'000'000;
inline constexpr qsizetype
    kMaximumDesignExtensionDomainReferenceDiagnostics = 256;

// Immutable lookup shared across a batch of extension validations. Building
// it once avoids repeatedly indexing the same Design Domain snapshot.
class DesignDomainReferenceIndex {
public:
    [[nodiscard]] static DesignDomainReferenceIndex fromDomains(
        const QVector<DomainDefinition>& domains);
    [[nodiscard]] const QString* typeForId(const QString& id) const;

private:
    QHash<QString, QString> m_typesById;
};

// Validates Package-declared references from one Design Extension value into
// the Design's generic Domain collection. Pointer patterns are decoded by the
// Package boundary; `*` visits every item of an array while ordinary tokens
// select object properties or canonical array indices.
[[nodiscard]] QVector<Diagnostic> validateDesignExtensionDomainReferences(
    const QJsonValue& value,
    const DesignExtensionDefinition& definition,
    const DesignDomainReferenceIndex& domains,
    const QString& basePath = {});
[[nodiscard]] QVector<Diagnostic> validateDesignExtensionDomainReferences(
    const QJsonValue& value,
    const DesignExtensionDefinition& definition,
    const QVector<DomainDefinition>& domains,
    const QString& basePath = {});

} // namespace finepaper
