// Tests for optional IP-XACT connection-only strict checks.
#include "ipcraft/ipcraftmanifest.h"
#include "ipcraft/ipxactconnectionchecker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "failed to open test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

QByteArray designXml(const QString& targetMode = QStringLiteral("target"),
                     int sourceInstanceCount = 1,
                     int targetInstanceCount = 1) {
    QString componentInstances;
    for (int index = 0; index < sourceInstanceCount; ++index) {
        componentInstances += QStringLiteral(R"xml(    <ipxact:componentInstance>
      <ipxact:instanceName>source_%1</ipxact:instanceName>
      <ipxact:componentRef vendor="org.example" library="noc" name="SourceTile" version="1.0"/>
    </ipxact:componentInstance>
)xml").arg(index);
    }
    for (int index = 0; index < targetInstanceCount; ++index) {
        componentInstances += QStringLiteral(R"xml(    <ipxact:componentInstance>
      <ipxact:instanceName>target_%1</ipxact:instanceName>
      <ipxact:componentRef vendor="org.example" library="noc" name="TargetTile" version="1.0"/>
    </ipxact:componentInstance>
)xml").arg(index);
    }

    return QStringLiteral(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<ipxact:design xmlns:ipxact="http://www.accellera.org/XMLSchema/IPXACT/1685-2022">
  <ipxact:componentInstances>
%1
  </ipxact:componentInstances>
  <ipxact:componentDefinitions>
    <ipxact:component>
      <ipxact:vendor>org.example</ipxact:vendor>
      <ipxact:library>noc</ipxact:library>
      <ipxact:name>SourceTile</ipxact:name>
      <ipxact:version>1.0</ipxact:version>
      <ipxact:busInterfaces>
        <ipxact:busInterface>
          <ipxact:name>source_bus</ipxact:name>
          <ipxact:busType vendor="org.example" library="bus" name="link" version="1.0"/>
          <ipxact:abstractionTypes>
            <ipxact:abstractionType>
              <ipxact:abstractionRef vendor="org.example" library="abs" name="link_rtl" version="1.0"/>
            </ipxact:abstractionType>
          </ipxact:abstractionTypes>
          <ipxact:initiator/>
        </ipxact:busInterface>
      </ipxact:busInterfaces>
    </ipxact:component>
    <ipxact:component>
      <ipxact:vendor>org.example</ipxact:vendor>
      <ipxact:library>noc</ipxact:library>
      <ipxact:name>TargetTile</ipxact:name>
      <ipxact:version>1.0</ipxact:version>
      <ipxact:busInterfaces>
        <ipxact:busInterface>
          <ipxact:name>target_bus</ipxact:name>
          <ipxact:busType vendor="org.example" library="bus" name="link" version="1.0"/>
          <ipxact:abstractionTypes>
            <ipxact:abstractionType>
              <ipxact:abstractionRef vendor="org.example" library="abs" name="link_rtl" version="1.0"/>
            </ipxact:abstractionType>
          </ipxact:abstractionTypes>
          <ipxact:%2/>
        </ipxact:busInterface>
      </ipxact:busInterfaces>
    </ipxact:component>
  </ipxact:componentDefinitions>
</ipxact:design>
)xml").arg(componentInstances, targetMode).toUtf8();
}

IpcraftInterfaceDescriptor manifestInterface(const QString& id,
                                             const QString& busInterface,
                                             const QString& mode) {
    IpcraftInterfaceDescriptor descriptor;
    descriptor.id = id;
    descriptor.label = id;
    descriptor.modes = {mode};
    descriptor.ipxactBusInterface = busInterface;
    return descriptor;
}

IpcraftModuleDescriptor manifestModule(const QString& id,
                                       const QString& busInterface,
                                       const QString& mode) {
    IpcraftModuleDescriptor module;
    module.id = id;
    module.name = id;
    module.interfaces = {manifestInterface(QStringLiteral("link"), busInterface, mode)};
    return module;
}

IpcraftPackageManifest manifestWithIpxactRoot(const QString& ipxactRootPath,
                                              const QString& sourceBusInterface,
                                              const QString& targetBusInterface,
                                              const QString& targetMode = QStringLiteral("target")) {
    IpcraftPackageManifest manifest;
    manifest.schema = QStringLiteral("ipcraft.manifest.v1");
    manifest.id = QStringLiteral("org.example.noc");
    manifest.name = QStringLiteral("Example NoC");
    manifest.version = QStringLiteral("1.0");
    manifest.modules = {
        manifestModule(QStringLiteral("Source"), sourceBusInterface, QStringLiteral("initiator")),
        manifestModule(QStringLiteral("Target"), targetBusInterface, targetMode)
    };

    IpcraftIpxactDescriptor ipxact;
    ipxact.rootPath = ipxactRootPath;
    ipxact.resolvedRootPath = ipxactRootPath;
    manifest.ipxact = ipxact;
    return manifest;
}

IpxactConnection connectionRecord() {
    IpxactConnection connection;
    connection.id = QStringLiteral("conn_0");
    connection.participants = {
        IpxactConnectionParticipant{QStringLiteral("source_0"),
                                    QString(),
                                    QStringLiteral("Source"),
                                    QStringLiteral("link")},
        IpxactConnectionParticipant{QStringLiteral("target_0"),
                                    QString(),
                                    QStringLiteral("Target"),
                                    QStringLiteral("link")}
    };
    return connection;
}

IpxactConnection missingSourceInstanceConnectionRecord() {
    IpxactConnection connection = connectionRecord();
    connection.participants[0].instanceId = QStringLiteral("source_missing");
    connection.participants[0].componentRef = QStringLiteral("SourceTile");
    return connection;
}

IpxactConnection multiTargetConnectionRecord() {
    IpxactConnection connection;
    connection.id = QStringLiteral("conn_multi_target");
    connection.participants = {
        IpxactConnectionParticipant{QStringLiteral("source_0"),
                                    QString(),
                                    QStringLiteral("Source"),
                                    QStringLiteral("link")},
        IpxactConnectionParticipant{QStringLiteral("target_0"),
                                    QString(),
                                    QStringLiteral("Target"),
                                    QStringLiteral("link")},
        IpxactConnectionParticipant{QStringLiteral("target_1"),
                                    QString(),
                                    QStringLiteral("Target"),
                                    QStringLiteral("link")}
    };
    return connection;
}

IpxactConnection twoInitiatorConnectionRecord() {
    IpxactConnection connection;
    connection.id = QStringLiteral("conn_two_initiators");
    connection.participants = {
        IpxactConnectionParticipant{QStringLiteral("source_0"),
                                    QString(),
                                    QStringLiteral("Source"),
                                    QStringLiteral("link")},
        IpxactConnectionParticipant{QStringLiteral("source_1"),
                                    QString(),
                                    QStringLiteral("Source"),
                                    QStringLiteral("link")},
        IpxactConnectionParticipant{QStringLiteral("target_0"),
                                    QString(),
                                    QStringLiteral("Target"),
                                    QStringLiteral("link")}
    };
    return connection;
}

IpxactConnectionCheckResult check(const IpcraftPackageManifest& manifest) {
    return IpxactConnectionChecker().check(manifest, {connectionRecord()});
}

IpxactConnectionCheckResult check(const IpcraftPackageManifest& manifest,
                                  const IpxactConnection& connection) {
    return IpxactConnectionChecker().check(manifest, {connection});
}

void testSkipsWhenNoIpxactRoot() {
    IpcraftPackageManifest manifest;
    manifest.id = QStringLiteral("org.example.noc");
    manifest.modules = {
        manifestModule(QStringLiteral("Source"), QStringLiteral("source_bus"), QStringLiteral("initiator")),
        manifestModule(QStringLiteral("Target"), QStringLiteral("target_bus"), QStringLiteral("target"))
    };

    const IpxactConnectionCheckResult result = check(manifest);

    require(result.diagnostics.isEmpty(), "checker should skip packages without an IP-XACT root");
}

void testReportsMissingBusInterface() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString rootPath = QDir(tempDir.path()).filePath(QStringLiteral("component.xml"));
    writeFile(rootPath, designXml());
    const IpcraftPackageManifest manifest =
        manifestWithIpxactRoot(rootPath,
                               QStringLiteral("missing_bus"),
                               QStringLiteral("target_bus"));

    const IpxactConnectionCheckResult result = check(manifest);

    require(result.diagnostics.size() == 1, "missing bus interface should produce one diagnostic");
    require(result.diagnostics.first().connectionId == QStringLiteral("conn_0"),
            "diagnostic should reference the project connection");
    require(result.diagnostics.first().message.contains(QStringLiteral("missing IP-XACT bus interface")),
            "diagnostic should explain that the bus interface is missing");
    require(result.diagnostics.first().message.contains(QStringLiteral("missing_bus")),
            "diagnostic should include the missing bus interface name");
}

void testReportsMissingComponentInstanceBeforeComponentFallback() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString rootPath = QDir(tempDir.path()).filePath(QStringLiteral("component.xml"));
    writeFile(rootPath, designXml());
    const IpcraftPackageManifest manifest =
        manifestWithIpxactRoot(rootPath,
                               QStringLiteral("source_bus"),
                               QStringLiteral("target_bus"));

    const IpxactConnectionCheckResult result =
        check(manifest, missingSourceInstanceConnectionRecord());

    require(result.diagnostics.size() == 1,
            "missing component instance should produce one diagnostic before component fallback");
    require(result.diagnostics.first().connectionId == QStringLiteral("conn_0"),
            "missing instance diagnostic should reference the project connection");
    require(result.diagnostics.first().message.contains(QStringLiteral("missing component instance")),
            "diagnostic should explain that the component instance is missing");
    require(result.diagnostics.first().message.contains(QStringLiteral("source_missing")),
            "diagnostic should include the missing instance name");
}

void testAcceptsCompatibleActiveInterfaces() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString rootPath = QDir(tempDir.path()).filePath(QStringLiteral("component.xml"));
    writeFile(rootPath, designXml());
    const IpcraftPackageManifest manifest =
        manifestWithIpxactRoot(rootPath,
                               QStringLiteral("source_bus"),
                               QStringLiteral("target_bus"));

    const IpxactConnectionCheckResult result = check(manifest);

    require(result.diagnostics.isEmpty(),
            "initiator-to-target interfaces with matching bus and abstraction types should pass");
}

void testAcceptsMultiParticipantInitiatorToTargets() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString rootPath = QDir(tempDir.path()).filePath(QStringLiteral("component.xml"));
    writeFile(rootPath, designXml(QStringLiteral("target"), 1, 2));
    const IpcraftPackageManifest manifest =
        manifestWithIpxactRoot(rootPath,
                               QStringLiteral("source_bus"),
                               QStringLiteral("target_bus"));

    const IpxactConnectionCheckResult result = check(manifest, multiTargetConnectionRecord());

    require(result.diagnostics.isEmpty(),
            "one initiator with multiple compatible target interfaces should pass");
}

void testRejectsIncompatibleInterfaceModes() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString rootPath = QDir(tempDir.path()).filePath(QStringLiteral("component.xml"));
    writeFile(rootPath, designXml(QStringLiteral("initiator")));
    const IpcraftPackageManifest manifest =
        manifestWithIpxactRoot(rootPath,
                               QStringLiteral("source_bus"),
                               QStringLiteral("target_bus"),
                               QStringLiteral("initiator"));

    const IpxactConnectionCheckResult result = check(manifest);

    require(result.diagnostics.size() == 1, "incompatible interface modes should produce one diagnostic");
    require(result.diagnostics.first().message.contains(QStringLiteral("incompatible IP-XACT modes")),
            "diagnostic should explain that the participant modes are incompatible");
    require(result.diagnostics.first().message.contains(QStringLiteral("initiator")),
            "diagnostic should include the incompatible mode");
}

void testRejectsMultiParticipantWithTwoInitiators() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString rootPath = QDir(tempDir.path()).filePath(QStringLiteral("component.xml"));
    writeFile(rootPath, designXml(QStringLiteral("target"), 2, 1));
    const IpcraftPackageManifest manifest =
        manifestWithIpxactRoot(rootPath,
                               QStringLiteral("source_bus"),
                               QStringLiteral("target_bus"));

    const IpxactConnectionCheckResult result = check(manifest, twoInitiatorConnectionRecord());

    require(result.diagnostics.size() == 1,
            "two initiators in one IP-XACT interconnection should produce one diagnostic");
    require(result.diagnostics.first().message.contains(QStringLiteral("incompatible IP-XACT modes")),
            "diagnostic should explain that the participant modes are incompatible");
    require(result.diagnostics.first().message.contains(QStringLiteral("initiator")),
            "diagnostic should include the repeated initiator mode");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testSkipsWhenNoIpxactRoot();
        testReportsMissingBusInterface();
        testReportsMissingComponentInstanceBeforeComponentFallback();
        testAcceptsCompatibleActiveInterfaces();
        testAcceptsMultiParticipantInitiatorToTargets();
        testRejectsIncompatibleInterfaceModes();
        testRejectsMultiParticipantWithTwoInitiators();
    } catch (const std::exception& error) {
        std::cerr << "ipxactconnectionchecker_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipxactconnectionchecker_test passed\n";
    return 0;
}
