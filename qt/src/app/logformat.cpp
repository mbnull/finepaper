// LogFormat keeps console/file log output compact while preserving useful context.
#include "app/logformat.h"

#include <QFileInfo>
#include <QStringList>

namespace LogFormat {

QString levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }

    return QStringLiteral("UNKNOWN");
}

QString formatMessage(QtMsgType type,
                      const QMessageLogContext& context,
                      const QString& message,
                      const QDateTime& timestamp) {
    QStringList parts;
    parts << QStringLiteral("[%1]").arg(timestamp.toString(Qt::ISODateWithMs));
    parts << QStringLiteral("[%1]").arg(levelName(type));

    const QString category = context.category
        ? QString::fromUtf8(context.category)
        : QString();
    if (!category.isEmpty() && category != QStringLiteral("default")) {
        parts << QStringLiteral("[%1]").arg(category);
    }

    if (context.file && context.line > 0) {
        const QString fileName = QFileInfo(QString::fromUtf8(context.file)).fileName();
        parts << QStringLiteral("%1:%2")
                     .arg(fileName)
                     .arg(context.line);
    }

    parts << message;
    return parts.join(QStringLiteral(" "));
}

} // namespace LogFormat
