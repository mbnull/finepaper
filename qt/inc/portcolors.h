// PortColors provides color mapping for different port types and directions
#pragma once

#include "port.h"
#include "portlayout.h"
#include <QColor>

namespace PortColors {

inline QColor colorForPort(const Port& port) {
    if (port.type() == "endpoint") {
        return QColor(96, 203, 132);
    }

    const QString side = PortLayout::routerSideId(port.id());
    if (side == "north") return QColor(69, 156, 255);
    if (side == "east") return QColor(255, 177, 66);
    if (side == "south") return QColor(255, 107, 129);
    if (side == "west") return QColor(170, 122, 255);

    return QColor(70, 70, 70);
}

} // namespace PortColors
