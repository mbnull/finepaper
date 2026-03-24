// PortRef identifies a specific port on a specific module
#pragma once

#include <QString>

struct PortRef {
    QString moduleId;
    QString portId;

    bool operator==(const PortRef& other) const {
        return moduleId == other.moduleId && portId == other.portId;
    }
};
