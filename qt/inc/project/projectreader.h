// ProjectReader parses Finepaper .fpproj JSON files.
#pragma once

#include "project/projectdocument.h"

#include <QString>

struct ProjectReadResult {
    bool success = false;
    ProjectDocument document;
    QString error;
};

class ProjectReader {
public:
    static ProjectReadResult readFile(const QString& path);
};
