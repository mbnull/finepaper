// Entry point for Qt NoC/SoC editor application
#include "app/mainwindow.h"
#include "app/logformat.h"
#include "app/uiscale.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace {

// Process-wide mutex used by the Qt message handler because Qt logs can come
// from any thread.
QMutex& logMutex() {
    static QMutex mutex;
    return mutex;
}

QFile*& logFileHandle() {
    static QFile* file = nullptr;
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
    QFile* logFile = logFileHandle();
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

void installFileLogger() {
    QFile* logFile = new QFile(resolveLogFilePath());
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "Failed to open log file: %s\n", logFile->fileName().toLocal8Bit().constData());
        delete logFile;
        return;
    }

    // Redirect all qDebug/qInfo/qWarning/etc. messages through logToFile().
    logFileHandle() = logFile;
    qInstallMessageHandler(logToFile);
    qInfo().noquote() << "Writing logs to" << logFile->fileName();
}

} // namespace

int main(int argc, char *argv[]) {
    // Initialize Qt app state, install global logging, then show the main UI.
    UiScale::applyDefaultScaleFactor();
    QApplication a(argc, argv);
    QApplication::setApplicationName("finepaper");
    QApplication::setOrganizationName("finepaper");
    installFileLogger();
    MainWindow   w;
    w.show();
    return a.exec();
}
