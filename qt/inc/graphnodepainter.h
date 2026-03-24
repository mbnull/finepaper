// GraphNodePainter handles custom rendering of nodes in the visual editor
#pragma once

#include <QtNodes/internal/AbstractNodePainter.hpp>

class GraphNodePainter : public QtNodes::AbstractNodePainter {
public:
    void paint(QPainter* painter, QtNodes::NodeGraphicsObject& ngo) const override;
};
