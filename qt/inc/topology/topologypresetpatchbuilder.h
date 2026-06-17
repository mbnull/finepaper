#pragma once

#include "ipcraft/core/project_patch.h"
#include "topology/topologypresetbuilder.h"

#include <QString>

struct TopologyPresetPatchBuildResult {
    bool success = false;
    QString error;
    ipcraft::core::ProjectPatch patch;
};

class TopologyPresetPatchBuilder {
public:
    static TopologyPresetPatchBuildResult build(const TopologyPresetRequest& request);
};
