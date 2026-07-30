#pragma once

#include "application/mesh_resize_plan.h"
#include "package/package.h"

#include <QDialog>
#include <QHash>

class QDialogButtonBox;
class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSpinBox;

namespace finepaper {

// Edits a Mesh topology request and all state required to apply it as one
// transaction. Routers and Router links are intentionally never edited here:
// they remain projections of the requested Mesh dimensions.
class MeshResizeDialog final : public QDialog {
public:
    MeshResizeDialog(NocDesign design,
                     PackageDefinition package,
                     QWidget* parent = nullptr);

    [[nodiscard]] int requestedRows() const;
    [[nodiscard]] int requestedColumns() const;
    [[nodiscard]] QVector<DomainMembership> newRouterMemberships() const;
    [[nodiscard]] MeshResizeImpactConfirmation impactConfirmation() const;
    [[nodiscard]] const MeshResizePlan& plan() const { return m_plan; }
    [[nodiscard]] QStringList localErrors() const;

protected:
    void accept() override;

private:
    using RouterAssignments = QHash<QString, QStringList>;

    void rebuildPlan();
    void normalizeDraftForPlan();
    void rebuildRouterNavigator(const QString& preferredRouterId = {});
    void rebuildAssignmentEditor();
    void rebuildImpactEditors();
    void refreshRouterNavigator();
    void updateValidation();
    void copyCurrentAssignmentsToAll();
    void jumpToNextIncomplete();

    [[nodiscard]] QString currentRouterId() const;
    [[nodiscard]] bool routerAssignmentsComplete(const QString& routerId) const;
    [[nodiscard]] QVector<DomainMembership> draftMemberships() const;
    [[nodiscard]] QStringList collectLocalErrors() const;

    NocDesign m_design;
    PackageDefinition m_package;
    MeshResizePlan m_plan;
    QHash<QString, RouterAssignments> m_draftAssignments;
    bool m_rebuilding = false;

    QSpinBox* m_rows = nullptr;
    QSpinBox* m_columns = nullptr;
    QLabel* m_deltaSummary = nullptr;
    QLabel* m_blockers = nullptr;
    QListWidget* m_routerList = nullptr;
    QScrollArea* m_assignmentScroll = nullptr;
    QPushButton* m_copyToAll = nullptr;
    QPushButton* m_nextIncomplete = nullptr;
    QPushButton* m_confirmAllImpacts = nullptr;
    QPushButton* m_clearImpactConfirmations = nullptr;
    QListWidget* m_removedMemberships = nullptr;
    QListWidget* m_removedEdgeOverrides = nullptr;
    QLabel* m_diagnostics = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QPushButton* m_apply = nullptr;
};

} // namespace finepaper
