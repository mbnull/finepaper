#include "ipcraft/ipcraftmanifest.h"

const IpcraftInterfaceDescriptor*
IpcraftModuleDescriptor::interfaceDescriptor(const QString& interfaceId) const {
    for (const IpcraftInterfaceDescriptor& descriptor : interfaces) {
        if (descriptor.id == interfaceId) {
            return &descriptor;
        }
    }
    return nullptr;
}

const IpcraftConnectionClass*
IpcraftPackageManifest::connectionClass(const QString& connectionClassId) const {
    for (const IpcraftConnectionClass& descriptor : connectionClasses) {
        if (descriptor.id == connectionClassId) {
            return &descriptor;
        }
    }
    return nullptr;
}

const IpcraftModuleDescriptor* IpcraftPackageManifest::module(const QString& moduleId) const {
    for (const IpcraftModuleDescriptor& descriptor : modules) {
        if (descriptor.id == moduleId) {
            return &descriptor;
        }
    }
    return nullptr;
}

const IpcraftInterfaceDescriptor*
IpcraftPackageManifest::interfaceDescriptor(const QString& moduleId,
                                            const QString& interfaceId) const {
    const IpcraftModuleDescriptor* moduleDescriptor = module(moduleId);
    if (moduleDescriptor == nullptr) {
        return nullptr;
    }
    return moduleDescriptor->interfaceDescriptor(interfaceId);
}

const IpcraftViewDescriptor* IpcraftPackageManifest::viewForModule(const QString& moduleId) const {
    for (const IpcraftViewDescriptor& descriptor : views) {
        if (descriptor.moduleId == moduleId) {
            return &descriptor;
        }
    }
    return nullptr;
}
