#include "execution/process.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

namespace {

using namespace finepaper;

int failures = 0;

void runRubyTest(const QString& ruby,
                 const QString& projectRoot,
                 const QString& relativePath) {
    const ProcessResult result = runProcess(
        ruby,
        QStringList{QDir(projectRoot).filePath(relativePath)},
        projectRoot,
        120'000);
    if (result.started && !result.timedOut && !result.crashed
        && result.exitCode == 0) {
        return;
    }

    QTextStream(stderr)
        << "FAILED: " << relativePath << Qt::endl
        << result.standardOutput << result.standardError << result.error
        << Qt::endl;
    ++failures;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString ruby = QStandardPaths::findExecutable(QStringLiteral("ruby"));
    if (ruby.isEmpty()) {
        QTextStream(stderr) << "FAILED: Ruby is required by the bundled V3 runtime"
                            << Qt::endl;
        return 1;
    }

    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_link_bundle.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_domain_realizer.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_domain_rtl_context.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_power_intent_compiler.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_power_intent_adapter.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_rtl_hierarchy_manifest.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_domain_rtl_materialization.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_domain_rtl_evidence.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_async_fifo.rb"));
    runRubyTest(
        ruby,
        projectRoot,
        QStringLiteral(
            "packages/finepaper-noc-v3/runtime/test/test_reset_synchronizer.rb"));

    if (failures == 0) {
        QTextStream(stdout) << "finepaper-v3-runtime-tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
