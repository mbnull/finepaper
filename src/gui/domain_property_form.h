#pragma once

#include "gui/schema_value_editor.h"

#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QFormLayout;

namespace finepaper {

enum class PropertyInitialization {
    CreateWithDefaults,
    ExactValues
};

struct DomainPropertyFormOptions {
    PropertyInitialization initialization = PropertyInitialization::ExactValues;
    PropertyValidationMode validationMode = PropertyValidationMode::Complete;
    bool allowCustomReferences = false;
};

class DomainPropertyForm final : public QWidget {
public:
    explicit DomainPropertyForm(QWidget* parent = nullptr);

    void setSchema(const QVector<DomainPropertyDefinition>& definitions,
                   const QVector<DomainDefinition>& draftDomains,
                   const QJsonObject& values,
                   DomainPropertyFormOptions options = {});

    // Compatibility overload for the existing strict, single-Domain editor.
    void setSchema(const QVector<DomainPropertyDefinition>& definitions,
                   const QVector<DomainDefinition>& draftDomains,
                   const QJsonObject& values,
                   PropertyInitialization initialization);

    [[nodiscard]] QJsonObject values() const;
    [[nodiscard]] QStringList localErrors() const;
    [[nodiscard]] bool locallyValid() const { return localErrors().isEmpty(); }

    std::function<void()> valuesChanged;

private:
    struct PropertyRow {
        DomainPropertyDefinition definition;
        SchemaValueEditor* editor = nullptr;
    };

    void clearRows();

    QFormLayout* m_form = nullptr;
    QVector<PropertyRow> m_rows;
    QJsonObject m_passthroughValues;
};

} // namespace finepaper
