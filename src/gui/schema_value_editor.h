#pragma once

#include "package/package.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace finepaper {

struct SchemaChoice {
    QString value;
    QString label;
    bool valid = true;

    bool operator==(const SchemaChoice&) const = default;
};

struct SchemaValueOptions {
    bool multiple = false;
    bool required = false;
    std::optional<QString> referenceDomainType;
    QVector<SchemaChoice> choices;
};

// SchemaValueEditor owns the distinction between an absent JSON property and a
// present false/zero/empty-string/empty-array value.  It deliberately keeps
// conversion and validation beside the widget instead of making callers infer
// JSON values from concrete Qt widget classes.
class SchemaValueEditor final : public QWidget {
public:
    explicit SchemaValueEditor(ParameterDefinition definition,
                               SchemaValueOptions options = {},
                               QWidget* parent = nullptr);

    void setValue(
        std::optional<QJsonValue> value,
        std::optional<QJsonValue> absentSuggestion = std::nullopt);
    [[nodiscard]] std::optional<QJsonValue> value() const;
    [[nodiscard]] QStringList localErrors() const;
    [[nodiscard]] bool locallyValid() const { return localErrors().isEmpty(); }

    // These callbacks avoid a moc dependency and keep the editor usable by the
    // lightweight GUI test targets.
    std::function<void()> valueChanged;

private:
    class ScalarValueWidget;

    void setPresent(bool present);
    [[nodiscard]] bool isPresent() const;
    void notifyValueChanged();
    void addMultipleValue(const QJsonValue& value);
    void rebuildMultipleValues(const QVector<QJsonValue>& values,
                               int selectedRow = -1);
    [[nodiscard]] QVector<QJsonValue> multipleValues() const;
    void updateMultipleButtons();
    [[nodiscard]] QJsonValue defaultItemValue() const;
    [[nodiscard]] QString objectNamePrefix() const;

    ParameterDefinition m_definition;
    SchemaValueOptions m_options;
    bool m_updating = false;
    bool m_preserveInvalidMultipleValue = false;
    QJsonValue m_invalidMultipleValue;
    bool m_preserveInvalidBooleanValue = false;
    QJsonValue m_invalidBooleanValue;
    std::optional<QJsonValue> m_absentSuggestion;
    quint64 m_nextMultipleItemToken = 0;

    QVBoxLayout* m_rootLayout = nullptr;
    QCheckBox* m_presenceToggle = nullptr;
    QCheckBox* m_booleanEditor = nullptr;
    ScalarValueWidget* m_scalarEditor = nullptr;
    QWidget* m_multipleContainer = nullptr;
    QLabel* m_invalidMultipleLabel = nullptr;
    QTableWidget* m_multipleTable = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
};

} // namespace finepaper
