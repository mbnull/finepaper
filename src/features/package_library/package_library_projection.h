#pragma once

#include "features/package_library/package_library_panel.h"
#include "package/package.h"

namespace finepaper {

[[nodiscard]] CreationPackageItem packageLibraryCreationItem(
    const PackageDefinition& package);
[[nodiscard]] EndpointLibraryItem packageLibraryEndpointItem(
    const EndpointTypeDefinition& type);

} // namespace finepaper
