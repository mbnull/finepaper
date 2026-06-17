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

void testToolPipelineFilesExist() {
    const QStringList files = {
        QStringLiteral("qt/inc/app/generationflowprovider.h"),
        QStringLiteral("qt/src/app/generationflowprovider.cpp"),
        QStringLiteral("qt/inc/app/toolpipelineservice.h"),
        QStringLiteral("qt/src/app/toolpipelineservice.cpp"),
        QStringLiteral("qt/inc/app/toolpipelineplugin.h"),
        QStringLiteral("qt/src/app/toolpipelineplugin.cpp")
    };

    for (const QString& file : files) {
        require(QFile::exists(file), QStringLiteral("missing tool pipeline file: ") + file);
    }
}

void testFlowRunnerOwnedByProvider() {
    const QString providerSource = readText(QStringLiteral("qt/src/app/generationflowprovider.cpp"));
    const QString runnerSource = readText(QStringLiteral("qt/src/app/projectgenerationrunner.cpp"));

    requireContains(providerSource,
                    QStringLiteral("ipcraft::FlowRunner::runFlow"),
                    QStringLiteral("generation flow provider source"));
    requireContains(runnerSource,
                    QStringLiteral("generationFlowProviderFor"),
                    QStringLiteral("project generation runner source"));
    requireContains(runnerSource,
                    QStringLiteral("flowProvider->run"),
                    QStringLiteral("project generation runner source"));
    requireContains(runnerSource,
                    QStringLiteral("m_generationFlowProviders"),
                    QStringLiteral("project generation runner source"));
    requireNotContains(runnerSource,
                       QStringLiteral("ipcraft::FlowRunner::runFlow"),
                       QStringLiteral("project generation runner source"));
}

void testMainWindowUsesToolPipelineService() {
    const QString header = readText(QStringLiteral("qt/inc/app/mainwindow.h"));
    const QString source = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));

    requireContains(header,
                    QStringLiteral("ToolPipelineService"),
                    QStringLiteral("mainwindow header"));
    requireContains(header,
                    QStringLiteral("m_toolPipelineService"),
                    QStringLiteral("mainwindow header"));
    requireContains(source,
                    QStringLiteral("std::make_unique<ToolPipelineService>"),
                    QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("m_toolPipelineService->generateProject"),
                    QStringLiteral("mainwindow source"));
    requireNotContains(source,
                       QStringLiteral("ProjectGenerationRunner runner"),
                       QStringLiteral("mainwindow source"));
}

void testAppContextIsRegistryOnlyForToolPipelineService() {
    const QString context = readText(QStringLiteral("qt/inc/app/appcontext.h"));
    const QString pluginSource = readText(QStringLiteral("qt/src/app/toolpipelineplugin.cpp"));

    requireNotContains(context,
                       QStringLiteral("ToolPipelineService* toolPipelineService"),
                       QStringLiteral("app context"));
    requireNotContains(pluginSource,
                       QStringLiteral("context.toolPipelineService"),
                       QStringLiteral("tool pipeline plugin source"));
    requireContains(pluginSource,
                    QStringLiteral("finepaper.tool-pipeline"),
                    QStringLiteral("tool pipeline plugin source"));
    requireContains(pluginSource,
                    QStringLiteral("ToolPipelineService is required"),
                    QStringLiteral("tool pipeline plugin source"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testToolPipelineFilesExist();
    testFlowRunnerOwnedByProvider();
    testMainWindowUsesToolPipelineService();
    testAppContextIsRegistryOnlyForToolPipelineService();
    std::cout << "plugin_architecture_phase6_scan_test passed\n";
    return 0;
}
