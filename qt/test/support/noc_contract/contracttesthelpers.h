#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

void requireContract(bool condition, const QString &message);
QStringList sortedUniqueStrings(const QStringList &values, const QString &location);
QString runContractPythonVerifier(const QString &relativeScript,
                                  const QStringList &arguments,
                                  const QString &requiredOutputFragment);
