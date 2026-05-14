// ModuleProvider abstractions for loading and composing module type definitions
#pragma once

#include "ipcraft/ipcraftmanifest.h"
#include "modules/moduleregistry.h"
#include <QHash>
#include <QStringList>
#include <QVector>
#include <vector>

QStringList defaultIpcraftPackageRoots();
QVector<IpcraftPackageManifest> loadIpcraftPackageManifests(const QStringList& rootPaths);

class ModuleProvider {
public:
    virtual ~ModuleProvider() = default;
    virtual std::vector<ModuleType> loadModules() = 0;
};

class ModuleTypeSource {
public:
    virtual ~ModuleTypeSource() = default;
    virtual QHash<QString, ModuleType> loadModuleTypes() = 0;
    virtual QStringList orderedTypeNames() const = 0;
};

class ModuleTypeOverlay {
public:
    virtual ~ModuleTypeOverlay() = default;
    virtual void apply(QHash<QString, ModuleType>& types) = 0;
};

// XmlModuleTypeSource loads module core metadata from legacy XML bundles.
class XmlModuleTypeSource : public ModuleTypeSource {
public:
    explicit XmlModuleTypeSource(const QString& bundlePath);
    QHash<QString, ModuleType> loadModuleTypes() override;
    QStringList orderedTypeNames() const override;

private:
    QString m_bundlePath;
    QStringList m_orderedTypeNames;
};

// IpcraftModuleTypeSource loads module core metadata from ipcraft.json.
class IpcraftModuleTypeSource : public ModuleTypeSource {
public:
    explicit IpcraftModuleTypeSource(IpcraftPackageManifest manifest);
    QHash<QString, ModuleType> loadModuleTypes() override;
    QStringList orderedTypeNames() const override;

private:
    IpcraftPackageManifest m_manifest;
    QStringList m_orderedTypeNames;
};

// XmlModuleGraphicsOverlay applies per-module graphics overlays from a directory of XML files.
class XmlModuleGraphicsOverlay : public ModuleTypeOverlay {
public:
    explicit XmlModuleGraphicsOverlay(const QString& graphicsDirectory);
    void apply(QHash<QString, ModuleType>& types) override;

private:
    QString m_graphicsDirectory;
};

// IpcraftModuleViewOverlay applies package source view XML files referenced by ipcraft.json.
class IpcraftModuleViewOverlay : public ModuleTypeOverlay {
public:
    explicit IpcraftModuleViewOverlay(QVector<IpcraftViewDescriptor> views);
    void apply(QHash<QString, ModuleType>& types) override;

private:
    QVector<IpcraftViewDescriptor> m_views;
};

// LayeredModuleProvider composes a base source with optional overlays.
class LayeredModuleProvider : public ModuleProvider {
public:
    explicit LayeredModuleProvider(std::unique_ptr<ModuleTypeSource> source);

    void addOverlay(std::unique_ptr<ModuleTypeOverlay> overlay);
    std::vector<ModuleType> loadModules() override;

private:
    std::unique_ptr<ModuleTypeSource> m_source;
    std::vector<std::unique_ptr<ModuleTypeOverlay>> m_overlays;
};
