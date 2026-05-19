// ProjectReader parses Finepaper .fpproj JSON files.
#pragma once

#include "project/projectdocument.h"

#include <QString>

struct ProjectReadResult {
    bool success = false;
    ProjectDocument document;
    QString error;
};

enum class ProjectFileKind {
    Unknown,
    Project,
};

class ProjectReader {
public:
    static ProjectFileKind detectKind(const QString& path);
    static ProjectReadResult readFile(const QString& path);
};
