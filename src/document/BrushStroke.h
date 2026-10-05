#pragma once

#include <QColor>
#include <QPointF>
#include <QVector>

namespace velox {

struct BrushStroke {
    QVector<QPointF> points;
    QColor color = Qt::black;
    qreal diameter = 18.0;
};

} // namespace velox
