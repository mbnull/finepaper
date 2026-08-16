#include "gui/main_window.h"
#include "ui/theme/workbench_style.h"

#include <QApplication>
#include <QSettings>

namespace {

QStringList explicitPackageRoots(const QStringList& arguments) {
    QStringList roots;
    for (qsizetype index = 1; index + 1 < arguments.size(); ++index) {
        if (arguments.at(index) == QStringLiteral("--package-root")) {
            roots.append(arguments.at(index + 1));
        }
    }
    return roots;
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Finepaper"));
    QCoreApplication::setApplicationName(QStringLiteral("finepaper"));
    finepaper::ui::applyWorkbenchStyle(application);
    const QStringList configuredPackageRoots = QSettings().value(
        finepaper::installedPackageRootsSetting).toStringList();
    finepaper::FinepaperMainWindow window(
        finepaper::resolveRuntimeLocations(
            explicitPackageRoots(QCoreApplication::arguments()), configuredPackageRoots));
    window.show();
    return application.exec();
}
