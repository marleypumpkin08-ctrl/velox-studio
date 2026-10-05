#include "UpdateManifest.h"
#include "UpdaterArguments.h"
#include "Version.h"
#include "VlxFormat.h"
#include "VulkanWindow.h"

#include <BrushStroke.h>
#include <Document.h>
#include <Layer.h>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QVersionNumber>
#include <QVulkanInstance>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message);

void sendMouse(VulkanWindow& window, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, position, position, position, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
}

void canvasInteractionTests()
{
    VulkanWindow canvas;
    canvas.resize(800, 600);
    const QPointF firstPoint(200.0, 200.0);
    const QPointF firstStrokeEnd(260.0, 240.0);
    sendMouse(canvas, QEvent::MouseButtonPress, firstPoint,
              Qt::LeftButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseMove, firstStrokeEnd,
              Qt::NoButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, firstStrokeEnd,
              Qt::LeftButton, Qt::NoButton);

    std::unique_ptr<velox::Document> document = canvas.documentSnapshot();
    check(document->layerAt(0)->strokes().size() == 1,
          "commits a brush stroke from canvas input");
    check(document->layerAt(0)->strokes().constFirst().points.size() > 1,
          "interpolates brush points while dragging");
    check(canvas.undoCount() == 1, "adds the stroke to undo history");
    canvas.undo();
    check(canvas.documentSnapshot()->layerAt(0)->strokes().isEmpty()
              && canvas.redoCount() == 1,
          "undo removes a stroke and enables redo");
    canvas.redo();
    check(canvas.documentSnapshot()->layerAt(0)->strokes().size() == 1
              && canvas.undoCount() == 1,
          "redo restores the stroke");

    canvas.setTool(CanvasTool::RectangleSelection);
    const QPointF selectionStart(200.0, 200.0);
    const QPointF selectionEnd(350.0, 350.0);
    sendMouse(canvas, QEvent::MouseButtonPress, selectionStart,
              Qt::LeftButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseMove, selectionEnd,
              Qt::NoButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, selectionEnd,
              Qt::LeftButton, Qt::NoButton);
    check(canvas.hasSelection(), "creates a rectangular canvas selection");

    canvas.setTool(CanvasTool::Brush);
    const QPointF outsideSelection(500.0, 500.0);
    sendMouse(canvas, QEvent::MouseButtonPress, outsideSelection,
              Qt::LeftButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, outsideSelection,
              Qt::LeftButton, Qt::NoButton);
    check(canvas.documentSnapshot()->layerAt(0)->strokes().size() == 1,
          "clips brush input outside the active selection");

    const QPointF insideSelection(300.0, 300.0);
    sendMouse(canvas, QEvent::MouseButtonPress, insideSelection,
              Qt::LeftButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, insideSelection,
              Qt::LeftButton, Qt::NoButton);
    check(canvas.documentSnapshot()->layerAt(0)->strokes().size() == 2,
          "accepts brush input inside the active selection");

    const int firstLayerId = canvas.activeLayerId();
    canvas.setLayerVisible(firstLayerId, false);
    check(!canvas.layers().constFirst().visible,
          "updates layer visibility from the canvas model");
    canvas.addLayer();
    check(canvas.layers().size() == 2 && canvas.activeLayerId() != firstLayerId,
          "adds a layer and makes it active");
    QImage imported(8, 6, QImage::Format_ARGB32);
    imported.fill(QColor(220, 40, 30, 190));
    check(canvas.importImageLayer(QStringLiteral("Imported"), imported),
          "imports an image into a new canvas layer");
    document = canvas.documentSnapshot();
    const velox::Layer* imageLayer = document->layerAt(document->layerCount() - 1);
    const QImage normalizedImport =
        imported.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    check(imageLayer->image().size() == imported.size()
              && imageLayer->image().pixelColor(0, 0) == normalizedImport.pixelColor(0, 0),
          "preserves imported layer pixels in the document");
    canvas.setLayerBlendMode(imageLayer->id(), velox::BlendMode::Multiply);
    check(canvas.layers().constFirst().blendMode == velox::BlendMode::Multiply,
          "stores a supported GPU layer blend mode");
}

void vulkanImageLayerSmokeTest(QGuiApplication& app)
{
    QVulkanInstance instance;
    instance.setApiVersion(QVersionNumber(1, 1, 0));
    if (!instance.create()) {
        std::cout << "SKIP: Vulkan instance is unavailable on this machine.\n";
        return;
    }

    VulkanWindow canvas;
    canvas.setVulkanInstance(&instance);
    canvas.resize(640, 480);
    QImage image(64, 48, QImage::Format_ARGB32);
    image.fill(QColor(180, 60, 30, 210));
    check(canvas.importImageLayer(QStringLiteral("GPU upload"), image),
          "prepares a raster layer for Vulkan upload");
    canvas.setLayerBlendMode(canvas.activeLayerId(), velox::BlendMode::Multiply);
    image.fill(QColor(30, 120, 200, 170));
    check(canvas.importImageLayer(QStringLiteral("GPU screen"), image),
          "prepares a second raster layer for Vulkan compositing");
    canvas.setLayerBlendMode(canvas.activeLayerId(), velox::BlendMode::Screen);

    bool deviceInitialized = false;
    QString deviceName;
    QString renderError;
    QObject::connect(&canvas, &VulkanWindow::deviceInitialized, &app,
                     [&deviceInitialized, &deviceName](const QString& name) {
        deviceInitialized = true;
        deviceName = name;
    });
    QObject::connect(&canvas, &VulkanWindow::renderError, &app,
                     [&renderError](const QString& message) { renderError = message; });
    canvas.show();
    QTimer::singleShot(1800, &app, &QCoreApplication::quit);
    app.exec();
    canvas.close();
    app.processEvents();
    check(deviceInitialized, "initializes the Vulkan device for raster rendering");
    check(renderError.isEmpty(), "uploads and binds raster layers without Vulkan errors");
    if (deviceInitialized && renderError.isEmpty()) {
        std::cout << "Vulkan raster compositing smoke test passed on "
                  << deviceName.toStdString() << ".\n";
    }
    if (!renderError.isEmpty()) {
        std::cerr << "Vulkan layer error: " << renderError.toStdString() << '\n';
    }
}

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

QByteArray releasePayload(const QString& digest,
                          const QString& downloadUrl =
                              QStringLiteral("https://github.com/veloxstudio/VeloxStudio/"
                                             "releases/download/v0.2.0/"
                                             "VeloxStudio-windows-x64.zip"))
{
    QJsonObject asset{
        {QStringLiteral("name"), QStringLiteral("VeloxStudio-windows-x64.zip")},
        {QStringLiteral("browser_download_url"), downloadUrl},
        {QStringLiteral("digest"), digest}
    };
    QJsonObject release{
        {QStringLiteral("tag_name"), QStringLiteral("v0.2.0")},
        {QStringLiteral("html_url"),
         QStringLiteral("https://github.com/veloxstudio/VeloxStudio/releases/tag/v0.2.0")},
        {QStringLiteral("body"), QStringLiteral("## Notes\n- Update")},
        {QStringLiteral("draft"), false},
        {QStringLiteral("assets"), QJsonArray{asset}}
    };
    return QJsonDocument(release).toJson(QJsonDocument::Compact);
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    canvasInteractionTests();
    vulkanImageLayerSmokeTest(app);

    const QString digest(64, QLatin1Char('a'));
    QString error;
    const auto manifest = velox::Update::parseLatestRelease(
        releasePayload(QStringLiteral("sha256:") + digest.toUpper()), &error);
    check(manifest.has_value(), "parses GitHub release and sha256 digest");
    if (manifest) {
        check(manifest->version.toString() == QStringLiteral("0.2.0"),
              "reads semantic version tag");
        check(manifest->sha256 == digest, "normalizes SHA-256 digest");
        check(manifest->releaseNotes.contains(QStringLiteral("Update")),
              "preserves release notes");
    }
    check(!velox::Update::parseLatestRelease(releasePayload(QStringLiteral("bad")), &error),
          "rejects missing or malformed checksum");
    check(!velox::Update::parseLatestRelease(
              releasePayload(digest, QStringLiteral("http://example.invalid/update.zip")),
              &error),
          "rejects non-GitHub update URLs");
    check(!velox::Update::parseLatestRelease(QByteArrayLiteral("{not json"), &error),
          "rejects malformed release JSON");

    const auto release = velox::Version::parse(QStringLiteral("v1.2.3"));
    const auto prerelease = velox::Version::parse(QStringLiteral("1.2.3-rc.1"));
    check(release && prerelease && *prerelease < *release,
          "semantic version release sorts after prerelease");

    velox::updater::Arguments updaterArguments;
    const QStringList validArguments{
        QStringLiteral("VeloxUpdater"),
        QStringLiteral("--wait-pid"), QStringLiteral("42"),
        QStringLiteral("--archive"), QStringLiteral("C:/updates/update.zip"),
        QStringLiteral("--target"), QStringLiteral("C:/Program Files/Velox"),
        QStringLiteral("--restart"), QStringLiteral("C:/Program Files/Velox/VeloxStudio.exe")
    };
    check(velox::updater::parseArguments(validArguments, &updaterArguments, &error),
          "parses updater handoff arguments");
    check(updaterArguments.waitPid == 42, "preserves updater wait PID");
    QStringList invalidArguments = validArguments;
    invalidArguments[2] = QStringLiteral("-5");
    check(!velox::updater::parseArguments(invalidArguments, &updaterArguments, &error),
          "rejects invalid updater process ID");

    QTemporaryDir temporaryDirectory;
    check(temporaryDirectory.isValid(), "creates temporary project directory");
    if (temporaryDirectory.isValid()) {
        velox::Document project(QSize(32, 24), QStringLiteral("Round trip"));
        velox::Layer* layer = project.addLayer(QStringLiteral("Paint"));
        layer->setVisible(false);
        layer->setOpacity(0.65);
        layer->setBlendMode(velox::BlendMode::Screen);
        layer->setOffset(QPoint(2, 3));
        QImage image(4, 4, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        layer->setImage(image);
        velox::BrushStroke stroke;
        stroke.color = QColor(QStringLiteral("#80445566"));
        stroke.diameter = 7.5;
        stroke.points = {QPointF(5.0, 6.0), QPointF(10.0, 12.0)};
        layer->addStroke(stroke);

        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("roundtrip.vlx"));
        const velox::VlxFormat::SaveResult saveResult =
            velox::VlxFormat::save(project, projectPath);
        check(saveResult.ok, "saves native layered canvas");
        QString loadError;
        const std::unique_ptr<velox::Document> loaded =
            velox::VlxFormat::load(projectPath, &loadError);
        check(loaded != nullptr, "loads native project");
        if (loaded) {
            const velox::Layer* loadedLayer = loaded->layerAt(0);
            check(loaded->canvasSize() == QSize(32, 24), "preserves canvas size");
            check(loaded->name() == QStringLiteral("Round trip"), "preserves document name");
            check(!loadedLayer->visible() && loadedLayer->opacity() == 0.65,
                  "preserves layer visibility and opacity");
            check(loadedLayer->blendMode() == velox::BlendMode::Screen,
                  "preserves layer blend mode");
            check(loadedLayer->image().pixelColor(0, 0) == QColor(Qt::red),
                  "preserves layer image data");
            check(loadedLayer->strokes().size() == 1
                      && loadedLayer->strokes().constFirst().points.size() == 2,
                  "preserves vector brush-stroke data");
            check(!loaded->isModified(), "loaded project starts unmodified");
        }

        const QString truncatedPath = temporaryDirectory.filePath(QStringLiteral("broken.vlx"));
        QFile source(projectPath);
        QFile truncated(truncatedPath);
        if (source.open(QIODevice::ReadOnly) && truncated.open(QIODevice::WriteOnly)) {
            truncated.write(source.read(9));
            truncated.close();
            check(!velox::VlxFormat::load(truncatedPath, &loadError),
                  "rejects truncated project files");
        } else {
            check(false, "creates truncated project test file");
        }
    }

    return failures == 0 ? 0 : 1;
}
