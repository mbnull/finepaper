// IpCoreRuntimeRegistry is retained for descriptor compatibility only.
#include "ipcore/ipcoreruntimeregistry.h"
#include "app/appsettings.h"

#include <QFileInfo>

namespace {

void appendUniquePath(QStringList& paths, const QString& path) {
    if (path.trimmed().isEmpty()) {
        return;
    }

    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (!paths.contains(absolutePath)) {
        paths.append(absolutePath);
    }
}

} // namespace

IpCoreRuntimeRegistry& IpCoreRuntimeRegistry::instance() {
    static IpCoreRuntimeRegistry registry;
    return registry;
}

IpCoreRuntimeRegistry::IpCoreRuntimeRegistry()
    : m_runtimes(discover(defaultRuntimeRoots())) {}

QList<IpCoreRuntimeDescriptor> IpCoreRuntimeRegistry::discover(const QStringList& roots) {
    Q_UNUSED(roots);
    return {};
}

QStringList IpCoreRuntimeRegistry::defaultRuntimeRoots() {
    QStringList roots;
    for (const QString& path : AppSettings().ipcorePaths()) {
        appendUniquePath(roots, path);
    }
    return roots;
}

const QList<IpCoreRuntimeDescriptor>& IpCoreRuntimeRegistry::runtimes() const {
    return m_runtimes;
}

const IpCoreRuntimeDescriptor* IpCoreRuntimeRegistry::runtime(const QString& ipcoreId) const {
    for (const IpCoreRuntimeDescriptor& descriptor : m_runtimes) {
        if (descriptor.id == ipcoreId) {
            return &descriptor;
        }
    }
    return nullptr;
}
