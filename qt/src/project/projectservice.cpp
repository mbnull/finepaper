// ProjectService owns ProjectReader/ProjectWriter backed durable project state.
#include "project/projectservice.h"

#include "ipcraft/schemaids.h"
#include "project/projectwriter.h"

#include <QFileInfo>
#include <utility>

namespace {

ProjectServiceResult successResult() {
    return {true, {}, {}};
}

ProjectServiceResult failureResult(const QString& error,
                                   ipcraft::DiagnosticStore diagnostics = {}) {
    ProjectServiceResult result;
    result.success = false;
    result.error = error;
    result.diagnostics = std::move(diagnostics);
    return result;
}

QString cleanProjectName(const QString& projectName) {
    const QString trimmed = projectName.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Untitled") : trimmed;
}

void normalizeDocument(ProjectDocument& document) {
    if (document.schema.isEmpty()) {
        document.schema = ipcraft::schemaids::projectV1;
    }
    if (document.projectName.trimmed().isEmpty()) {
        document.projectName = QStringLiteral("Untitled");
    }
}

} // namespace

ProjectService::ProjectService(QObject* parent) : QObject(parent) {}

bool ProjectService::hasDocument() const {
    return m_hasDocument;
}

const ProjectDocument& ProjectService::document() const {
    return m_document;
}

QString ProjectService::currentPath() const {
    return m_currentPath;
}

void ProjectService::clear() {
    m_document = ProjectDocument{};
    m_currentPath.clear();
    m_hasDocument = false;
    emit currentDocumentChanged();
}

ProjectServiceResult ProjectService::createNew(const QString& projectName) {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectName = cleanProjectName(projectName);
    document.name = document.projectName;
    return replaceDocument(std::move(document));
}

ProjectServiceResult ProjectService::loadFile(const QString& path) {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const ProjectReadResult readResult = ProjectReader::readFile(absolutePath);
    if (!readResult.success) {
        return failureResult(readResult.error, readResult.diagnostics);
    }

    m_document = readResult.document;
    m_currentPath = absolutePath;
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::saveFile(const QString& path) {
    if (!m_hasDocument) {
        return failureResult(QStringLiteral("No project document is open."));
    }

    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(absolutePath, m_document);
    if (!writeResult.success) {
        return failureResult(writeResult.error);
    }

    m_currentPath = absolutePath;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocument(ProjectDocument document) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_currentPath.clear();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromLoadedFile(ProjectDocument document,
                                                                   const QString& path) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_currentPath = QFileInfo(path).absoluteFilePath();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromProjection(ProjectDocument document) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ipcraft::core::PatchApplyResult ProjectService::applyDesignPatch(
    const ipcraft::core::ProjectDesign& project,
    const ipcraft::core::ProjectPatch& patch) const {
    return ipcraft::core::applyPatch(project, patch);
}
