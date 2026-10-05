#include "Layer.h"

#include <QtGlobal>

namespace velox {

Layer::Layer(int id, const QString& name, const QSize& size)
    : m_id(id)
    , m_name(name)
    , m_image(size, QImage::Format_ARGB32_Premultiplied)
{
    m_image.fill(Qt::transparent);
}

void Layer::setOpacity(double opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
}

void Layer::setImage(const QImage& image)
{
    m_image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

BrushStroke Layer::takeLastStroke()
{
    if (m_strokes.isEmpty()) {
        return {};
    }
    return m_strokes.takeLast();
}

} // namespace velox
