#pragma once

#include "package/package.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <optional>

class QEvent;
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

enum class PropertyValidationMode {
    Complete,
    Partial
};

// Presence is a feature-level semantic, not a visual side effect of the
// concrete scalar widget.  Automatic keeps the common Package/Domain
// behaviour while allowing sparse effective-value editors to opt into a
// direct, always-present value field.
enum class ValuePresenceSemantics {
    Automatic,
    RequiredValue,
    OptionalValue,
    SparseOverride,
};

struct SchemaValueOptions {
    bool multiple = false;
    bool required = false;
    PropertyValidationMode validationMode = PropertyValidationMode::Complete;
    ValuePresenceSemantics presenceSemantics =
        ValuePresenceSemantics::Automatic;
    std::optional<QString> referenceDomainType;
    QVector<SchemaChoice> choices;
    bool allowCustomReferences = false;
};

// JSON is the durable value format, but an editor draft can also contain a
// temporarily invalid numeric token such as "-" or "1e".  Keep that raw text
// outside the Design value so selection changes never destroy an in-progress
// edit merely because it is not valid JSON yet.
struct SchemaValueEditorDraft {
    std::optional<QJsonValue> value;
    std::optional<QString> invalidScalarText;

    bool operator==(const SchemaValueEditorDraft&) const = default;
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
    void setDraftState(const SchemaValueEditorDraft& draft);
    [[nodiscard]] std::optional<QJsonValue> value() const;
    [[nodiscard]] SchemaValueEditorDraft draftState() const;
    [[nodiscard]] QStringList localErrors() const;
    [[nodiscard]] bool locallyValid() const { return localErrors().isEmpty(); }
    // Returns the concrete child that should receive label mnemonics and
    // programmatic focus.  The target follows optional Set/Clear state.
    [[nodiscard]] QWidget* primaryInput() const;

    // These callbacks avoid a moc dependency and keep the editor usable by the
    // lightweight GUI test targets.
    std::function<void()> valueChanged;

private:
    class ScalarValueWidget;

    [[nodiscard]] ValuePresenceSemantics presenceSemantics() const;
    [[nodiscard]] bool valueCanBeAbsent() const;
    void setPresent(bool present);
    [[nodiscard]] bool isPresent() const;
    void notifyValueChanged();
    void updatePresentation();
    void updateMetrics();
    [[nodiscard]] QJsonValue missingValueSuggestion() const;
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
    bool m_present = false;
    bool m_requiredValueMissing = false;
    bool m_preserveInvalidMultipleValue = false;
    QJsonValue m_invalidMultipleValue;
    std::optional<QJsonValue> m_absentSuggestion;
    quint64 m_nextMultipleItemToken = 0;

    QVBoxLayout* m_rootLayout = nullptr;
    QPushButton* m_presenceAction = nullptr;
    QPushButton* m_acceptRequiredValue = nullptr;
    ScalarValueWidget* m_scalarEditor = nullptr;
    QWidget* m_multipleContainer = nullptr;
    QLabel* m_invalidMultipleLabel = nullptr;
    QTableWidget* m_multipleTable = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
    QLabel* m_validationLabel = nullptr;

protected:
    void changeEvent(QEvent* event) override;
};

} // namespace finepaper
