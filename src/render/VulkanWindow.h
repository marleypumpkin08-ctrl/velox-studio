#pragma once

#include "BrushStroke.h"
#include "Document.h"

#include <QImage>
#include <QVulkanWindow>

#include <QColor>
#include <QMutex>
#include <QRectF>
#include <QVector>

#include <memory>
#include <optional>

enum class CanvasTool {
    Brush,
    RectangleSelection
};

struct CanvasLayerInfo {
    int id = 0;
    QString name;
    bool visible = true;
    bool locked = false;
    velox::BlendMode blendMode = velox::BlendMode::Normal;
};

struct CanvasRenderLayer {
    int id = 0;
    QPoint offset;
    qreal opacity = 1.0;
    bool visible = true;
    velox::BlendMode blendMode = velox::BlendMode::Normal;
    QImage image;
    QVector<velox::BrushStroke> strokes;
};

struct CanvasRenderData {
    quint64 generation = 0;
    quint64 imageGeneration = 0;
    QSize canvasSize;
    QVector<CanvasRenderLayer> layers;
    std::optional<velox::BrushStroke> activeStroke;
    QRectF selection;
    bool hasSelection = false;
};

class VulkanWindow final : public QVulkanWindow
{
    Q_OBJECT

public:
    explicit VulkanWindow(QWindow* parent = nullptr);

    void reportDeviceInitialized(const QString& deviceName);
    void reportRenderError(const QString& message);

    void setTool(CanvasTool tool);
    CanvasTool tool() const;
    void setBrushColor(const QColor& color);
    void setBrushDiameter(qreal diameter);
    void addLayer();
    void setActiveLayer(int layerId);
    void setLayerVisible(int layerId, bool visible);
    void setLayerBlendMode(int layerId, velox::BlendMode blendMode);
    bool importImageLayer(const QString& name, const QImage& image);
    void markSaved();
    void undo();
    void redo();
    void clearSelection();
    void newCanvas(const QSize& size);
    void replaceDocument(std::unique_ptr<velox::Document> document);

    QVector<CanvasLayerInfo> layers() const;
    QStringList historyEntries() const;
    int undoCount() const;
    int redoCount() const;
    int activeLayerId() const;
    bool isModified() const;
    QRectF selection() const;
    bool hasSelection() const;
    std::unique_ptr<velox::Document> documentSnapshot() const;
    std::shared_ptr<const CanvasRenderData> renderData() const;

Q_SIGNALS:
    void deviceInitialized(const QString& deviceName);
    void renderError(const QString& message);
    void documentChanged();
    void layersChanged();
    void selectionChanged(bool active);

protected:
    QVulkanWindowRenderer* createRenderer() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct UndoRecord {
        int layerId = 0;
        velox::BrushStroke stroke;
    };

    QRectF canvasRectFor(const QSize& size) const;
    QPointF mapToCanvas(const QPointF& position) const;
    void publishRenderDataLocked();

    mutable QMutex m_mutex;
    std::unique_ptr<velox::Document> m_document;
    std::shared_ptr<const CanvasRenderData> m_renderData;
    QVector<UndoRecord> m_undo;
    QVector<UndoRecord> m_redo;
    std::optional<velox::BrushStroke> m_activeStroke;
    QPointF m_selectionStart;
    QRectF m_selection;
    bool m_hasSelection = false;
    bool m_isSelecting = false;
    int m_activeLayerId = 0;
    quint64 m_renderGeneration = 0;
    quint64 m_imageGeneration = 1;
    CanvasTool m_tool = CanvasTool::Brush;
    QColor m_brushColor = QColor(QStringLiteral("#25252a"));
    qreal m_brushDiameter = 18.0;
};
