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

// JsonModuleTypeSource loads runtime/default module metadata from modules.json.
class JsonModuleTypeSource : public ModuleTypeSource {
public:
    explicit JsonModuleTypeSource(const QString& bundlePath);
    QHash<QString, ModuleType> loadModuleTypes() override;
    QStringList orderedTypeNames() const override;

private:
    QString m_bundlePath;
    QStringList m_orderedTypeNames;
};

// XmlModulePresentationOverlay applies editor-only presentation metadata from modules.ui.xml.
class XmlModulePresentationOverlay : public ModuleTypeOverlay {
public:
    explicit XmlModulePresentationOverlay(const QString& presentationPath);
    void apply(QHash<QString, ModuleType>& types) override;

private:
    QString m_presentationPath;
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
