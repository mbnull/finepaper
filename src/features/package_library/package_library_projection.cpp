#include "features/package_library/package_library_projection.h"

namespace finepaper {

CreationPackageItem packageLibraryCreationItem(
    const PackageDefinition& package) {
    CreationPackageItem item;
    item.reference = PackageReference{package.id, package.version};
    item.name = package.name;
    item.capabilitySummary = QStringLiteral(
        "%1\nMesh: %2–%3 × %4–%5")
        .arg(package.key(),
             QString::number(package.mesh.minimumRows),
             QString::number(package.mesh.maximumRows),
             QString::number(package.mesh.minimumColumns),
             QString::number(package.mesh.maximumColumns));
    return item;
}

EndpointLibraryItem packageLibraryEndpointItem(
    const EndpointTypeDefinition& type) {
    return EndpointLibraryItem{
        type.id,
        type.label,
        QStringLiteral(
            "%1\nDrag anywhere onto the canvas, then connect it to a Router. "
            "Dropping directly on a Router attaches immediately.")
            .arg(type.id)};
}

} // namespace finepaper
