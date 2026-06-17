// ProjectService owns the durable project document source of truth.
#pragma once

#include "ipcraft/core/project_patch.h"
#include "project/projectdocument.h"
#include "project/projectreader.h"

#include <QObject>
#include <QString>

struct ProjectServiceResult {
    bool success = false;
    QString error;
    ipcraft::DiagnosticStore diagnostics;
};

class ProjectService : public QObject {
    Q_OBJECT

public:
    explicit ProjectService(QObject* parent = nullptr);

    bool hasDocument() const;
    const ProjectDocument& document() const;
    const ipcraft::core::ProjectDesign& design() const;
    QString currentPath() const;

    void clear();
    ProjectServiceResult createNew(const QString& projectName);
    ProjectServiceResult loadFile(const QString& path);
    ProjectServiceResult saveFile(const QString& path);
    ProjectServiceResult replaceDocument(ProjectDocument document);
    ProjectServiceResult replaceDocumentFromLoadedFile(ProjectDocument document, const QString& path);
    ProjectServiceResult replaceDocumentPreservingPath(ProjectDocument document);
    void replaceDesign(ipcraft::core::ProjectDesign design);
    void mergeDesignOnlyComponents(const ipcraft::core::ProjectDesign& design);
    ipcraft::core::PatchApplyResult applyDesignPatch(
        const ipcraft::core::ProjectDesign& project,
        const ipcraft::core::ProjectPatch& patch) const;

signals:
    void currentDocumentChanged();

private:
    void reloadDesignFromDocument();

    ProjectDocument m_document;
    ipcraft::core::ProjectDesign m_design;
    QString m_currentPath;
    bool m_hasDocument = false;
};
