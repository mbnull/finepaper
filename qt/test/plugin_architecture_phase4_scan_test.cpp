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

void testEditorProjectionServiceFilesExist() {
    const QStringList files = {
        QStringLiteral("qt/inc/project/editorprojectionservice.h"),
        QStringLiteral("qt/src/project/editorprojectionservice.cpp")
    };

    for (const QString& file : files) {
        require(QFile::exists(file), QStringLiteral("missing editor projection file: ") + file);
    }
}

void testEditorProjectionServiceOwnsProjectionBridge() {
    const QString source = readText(QStringLiteral("qt/src/project/editorprojectionservice.cpp"));

    requireContains(source,
                    QStringLiteral("GraphProjectSerializer::loadProject"),
                    QStringLiteral("editor projection source"));
    requireContains(source,
                    QStringLiteral("rebuildProjectionViewOnly"),
                    QStringLiteral("editor projection source"));
    requireContains(source,
                    QStringLiteral("replaceDocumentFromLoadedFile"),
                    QStringLiteral("editor projection source"));
    requireNotContains(source,
                       QStringLiteral("GraphProjectSerializer::toProject"),
                       QStringLiteral("editor projection source"));
    requireNotContains(source,
                       QStringLiteral("replaceDocumentFromProjection"),
                       QStringLiteral("editor projection source"));
    requireNotContains(source,
                       QStringLiteral("syncProjectFromProjection"),
                       QStringLiteral("editor projection source"));
}

void testMainWindowUsesEditorProjectionService() {
    const QString header = readText(QStringLiteral("qt/inc/app/mainwindow.h"));
    const QString source = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));

    requireContains(header, QStringLiteral("EditorProjectionService"), QStringLiteral("mainwindow header"));
    requireContains(source, QStringLiteral("m_editorProjectionService"), QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("rebuildProjectionFromDocument"),
                    QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("rebuildProjectionViewOnly"),
                    QStringLiteral("mainwindow source"));
    requireContains(source, QStringLiteral("clearProjection"), QStringLiteral("mainwindow source"));
    requireNotContains(source,
                       QStringLiteral("syncProjectFromProjection"),
                       QStringLiteral("mainwindow source"));
    requireNotContains(source,
                       QStringLiteral("GraphProjectSerializer::loadProject"),
                       QStringLiteral("mainwindow source"));
    requireNotContains(source,
                       QStringLiteral("GraphProjectSerializer::toProject"),
                       QStringLiteral("mainwindow source"));
}

void testNodeEditorInteractionTestsRemainPresent() {
    const QString xmake = readText(QStringLiteral("qt/xmake.lua"));

    requireContains(xmake, QStringLiteral("nodeeditor_geometry_test"), QStringLiteral("xmake"));
    requireContains(xmake, QStringLiteral("src/nodeeditor/nodeeditorwidget.cpp"), QStringLiteral("xmake"));
    requireContains(xmake, QStringLiteral("editorprojectionservice_test"), QStringLiteral("xmake"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testEditorProjectionServiceFilesExist();
    testEditorProjectionServiceOwnsProjectionBridge();
    testMainWindowUsesEditorProjectionService();
    testNodeEditorInteractionTestsRemainPresent();
    std::cout << "plugin_architecture_phase4_scan_test passed\n";
    return 0;
}
