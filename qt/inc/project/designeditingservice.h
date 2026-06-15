// DesignEditingService owns ProjectDesign edit transactions and undo/redo.
#pragma once

#include "ipcraft/core/project_patch.h"

#include <QObject>
#include <QString>
#include <QVector>

struct DesignEditResult {
    bool success = false;
    QString error;
    QVector<ipcraft::core::ValidationIssue> issues;
};

class DesignEditingService : public QObject {
    Q_OBJECT

public:
    explicit DesignEditingService(QObject* parent = nullptr);

    const ipcraft::core::ProjectDesign& design() const;
    void replaceDesign(ipcraft::core::ProjectDesign design);

    DesignEditResult applyPatch(const ipcraft::core::ProjectPatch& patch);
    bool canUndo() const;
    bool canRedo() const;
    DesignEditResult undo();
    DesignEditResult redo();

signals:
    void designChanged();

private:
    ipcraft::core::ProjectDesign m_design;
    QVector<ipcraft::core::ProjectDesign> m_undo;
    QVector<ipcraft::core::ProjectDesign> m_redo;
};
