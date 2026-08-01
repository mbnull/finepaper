#pragma once

#include "noc/model.h"
#include "package/package.h"
#include "ui/common/schema_value_editor.h"

#include <QHash>
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

struct ElementConfigurationContextStamp final {
    QString designIdentity;
    quint64 designRevision = 0;
    quint64 packageCatalogRevision = 0;

    bool operator==(const ElementConfigurationContextStamp&) const = default;
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
                    ElementConfigurationContextStamp stamp,
                    bool busy = false);
    void setBusy(bool busy);
    [[nodiscard]] ElementConfigurationPanelState projectionState() const {
        return m_projection.state;
    }

    [[nodiscard]] bool hasUnappliedDrafts(
        const QString& designIdentity) const;
    [[nodiscard]] bool hasUnappliedDraft(
        const QString& designIdentity,
        const ElementRef& element) const;
    [[nodiscard]] QStringList unappliedDraftDescriptions(
        const QString& designIdentity) const;
    void discardDraft(const QString& designIdentity,
                      const ElementRef& element,
                      const QString& propertySetId);
    void discardDraftsForElement(const QString& designIdentity,
                                 const ElementRef& element);
    void clearDraftsForDesign(const QString& designIdentity);
    void clearDrafts();

    std::function<void(ElementRef, QString, QJsonObject)> applyRequested;
    std::function<void(ElementRef, QString)> resetRequested;
    std::function<void()> draftStateChanged;

private:
    struct PropertyRow {
        ElementPropertyDefinition definition;
        SchemaValueEditor* editor = nullptr;
    };

    struct CachedDraft {
        ElementRef element;
        QString propertySetId;
        QString sourceSchemaIdentity;
        QJsonObject sourceEffectiveValues;
        QJsonObject sourceOverrideValues;
        QHash<QString, SchemaValueEditorDraft> editorState;

        bool operator==(const CachedDraft&) const = default;
    };

    void rebuildPropertySetSelector(const QString& preferredPropertySet = {});
    void rebuildForm();
    void clearForm();
    void updateButtons();
    void captureCurrentDraft();
    void captureCurrentDraft(
        const QHash<QString, SchemaValueEditorDraft>& editorState);
    void restoreCachedDraft();
    void notifyDraftStateChanged();
    [[nodiscard]] QJsonObject effectiveValues() const;
    [[nodiscard]] QHash<QString, SchemaValueEditorDraft>
        currentEditorState() const;
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] QStringList localErrors() const;
    [[nodiscard]] const ElementPropertySetDefinition* currentPropertySet() const;
    void showProjectionMessage();

    const NocDesign* m_design = nullptr;
    const PackageDefinition* m_package = nullptr;
    ElementConfigurationPanelProjection m_projection;
    ElementConfigurationContextStamp m_contextStamp;
    QString m_currentPropertySetId;
    QString m_currentSchemaIdentity;
    QString m_resolutionFailureStatus;
    bool m_hasContext = false;
    bool m_busy = false;
    bool m_resolved = false;
    bool m_updating = false;
    bool m_draftConflict = false;
    bool m_reportedDraftPending = false;
    QJsonObject m_initialEffectiveValues;
    QJsonObject m_overrideValues;
    QHash<QString, SchemaValueEditorDraft> m_initialEditorState;
    QVector<PropertyRow> m_rows;
    QHash<QString, QVector<CachedDraft>> m_drafts;

    QLabel* m_heading = nullptr;
    QLabel* m_status = nullptr;
    QComboBox* m_propertySetSelector = nullptr;
    QLabel* m_overrideState = nullptr;
    QLabel* m_draftStatus = nullptr;
    QWidget* m_formContent = nullptr;
    QFormLayout* m_form = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_reset = nullptr;
    QPushButton* m_discardDraft = nullptr;
};

} // namespace finepaper
