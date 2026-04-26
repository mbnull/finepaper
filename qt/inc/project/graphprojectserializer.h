// GraphProjectSerializer converts between Graph and Finepaper project documents.
#pragma once

#include "project/projectdocument.h"

#include <QString>

class Graph;

struct GraphProjectLoadResult {
    bool success = false;
    QString error;
};

class GraphProjectSerializer {
public:
    static ProjectDocument toProject(const Graph& graph, const QString& projectName);
    static GraphProjectLoadResult loadProject(const ProjectDocument& document, Graph& graph);
};
