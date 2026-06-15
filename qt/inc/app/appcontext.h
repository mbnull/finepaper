#pragma once

class CapabilityRegistry;
class ExtensionPointRegistry;
class PackageService;
class ProjectService;
class ServiceRegistry;
class ToolPipelineService;
class WorkbenchService;

struct AppContext {
    ServiceRegistry* services = nullptr;
    ExtensionPointRegistry* extensionPoints = nullptr;
    CapabilityRegistry* capabilities = nullptr;

    WorkbenchService* workbench = nullptr;
    ProjectService* projectService = nullptr;
    PackageService* packageService = nullptr;
    ToolPipelineService* toolPipelineService = nullptr;
};
