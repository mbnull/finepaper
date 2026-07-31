#include "application/design_extension_references.h"

#include <QJsonArray>
#include <QJsonObject>

#include <limits>
#include <utility>

namespace finepaper {
namespace {

struct ValidationState {
    QVector<Diagnostic> diagnostics;
    quint64 evaluationSteps = 0;
    bool aborted = false;
};

void appendDiagnostic(ValidationState& state,
                      const QString& code,
                      const QString& message,
                      const QString& path) {
    if (state.aborted) {
        return;
    }
    if (state.diagnostics.size()
        >= kMaximumDesignExtensionDomainReferenceDiagnostics - 1) {
        state.diagnostics.append(Diagnostic{
            QStringLiteral("error"),
            QStringLiteral(
                "design.extension_domain_reference_diagnostics_truncated"),
            QStringLiteral(
                "Domain reference validation stopped after reaching the diagnostic limit"),
            path,
            QStringLiteral("package")});
        state.aborted = true;
        return;
    }
    state.diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        code,
        message,
        path,
        QStringLiteral("package")});
}

bool consumeEvaluationStep(ValidationState& state, const QString& path) {
    if (state.aborted) {
        return false;
    }
    if (state.evaluationSteps
        >= kMaximumDesignExtensionDomainReferenceEvaluationSteps) {
        appendDiagnostic(
            state,
            QStringLiteral(
                "design.extension_domain_reference_evaluation_budget_exceeded"),
            QStringLiteral(
                "Domain reference validation exceeded the evaluation budget"),
            path);
        state.aborted = true;
        return false;
    }
    ++state.evaluationSteps;
    return true;
}

QString jsonPointerToken(QString value) {
    value.replace(QLatin1Char('~'), QStringLiteral("~0"));
    value.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return value;
}

QString childPath(const QString& path, const QString& token) {
    return path + QLatin1Char('/') + jsonPointerToken(token);
}

bool canonicalArrayIndex(const QString& token, qsizetype* index) {
    if (token.isEmpty()
        || (token.size() > 1 && token.front() == QLatin1Char('0'))) {
        return false;
    }
    for (qsizetype characterIndex = 0;
         characterIndex < token.size(); ++characterIndex) {
        const char16_t character = token.at(characterIndex).unicode();
        if (character < u'0' || character > u'9') {
            return false;
        }
    }

    bool converted = false;
    const qulonglong candidate = token.toULongLong(&converted);
    if (!converted
        || candidate > static_cast<qulonglong>(
            (std::numeric_limits<qsizetype>::max)())) {
        return false;
    }
    *index = static_cast<qsizetype>(candidate);
    return true;
}

void appendInvalidContainer(
    ValidationState& state,
    const DesignExtensionDomainReferenceDefinition& reference,
    const QString& path,
    const QString& expected) {
    appendDiagnostic(
        state,
        QStringLiteral("design.extension_domain_reference_invalid_container"),
        QStringLiteral(
            "Domain reference pattern %1 expected %2 at this location")
            .arg(reference.pointer(), expected),
        path);
}

void validateTerminal(
    const QJsonValue& value,
    const DesignExtensionDomainReferenceDefinition& reference,
    const DesignDomainReferenceIndex& domains,
    const QString& path,
    ValidationState& state) {
    if (!value.isString()) {
        appendDiagnostic(
            state,
            QStringLiteral("design.extension_domain_reference_invalid_type"),
            QStringLiteral(
                "Domain reference must be a string naming a %1 Domain")
                .arg(reference.domainType),
            path);
        return;
    }

    const QString domainId = value.toString();
    const QString* domainType = domains.typeForId(domainId);
    if (!domainType) {
        const QString quotedDomainId =
            QStringLiteral("\"%1\"").arg(domainId);
        appendDiagnostic(
            state,
            QStringLiteral("design.extension_unknown_domain_reference"),
            QStringLiteral(
                "Domain reference id %1 does not name an existing Domain")
                .arg(quotedDomainId),
            path);
        return;
    }
    if (*domainType != reference.domainType) {
        appendDiagnostic(
            state,
            QStringLiteral("design.extension_domain_reference_type_mismatch"),
            QStringLiteral("Domain %1 has type %2; expected %3")
                .arg(domainId, *domainType, reference.domainType),
            path);
    }
}

void visitReference(
    const QJsonValue& value,
    const DesignExtensionDomainReferenceDefinition& reference,
    qsizetype tokenIndex,
    const DesignDomainReferenceIndex& domains,
    const QString& path,
    ValidationState& state) {
    if (!consumeEvaluationStep(state, path)) {
        return;
    }
    if (tokenIndex == reference.pointerTokens.size()) {
        validateTerminal(value, reference, domains, path, state);
        return;
    }

    const QString& token = reference.pointerTokens.at(tokenIndex);
    if (token == QStringLiteral("*")) {
        if (!value.isArray()) {
            appendInvalidContainer(
                state, reference, path, QStringLiteral("an array"));
            return;
        }
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0;
             index < array.size() && !state.aborted; ++index) {
            visitReference(
                array.at(index),
                reference,
                tokenIndex + 1,
                domains,
                path + QLatin1Char('/') + QString::number(index),
                state);
        }
        return;
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (!object.contains(token)) {
            return;
        }
        visitReference(
            object.value(token),
            reference,
            tokenIndex + 1,
            domains,
            childPath(path, token),
            state);
        return;
    }

    if (value.isArray()) {
        qsizetype index = 0;
        if (!canonicalArrayIndex(token, &index)) {
            appendInvalidContainer(
                state,
                reference,
                path,
                QStringLiteral("a canonical array index"));
            return;
        }
        const QJsonArray array = value.toArray();
        if (index >= array.size()) {
            return;
        }
        visitReference(
            array.at(index),
            reference,
            tokenIndex + 1,
            domains,
            path + QLatin1Char('/') + QString::number(index),
            state);
        return;
    }

    appendInvalidContainer(
        state,
        reference,
        path,
        QStringLiteral("an object or array"));
}

} // namespace

DesignDomainReferenceIndex DesignDomainReferenceIndex::fromDomains(
    const QVector<DomainDefinition>& domains) {
    DesignDomainReferenceIndex index;
    index.m_typesById.reserve(domains.size());
    for (const DomainDefinition& domain : domains) {
        index.m_typesById.insert(domain.id, domain.type);
    }
    return index;
}

const QString* DesignDomainReferenceIndex::typeForId(
    const QString& id) const {
    const auto found = m_typesById.constFind(id);
    return found == m_typesById.cend() ? nullptr : &found.value();
}

QVector<Diagnostic> validateDesignExtensionDomainReferences(
    const QJsonValue& value,
    const DesignExtensionDefinition& definition,
    const DesignDomainReferenceIndex& domains,
    const QString& basePath) {
    ValidationState state;
    if (definition.domainReferences.isEmpty()) {
        return {};
    }

    for (const DesignExtensionDomainReferenceDefinition& reference
         : definition.domainReferences) {
        if (state.aborted) {
            break;
        }
        visitReference(
            value,
            reference,
            0,
            domains,
            basePath,
            state);
    }
    return std::exchange(state.diagnostics, {});
}

QVector<Diagnostic> validateDesignExtensionDomainReferences(
    const QJsonValue& value,
    const DesignExtensionDefinition& definition,
    const QVector<DomainDefinition>& domains,
    const QString& basePath) {
    if (definition.domainReferences.isEmpty()) {
        return {};
    }
    return validateDesignExtensionDomainReferences(
        value,
        definition,
        DesignDomainReferenceIndex::fromDomains(domains),
        basePath);
}

} // namespace finepaper
