// LogFormat formats process-wide Qt log lines for stderr and file output.
#pragma once

#include <QDateTime>
#include <QMessageLogContext>
#include <QString>
#include <QtGlobal>

namespace LogFormat {

QString levelName(QtMsgType type);
QString formatMessage(QtMsgType type,
                      const QMessageLogContext& context,
                      const QString& message,
                      const QDateTime& timestamp);

} // namespace LogFormat
