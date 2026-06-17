#include "package/packagecoverage.h"

#include "ipcraft/contract/packagekeys.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace {

namespace packagekeys = ipcraft::contract::packagekeys;

struct DeclaredCapability {
    QString id;
    bool required = true;
    QJsonObject descriptor;
};

const QSet<QString>& knownTopLevelSections() {
    static const QSet<QString> sections{
        packagekeys::schema(),
        packagekeys::id(),
        packagekeys::version(),
        packagekeys::name(),
        packagekeys::display(),
        packagekeys::extensions(),
        packagekeys::configSchema(),
        packagekeys::interfaces(),
        packagekeys::connectionRules(),
        packagekeys::emitters(),
        packagekeys::flows(),
        packagekeys::artifacts(),
        packagekeys::diagnostics(),
        packagekeys::views(),
        packagekeys::plugin(),
        packagekeys::nativeSchema(),
        packagekeys::metadata(),
        packagekeys::native(),
        packagekeys::graphConfig(),
    };
    return sections;
}

QJsonObject objectDescriptor(const QJsonValue& value) {
    if (value.isObject()) {
        return value.toObject();
    }

    QJsonObject descriptor;
    descriptor.insert(QStringLiteral("value"), value);
    return descriptor;
}

QString objectIdOrIndex(const QJsonObject& object,
                        const QString& fallbackPrefix,
                        qsizetype index) {
    const QString id = object.value(packagekeys::id()).toString().trimmed();
    if (!id.isEmpty()) {
        return id;
    }
    const QString name = object.value(packagekeys::name()).toString().trimmed();
    if (!name.isEmpty()) {
        return name;
    }
    return QStringLiteral("%1%2").arg(fallbackPrefix).arg(index);
}

void appendItem(PackageCoverageReport& report,
                const QString& id,
                const QString& label,
                PackageFeatureCoverageStatus status,
                const QString& message,
                const QJsonObject& descriptor) {
    PackageFeatureCoverageItem item;
    item.id = id;
    item.label = label;
    item.status = status;
    item.message = message;
    item.descriptor = descriptor;
    report.items.append(item);
}

void appendVisibleObject(PackageCoverageReport& report,
                         const QString& id,
                         const QString& label,
                         const QJsonObject& descriptor) {
    if (descriptor.isEmpty()) {
        return;
    }
    appendItem(report,
               id,
               label,
               PackageFeatureCoverageStatus::Visible,
               QStringLiteral("Descriptor section is visible in the package inspector."),
               descriptor);
}

void appendVisibleArray(PackageCoverageReport& report,
                        const QJsonArray& values,
                        const QString& idPrefix,
                        const QString& labelPrefix,
                        const QString& fallbackPrefix) {
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QJsonValue value = values.at(index);
        const QJsonObject descriptor = objectDescriptor(value);
        const QString localId = objectIdOrIndex(descriptor, fallbackPrefix, index);
        appendItem(report,
                   idPrefix + localId,
                   labelPrefix + localId,
                   PackageFeatureCoverageStatus::Visible,
                   QStringLiteral("Descriptor declaration is visible in the package inspector."),
                   descriptor);
    }
}

DeclaredCapability capabilityFromObject(const QJsonObject& object) {
    DeclaredCapability capability;
    capability.id = object.value(packagekeys::id()).toString().trimmed();
    capability.required = object.value(packagekeys::required()).isBool()
        ? object.value(packagekeys::required()).toBool()
        : true;
    capability.descriptor = object;
    return capability;
}

QVector<DeclaredCapability> capabilitiesFromDescriptor(const QJsonObject& descriptor) {
    QVector<DeclaredCapability> capabilities;
    const QJsonValue extensions = descriptor.value(packagekeys::extensions());
    if (extensions.isArray()) {
        const QJsonArray array = extensions.toArray();
        for (const QJsonValue& value : array) {
            if (value.isString()) {
                DeclaredCapability capability;
                capability.id = value.toString().trimmed();
                capability.required = true;
                capability.descriptor.insert(packagekeys::id(), capability.id);
                capabilities.append(capability);
                continue;
            }
            if (value.isObject()) {
                const DeclaredCapability capability = capabilityFromObject(value.toObject());
                if (!capability.id.isEmpty()) {
                    capabilities.append(capability);
                }
            }
        }
        return capabilities;
    }

    return capabilities;
}

QVector<DeclaredCapability> capabilitiesFromSpec(const ipcraft::PackageSpec& spec) {
    QVector<DeclaredCapability> capabilities;
    if (!spec.extensionDeclarations.isEmpty()) {
        for (const ipcraft::PackageExtensionDeclaration& declaration : spec.extensionDeclarations) {
            DeclaredCapability capability;
            capability.id = declaration.id;
            capability.required = declaration.required;
            capability.descriptor = declaration.descriptor;
            capability.descriptor.insert(packagekeys::id(), declaration.id);
            capabilities.append(capability);
        }
        return capabilities;
    }

    for (const QString& extensionId : spec.extensions) {
        DeclaredCapability capability;
        capability.id = extensionId;
        capability.required = true;
        capability.descriptor.insert(packagekeys::id(), extensionId);
        capabilities.append(capability);
    }
    return capabilities;
}

QJsonObject endpointMatchDescriptor(const ipcraft::PackageEndpointMatch& endpoint) {
    QJsonObject object;
    object.insert(QStringLiteral("kind"), endpoint.kind);
    object.insert(QStringLiteral("protocol"), endpoint.protocol);
    object.insert(QStringLiteral("role"), endpoint.role);
    object.insert(QStringLiteral("direction"), endpoint.direction);
    return object;
}

const CapabilityHandlerDescriptor* handlerForCapability(
    const QVector<CapabilityHandlerDescriptor>& handlers,
    const QString& capabilityId) {
    for (const CapabilityHandlerDescriptor& handler : handlers) {
        if (handler.capabilityId == capabilityId) {
            return &handler;
        }
    }
    return nullptr;
}

void appendCapabilities(PackageCoverageReport& report,
                        const QVector<DeclaredCapability>& capabilities,
                        const CapabilityRegistry& registry) {
    const QVector<CapabilityHandlerDescriptor> handlers = registry.handlers();
    for (const DeclaredCapability& capability : capabilities) {
        if (capability.id.isEmpty()) {
            continue;
        }

        const CapabilityHandlerDescriptor* handler =
            handlerForCapability(handlers, capability.id);
        if (handler != nullptr) {
            QJsonObject descriptor = capability.descriptor;
            descriptor.insert(QStringLiteral("handler"), handler->ownerPluginId);
            appendItem(report,
                       QStringLiteral("capability:") + capability.id,
                       capability.id,
                       PackageFeatureCoverageStatus::Handled,
                       QStringLiteral("Capability is handled by %1.").arg(handler->ownerPluginId),
                       descriptor);
            continue;
        }

        appendItem(report,
                   QStringLiteral("capability:") + capability.id,
                   capability.id,
                   capability.required ? PackageFeatureCoverageStatus::Blocking
                                       : PackageFeatureCoverageStatus::Unsupported,
                   capability.required
                       ? QStringLiteral("Required capability has no registered handler.")
                       : QStringLiteral("Optional capability has no registered handler."),
                   capability.descriptor);
    }
}

void appendConfigSchema(PackageCoverageReport& report, const QJsonObject& configSchema) {
    appendVisibleObject(report,
                        packagekeys::configSchema(),
                        QStringLiteral("Config Schema"),
                        configSchema);
    for (const QString& group : {QStringLiteral("parameters"),
                                QStringLiteral("tables"),
                                QStringLiteral("documents"),
                                QStringLiteral("files")}) {
        const QJsonValue value = configSchema.value(group);
        if (value.isArray()) {
            QJsonObject descriptor;
            descriptor.insert(group, value);
            appendVisibleObject(report,
                                QStringLiteral("config_schema:") + group,
                                QStringLiteral("Config ") + group,
                                descriptor);
        }
    }
}

void appendDescriptorSections(PackageCoverageReport& report, const QJsonObject& descriptor) {
    appendConfigSchema(report, descriptor.value(packagekeys::configSchema()).toObject());
    appendVisibleArray(report,
                       descriptor.value(packagekeys::interfaces()).toArray(),
                       QStringLiteral("interface:"),
                       QStringLiteral("Interface "),
                       QStringLiteral("interface"));
    appendVisibleObject(report,
                        packagekeys::connectionRules(),
                        QStringLiteral("Connection Rules"),
                        descriptor.value(packagekeys::connectionRules()).toObject());
    appendVisibleArray(report,
                       descriptor.value(packagekeys::emitters()).toArray(),
                       QStringLiteral("emitter:"),
                       QStringLiteral("Emitter "),
                       QStringLiteral("emitter"));
    appendVisibleArray(report,
                       descriptor.value(packagekeys::flows()).toArray(),
                       QStringLiteral("flow:"),
                       QStringLiteral("Flow "),
                       QStringLiteral("flow"));
    appendVisibleArray(report,
                       descriptor.value(packagekeys::artifacts()).toArray(),
                       QStringLiteral("artifact:"),
                       QStringLiteral("Artifact "),
                       QStringLiteral("artifact"));
    appendVisibleObject(report,
                        packagekeys::diagnostics(),
                        QStringLiteral("Diagnostics"),
                        descriptor.value(packagekeys::diagnostics()).toObject());
    appendVisibleArray(report,
                       descriptor.value(packagekeys::views()).toArray(),
                       QStringLiteral("view:"),
                       QStringLiteral("View "),
                       QStringLiteral("view"));
    appendVisibleObject(report,
                        packagekeys::plugin(),
                        QStringLiteral("Plugin"),
                        descriptor.value(packagekeys::plugin()).toObject());
    appendVisibleObject(report,
                        packagekeys::nativeSchema(),
                        QStringLiteral("Native Schema"),
                        descriptor.value(packagekeys::nativeSchema()).toObject());
    appendVisibleObject(report,
                        packagekeys::metadata(),
                        QStringLiteral("Metadata"),
                        descriptor.value(packagekeys::metadata()).toObject());
    appendVisibleObject(report,
                        packagekeys::native(),
                        QStringLiteral("Native"),
                        descriptor.value(packagekeys::native()).toObject());
    appendVisibleObject(report,
                        packagekeys::graphConfig(),
                        QStringLiteral("Graph Config"),
                        descriptor.value(packagekeys::graphConfig()).toObject());
}

void appendUnknownSections(PackageCoverageReport& report, const QJsonObject& descriptor) {
    const QSet<QString>& knownSections = knownTopLevelSections();
    for (auto it = descriptor.constBegin(); it != descriptor.constEnd(); ++it) {
        if (knownSections.contains(it.key())) {
            continue;
        }
        appendItem(report,
                   QStringLiteral("unknown:") + it.key(),
                   it.key(),
                   PackageFeatureCoverageStatus::Visible,
                   QStringLiteral("Unknown descriptor section is preserved for inspection."),
                   objectDescriptor(it.value()));
    }
}

QJsonObject specDescriptor(const ipcraft::PackageSpec& spec) {
    QJsonObject descriptor;
    descriptor.insert(packagekeys::schema(), spec.schema);
    descriptor.insert(packagekeys::id(), spec.id);
    descriptor.insert(packagekeys::version(), spec.version);
    descriptor.insert(packagekeys::name(), spec.name);
    descriptor.insert(packagekeys::display(), spec.display);
    descriptor.insert(packagekeys::configSchema(), spec.configSchema);
    descriptor.insert(packagekeys::emitters(), spec.emitters);
    descriptor.insert(packagekeys::flows(), spec.flows);
    descriptor.insert(packagekeys::artifacts(), spec.artifacts);
    descriptor.insert(packagekeys::diagnostics(), spec.diagnostics);
    descriptor.insert(packagekeys::views(), spec.views);
    descriptor.insert(packagekeys::plugin(), spec.plugin);
    descriptor.insert(packagekeys::nativeSchema(), spec.nativeSchema);
    descriptor.insert(packagekeys::metadata(), spec.metadata);
    descriptor.insert(packagekeys::native(), spec.native);
    descriptor.insert(packagekeys::graphConfig(), spec.graphConfig);

    QJsonArray interfaces;
    for (const ipcraft::PackageInterfaceSpec& interfaceSpec : spec.interfaces) {
        QJsonObject object;
        object.insert(packagekeys::id(), interfaceSpec.id);
        object.insert(packagekeys::name(), interfaceSpec.name);
        object.insert(QStringLiteral("label"), interfaceSpec.label);
        object.insert(QStringLiteral("kind"), interfaceSpec.kind);
        object.insert(QStringLiteral("protocol"), interfaceSpec.protocol);
        object.insert(QStringLiteral("role"), interfaceSpec.role);
        object.insert(QStringLiteral("direction"), interfaceSpec.direction);
        object.insert(packagekeys::required(), interfaceSpec.required);
        object.insert(QStringLiteral("fanout"), interfaceSpec.fanout);
        object.insert(QStringLiteral("properties"), interfaceSpec.properties);
        object.insert(packagekeys::metadata(), interfaceSpec.metadata);
        object.insert(packagekeys::native(), interfaceSpec.native);
        interfaces.append(object);
    }
    descriptor.insert(packagekeys::interfaces(), interfaces);

    QJsonObject connectionRules;
    if (!spec.connectionRules.metadata.isEmpty()) {
        connectionRules.insert(packagekeys::metadata(), spec.connectionRules.metadata);
    }
    if (!spec.connectionRules.native.isEmpty()) {
        connectionRules.insert(packagekeys::native(), spec.connectionRules.native);
    }
    if (!spec.connectionRules.protocolAliases.isEmpty()) {
        QJsonObject aliases;
        for (auto it = spec.connectionRules.protocolAliases.constBegin();
             it != spec.connectionRules.protocolAliases.constEnd();
             ++it) {
            aliases.insert(it.key(), it.value());
        }
        connectionRules.insert(QStringLiteral("protocol_aliases"), aliases);
    }
    if (!spec.connectionRules.kindAliases.isEmpty()) {
        QJsonObject aliases;
        for (auto it = spec.connectionRules.kindAliases.constBegin();
             it != spec.connectionRules.kindAliases.constEnd();
             ++it) {
            aliases.insert(it.key(), it.value());
        }
        connectionRules.insert(QStringLiteral("kind_aliases"), aliases);
    }
    if (!spec.connectionRules.compatibility.isEmpty()) {
        QJsonArray compatibility;
        for (const ipcraft::PackageCompatibilityRule& rule :
             spec.connectionRules.compatibility) {
            QJsonObject object;
            object.insert(QStringLiteral("connection_type"), rule.connectionType);
            object.insert(QStringLiteral("from"), endpointMatchDescriptor(rule.from));
            object.insert(QStringLiteral("to"), endpointMatchDescriptor(rule.to));
            object.insert(QStringLiteral("arity"), rule.arity);
            object.insert(packagekeys::metadata(), rule.metadata);
            compatibility.append(object);
        }
        connectionRules.insert(QStringLiteral("compatibility"), compatibility);
    }
    descriptor.insert(packagekeys::connectionRules(), connectionRules);

    for (auto it = spec.unknownSections.constBegin(); it != spec.unknownSections.constEnd(); ++it) {
        descriptor.insert(it.key(), it.value());
    }

    return descriptor;
}

} // namespace

bool PackageCoverageReport::hasBlockingItems() const {
    for (const PackageFeatureCoverageItem& coverageItem : items) {
        if (coverageItem.status == PackageFeatureCoverageStatus::Blocking) {
            return true;
        }
    }
    return false;
}

PackageFeatureCoverageItem PackageCoverageReport::item(const QString& id) const {
    for (const PackageFeatureCoverageItem& coverageItem : items) {
        if (coverageItem.id == id) {
            return coverageItem;
        }
    }

    PackageFeatureCoverageItem missing;
    missing.label = id;
    missing.status = PackageFeatureCoverageStatus::Invalid;
    missing.message = QStringLiteral("Coverage item was not reported.");
    return missing;
}

QString packageFeatureCoverageStatusLabel(PackageFeatureCoverageStatus status) {
    switch (status) {
    case PackageFeatureCoverageStatus::Handled:
        return QStringLiteral("Handled");
    case PackageFeatureCoverageStatus::Visible:
        return QStringLiteral("Visible");
    case PackageFeatureCoverageStatus::Unsupported:
        return QStringLiteral("Unsupported");
    case PackageFeatureCoverageStatus::Blocking:
        return QStringLiteral("Blocking");
    case PackageFeatureCoverageStatus::Invalid:
        return QStringLiteral("Invalid");
    }
    return QStringLiteral("Invalid");
}

PackageCoverageReport buildPackageCoverageReport(const QJsonObject& descriptor,
                                                 const CapabilityRegistry& capabilities) {
    PackageCoverageReport report;
    report.packageId = descriptor.value(packagekeys::id()).toString().trimmed();
    appendCapabilities(report, capabilitiesFromDescriptor(descriptor), capabilities);
    appendDescriptorSections(report, descriptor);
    appendUnknownSections(report, descriptor);
    return report;
}

PackageCoverageReport buildPackageCoverageReport(const ipcraft::PackageSpec& spec,
                                                 const CapabilityRegistry& capabilities) {
    PackageCoverageReport report;
    report.packageId = spec.id;
    const QJsonObject descriptor = specDescriptor(spec);
    appendCapabilities(report, capabilitiesFromSpec(spec), capabilities);
    appendDescriptorSections(report, descriptor);
    appendUnknownSections(report, descriptor);
    return report;
}
