#pragma once

class CapabilityRegistry;
class ExtensionPointRegistry;
class PackageService;
class PluginInteractionRegistry;
class ProjectService;
class ServiceRegistry;
class ToolPipelineService;
class WorkbenchService;

struct AppContext {
    ServiceRegistry* services = nullptr;
    ExtensionPointRegistry* extensionPoints = nullptr;
    CapabilityRegistry* capabilities = nullptr;
    PluginInteractionRegistry* interactions = nullptr;

    WorkbenchService* workbench = nullptr;
    ProjectService* projectService = nullptr;
    PackageService* packageService = nullptr;
    ToolPipelineService* toolPipelineService = nullptr;
};
