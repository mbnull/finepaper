#pragma once

#include "noc/model.h"
#include "package/package.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <optional>

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QScrollArea;

namespace finepaper {

class SchemaValueEditor;

enum class ElementConfigurationPanelState {
    NoDesign,
    PackageUnavailable,
    UnsupportedFormat,
    NoSelection,
    UnsupportedSelection,
    MissingElement,
    NoApplicablePropertySets,
    Ready
};

struct ElementConfigurationPanelProjection {
    ElementConfigurationPanelState state =
        ElementConfigurationPanelState::NoDesign;
    std::optional<ElementRef> element;
    QStringList propertySetIds;

    [[nodiscard]] bool ready() const {
        return state == ElementConfigurationPanelState::Ready;
    }
};

ElementConfigurationPanelProjection projectElementConfigurationPanel(
    const NocDesign* design,
    const PackageDefinition* package,
    std::optional<ElementRef> selection);

class ElementConfigurationPanel final : public QWidget {
public:
    explicit ElementConfigurationPanel(QWidget* parent = nullptr);

    void setContext(const NocDesign* design,
                    const PackageDefinition* package,
                    std::optional<ElementRef> selection,
                    bool busy = false);
    void setBusy(bool busy);

    std::function<void(ElementRef, QString, QJsonObject)> applyRequested;
    std::function<void(ElementRef, QString)> resetRequested;

private:
    struct PropertyRow {
        ElementPropertyDefinition definition;
        SchemaValueEditor* editor = nullptr;
    };

    void rebuildPropertySetSelector(const QString& preferredPropertySet = {});
    void rebuildForm();
    void clearForm();
    void updateButtons();
    [[nodiscard]] QJsonObject effectiveValues() const;
    [[nodiscard]] QStringList localErrors() const;
    [[nodiscard]] const ElementPropertySetDefinition* currentPropertySet() const;
    void showProjectionMessage();

    const NocDesign* m_design = nullptr;
    const PackageDefinition* m_package = nullptr;
    ElementConfigurationPanelProjection m_projection;
    bool m_busy = false;
    bool m_resolved = false;
    QJsonObject m_initialEffectiveValues;
    QJsonObject m_overrideValues;
    QVector<PropertyRow> m_rows;

    QLabel* m_status = nullptr;
    QLabel* m_target = nullptr;
    QComboBox* m_propertySetSelector = nullptr;
    QLabel* m_overrideState = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_formContent = nullptr;
    QFormLayout* m_form = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_reset = nullptr;
};

} // namespace finepaper
