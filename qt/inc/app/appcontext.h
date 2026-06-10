#pragma once

class ProjectService;
class PackageService;
class WorkbenchService;

struct AppContext {
    WorkbenchService* workbench = nullptr;
    ProjectService* projectService = nullptr;
    PackageService* packageService = nullptr;
};
