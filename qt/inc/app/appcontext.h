#pragma once

class CapabilityRegistry;
class ExtensionPointRegistry;
class PluginInteractionRegistry;
class ServiceRegistry;

struct AppContext {
    ServiceRegistry* services = nullptr;
    ExtensionPointRegistry* extensionPoints = nullptr;
    CapabilityRegistry* capabilities = nullptr;
    PluginInteractionRegistry* interactions = nullptr;
};
