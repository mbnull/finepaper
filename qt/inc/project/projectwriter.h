// ProjectWriter writes deterministic Finepaper .fpproj JSON files.
#pragma once

#include "project/projectdocument.h"

#include <QJsonObject>
#include <QString>

struct ProjectWriteResult {
    bool success = false;
    QString error;
};

struct ProjectJsonResult {
    bool success = false;
    QJsonObject object;
    QString error;
};

class ProjectWriter {
public:
    static QJsonObject toJsonObject(const ProjectDocument& document);
    static ProjectJsonResult toJsonObjectResult(const ProjectDocument& document);
    static ProjectWriteResult writeFile(const QString& path, const ProjectDocument& document);
};
