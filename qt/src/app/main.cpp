// Entry point for Qt NoC/SoC editor application
#include "app/mainwindow.h"
#include "app/logformat.h"
#include "app/projectlauncher.h"
#include "app/startupflow.h"
#include "app/uiscale.h"
#include <QApplication>
#include <QPixmap>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <memory>

namespace {

// Process-wide mutex used by the Qt message handler because Qt logs can come
// from any thread.
QMutex& logMutex() {
    static QMutex mutex;
    return mutex;
}

std::unique_ptr<QFile>& logFileHandle() {
    static std::unique_ptr<QFile> file;
    return file;
}

QString resolveLogFilePath() {
    QString logDirPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (logDirPath.isEmpty()) {
        logDirPath = QDir::homePath() + "/.finepaper";
    }

    QDir logDir(logDirPath);
    logDir.mkpath(".");
    return logDir.filePath("finepaper.log");
}

void logToFile(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const QString formatted = LogFormat::formatMessage(type,
                                                       context,
                                                       message,
                                                       QDateTime::currentDateTime());

    // Keep line writes atomic so multi-threaded logs do not interleave.
    QMutexLocker locker(&logMutex());
    QFile* logFile = logFileHandle().get();
    if (logFile && logFile->isOpen()) {
        logFile->write(formatted.toUtf8());
        logFile->write("\n");
        logFile->flush();
    }

    fprintf(stderr, "%s\n", formatted.toLocal8Bit().constData());
    fflush(stderr);

    if (type == QtFatalMsg) {
        abort();
    }
}

void cleanupFileLogger() {
    qInstallMessageHandler(nullptr);
    QMutexLocker locker(&logMutex());
    logFileHandle().reset();
}

void installFileLogger() {
    auto logFile = std::make_unique<QFile>(resolveLogFilePath());
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "Failed to open log file: %s\n", logFile->fileName().toLocal8Bit().constData());
        return;
    }

    // Redirect all qDebug/qInfo/qWarning/etc. messages through logToFile().
    logFileHandle() = std::move(logFile);
    qInstallMessageHandler(logToFile);
    qAddPostRoutine(cleanupFileLogger);
    qInfo().noquote() << "Writing logs to" << logFileHandle()->fileName();
}

} // namespace

int main(int argc, char *argv[]) {
    // Initialize Qt app state, install global logging, then show the main UI.
    UiScale::applyDefaultScaleFactor();
    QApplication a(argc, argv);
    QApplication::setApplicationName("finepaper");
    QApplication::setOrganizationName("finepaper");
    installFileLogger();
    MainWindow w;
    const StartupFlowResult startupResult = selectStartupProject(
        a.arguments(),
        StartupFlowCallbacks{
            .loadProject = [&](const QString& path) { return w.loadGraph(path); },
            .createProject = [&](const QString& path) { return w.createProjectAt(path); },
            .showLauncher = [&]() {
                ProjectLauncherDialog launcher;
                launcher.exec();
                return launcher.result();
            }
        });
    if (startupResult.action != StartupFlowResult::Action::ShowMainWindow) {
        return startupResult.exitCode;
    }

    w.show();

    if (qgetenv("FINEPAPER_SCREENSHOT") == "1") {
        QTimer::singleShot(3000, [&]() {
            QPixmap pixmap = w.grab();
            pixmap.save("/tmp/finepaper_ui.png");
            fprintf(stderr, "Screenshot saved to /tmp/finepaper_ui.png\n");
            a.quit();
        });
    }

    return a.exec();
}
