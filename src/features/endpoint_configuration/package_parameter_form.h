#pragma once

#include "ui/common/schema_value_editor.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QVBoxLayout;

namespace finepaper {

using PackageParameterDraft = QHash<QString, SchemaValueEditorDraft>;

struct PackageParameterEditorSnapshot {
    QJsonObject values;
    PackageParameterDraft draftValues;
    QStringList localErrors;
    bool modified = false;
};

// Generic editor for the complete ParameterDefinition schema used by design
// and Endpoint parameters.  It deliberately knows nothing about concrete
// parameter ids: type widgets, defaults, validation, units, descriptions and
// presentation grouping all come from the active Package.
class PackageParameterForm final : public QWidget {
public:
    explicit PackageParameterForm(
        QString objectNamePrefix,
        QWidget* parent = nullptr);

    void setSchema(const QVector<ParameterDefinition>& definitions,
                   const QJsonObject& values);
    void setValues(const QJsonObject& values);
    void setDraftValues(const PackageParameterDraft& values);

    [[nodiscard]] QJsonObject values() const;
    [[nodiscard]] PackageParameterDraft draftValues() const;
    [[nodiscard]] QStringList localErrors() const;
    [[nodiscard]] PackageParameterEditorSnapshot editorSnapshot() const;
    [[nodiscard]] bool locallyValid() const { return localErrors().isEmpty(); }
    [[nodiscard]] bool isEmpty() const { return m_controls.isEmpty(); }
    [[nodiscard]] const QString& schemaIdentity() const {
        return m_schemaIdentity;
    }
    // Compare the complete editor draft rather than only its JSON projection.
    // A temporarily invalid token (for example "1e") is still a user change
    // that must be protected from selection and document transitions.
    [[nodiscard]] bool isModified() const;

    std::function<void()> valueChanged;

private:
    struct Control {
        ParameterDefinition definition;
        SchemaValueEditor* editor = nullptr;
    };

    void rebuild(const QJsonObject& values);
    void loadValues(const QJsonObject& values, bool resetBaseline);
    void notifyValueChanged();

    QString m_objectNamePrefix;
    QString m_schemaIdentity;
    QVector<ParameterDefinition> m_definitions;
    QVector<Control> m_controls;
    PackageParameterDraft m_baselineDraftValues;
    QVBoxLayout* m_rootLayout = nullptr;
    QWidget* m_content = nullptr;
};

} // namespace finepaper
