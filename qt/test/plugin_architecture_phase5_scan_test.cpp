#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

QString readText(const QString& path) {
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot read " + path).toStdString());
    }
    return QString::fromUtf8(source.readAll());
}

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

void requireContains(const QString& text, const QString& needle, const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" should contain ") + needle);
}

void requireNotContains(const QString& text, const QString& needle, const QString& context) {
    require(!text.contains(needle), context + QStringLiteral(" should not contain ") + needle);
}

void testConnectionProviderFilesExist() {
    const QStringList files = {
        QStringLiteral("qt/inc/connection/connectionruleprovider.h"),
        QStringLiteral("qt/src/connection/connectionruleprovider.cpp")
    };

    for (const QString& file : files) {
        require(QFile::exists(file), QStringLiteral("missing connection provider file: ") + file);
    }
}

void testPackageProviderOwnsManifestValidator() {
    const QString providerHeader =
        readText(QStringLiteral("qt/inc/connection/connectionruleprovider.h"));
    const QString providerSource =
        readText(QStringLiteral("qt/src/connection/connectionruleprovider.cpp"));

    requireContains(providerHeader,
                    QStringLiteral("class ConnectionRuleProvider"),
                    QStringLiteral("connection provider header"));
    requireContains(providerHeader,
                    QStringLiteral("class PackageConnectionRuleProvider"),
                    QStringLiteral("connection provider header"));
    requireContains(providerSource,
                    QStringLiteral("ipcraft/ipcraftconnectionvalidator.h"),
                    QStringLiteral("connection provider source"));
    requireContains(providerSource,
                    QStringLiteral("IpcraftConnectionValidator validator"),
                    QStringLiteral("connection provider source"));
}

void testConnectionRuleServiceCallsProviders() {
    const QString header = readText(QStringLiteral("qt/inc/connection/connectionruleservice.h"));
    const QString source = readText(QStringLiteral("qt/src/connection/connectionruleservice.cpp"));

    requireContains(header,
                    QStringLiteral("ConnectionCheckStatus::Warning"),
                    QStringLiteral("connection rule service header"));
    requireContains(header,
                    QStringLiteral("addRuleProvider"),
                    QStringLiteral("connection rule service header"));
    requireContains(header,
                    QStringLiteral("m_ruleProviders"),
                    QStringLiteral("connection rule service header"));
    requireContains(source,
                    QStringLiteral("PackageConnectionRuleProvider"),
                    QStringLiteral("connection rule service source"));
    requireContains(source,
                    QStringLiteral("provider->canEvaluate"),
                    QStringLiteral("connection rule service source"));
    requireContains(source,
                    QStringLiteral("provider->evaluate"),
                    QStringLiteral("connection rule service source"));
    requireContains(source,
                    QStringLiteral("ConnectionRuleProviderStatus::Warning"),
                    QStringLiteral("connection rule service source"));
    requireNotContains(source,
                       QStringLiteral("ipcraft/ipcraftconnectionvalidator.h"),
                       QStringLiteral("connection rule service source"));
    requireNotContains(source,
                       QStringLiteral("IpcraftConnectionValidator validator"),
                       QStringLiteral("connection rule service source"));
}

void testWarningStatusRemainsConnectable() {
    const QString header = readText(QStringLiteral("qt/inc/connection/connectionruleservice.h"));

    requireContains(header,
                    QStringLiteral("status == ConnectionCheckStatus::Allowed"),
                    QStringLiteral("connection result helper"));
    requireContains(header,
                    QStringLiteral("status == ConnectionCheckStatus::Warning"),
                    QStringLiteral("connection result helper"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testConnectionProviderFilesExist();
    testPackageProviderOwnsManifestValidator();
    testConnectionRuleServiceCallsProviders();
    testWarningStatusRemainsConnectable();
    std::cout << "plugin_architecture_phase5_scan_test passed\n";
    return 0;
}
