// Plugin system tests for manifest discovery and command metadata.
#include "plugins/pluginregistry.h"
#include "modules/moduleprovider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

void testPluginManifestLoadsRelativePaths() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("demo/graphics")), "failed to create plugin dirs");
    writeFile(root.filePath(QStringLiteral("demo/modules.xml")), QByteArrayLiteral("<module-bundle/>"));
    writeFile(root.filePath(QStringLiteral("demo/plugin.json")), QByteArrayLiteral(R"json({
      "id": "finepaper.demo",
      "name": "Demo",
      "version": "1.0",
      "modules": "modules.xml",
      "graphics": "graphics",
      "generator": {"command": "ruby", "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}"]},
      "native": {"enabled": true, "library": "libdemo.so"}
    })json"));

    const QList<PluginDescriptor> plugins = PluginRegistry::discover({temp.path()});

    require(plugins.size() == 1, "expected one plugin");
    require(plugins.first().id == QStringLiteral("finepaper.demo"), "plugin id should load");
    require(QFileInfo(plugins.first().modulesPath).isAbsolute(), "modules path should be absolute");
    require(QFileInfo(plugins.first().graphicsPath).isAbsolute(), "graphics path should be absolute");
    require(plugins.first().native.enabled, "native metadata should be retained");
    require(plugins.first().generator.hasGenerator(), "generator should be retained");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testPluginManifestLoadsRelativePaths();
    } catch (const std::exception& error) {
        std::cerr << "plugin_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "plugin_test passed\n";
    return 0;
}
