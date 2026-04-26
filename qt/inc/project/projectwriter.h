// ProjectWriter writes deterministic Finepaper .fpproj JSON files.
#pragma once

#include "project/projectdocument.h"

#include <QString>

struct ProjectWriteResult {
    bool success = false;
    QString error;
};

class ProjectWriter {
public:
    static ProjectWriteResult writeFile(const QString& path, const ProjectDocument& document);
};
