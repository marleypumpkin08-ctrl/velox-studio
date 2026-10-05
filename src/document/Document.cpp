#include "Document.h"

#include <algorithm>

namespace velox {

Document::Document(const QSize& canvasSize, const QString& name)
    : m_name(name.isEmpty() ? QStringLiteral("Untitled") : name)
    , m_canvasSize(canvasSize)
{
}

Layer* Document::layerAt(int index)
{
    if (index < 0 || index >= layerCount()) {
        return nullptr;
    }
    return m_layers[static_cast<size_t>(index)].get();
}

const Layer* Document::layerAt(int index) const
{
    if (index < 0 || index >= layerCount()) {
        return nullptr;
    }
    return m_layers[static_cast<size_t>(index)].get();
}

Layer* Document::layerById(int id)
{
    for (const auto& layer : m_layers) {
        if (layer->id() == id) {
            return layer.get();
        }
    }
    return nullptr;
}

const Layer* Document::layerById(int id) const
{
    for (const auto& layer : m_layers) {
        if (layer->id() == id) {
            return layer.get();
        }
    }
    return nullptr;
}

Layer* Document::addLayer(const QString& name, int aboveIndex)
{
    const int id = nextLayerId();
    const QString layerName = name.isEmpty() ? QStringLiteral("Layer %1").arg(id) : name;
    auto layer = std::make_unique<Layer>(id, layerName, m_canvasSize);
    Layer* raw = layer.get();

    size_t position = m_layers.size();
    if (aboveIndex >= 0 && aboveIndex < layerCount()) {
        position = static_cast<size_t>(aboveIndex) + 1;
    }
    m_layers.insert(m_layers.begin() + static_cast<std::ptrdiff_t>(position), std::move(layer));
    m_modified = true;
    return raw;
}

Layer* Document::addLayerWithImage(const QString& name, const QImage& image)
{
    Layer* layer = addLayer(name);
    layer->setImage(image);
    return layer;
}

std::unique_ptr<Document> Document::clone() const
{
    auto copy = std::make_unique<Document>(m_canvasSize, m_name);
    for (const auto& layer : m_layers) {
        Layer* cloned = copy->addLayerWithImage(layer->name(), layer->image());
        cloned->setVisible(layer->visible());
        cloned->setLocked(layer->locked());
        cloned->setOpacity(layer->opacity());
        cloned->setBlendMode(layer->blendMode());
        cloned->setOffset(layer->offset());
        for (const BrushStroke& stroke : layer->strokes()) {
            cloned->addStroke(stroke);
        }
    }
    copy->setModified(m_modified);
    return copy;
}

bool Document::removeLayer(int index)
{
    if (index < 0 || index >= layerCount()) {
        return false;
    }
    m_layers.erase(m_layers.begin() + index);
    m_modified = true;
    return true;
}

bool Document::moveLayer(int from, int to)
{
    if (from < 0 || from >= layerCount() || to < 0 || to >= layerCount()) {
        return false;
    }
    if (from == to) {
        return true;
    }
    auto layer = std::move(m_layers[static_cast<size_t>(from)]);
    m_layers.erase(m_layers.begin() + from);
    m_layers.insert(m_layers.begin() + to, std::move(layer));
    m_modified = true;
    return true;
}

} // namespace velox
