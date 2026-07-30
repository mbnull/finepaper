#pragma once

#include "application/domain_assignment.h"
#include "gui/domain_manager_projection.h"
#include "gui/domain_presentation.h"

#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace finepaper {

inline constexpr int domainManagerDomainIdRole = Qt::UserRole + 40;
inline constexpr int domainManagerDomainTypeRole = Qt::UserRole + 41;
inline constexpr int domainManagerColorRole = Qt::UserRole + 42;
inline constexpr int domainManagerMemberCountRole = Qt::UserRole + 43;
inline constexpr int domainManagerCrossingCountRole = Qt::UserRole + 44;
inline constexpr int domainManagerInitialCheckStateRole = Qt::UserRole + 45;

class DomainManagerPanel final : public QWidget {
public:
    explicit DomainManagerPanel(QWidget* parent = nullptr);

    void setContext(const NocDesign* design,
                    const ResolvedDesign* resolved,
                    const PackageDefinition* package,
                    const QString& canvasDomainType);
    void setSelection(QVector<ElementRef> selection);
    void setCanvasDomainType(const QString& domainType);
    void setBusy(bool busy);
    void setDiagnostics(const QVector<Diagnostic>& diagnostics);

    [[nodiscard]] QString currentDomainType() const;
    [[nodiscard]] bool hasPendingAssignmentChanges() const {
        return m_assignmentEdited;
    }
    void discardPendingAssignmentChanges();

    std::function<QVector<Diagnostic>(const DomainDefinition&)>
        validateAddDomain;
    std::function<QVector<Diagnostic>(const QString&, const DomainDefinition&)>
        validateUpdateDomain;
    std::function<void(DomainDefinition)> addDomainRequested;
    std::function<void(QString, DomainDefinition)> updateDomainRequested;
    std::function<void(QString)> removeDomainRequested;
    std::function<void(QVector<ElementRef>, QString, DomainAssignmentPatch)>
        assignmentPatchRequested;
    std::function<void()> completeConfigurationRequested;
    std::function<void(QString)> showDomainLayerRequested;

private:
    [[nodiscard]] const DomainTypeDefinition* selectedType() const;
    [[nodiscard]] const DomainDefinition* selectedDomain() const;
    void rebuildTypeSelector(const QString& preferredType);
    void refreshCurrentType();
    void refreshInstances();
    void refreshAssignment();
    void updateActionState();
    void addDomain();
    void editDomain();
    void removeDomain();
    void applyAssignment();
    void clearAssignment();
    void discardAssignment();
    void handleAssignmentItemChanged(QListWidgetItem* item);
    void updateSingleAssignmentEdited();
    [[nodiscard]] QString selectedDomainId() const;

    const NocDesign* m_design = nullptr;
    const ResolvedDesign* m_resolved = nullptr;
    const PackageDefinition* m_package = nullptr;
    QString m_canvasDomainType;
    QVector<ElementRef> m_selection;
    DomainAssignmentAggregate m_assignment;
    QSet<QString> m_touchedAssignmentDomains;
    bool m_busy = false;
    bool m_updating = false;
    bool m_assignmentEdited = false;
    bool m_clearAssignmentStaged = false;
    bool m_selectionChangedWhileEditing = false;

    QLabel* m_status = nullptr;
    QPushButton* m_completeConfiguration = nullptr;
    QComboBox* m_typeSelector = nullptr;
    QPushButton* m_showOnCanvas = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_instances = nullptr;
    QPushButton* m_addDomain = nullptr;
    QPushButton* m_editDomain = nullptr;
    QPushButton* m_removeDomain = nullptr;
    QLabel* m_assignmentState = nullptr;
    QComboBox* m_singleAssignment = nullptr;
    QListWidget* m_multipleAssignment = nullptr;
    QPushButton* m_applyAssignment = nullptr;
    QPushButton* m_clearAssignment = nullptr;
    QPushButton* m_discardAssignment = nullptr;
    QLabel* m_diagnostics = nullptr;
};

} // namespace finepaper
