#pragma once

class ProjectService;
class WorkbenchService;

struct AppContext {
    WorkbenchService* workbench = nullptr;
    ProjectService* projectService = nullptr;
};
