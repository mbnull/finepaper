// Entry point for Qt NoC/SoC editor application
#include "mainwindow.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace {

QMutex& logMutex() {
    static QMutex mutex;
    return mutex;
}

QFile*& logFileHandle() {
    static QFile* file = nullptr;
    return file;
}

QString logLevelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }

    return "UNKNOWN";
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
    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString category = context.category ? QString::fromUtf8(context.category) : QStringLiteral("default");
    const QString fileName = context.file ? QFileInfo(QString::fromUtf8(context.file)).fileName() : QStringLiteral("-");
    const QString line = context.line > 0 ? QString::number(context.line) : QStringLiteral("-");
    const QString formatted = QString("[%1] [%2] [%3] %4:%5 %6")
                                  .arg(timestamp,
                                       logLevelName(type),
                                       category,
                                       fileName,
                                       line,
                                       message);

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

    logFileHandle() = logFile;
    qInstallMessageHandler(logToFile);
    qInfo().noquote() << "Writing logs to" << logFile->fileName();
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    QApplication::setApplicationName("finepaper");
    QApplication::setOrganizationName("finepaper");
    installFileLogger();
    MainWindow   w;
    w.show();
    return a.exec();
}
