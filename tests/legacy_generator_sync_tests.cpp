#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace {

struct SharedFile {
    const char* relativePath;
};

// Keep this list explicit: files with V3 semantics and V3-only stubs must not
// accidentally become part of the byte-for-byte compatibility surface.
constexpr SharedFile sharedFiles[] = {
    {"runtime/legacy-generator/bin/drc"},
    {"runtime/legacy-generator/bin/generate"},
    {"runtime/legacy-generator/src/ruby/drc/connection_drc.rb"},
    {"runtime/legacy-generator/src/ruby/drc/drc_runner.rb"},
    {"runtime/legacy-generator/src/ruby/drc/endpoint_drc.rb"},
    {"runtime/legacy-generator/src/ruby/drc/xp_drc.rb"},
    {"runtime/legacy-generator/src/ruby/model/connection.rb"},
    {"runtime/legacy-generator/src/ruby/model/endpoint.rb"},
    {"runtime/legacy-generator/src/ruby/model/xp.rb"},
    {"runtime/legacy-generator/src/ruby/parser/verilog_parser.rb"},
    {"runtime/legacy-generator/src/ruby/plugin/plugin_base.rb"},
    {"runtime/legacy-generator/template/stubs/fp_ni_credit_flow.sv"},
    {"runtime/legacy-generator/template/stubs/fp_ni_error_check.sv"},
    {"runtime/legacy-generator/template/stubs/fp_ni_protocol_decode.sv"},
    {"runtime/legacy-generator/template/stubs/fp_ni_qos_classifier.sv"},
    {"runtime/legacy-generator/template/stubs/fp_ni_trace_probe.sv"},
    {"runtime/legacy-generator/template/stubs/fp_xp_channel_switch.sv"},
    {"runtime/legacy-generator/template/stubs/fp_xp_credit_accounting.sv"},
    {"runtime/legacy-generator/template/stubs/fp_xp_route_decode.sv"},
};

constexpr const char* semanticDifferences[] = {
    "runtime/legacy-generator/src/ruby/generator/rtl_generator.rb",
    "runtime/legacy-generator/src/ruby/model/noc_config.rb",
    "runtime/legacy-generator/src/ruby/parser/json_parser.rb",
    "runtime/legacy-generator/src/ruby/topology/topology_expander.rb",
    "runtime/legacy-generator/template/ni.sv.erb",
    "runtime/legacy-generator/template/stubs/fp_ni_request_queue.sv",
    "runtime/legacy-generator/template/stubs/fp_ni_response_queue.sv",
    "runtime/legacy-generator/template/top.v.erb",
    "runtime/legacy-generator/template/xp.sv.erb",
};

constexpr const char* v3OnlyStubs[] = {
    "runtime/legacy-generator/template/stubs/fp_async_ready_valid_fifo.sv",
    "runtime/legacy-generator/template/stubs/fp_reset_synchronizer.sv",
};

// V1 retains its original compatibility documentation, examples, regression
// driver, and golden outputs. Directory-level categories are intentional here:
// adding another fixture remains V1-local, while any new production path must
// still be classified explicitly by the intersection/symmetric-difference gate.
constexpr const char* v1OnlyFiles[] = {
    "runtime/legacy-generator/CLAUDE.md",
};

constexpr const char* v1OnlyDirectoryPrefixes[] = {
    "runtime/legacy-generator/examples/",
    "runtime/legacy-generator/test/",
};

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    QTextStream(stderr) << "FAILED: " << message << Qt::endl;
    ++failures;
}

QString path(const QString& packageRoot, const char* relativePath) {
    return QDir(packageRoot).filePath(QString::fromUtf8(relativePath));
}

bool contains(const char* const* paths, int count, const char* candidate) {
    return std::any_of(paths, paths + count, [candidate](const char* value) {
        return QString::fromUtf8(value) == QString::fromUtf8(candidate);
    });
}

bool isClassifiedV1OnlyPath(const QString& candidate) {
    constexpr int fileCount = sizeof(v1OnlyFiles) / sizeof(v1OnlyFiles[0]);
    for (int index = 0; index < fileCount; ++index) {
        if (candidate == QString::fromUtf8(v1OnlyFiles[index])) {
            return true;
        }
    }
    for (const char* prefix : v1OnlyDirectoryPrefixes) {
        if (candidate.startsWith(QString::fromUtf8(prefix))) {
            return true;
        }
    }
    return false;
}

QByteArray fileDigest(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(
        file.readAll(), QCryptographicHash::Sha256);
}

QSet<QString> legacyGeneratorFiles(const QString& packageRoot) {
    const QString generatorRoot = QDir(packageRoot).filePath(
        QStringLiteral("runtime/legacy-generator"));
    QSet<QString> files;
    QDirIterator iterator(
        generatorRoot,
        QDir::Files,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolutePath = iterator.next();
        files.insert(QDir(packageRoot).relativeFilePath(absolutePath));
    }
    return files;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);
    const QString v1Root = QDir(projectRoot).filePath(
        QStringLiteral("packages/finepaper-noc"));
    const QString v3Root = QDir(projectRoot).filePath(
        QStringLiteral("packages/finepaper-noc-v3"));

    constexpr int sharedCount = sizeof(sharedFiles) / sizeof(sharedFiles[0]);
    constexpr int semanticCount =
        sizeof(semanticDifferences) / sizeof(semanticDifferences[0]);
    constexpr int stubCount = sizeof(v3OnlyStubs) / sizeof(v3OnlyStubs[0]);
    check(sharedCount == 19, QStringLiteral("the shared compatibility manifest has 19 entries"));

    QSet<QString> sharedManifest;
    QSet<QString> semanticManifest;
    for (const SharedFile& shared : sharedFiles) {
        const QString relativePath = QString::fromUtf8(shared.relativePath);
        check(!sharedManifest.contains(relativePath),
              QStringLiteral("shared manifest has no duplicate: %1")
                  .arg(relativePath));
        sharedManifest.insert(relativePath);
    }
    for (const char* semantic : semanticDifferences) {
        const QString relativePath = QString::fromUtf8(semantic);
        check(!semanticManifest.contains(relativePath),
              QStringLiteral("semantic manifest has no duplicate: %1")
                  .arg(relativePath));
        semanticManifest.insert(relativePath);
    }

    const QSet<QString> v1Files = legacyGeneratorFiles(v1Root);
    const QSet<QString> v3Files = legacyGeneratorFiles(v3Root);
    for (const QString& relativePath : v1Files) {
        if (!v3Files.contains(relativePath)) {
            check(isClassifiedV1OnlyPath(relativePath),
                  QStringLiteral(
                      "every V1-only generator-tree file is explicitly categorized: %1")
                      .arg(relativePath));
            continue;
        }
        check(sharedManifest.contains(relativePath)
                  || semanticManifest.contains(relativePath),
              QStringLiteral(
                  "every file present in both generator trees is explicitly categorized: %1")
                  .arg(relativePath));
    }
    for (const QString& relativePath : v3Files) {
        if (v1Files.contains(relativePath)) {
            continue;
        }
        const QByteArray encoded = relativePath.toUtf8();
        check(contains(v3OnlyStubs, stubCount, encoded.constData()),
              QStringLiteral(
                  "every V3-only generator-tree file is explicitly categorized: %1")
                  .arg(relativePath));
    }

    for (int i = 0; i < sharedCount; ++i) {
        const char* relativePath = sharedFiles[i].relativePath;
        check(!contains(semanticDifferences, semanticCount, relativePath)
                  && !contains(v3OnlyStubs, stubCount, relativePath),
              QStringLiteral("shared manifest excludes semantic and V3-only paths: %1")
                  .arg(QString::fromUtf8(relativePath)));
        for (int j = 0; j < i; ++j) {
            check(QString::fromUtf8(sharedFiles[j].relativePath)
                      != QString::fromUtf8(relativePath),
                  QStringLiteral("shared manifest has no duplicate: %1")
                      .arg(QString::fromUtf8(relativePath)));
        }

        const QString v1Path = path(v1Root, relativePath);
        const QString v3Path = path(v3Root, relativePath);
        check(QFileInfo(v1Path).isFile() && QFileInfo(v3Path).isFile(),
              QStringLiteral("shared file exists in both Packages: %1")
                  .arg(QString::fromUtf8(relativePath)));
        check(fileDigest(v1Path) == fileDigest(v3Path),
              QStringLiteral("shared file differs between V1 and V3: %1")
                  .arg(QString::fromUtf8(relativePath)));
    }

    for (int i = 0; i < semanticCount; ++i) {
        const QString v1Path = path(v1Root, semanticDifferences[i]);
        const QString v3Path = path(v3Root, semanticDifferences[i]);
        check(!contains(v3OnlyStubs, stubCount, semanticDifferences[i]),
              QStringLiteral("semantic-difference manifest has no V3-stub overlap: %1")
                  .arg(QString::fromUtf8(semanticDifferences[i])));
        check(QFileInfo(v1Path).isFile() && QFileInfo(v3Path).isFile(),
              QStringLiteral("semantic-difference file exists in both Packages: %1")
                  .arg(QString::fromUtf8(semanticDifferences[i])));
        check(fileDigest(v1Path) != fileDigest(v3Path),
              QStringLiteral(
                  "a converged semantic file must move into the shared manifest: %1")
                  .arg(QString::fromUtf8(semanticDifferences[i])));
    }

    for (const char* relativePath : v3OnlyStubs) {
        check(QFileInfo(path(v3Root, relativePath)).isFile(),
              QStringLiteral("V3-only stub exists: %1").arg(QString::fromUtf8(relativePath)));
        check(!QFileInfo::exists(path(v1Root, relativePath)),
              QStringLiteral("V3-only stub is not copied into V1: %1")
                  .arg(QString::fromUtf8(relativePath)));
    }

    if (failures == 0) {
        QTextStream(stdout) << "finepaper-legacy-generator-sync-tests passed (19 shared files)"
                            << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
