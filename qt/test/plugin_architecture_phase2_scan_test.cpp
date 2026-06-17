#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

QString readText(const QString& path) {
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot read " + path).toStdString());
    }
    return QString::fromUtf8(source.readAll());
}

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

void requireContains(const QString& text, const QString& needle, const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" should contain ") + needle);
}

void requireNotContains(const QString& text, const QString& needle, const QString& context) {
    require(!text.contains(needle), context + QStringLiteral(" should not contain ") + needle);
}

void testProjectPluginFilesExist() {
    const QStringList files = {
        QStringLiteral("qt/inc/project/projectservice.h"),
        QStringLiteral("qt/src/project/projectservice.cpp"),
        QStringLiteral("qt/inc/project/projectplugin.h"),
        QStringLiteral("qt/src/project/projectplugin.cpp")
    };

    for (const QString& file : files) {
        require(QFile::exists(file), QStringLiteral("missing project plugin file: ") + file);
    }
}

void testMainWindowUsesProjectServiceForDurableIoAndProjectionSync() {
    const QString mainWindow = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));
    const QString projectionService =
        readText(QStringLiteral("qt/src/project/editorprojectionservice.cpp"));

    requireContains(mainWindow, QStringLiteral("ProjectService stagedProject"), QStringLiteral("mainwindow source"));
    requireContains(mainWindow, QStringLiteral("stagedProject.createNew"), QStringLiteral("mainwindow source"));
    requireContains(mainWindow, QStringLiteral("stagedProject.loadFile"), QStringLiteral("mainwindow source"));
    requireContains(mainWindow,
                    QStringLiteral("m_projectService->replaceDocumentFromLoadedFile"),
                    QStringLiteral("mainwindow source"));
    requireContains(mainWindow,
                    QStringLiteral("m_editorProjectionService->rebuildProjectionFromDocument"),
                    QStringLiteral("mainwindow source"));
    requireContains(mainWindow,
                    QStringLiteral("m_editorProjectionService->rebuildProjectionViewOnly"),
                    QStringLiteral("mainwindow source"));
    requireNotContains(mainWindow,
                       QStringLiteral("m_editorProjectionService->syncProjectFromProjection"),
                       QStringLiteral("mainwindow source"));
    requireContains(mainWindow, QStringLiteral("m_projectService->saveFile"), QStringLiteral("mainwindow source"));
    requireNotContains(mainWindow, QStringLiteral("ProjectReader::readFile"), QStringLiteral("mainwindow source"));
    requireNotContains(mainWindow, QStringLiteral("ProjectWriter::writeFile"), QStringLiteral("mainwindow source"));

    requireContains(projectionService,
                    QStringLiteral("GraphProjectSerializer::loadProject"),
                    QStringLiteral("editor projection service source"));
    requireNotContains(projectionService,
                       QStringLiteral("GraphProjectSerializer::toProject"),
                       QStringLiteral("editor projection service source"));
    requireContains(projectionService,
                    QStringLiteral("m_projectService->replaceDocumentFromLoadedFile"),
                    QStringLiteral("editor projection service source"));
    requireNotContains(projectionService,
                       QStringLiteral("m_projectService->replaceDocumentFromProjection"),
                       QStringLiteral("editor projection service source"));
}

void testProjectServiceKeepsV1AndPatchBoundary() {
    const QString header = readText(QStringLiteral("qt/inc/project/projectservice.h"));
    const QString source = readText(QStringLiteral("qt/src/project/projectservice.cpp"));

    requireContains(header, QStringLiteral("ProjectDocument"), QStringLiteral("project service header"));
    requireContains(header, QStringLiteral("applyDesignPatch"), QStringLiteral("project service header"));
    requireContains(source, QStringLiteral("ipcraft::schemaids::projectV1"), QStringLiteral("project service source"));
    requireContains(source, QStringLiteral("ipcraft::core::applyPatch"), QStringLiteral("project service source"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testProjectPluginFilesExist();
    testMainWindowUsesProjectServiceForDurableIoAndProjectionSync();
    testProjectServiceKeepsV1AndPatchBoundary();
    std::cout << "plugin_architecture_phase2_scan_test passed\n";
    return 0;
}
