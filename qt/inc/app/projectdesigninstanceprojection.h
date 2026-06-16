#pragma once

#include "ipcore/ipcatalogservice.h"
#include "ipcraft/core/project_design.h"
#include "project/ipinstancestate.h"

#include <QList>
#include <QVector>

namespace ProjectDesignInstanceProjection {

QVector<ProjectIpInstanceRecord> instancesFromProjectDesign(
    const ipcraft::core::ProjectDesign& design,
    const QList<IpCatalogEntry>& catalogEntries);

} // namespace ProjectDesignInstanceProjection
