#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QSet>

void requireContract(bool condition, const QString &message);
QStringList sortedUniqueStrings(const QStringList &values, const QString &location);
QString runContractPythonVerifier(const QString &relativeScript,
                                  const QStringList &arguments,
                                  const QString &requiredOutputFragment);
void validateFrozenMarkdownLink(const QString &repositoryRoot,
                                const QString &sourceRelativePath,
                                const QString &rawTarget,
                                const QSet<QString> &frozenPaths);
