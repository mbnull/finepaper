#include "gui/main_window.h"

#include <QApplication>
#include <QDir>

namespace {

QStringList packageRoots(const QStringList& arguments) {
    QStringList roots;
    for (qsizetype index = 1; index + 1 < arguments.size(); ++index) {
        if (arguments.at(index) == QStringLiteral("--package-root")) {
            roots.append(arguments.at(index + 1));
        }
    }
    if (roots.isEmpty()) {
        const QString configured = qEnvironmentVariable("FINEPAPER_PACKAGE_PATH");
        if (!configured.isEmpty()) {
            roots += configured.split(QDir::listSeparator(), Qt::SkipEmptyParts);
        }
    }
    if (roots.isEmpty()) {
        roots.append(QDir::current().filePath(QStringLiteral("packages")));
    }
    return roots;
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("finepaper"));
    finepaper::FinepaperMainWindow window(packageRoots(QCoreApplication::arguments()));
    window.show();
    return application.exec();
}
