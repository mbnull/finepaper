#pragma once

#include "application/domain_configuration.h"

#include <optional>
#include <utility>

namespace finepaper {

// Typed request used by the interactive "New Design" workflow. The JSON
// createDesign overload remains the complete protocol boundary for automation
// requests that also seed parameters, Endpoints, extensions, or Package data.
struct DesignCreationRequest final {
    DesignCreationRequest(
        QString designName,
        PackageReference packageReference,
        TopologySpec initialTopology,
        std::optional<DomainConfiguration> initialDomainConfiguration =
            std::nullopt)
        : name(std::move(designName)),
          package(std::move(packageReference)),
          topology(std::move(initialTopology)),
          domainConfiguration(std::move(initialDomainConfiguration)) {}

    QString name;
    PackageReference package;
    TopologySpec topology;
    std::optional<DomainConfiguration> domainConfiguration = std::nullopt;
};

} // namespace finepaper
