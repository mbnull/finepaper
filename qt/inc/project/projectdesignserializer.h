// ProjectDesignSerializer converts between runtime designs and durable project documents.
#pragma once

#include "ipcraft/core/project_design.h"
#include "project/projectdocument.h"

class ProjectDesignSerializer {
public:
    static ProjectDocument toDocument(const ipcraft::core::ProjectDesign& design);
    static ipcraft::core::ProjectDesign fromDocument(const ProjectDocument& document);
};
