#include "package/packagecoverage.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace {

struct DeclaredCapability {
    QString id;
    bool required = true;
    QJsonObject descriptor;
};

const QSet<QString>& knownTopLevelSections() {
    static const QSet<QString> sections{
        QStringLiteral("schema"),
        QStringLiteral("id"),
        QStringLiteral("version"),
        QStringLiteral("name"),
        QStringLiteral("display"),
        QStringLiteral("extensions"),
        QStringLiteral("config_schema"),
        QStringLiteral("interfaces"),
        QStringLiteral("connection_rules"),
        QStringLiteral("emitters"),
        QStringLiteral("flows"),
        QStringLiteral("artifacts"),
        QStringLiteral("diagnostics"),
        QStringLiteral("views"),
        QStringLiteral("plugin"),
        QStringLiteral("native_schema"),
        QStringLiteral("metadata"),
        QStringLiteral("native"),
        QStringLiteral("graph_config"),
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
    const QString id = object.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty()) {
        return id;
    }
    const QString name = object.value(QStringLiteral("name")).toString().trimmed();
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
    capability.id = object.value(QStringLiteral("id")).toString().trimmed();
    capability.required = object.value(QStringLiteral("required")).isBool()
        ? object.value(QStringLiteral("required")).toBool()
        : true;
    capability.descriptor = object;
    return capability;
}

QVector<DeclaredCapability> capabilitiesFromDescriptor(const QJsonObject& descriptor) {
    QVector<DeclaredCapability> capabilities;
    const QJsonValue extensions = descriptor.value(QStringLiteral("extensions"));
    if (extensions.isArray()) {
        const QJsonArray array = extensions.toArray();
        for (const QJsonValue& value : array) {
            if (value.isString()) {
                DeclaredCapability capability;
                capability.id = value.toString().trimmed();
                capability.required = true;
                capability.descriptor.insert(QStringLiteral("id"), capability.id);
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
            capability.descriptor.insert(QStringLiteral("id"), declaration.id);
            capabilities.append(capability);
        }
        return capabilities;
    }

    for (const QString& extensionId : spec.extensions) {
        DeclaredCapability capability;
        capability.id = extensionId;
        capability.required = true;
        capability.descriptor.insert(QStringLiteral("id"), extensionId);
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
                        QStringLiteral("config_schema"),
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
    appendConfigSchema(report, descriptor.value(QStringLiteral("config_schema")).toObject());
    appendVisibleArray(report,
                       descriptor.value(QStringLiteral("interfaces")).toArray(),
                       QStringLiteral("interface:"),
                       QStringLiteral("Interface "),
                       QStringLiteral("interface"));
    appendVisibleObject(report,
                        QStringLiteral("connection_rules"),
                        QStringLiteral("Connection Rules"),
                        descriptor.value(QStringLiteral("connection_rules")).toObject());
    appendVisibleArray(report,
                       descriptor.value(QStringLiteral("emitters")).toArray(),
                       QStringLiteral("emitter:"),
                       QStringLiteral("Emitter "),
                       QStringLiteral("emitter"));
    appendVisibleArray(report,
                       descriptor.value(QStringLiteral("flows")).toArray(),
                       QStringLiteral("flow:"),
                       QStringLiteral("Flow "),
                       QStringLiteral("flow"));
    appendVisibleArray(report,
                       descriptor.value(QStringLiteral("artifacts")).toArray(),
                       QStringLiteral("artifact:"),
                       QStringLiteral("Artifact "),
                       QStringLiteral("artifact"));
    appendVisibleObject(report,
                        QStringLiteral("diagnostics"),
                        QStringLiteral("Diagnostics"),
                        descriptor.value(QStringLiteral("diagnostics")).toObject());
    appendVisibleArray(report,
                       descriptor.value(QStringLiteral("views")).toArray(),
                       QStringLiteral("view:"),
                       QStringLiteral("View "),
                       QStringLiteral("view"));
    appendVisibleObject(report,
                        QStringLiteral("plugin"),
                        QStringLiteral("Plugin"),
                        descriptor.value(QStringLiteral("plugin")).toObject());
    appendVisibleObject(report,
                        QStringLiteral("native_schema"),
                        QStringLiteral("Native Schema"),
                        descriptor.value(QStringLiteral("native_schema")).toObject());
    appendVisibleObject(report,
                        QStringLiteral("metadata"),
                        QStringLiteral("Metadata"),
                        descriptor.value(QStringLiteral("metadata")).toObject());
    appendVisibleObject(report,
                        QStringLiteral("native"),
                        QStringLiteral("Native"),
                        descriptor.value(QStringLiteral("native")).toObject());
    appendVisibleObject(report,
                        QStringLiteral("graph_config"),
                        QStringLiteral("Graph Config"),
                        descriptor.value(QStringLiteral("graph_config")).toObject());
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
    descriptor.insert(QStringLiteral("schema"), spec.schema);
    descriptor.insert(QStringLiteral("id"), spec.id);
    descriptor.insert(QStringLiteral("version"), spec.version);
    descriptor.insert(QStringLiteral("name"), spec.name);
    descriptor.insert(QStringLiteral("display"), spec.display);
    descriptor.insert(QStringLiteral("config_schema"), spec.configSchema);
    descriptor.insert(QStringLiteral("emitters"), spec.emitters);
    descriptor.insert(QStringLiteral("flows"), spec.flows);
    descriptor.insert(QStringLiteral("artifacts"), spec.artifacts);
    descriptor.insert(QStringLiteral("diagnostics"), spec.diagnostics);
    descriptor.insert(QStringLiteral("views"), spec.views);
    descriptor.insert(QStringLiteral("plugin"), spec.plugin);
    descriptor.insert(QStringLiteral("native_schema"), spec.nativeSchema);
    descriptor.insert(QStringLiteral("metadata"), spec.metadata);
    descriptor.insert(QStringLiteral("native"), spec.native);
    descriptor.insert(QStringLiteral("graph_config"), spec.graphConfig);

    QJsonArray interfaces;
    for (const ipcraft::PackageInterfaceSpec& interfaceSpec : spec.interfaces) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), interfaceSpec.id);
        object.insert(QStringLiteral("name"), interfaceSpec.name);
        object.insert(QStringLiteral("label"), interfaceSpec.label);
        object.insert(QStringLiteral("kind"), interfaceSpec.kind);
        object.insert(QStringLiteral("protocol"), interfaceSpec.protocol);
        object.insert(QStringLiteral("role"), interfaceSpec.role);
        object.insert(QStringLiteral("direction"), interfaceSpec.direction);
        object.insert(QStringLiteral("required"), interfaceSpec.required);
        object.insert(QStringLiteral("fanout"), interfaceSpec.fanout);
        object.insert(QStringLiteral("properties"), interfaceSpec.properties);
        object.insert(QStringLiteral("metadata"), interfaceSpec.metadata);
        object.insert(QStringLiteral("native"), interfaceSpec.native);
        interfaces.append(object);
    }
    descriptor.insert(QStringLiteral("interfaces"), interfaces);

    QJsonObject connectionRules;
    if (!spec.connectionRules.metadata.isEmpty()) {
        connectionRules.insert(QStringLiteral("metadata"), spec.connectionRules.metadata);
    }
    if (!spec.connectionRules.native.isEmpty()) {
        connectionRules.insert(QStringLiteral("native"), spec.connectionRules.native);
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
            object.insert(QStringLiteral("metadata"), rule.metadata);
            compatibility.append(object);
        }
        connectionRules.insert(QStringLiteral("compatibility"), compatibility);
    }
    descriptor.insert(QStringLiteral("connection_rules"), connectionRules);

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
    report.packageId = descriptor.value(QStringLiteral("id")).toString().trimmed();
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
