#include "VlxFormat.h"

#include "BlendMode.h"
#include "velox/Version.generated.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <limits>
#include <utility>

namespace velox::VlxFormat {
namespace {

constexpr quint32 kMaximumHeaderBytes = 64U * 1024U * 1024U;
constexpr int kMaximumCanvasDimension = 8192;
constexpr int kMaximumLayers = 256;
constexpr qint64 kMaximumDecodedImagePixels = 64LL * 1024LL * 1024LL;
constexpr qsizetype kMaximumPointsPerStroke = 1'000'000;

SaveResult failure(const QString& message)
{
    return {false, message};
}

QJsonObject serializeLayer(const Layer& layer)
{
    QJsonArray strokes;
    for (const BrushStroke& stroke : layer.strokes()) {
        QJsonArray points;
        for (const QPointF& point : stroke.points) {
            points.append(QJsonArray{point.x(), point.y()});
        }
        strokes.append(QJsonObject{
            {QStringLiteral("color"), stroke.color.name(QColor::HexArgb)},
            {QStringLiteral("diameter"), stroke.diameter},
            {QStringLiteral("points"), points}
        });
    }

    return {
        {QStringLiteral("name"), layer.name()},
        {QStringLiteral("visible"), layer.visible()},
        {QStringLiteral("locked"), layer.locked()},
        {QStringLiteral("opacity"), layer.opacity()},
        {QStringLiteral("blendMode"), static_cast<int>(layer.blendMode())},
        {QStringLiteral("offsetX"), layer.offset().x()},
        {QStringLiteral("offsetY"), layer.offset().y()},
        {QStringLiteral("strokes"), strokes}
    };
}

bool parseStroke(const QJsonValue& value, const QSize& canvasSize,
                 BrushStroke* stroke, QString* error)
{
    if (!value.isObject()) {
        *error = QStringLiteral("A stroke entry is not an object.");
        return false;
    }
    const QJsonObject object = value.toObject();
    const QColor color(object.value(QStringLiteral("color")).toString());
    const double diameter = object.value(QStringLiteral("diameter")).toDouble(-1.0);
    const QJsonArray points = object.value(QStringLiteral("points")).toArray();
    if (!color.isValid() || !std::isfinite(diameter) || diameter <= 0.0 || diameter > 2048.0
        || points.isEmpty() || points.size() > kMaximumPointsPerStroke) {
        *error = QStringLiteral("A brush stroke contains invalid color, size, or points.");
        return false;
    }

    BrushStroke result;
    result.color = color;
    result.diameter = diameter;
    result.points.reserve(points.size());
    for (const QJsonValue& pointValue : points) {
        const QJsonArray point = pointValue.toArray();
        if (point.size() != 2 || !point.at(0).isDouble() || !point.at(1).isDouble()) {
            *error = QStringLiteral("A brush stroke contains an invalid point.");
            return false;
        }
        const double x = point.at(0).toDouble();
        const double y = point.at(1).toDouble();
        if (!std::isfinite(x) || !std::isfinite(y)
            || x < 0.0 || y < 0.0
            || x > canvasSize.width() || y > canvasSize.height()) {
            *error = QStringLiteral("A brush point lies outside the canvas.");
            return false;
        }
        result.points.append(QPointF(x, y));
    }
    *stroke = std::move(result);
    return true;
}

bool readLayerImage(QDataStream& stream, const QSize& canvasSize,
                    qint64* decodedPixels, QImage* image, QString* error)
{
    quint32 imageSize = 0;
    stream >> imageSize;
    if (stream.status() != QDataStream::Ok || imageSize > 64U * 1024U * 1024U
        || imageSize > static_cast<quint64>(stream.device()->bytesAvailable())) {
        *error = QStringLiteral("A layer image block has an invalid size.");
        return false;
    }
    if (imageSize == 0) {
        *image = QImage();
        return true;
    }

    QByteArray png(static_cast<qsizetype>(imageSize), Qt::Uninitialized);
    if (stream.readRawData(png.data(), static_cast<int>(imageSize))
        != static_cast<int>(imageSize)) {
        *error = QStringLiteral("A layer image block is truncated.");
        return false;
    }
    QBuffer buffer(&png);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    const QSize dimensions = reader.size();
    const qint64 pixels = static_cast<qint64>(dimensions.width()) * dimensions.height();
    if (dimensions.isEmpty()
        || dimensions.width() > kMaximumCanvasDimension
        || dimensions.height() > kMaximumCanvasDimension
        || dimensions.width() > canvasSize.width() * 2
        || dimensions.height() > canvasSize.height() * 2
        || pixels < 0 || *decodedPixels + pixels > kMaximumDecodedImagePixels) {
        *error = QStringLiteral("A layer image exceeds the supported dimensions.");
        return false;
    }
    *image = reader.read();
    if (image->isNull()) {
        *error = QStringLiteral("A layer image could not be decoded.");
        return false;
    }
    *decodedPixels += pixels;
    return true;
}

} // namespace

SaveResult save(const Document& document, const QString& path)
{
    const QSize canvasSize = document.canvasSize();
    if (path.isEmpty() || canvasSize.width() <= 0 || canvasSize.height() <= 0
        || canvasSize.width() > kMaximumCanvasDimension
        || canvasSize.height() > kMaximumCanvasDimension
        || document.layerCount() <= 0 || document.layerCount() > kMaximumLayers) {
        return failure(QStringLiteral("The document has invalid dimensions or layer count."));
    }

    QJsonArray layers;
    for (int i = 0; i < document.layerCount(); ++i) {
        const Layer* layer = document.layerAt(i);
        layers.append(serializeLayer(*layer));
    }
    const QJsonObject root{
        {QStringLiteral("name"), document.name()},
        {QStringLiteral("width"), canvasSize.width()},
        {QStringLiteral("height"), canvasSize.height()},
        {QStringLiteral("layers"), layers}
    };
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (json.size() > static_cast<qsizetype>(kMaximumHeaderBytes)) {
        return failure(QStringLiteral("The document metadata exceeds the file-format limit."));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return failure(QStringLiteral("Could not open the project file for writing: %1")
                           .arg(file.errorString()));
    }
    if (file.write("VLX1", 4) != 4) {
        return failure(QStringLiteral("Could not write the project-file signature."));
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint32>(VELOX_VLX_FORMAT_VERSION)
           << static_cast<quint32>(json.size());
    if (stream.status() != QDataStream::Ok
        || stream.writeRawData(json.constData(), json.size()) != json.size()) {
        return failure(QStringLiteral("Could not write project metadata."));
    }

    for (int i = 0; i < document.layerCount(); ++i) {
        const QImage& image = document.layerAt(i)->image();
        QByteArray png;
        if (!image.isNull()) {
            QBuffer buffer(&png);
            if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")
                || png.size() > static_cast<qsizetype>(std::numeric_limits<quint32>::max())) {
                return failure(QStringLiteral("Could not encode a layer image."));
            }
        }
        stream << static_cast<quint32>(png.size());
        if (stream.status() != QDataStream::Ok
            || (!png.isEmpty()
                && stream.writeRawData(png.constData(), png.size()) != png.size())) {
            return failure(QStringLiteral("Could not write layer image data."));
        }
    }
    if (!file.commit()) {
        return failure(QStringLiteral("Could not finalize the project file: %1")
                           .arg(file.errorString()));
    }
    return {true, {}};
}

std::unique_ptr<Document> load(const QString& path, QString* error)
{
    auto fail = [error](const QString& message) -> std::unique_ptr<Document> {
        if (error != nullptr) {
            *error = message;
        }
        return nullptr;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Could not open the project file: %1").arg(file.errorString()));
    }
    if (file.read(4) != QByteArrayLiteral("VLX1")) {
        return fail(QStringLiteral("The file is not a Velox Studio project."));
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 version = 0;
    quint32 jsonSize = 0;
    stream >> version >> jsonSize;
    if (stream.status() != QDataStream::Ok || version != VELOX_VLX_FORMAT_VERSION
        || jsonSize == 0 || jsonSize > kMaximumHeaderBytes
        || jsonSize > static_cast<quint64>(stream.device()->bytesAvailable())) {
        return fail(QStringLiteral("The project header is invalid or unsupported."));
    }

    QByteArray json(static_cast<qsizetype>(jsonSize), Qt::Uninitialized);
    if (stream.readRawData(json.data(), static_cast<int>(jsonSize))
        != static_cast<int>(jsonSize)) {
        return fail(QStringLiteral("The project metadata is truncated."));
    }
    QJsonParseError parseError{};
    const QJsonDocument documentJson = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !documentJson.isObject()) {
        return fail(QStringLiteral("The project metadata is invalid JSON."));
    }

    const QJsonObject root = documentJson.object();
    const int width = root.value(QStringLiteral("width")).toInt();
    const int height = root.value(QStringLiteral("height")).toInt();
    const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
    if (width <= 0 || height <= 0 || width > kMaximumCanvasDimension
        || height > kMaximumCanvasDimension || layers.isEmpty()
        || layers.size() > kMaximumLayers) {
        return fail(QStringLiteral("The project canvas dimensions or layer count are invalid."));
    }

    auto result = std::make_unique<Document>(QSize(width, height),
                                              root.value(QStringLiteral("name")).toString());
    qint64 decodedPixels = 0;
    for (const QJsonValue& layerValue : layers) {
        if (!layerValue.isObject()) {
            return fail(QStringLiteral("A project layer entry is invalid."));
        }
        const QJsonObject layerObject = layerValue.toObject();
        const auto blendMode = blendModeFromInt(layerObject.value(QStringLiteral("blendMode")).toInt());
        const double opacity = layerObject.value(QStringLiteral("opacity")).toDouble(-1.0);
        const QJsonArray strokes = layerObject.value(QStringLiteral("strokes")).toArray();
        if (!blendMode || !std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0
            || strokes.size() > 100000) {
            return fail(QStringLiteral("A project layer has invalid properties."));
        }

        Layer* layer = result->addLayer(layerObject.value(QStringLiteral("name")).toString());
        layer->setVisible(layerObject.value(QStringLiteral("visible")).toBool(true));
        layer->setLocked(layerObject.value(QStringLiteral("locked")).toBool(false));
        layer->setOpacity(opacity);
        layer->setBlendMode(*blendMode);
        layer->setOffset(QPoint(layerObject.value(QStringLiteral("offsetX")).toInt(),
                                layerObject.value(QStringLiteral("offsetY")).toInt()));
        for (const QJsonValue& strokeValue : strokes) {
            BrushStroke stroke;
            QString strokeError;
            if (!parseStroke(strokeValue, QSize(width, height), &stroke, &strokeError)) {
                return fail(strokeError);
            }
            layer->addStroke(stroke);
        }

        QImage image;
        QString imageError;
        if (!readLayerImage(stream, QSize(width, height), &decodedPixels, &image, &imageError)) {
            return fail(imageError);
        }
        if (!image.isNull()) {
            layer->setImage(image);
        }
    }
    if (stream.status() != QDataStream::Ok || !file.atEnd()) {
        return fail(QStringLiteral("The project file has invalid or trailing data."));
    }
    result->setModified(false);
    return result;
}

} // namespace velox::VlxFormat
