// ModuleProvider abstractions for loading and composing module type definitions
#pragma once

#include "moduleregistry.h"
#include <QHash>
#include <vector>

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

// XmlModuleTypeSource loads module core metadata from modules.xml.
class XmlModuleTypeSource : public ModuleTypeSource {
public:
    explicit XmlModuleTypeSource(const QString& bundlePath);
    QHash<QString, ModuleType> loadModuleTypes() override;
    QStringList orderedTypeNames() const override;

private:
    QString m_bundlePath;
    QStringList m_orderedTypeNames;
};

// JsonModuleTypeSource loads authored JSON metadata that can also be converted into XML bundles.
class JsonModuleTypeSource : public ModuleTypeSource {
public:
    explicit JsonModuleTypeSource(const QString& bundlePath);
    QHash<QString, ModuleType> loadModuleTypes() override;
    QStringList orderedTypeNames() const override;

private:
    QString m_bundlePath;
    QStringList m_orderedTypeNames;
};

// XmlModulePresentationOverlay applies legacy combined presentation metadata from modules.ui.xml.
class XmlModulePresentationOverlay : public ModuleTypeOverlay {
public:
    explicit XmlModulePresentationOverlay(const QString& presentationPath);
    void apply(QHash<QString, ModuleType>& types) override;

private:
    QString m_presentationPath;
};

// XmlModuleGraphicsOverlay applies per-module graphics overlays from a directory of XML files.
class XmlModuleGraphicsOverlay : public ModuleTypeOverlay {
public:
    explicit XmlModuleGraphicsOverlay(const QString& graphicsDirectory);
    void apply(QHash<QString, ModuleType>& types) override;

private:
    QString m_graphicsDirectory;
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
