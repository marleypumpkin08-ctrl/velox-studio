#pragma once

#include "BlendMode.h"
#include "BrushStroke.h"

#include <QImage>
#include <QPoint>
#include <QString>

namespace velox {

// Layer metadata and raster payloads are stored on the CPU. The current Vulkan
// canvas renderer draws brush-stroke geometry directly on the GPU.
class Layer {
public:
    Layer(int id, const QString& name, const QSize& size);

    int id() const { return m_id; }

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    bool visible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    bool locked() const { return m_locked; }
    void setLocked(bool locked) { m_locked = locked; }

    // 0.0 (transparent) to 1.0 (opaque).
    double opacity() const { return m_opacity; }
    void setOpacity(double opacity);

    BlendMode blendMode() const { return m_blendMode; }
    void setBlendMode(BlendMode mode) { m_blendMode = mode; }

    QPoint offset() const { return m_offset; }
    void setOffset(const QPoint& offset) { m_offset = offset; }

    const QImage& image() const { return m_image; }
    QImage& image() { return m_image; }
    void setImage(const QImage& image);

    const QVector<BrushStroke>& strokes() const { return m_strokes; }
    void addStroke(const BrushStroke& stroke) { m_strokes.append(stroke); }
    BrushStroke takeLastStroke();

private:
    int m_id;
    QString m_name;
    bool m_visible = true;
    bool m_locked = false;
    double m_opacity = 1.0;
    BlendMode m_blendMode = BlendMode::Normal;
    QPoint m_offset;
    QImage m_image;
    QVector<BrushStroke> m_strokes;
};

} // namespace velox
