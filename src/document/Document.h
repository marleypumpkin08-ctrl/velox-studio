#pragma once

#include "Layer.h"

#include <QSize>
#include <QString>

#include <memory>
#include <vector>

namespace velox {

// The document model: a canvas size and an ordered stack of layers
// (index 0 is the bottom layer).
class Document {
public:
    explicit Document(const QSize& canvasSize = QSize(1920, 1080), const QString& name = QString());

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    QSize canvasSize() const { return m_canvasSize; }

    bool isModified() const { return m_modified; }
    void setModified(bool modified) { m_modified = modified; }

    int layerCount() const { return static_cast<int>(m_layers.size()); }
    Layer* layerAt(int index);
    const Layer* layerAt(int index) const;
    Layer* layerById(int id);
    const Layer* layerById(int id) const;

    // Adds a new transparent layer above the given index (or on top when
    // index is negative) and returns it.
    Layer* addLayer(const QString& name = QString(), int aboveIndex = -1);

    // Adds a layer carrying an existing image (used by the file importers).
    Layer* addLayerWithImage(const QString& name, const QImage& image);
    std::unique_ptr<Document> clone() const;

    bool removeLayer(int index);
    bool moveLayer(int from, int to);

    int nextLayerId() { return m_nextLayerId++; }

private:
    QString m_name;
    QSize m_canvasSize;
    bool m_modified = false;
    int m_nextLayerId = 1;
    std::vector<std::unique_ptr<Layer>> m_layers;
};

} // namespace velox
