// ProjectReader parses Ipcraft V1 project JSON files.
#pragma once

#include "ipcraft/diagnostics.h"
#include "project/projectdocument.h"

#include <QString>

struct ProjectReadResult {
    bool success = false;
    ProjectDocument document;
    QString error;
    ipcraft::DiagnosticStore diagnostics;
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
