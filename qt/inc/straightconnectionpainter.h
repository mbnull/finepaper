#pragma once

#include <QtNodes/internal/AbstractConnectionPainter.hpp>
#include <QPainterPath>

class StraightConnectionPainter : public QtNodes::AbstractConnectionPainter {
public:
    void paint(QPainter* painter, QtNodes::ConnectionGraphicsObject const& cgo) const override;
    QPainterPath getPainterStroke(QtNodes::ConnectionGraphicsObject const& cgo) const override;
};
