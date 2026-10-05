#include "MainWindow.h"

#include "AboutDialog.h"
#include "DiagnosticsDialog.h"
#include "Paths.h"
#include "Settings.h"
#include "UpdateService.h"
#include "VulkanWindow.h"

#include <QAbstractAnimation>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QPair>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QShowEvent>
#include <QSequentialAnimationGroup>
#include <QTextBrowser>
#include <QToolBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QStyle>
#include <QParallelAnimationGroup>
#include <QPauseAnimation>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>
#include <memory>
#include <utility>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace velox::ui {
namespace {

class HoverGlowController final : public QObject
{
public:
    explicit HoverGlowController(QObject* parent)
        : QObject(parent)
    {
    }

    void install(QWidget* widget)
    {
        if (widget == nullptr || m_effects.contains(widget)) {
            return;
        }
        auto* effect = new QGraphicsDropShadowEffect(widget);
        effect->setOffset(0.0, 0.0);
        effect->setBlurRadius(0.0);
        effect->setColor(QColor(111, 70, 255, 0));
        widget->setGraphicsEffect(effect);

        auto* group = new QParallelAnimationGroup(this);
        auto* blur = new QPropertyAnimation(effect, "blurRadius", group);
        blur->setDuration(190);
        blur->setEasingCurve(QEasingCurve::OutCubic);
        auto* color = new QPropertyAnimation(effect, "color", group);
        color->setDuration(190);
        color->setEasingCurve(QEasingCurve::OutCubic);
        group->addAnimation(blur);
        group->addAnimation(color);
        m_effects.insert(widget, effect);
        m_groups.insert(widget, group);
        m_blurAnimations.insert(widget, blur);
        m_colorAnimations.insert(widget, color);
        widget->installEventFilter(this);
        connect(widget, &QObject::destroyed, this, [this, widget] {
            m_effects.remove(widget);
            m_groups.remove(widget);
            m_blurAnimations.remove(widget);
            m_colorAnimations.remove(widget);
        });
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget == nullptr || !m_effects.contains(widget)) {
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave
            || event->type() == QEvent::EnabledChange) {
            const bool glowing = event->type() == QEvent::Enter && widget->isEnabled();
            QGraphicsDropShadowEffect* effect = m_effects.value(widget);
            QParallelAnimationGroup* group = m_groups.value(widget);
            QPropertyAnimation* blur = m_blurAnimations.value(widget);
            QPropertyAnimation* color = m_colorAnimations.value(widget);
            if (effect != nullptr && group != nullptr && blur != nullptr && color != nullptr) {
                group->stop();
                blur->setStartValue(effect->blurRadius());
                blur->setEndValue(glowing ? 16.0 : 0.0);
                color->setStartValue(effect->color());
                color->setEndValue(glowing ? QColor(111, 70, 255, 155)
                                           : QColor(111, 70, 255, 0));
                group->start();
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QHash<QWidget*, QPointer<QGraphicsDropShadowEffect>> m_effects;
    QHash<QWidget*, QPointer<QParallelAnimationGroup>> m_groups;
    QHash<QWidget*, QPointer<QPropertyAnimation>> m_blurAnimations;
    QHash<QWidget*, QPointer<QPropertyAnimation>> m_colorAnimations;
};

class TitleBar final : public QWidget
{
public:
    explicit TitleBar(MainWindow* owner, QMenuBar* menuBar)
        : QWidget(owner)
        , m_owner(owner)
    {
        setFixedHeight(42);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 0, 8, 0);
        layout->setSpacing(10);

        auto* brandMark = new QLabel(QStringLiteral("V"), this);
        brandMark->setObjectName(QStringLiteral("brandMark"));
        brandMark->setAlignment(Qt::AlignCenter);
        brandMark->setFixedSize(26, 30);
        QFont markFont = brandMark->font();
        markFont.setBold(true);
        markFont.setPointSize(20);
        brandMark->setFont(markFont);
        layout->addWidget(brandMark);

        auto* title = new QLabel(tr("VELOX STUDIO"), this);
        title->setObjectName(QStringLiteral("brandName"));
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont titleFont = title->font();
        titleFont.setBold(true);
        titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
        title->setFont(titleFont);
        layout->addWidget(title);
        auto* divider = new QWidget(this);
        divider->setObjectName(QStringLiteral("brandDivider"));
        divider->setFixedSize(1, 20);
        layout->addWidget(divider);
        layout->addWidget(menuBar);
        layout->addStretch();
        auto* version = new QLabel(
            tr("v%1").arg(QApplication::applicationVersion()), this);
        version->setObjectName(QStringLiteral("appVersion"));
        layout->addWidget(version);

        auto makeButton = [this, layout](QStyle::StandardPixmap icon, const QString& label,
                                         const std::function<void()>& action) {
            auto* button = new QToolButton(this);
            button->setIcon(style()->standardIcon(icon));
            button->setToolTip(label);
            button->setAccessibleName(label);
            button->setFixedSize(32, 30);
            connect(button, &QToolButton::clicked, this, action);
            layout->addWidget(button);
        };
        makeButton(QStyle::SP_TitleBarMinButton, tr("Minimize"), [owner] {
            owner->showMinimized();
        });
        makeButton(QStyle::SP_TitleBarMaxButton, tr("Maximize"), [owner] {
            owner->isMaximized() ? owner->showNormal() : owner->showMaximized();
        });
        makeButton(QStyle::SP_TitleBarCloseButton, tr("Close"), [owner] {
            owner->close();
        });
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_owner->windowHandle() != nullptr) {
            m_owner->windowHandle()->startSystemMove();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_owner->isMaximized() ? m_owner->showNormal() : m_owner->showMaximized();
        }
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    MainWindow* m_owner;
};

} // namespace

MainWindow::MainWindow(QVulkanInstance* instance, QWidget* parent)
    : QMainWindow(parent)
    , m_vulkanInstance(instance)
    , m_updateService(new UpdateService(this))
{
    setWindowTitle(tr("Velox Studio"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(900, 600);
    m_recoveryPath = Paths::recoveryDirectory() + QStringLiteral("/autosave.vlx");
    createShell();
    createMenus();
    auto* hoverGlow = new HoverGlowController(this);
    for (QPushButton* button : findChildren<QPushButton*>()) {
        hoverGlow->install(button);
    }
    for (QToolButton* button : findChildren<QToolButton*>()) {
        hoverGlow->install(button);
    }

    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(1200);
    m_autosaveWatcher = new QFutureWatcher<VlxFormat::SaveResult>(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (!m_vulkanWindow->isModified()) {
            return;
        }
        if (m_autosaveWatcher->isRunning()) {
            m_autosavePending = true;
            return;
        }

        const std::shared_ptr<const Document> snapshot(
            m_vulkanWindow->documentSnapshot().release());
        const QString path = m_recoveryPath;
        m_autosaveWatcher->setFuture(QtConcurrent::run([snapshot, path] {
            return VlxFormat::save(*snapshot, path);
        }));
    });
    connect(m_autosaveWatcher, &QFutureWatcher<VlxFormat::SaveResult>::finished,
            this, [this] {
        const VlxFormat::SaveResult result = m_autosaveWatcher->result();
        if (!result.ok) {
            qWarning().noquote() << "Autosave failed:" << result.error;
            statusBar()->showMessage(tr("Autosave failed: %1").arg(result.error), 10000);
        } else {
            statusBar()->showMessage(tr("Recovery copy saved."), 2500);
        }
        if (m_autosavePending) {
            m_autosavePending = false;
            scheduleAutosave();
        }
    });

    const QByteArray geometry = Settings::value(Settings::kWindowGeometry).toByteArray();
    if (!geometry.isEmpty() && !restoreGeometry(geometry)) {
        resize(1280, 800);
    } else if (geometry.isEmpty()) {
        resize(1280, 800);
    }
    const QByteArray state = Settings::value(Settings::kWindowState).toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }

    connect(m_vulkanWindow, &VulkanWindow::documentChanged,
            this, [this] {
        refreshDocumentUi();
        scheduleAutosave();
    });
    connect(m_vulkanWindow, &VulkanWindow::layersChanged,
            this, &MainWindow::refreshDocumentUi);
    connect(m_vulkanWindow, &VulkanWindow::selectionChanged, this, [this](bool active) {
        statusBar()->showMessage(active ? tr("Rectangular selection active.")
                                        : tr("No selection."));
    });
    connect(m_vulkanWindow, &VulkanWindow::deviceInitialized, this, [this](const QString& name) {
        statusBar()->showMessage(tr("Vulkan device: %1").arg(name));
    });
    connect(m_vulkanWindow, &VulkanWindow::renderError, this, [this](const QString& message) {
        statusBar()->showMessage(message, 15000);
    });

    connect(m_updateService, &UpdateService::updateAvailable,
            this, &MainWindow::showUpdate);
    connect(m_updateService, &UpdateService::upToDate, this, [this] {
        statusBar()->showMessage(tr("Velox Studio is up to date."), 5000);
    });
    connect(m_updateService, &UpdateService::checkFailed, this, [this](const QString& error) {
        statusBar()->showMessage(tr("Update check failed: %1").arg(error), 10000);
    });
    connect(m_updateService, &UpdateService::downloadProgress,
            this, &MainWindow::setDownloadProgress);
    connect(m_updateService, &UpdateService::downloadFailed, this, [this](const QString& error) {
        setEditorLocked(false);
        m_updateButton->setEnabled(true);
        m_downloadProgress->setVisible(false);
        QMessageBox::warning(this, tr("Update failed"), error);
    });
    connect(m_updateService, &UpdateService::updaterStarted, this, [this] {
        statusBar()->showMessage(tr("Update ready. Velox Studio is restarting…"));
        QTimer::singleShot(300, this, &QWidget::close);
    });

    if (QFileInfo::exists(m_recoveryPath)) {
        QString error;
        std::unique_ptr<Document> recovered = VlxFormat::load(m_recoveryPath, &error);
        if (recovered) {
            m_vulkanWindow->replaceDocument(std::move(recovered));
            m_projectPath.clear();
            statusBar()->showMessage(tr("Recovered the previous workspace."));
        } else {
            QMessageBox::warning(this, tr("Recovery unavailable"), error);
        }
    }
    refreshDocumentUi();
    statusBar()->showMessage(tr("Starting Vulkan renderer…"));
    QTimer::singleShot(1200, m_updateService, &UpdateService::checkForUpdates);
}

void MainWindow::createShell()
{
    m_menuBar = new QMenuBar(this);
    setMenuWidget(new TitleBar(this, m_menuBar));

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("editorCentral"));
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_updateBanner = new QWidget(central);
    auto* bannerLayout = new QVBoxLayout(m_updateBanner);
    bannerLayout->setContentsMargins(14, 10, 14, 10);
    auto* headingRow = new QHBoxLayout();
    m_updateHeading = new QLabel(m_updateBanner);
    QFont headingFont = m_updateHeading->font();
    headingFont.setBold(true);
    m_updateHeading->setFont(headingFont);
    headingRow->addWidget(m_updateHeading);
    headingRow->addStretch();
    m_updateButton = new QPushButton(m_updateBanner);
    connect(m_updateButton, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::question(
            this, tr("Install update"),
            tr("Download and install version %1? Velox Studio will restart when the update is ready.")
                .arg(m_releaseVersion));
        if (answer == QMessageBox::Yes) {
            if (!persistRecoveryDocument()) {
                return;
            }
            setEditorLocked(true);
            m_updateButton->setEnabled(false);
            m_downloadProgress->setVisible(true);
            m_updateService->downloadAndInstall(m_downloadUrl, m_updateDigest, m_releaseVersion);
        }
    });
    headingRow->addWidget(m_updateButton);
    bannerLayout->addLayout(headingRow);
    m_changelog = new QTextBrowser(m_updateBanner);
    m_changelog->setReadOnly(true);
    m_changelog->setOpenExternalLinks(true);
    m_changelog->setMaximumHeight(82);
    bannerLayout->addWidget(m_changelog);
    m_downloadProgress = new QProgressBar(m_updateBanner);
    m_downloadProgress->setRange(0, 1000);
    m_downloadProgress->setVisible(false);
    bannerLayout->addWidget(m_downloadProgress);
    m_progressAnimation = new QPropertyAnimation(m_downloadProgress, "value", this);
    m_progressAnimation->setDuration(180);
    m_progressAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_updateBanner->setStyleSheet(
        QStringLiteral("background: #101a33; border-bottom: 1px solid #7544ff;"));
    auto* bannerOpacity = new QGraphicsOpacityEffect(m_updateBanner);
    bannerOpacity->setOpacity(0.0);
    m_updateBanner->setGraphicsEffect(bannerOpacity);
    m_updateBannerAnimation = new QPropertyAnimation(bannerOpacity, "opacity", this);
    m_updateBannerAnimation->setDuration(420);
    m_updateBannerAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_updateBanner->setVisible(false);
    layout->addWidget(m_updateBanner);

    m_vulkanWindow = new VulkanWindow();
    m_vulkanWindow->setVulkanInstance(m_vulkanInstance);
    m_canvasContainer = QWidget::createWindowContainer(m_vulkanWindow, central);
    m_canvasContainer->setObjectName(QStringLiteral("canvasContainer"));
    layout->addWidget(m_canvasContainer, 1);
    setCentralWidget(central);

    auto* toolsDock = new QDockWidget(tr("Tools"), this);
    toolsDock->setObjectName(QStringLiteral("toolsDock"));
    toolsDock->setMinimumWidth(156);
    m_toolPanel = new QWidget(toolsDock);
    auto* toolLayout = new QVBoxLayout(m_toolPanel);
    toolLayout->setContentsMargins(8, 8, 8, 8);

    auto* toolGroup = new QButtonGroup(m_toolPanel);
    toolGroup->setExclusive(true);
    m_brushToolButton = new QToolButton(m_toolPanel);
    m_brushToolButton->setText(tr("Brush"));
    m_brushToolButton->setCheckable(true);
    m_brushToolButton->setChecked(true);
    toolGroup->addButton(m_brushToolButton);
    toolLayout->addWidget(m_brushToolButton);
    connect(m_brushToolButton, &QToolButton::clicked, this, [this] {
        m_vulkanWindow->setTool(CanvasTool::Brush);
        if (m_toolbarTool != nullptr) {
            m_toolbarTool->setCurrentIndex(0);
        }
    });

    m_selectionToolButton = new QToolButton(m_toolPanel);
    m_selectionToolButton->setText(tr("Rectangle Select"));
    m_selectionToolButton->setCheckable(true);
    toolGroup->addButton(m_selectionToolButton);
    toolLayout->addWidget(m_selectionToolButton);
    connect(m_selectionToolButton, &QToolButton::clicked, this, [this] {
        m_vulkanWindow->setTool(CanvasTool::RectangleSelection);
        if (m_toolbarTool != nullptr) {
            m_toolbarTool->setCurrentIndex(1);
        }
    });

    toolLayout->addWidget(new QLabel(tr("Brush color"), m_toolPanel));
    m_brushColor = new QComboBox(m_toolPanel);
    const QList<QPair<QString, QColor>> colors{
        {tr("Charcoal"), QColor(QStringLiteral("#25252a"))},
        {tr("Black"), QColor(Qt::black)},
        {tr("White"), QColor(Qt::white)},
        {tr("Red"), QColor(QStringLiteral("#e34b4b"))},
        {tr("Blue"), QColor(QStringLiteral("#477fe5"))},
        {tr("Green"), QColor(QStringLiteral("#35a875"))},
        {tr("Yellow"), QColor(QStringLiteral("#f0c542"))}
    };
    for (const auto& entry : colors) {
        m_brushColor->addItem(entry.first, entry.second);
    }
    toolLayout->addWidget(m_brushColor);
    connect(m_brushColor, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        m_vulkanWindow->setBrushColor(m_brushColor->itemData(index).value<QColor>());
    });

    auto* brushSizeRow = new QHBoxLayout();
    brushSizeRow->addWidget(new QLabel(tr("Size"), m_toolPanel));
    m_brushSizeLabel = new QLabel(QStringLiteral("18 px"), m_toolPanel);
    brushSizeRow->addWidget(m_brushSizeLabel);
    toolLayout->addLayout(brushSizeRow);
    m_brushSize = new QSlider(Qt::Horizontal, m_toolPanel);
    m_brushSize->setRange(1, 96);
    m_brushSize->setValue(18);
    toolLayout->addWidget(m_brushSize);
    connect(m_brushSize, &QSlider::valueChanged, this, [this](int value) {
        m_brushSizeLabel->setText(tr("%1 px").arg(value));
        m_vulkanWindow->setBrushDiameter(value);
    });
    toolLayout->addStretch();
    toolsDock->setWidget(m_toolPanel);
    addDockWidget(Qt::LeftDockWidgetArea, toolsDock);

    auto* layersDock = new QDockWidget(tr("Layers"), this);
    layersDock->setObjectName(QStringLiteral("layersDock"));
    layersDock->setMinimumWidth(220);
    m_layerPanel = new QWidget(layersDock);
    auto* layersLayout = new QVBoxLayout(m_layerPanel);
    layersLayout->setContentsMargins(6, 6, 6, 6);
    m_layerList = new QListWidget(m_layerPanel);
    layersLayout->addWidget(m_layerList, 1);
    layersLayout->addWidget(new QLabel(tr("Blend mode"), m_layerPanel));
    m_layerBlendMode = new QComboBox(m_layerPanel);
    m_layerBlendMode->addItem(tr("Normal"), static_cast<int>(velox::BlendMode::Normal));
    m_layerBlendMode->addItem(tr("Multiply"), static_cast<int>(velox::BlendMode::Multiply));
    m_layerBlendMode->addItem(tr("Screen"), static_cast<int>(velox::BlendMode::Screen));
    layersLayout->addWidget(m_layerBlendMode);
    connect(m_layerBlendMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (!m_refreshingDocumentUi && index >= 0 && m_layerList->currentItem() != nullptr) {
            m_vulkanWindow->setLayerBlendMode(
                m_layerList->currentItem()->data(Qt::UserRole).toInt(),
                static_cast<velox::BlendMode>(m_layerBlendMode->itemData(index).toInt()));
        }
    });
    auto* addLayerButton = new QPushButton(tr("Add Layer"), m_layerPanel);
    layersLayout->addWidget(addLayerButton);
    connect(addLayerButton, &QPushButton::clicked, m_vulkanWindow, &VulkanWindow::addLayer);
    connect(m_layerList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!m_refreshingDocumentUi) {
            m_vulkanWindow->setLayerVisible(
                item->data(Qt::UserRole).toInt(), item->checkState() == Qt::Checked);
        }
    });
    connect(m_layerList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current) {
        if (current != nullptr && !m_refreshingDocumentUi) {
            m_vulkanWindow->setActiveLayer(current->data(Qt::UserRole).toInt());
            refreshDocumentUi();
        }
    });
    layersDock->setWidget(m_layerPanel);
    addDockWidget(Qt::RightDockWidgetArea, layersDock);

    auto* historyDock = new QDockWidget(tr("History"), this);
    historyDock->setObjectName(QStringLiteral("historyDock"));
    m_historyList = new QListWidget(historyDock);
    historyDock->setWidget(m_historyList);
    addDockWidget(Qt::BottomDockWidgetArea, historyDock);
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = m_menuBar->addMenu(tr("&File"));
    QAction* newAction = fileMenu->addAction(tr("&New Canvas…"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, [this] {
        if (!maybeSaveChanges()) {
            return;
        }
        bool widthOk = false;
        const int width = QInputDialog::getInt(this, tr("New Canvas"), tr("Width (pixels):"),
                                               1024, 1, 8192, 1, &widthOk);
        if (!widthOk) {
            return;
        }
        bool heightOk = false;
        const int height = QInputDialog::getInt(this, tr("New Canvas"), tr("Height (pixels):"),
                                                768, 1, 8192, 1, &heightOk);
        if (heightOk) {
            finishAutosave();
            if (QFileInfo::exists(m_recoveryPath) && !QFile::remove(m_recoveryPath)) {
                QMessageBox::warning(this, tr("Could not remove recovery file"),
                                     tr("The previous recovery copy could not be removed."));
                return;
            }
            m_projectPath.clear();
            m_vulkanWindow->newCanvas(QSize(width, height));
        }
    });
    QAction* importAction = fileMenu->addAction(tr("Import Image as &Layer…"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    connect(importAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import Image as Layer"), QString(),
            tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)"));
        if (path.isEmpty()) {
            return;
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize dimensions = reader.size();
        const qint64 pixelCount = static_cast<qint64>(dimensions.width()) * dimensions.height();
        if (dimensions.isEmpty() || dimensions.width() > 8192 || dimensions.height() > 8192
            || pixelCount <= 0 || pixelCount > 64LL * 1024LL * 1024LL) {
            QMessageBox::warning(this, tr("Could not import image"),
                                 tr("Imported images must be no larger than 8192 × 8192 pixels."));
            return;
        }
        const QImage image = reader.read();
        if (image.isNull()) {
            QMessageBox::warning(this, tr("Could not import image"), reader.errorString());
            return;
        }
        if (!m_vulkanWindow->importImageLayer(QFileInfo(path).completeBaseName(), image)) {
            QMessageBox::warning(this, tr("Could not import image"),
                                 tr("The image could not be added to the current project."));
        }
    });
    QAction* openAction = fileMenu->addAction(tr("&Open Project…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] {
        if (!maybeSaveChanges()) {
            return;
        }
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open Velox Project"), QString(),
            tr("Velox Studio project (*.vlx)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        std::unique_ptr<Document> document = VlxFormat::load(path, &error);
        if (!document) {
            QMessageBox::warning(this, tr("Could not open project"), error);
            return;
        }
        finishAutosave();
        if (QFileInfo::exists(m_recoveryPath) && !QFile::remove(m_recoveryPath)) {
            QMessageBox::warning(this, tr("Could not remove recovery file"),
                                 tr("The previous recovery copy could not be removed."));
            return;
        }
        m_projectPath = path;
        m_vulkanWindow->replaceDocument(std::move(document));
    });
    m_saveAction = fileMenu->addAction(tr("&Save Project"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, [this] { saveDocument(); });
    QAction* saveAsAction = fileMenu->addAction(tr("Save Project &As…"));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this] { saveDocument(true); });
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* editMenu = m_menuBar->addMenu(tr("&Edit"));
    m_undoAction = editMenu->addAction(tr("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, m_vulkanWindow, &VulkanWindow::undo);
    m_redoAction = editMenu->addAction(tr("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, m_vulkanWindow, &VulkanWindow::redo);
    editMenu->addSeparator();
    QAction* deselectAction = editMenu->addAction(tr("&Deselect"));
    deselectAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(deselectAction, &QAction::triggered, m_vulkanWindow, &VulkanWindow::clearSelection);

    QMenu* viewMenu = m_menuBar->addMenu(tr("&View"));
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        viewMenu->addAction(dock->toggleViewAction());
    }

    QMenu* helpMenu = m_menuBar->addMenu(tr("&Help"));
    QAction* checkAction = helpMenu->addAction(tr("Check for &Updates"));
    connect(checkAction, &QAction::triggered,
            m_updateService, &UpdateService::checkForUpdates);
    QAction* diagnosticsAction = helpMenu->addAction(tr("&Diagnostics"));
    connect(diagnosticsAction, &QAction::triggered, this, [this] {
        DiagnosticsDialog(m_vulkanInstance, this).exec();
    });
    helpMenu->addSeparator();
    QAction* aboutAction = helpMenu->addAction(tr("&About Velox Studio"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        AboutDialog(this).exec();
    });

    auto* toolbar = addToolBar(tr("Canvas"));
    toolbar->setObjectName(QStringLiteral("canvasToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    newAction->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    openAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    importAction->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    m_saveAction->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_undoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    m_redoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    toolbar->addAction(newAction);
    toolbar->addAction(openAction);
    toolbar->addAction(m_saveAction);
    toolbar->addSeparator();
    toolbar->addAction(m_undoAction);
    toolbar->addAction(m_redoAction);
    toolbar->addSeparator();
    toolbar->addAction(importAction);
    toolbar->addSeparator();

    m_toolbarTool = new QComboBox(toolbar);
    m_toolbarTool->setObjectName(QStringLiteral("toolbarToolSelector"));
    m_toolbarTool->addItem(tr("Brush"), static_cast<int>(CanvasTool::Brush));
    m_toolbarTool->addItem(tr("Rectangle Select"),
                           static_cast<int>(CanvasTool::RectangleSelection));
    toolbar->addWidget(m_toolbarTool);
    connect(m_toolbarTool, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index < 0) {
            return;
        }
        const auto tool = static_cast<CanvasTool>(m_toolbarTool->itemData(index).toInt());
        m_vulkanWindow->setTool(tool);
        m_brushToolButton->setChecked(tool == CanvasTool::Brush);
        m_selectionToolButton->setChecked(tool == CanvasTool::RectangleSelection);
    });
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Size"), toolbar));
    auto* toolbarBrushSize = new QSlider(Qt::Horizontal, toolbar);
    toolbarBrushSize->setObjectName(QStringLiteral("toolbarBrushSize"));
    toolbarBrushSize->setRange(1, 96);
    toolbarBrushSize->setValue(m_brushSize->value());
    toolbar->addWidget(toolbarBrushSize);
    auto* toolbarBrushSizeLabel = new QLabel(
        tr("%1 px").arg(m_brushSize->value()), toolbar);
    toolbarBrushSizeLabel->setMinimumWidth(42);
    toolbar->addWidget(toolbarBrushSizeLabel);
    connect(toolbarBrushSize, &QSlider::valueChanged, this,
            [this, toolbarBrushSizeLabel](int value) {
        m_brushSize->setValue(value);
        toolbarBrushSizeLabel->setText(tr("%1 px").arg(value));
    });
    connect(m_brushSize, &QSlider::valueChanged, toolbarBrushSize,
            &QSlider::setValue);

    auto* gpuStatus = new QLabel(tr("●  GPU: Vulkan starting…"), this);
    gpuStatus->setObjectName(QStringLiteral("gpuStatus"));
    statusBar()->addPermanentWidget(gpuStatus);
    connect(m_vulkanWindow, &VulkanWindow::deviceInitialized, this,
            [gpuStatus](const QString& name) {
        gpuStatus->setText(QObject::tr("●  GPU: Vulkan · %1").arg(name));
        gpuStatus->setToolTip(name);
    });
}

void MainWindow::refreshDocumentUi()
{
    if (m_vulkanWindow == nullptr) {
        return;
    }
    m_refreshingDocumentUi = true;
    for (int index = m_layerBlendMode->count() - 1; index >= 3; --index) {
        m_layerBlendMode->removeItem(index);
    }
    m_layerList->clear();
    const QVector<CanvasLayerInfo> layers = m_vulkanWindow->layers();
    for (const CanvasLayerInfo& layer : layers) {
        auto* item = new QListWidgetItem(layer.name, m_layerList);
        item->setData(Qt::UserRole, layer.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
        item->setText(layer.locked ? tr("%1 (locked)").arg(layer.name) : layer.name);
        if (layer.id == m_vulkanWindow->activeLayerId()) {
            m_layerList->setCurrentItem(item);
            int blendIndex = m_layerBlendMode->findData(static_cast<int>(layer.blendMode));
            if (blendIndex < 0) {
                auto* model = qobject_cast<QStandardItemModel*>(m_layerBlendMode->model());
                if (model != nullptr) {
                    m_layerBlendMode->addItem(
                        tr("%1 (preview unavailable)")
                            .arg(velox::blendModeDisplayName(layer.blendMode)),
                        static_cast<int>(layer.blendMode));
                    blendIndex = m_layerBlendMode->count() - 1;
                    model->item(blendIndex)->setEnabled(false);
                }
            }
            m_layerBlendMode->setCurrentIndex(blendIndex);
        }
    }
    m_historyList->clear();
    m_historyList->addItems(m_vulkanWindow->historyEntries());
    m_undoAction->setEnabled(m_vulkanWindow->undoCount() > 0);
    m_redoAction->setEnabled(m_vulkanWindow->redoCount() > 0);
    const QString title = m_projectPath.isEmpty()
        ? tr("Untitled") : QFileInfo(m_projectPath).fileName();
    setWindowTitle(tr("%1%2 — Velox Studio")
                       .arg(title, m_vulkanWindow->isModified() ? QStringLiteral(" *") : QString()));
    m_refreshingDocumentUi = false;
}

bool MainWindow::maybeSaveChanges()
{
    if (!m_vulkanWindow->isModified()) {
        return true;
    }
    QMessageBox prompt(this);
    prompt.setWindowTitle(tr("Unsaved changes"));
    prompt.setText(tr("Save changes to the current canvas?"));
    prompt.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    prompt.setDefaultButton(QMessageBox::Save);
    const QMessageBox::StandardButton answer =
        static_cast<QMessageBox::StandardButton>(prompt.exec());
    if (answer == QMessageBox::Save) {
        return saveDocument();
    }
    if (answer == QMessageBox::Discard) {
        if (QFileInfo::exists(m_recoveryPath) && !QFile::remove(m_recoveryPath)) {
            QMessageBox::warning(this, tr("Could not remove recovery file"),
                                 tr("The recovery copy could not be removed."));
            return false;
        }
        return true;
    }
    return false;
}

bool MainWindow::saveDocument(bool saveAs)
{
    QString path = m_projectPath;
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Velox Project"), path,
            tr("Velox Studio project (*.vlx)"));
        if (path.isEmpty()) {
            return false;
        }
        if (QFileInfo(path).suffix().isEmpty()) {
            path += QStringLiteral(".vlx");
        }
    }

    finishAutosave();
    const std::shared_ptr<const Document> snapshot(
        m_vulkanWindow->documentSnapshot().release());
    const VlxFormat::SaveResult result = VlxFormat::save(*snapshot, path);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Could not save project"), result.error);
        return false;
    }
    m_projectPath = path;
    m_vulkanWindow->markSaved();
    m_autosaveTimer->stop();
    if (QFileInfo::exists(m_recoveryPath) && !QFile::remove(m_recoveryPath)) {
        qWarning("The saved project recovery file could not be removed.");
    }
    refreshDocumentUi();
    return true;
}

bool MainWindow::persistRecoveryDocument()
{
    finishAutosave();
    const std::shared_ptr<const Document> snapshot(
        m_vulkanWindow->documentSnapshot().release());
    const VlxFormat::SaveResult result = VlxFormat::save(*snapshot, m_recoveryPath);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Could not save recovery copy"), result.error);
        return false;
    }
    m_vulkanWindow->markSaved();
    return true;
}

void MainWindow::finishAutosave()
{
    m_autosaveTimer->stop();
    m_autosavePending = false;
    if (m_autosaveWatcher->isRunning()) {
        m_autosaveWatcher->waitForFinished();
    }
}

void MainWindow::scheduleAutosave()
{
    if (!m_vulkanWindow->isModified()) {
        return;
    }
    if (m_autosaveWatcher->isRunning()) {
        m_autosavePending = true;
        return;
    }
    m_autosaveTimer->start();
}

void MainWindow::setEditorLocked(bool locked)
{
    m_editorLocked = locked;
    m_canvasContainer->setEnabled(!locked);
    m_toolPanel->setEnabled(!locked);
    m_layerPanel->setEnabled(!locked);
    m_saveAction->setEnabled(!locked);
    m_undoAction->setEnabled(!locked && m_vulkanWindow->undoCount() > 0);
    m_redoAction->setEnabled(!locked && m_vulkanWindow->redoCount() > 0);
}

void MainWindow::showUpdate(QString version, QString notes, QUrl downloadUrl,
                            QString digest, QUrl releaseUrl)
{
    Q_UNUSED(releaseUrl);
    m_releaseVersion = std::move(version);
    m_downloadUrl = std::move(downloadUrl);
    m_updateDigest = std::move(digest);
    m_updateHeading->setText(tr("New Version Available — %1").arg(m_releaseVersion));
    m_updateButton->setText(tr("Update to Version %1").arg(m_releaseVersion));
    m_changelog->setMarkdown(notes);
    animateUpdateBannerIn();
    statusBar()->showMessage(tr("Version %1 is available.").arg(m_releaseVersion), 10000);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_introAnimationPlayed) {
        m_introAnimationPlayed = true;
        animateWorkspaceIn();
    }
}

void MainWindow::animateWorkspaceIn()
{
    m_introAnimation = new QSequentialAnimationGroup(this);
    const QList<QWidget*> surfaces{
        menuWidget(),
        findChild<QToolBar*>(QStringLiteral("canvasToolbar")),
        m_toolPanel,
        m_layerPanel,
        m_historyList
    };
    for (QWidget* surface : surfaces) {
        if (surface == nullptr || !surface->isVisible()) {
            continue;
        }
        auto* opacity = new QGraphicsOpacityEffect(surface);
        opacity->setOpacity(0.0);
        surface->setGraphicsEffect(opacity);
        auto* fade = new QPropertyAnimation(opacity, "opacity", m_introAnimation);
        fade->setDuration(260);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        m_introAnimation->addPause(55);
        m_introAnimation->addAnimation(fade);
    }
    m_introAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::animateUpdateBannerIn()
{
    if (m_updateBanner->isVisible()) {
        return;
    }
    m_updateBanner->setVisible(true);
    m_updateBannerAnimation->stop();
    m_updateBannerAnimation->setStartValue(0.0);
    m_updateBannerAnimation->setEndValue(1.0);
    m_updateBannerAnimation->start();
}

void MainWindow::setDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        m_downloadProgress->setRange(0, 0);
        return;
    }

    m_downloadProgress->setRange(0, 1000);
    const int target = static_cast<int>(
        (static_cast<double>(received) / static_cast<double>(total)) * 1000.0);
    m_progressAnimation->stop();
    m_progressAnimation->setStartValue(m_downloadProgress->value());
    m_progressAnimation->setEndValue(qBound(0, target, 1000));
    m_progressAnimation->start();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    finishAutosave();
    if (!maybeSaveChanges()) {
        scheduleAutosave();
        event->ignore();
        return;
    }
    if (!m_editorLocked && QFileInfo::exists(m_recoveryPath)
        && !QFile::remove(m_recoveryPath)) {
        qWarning("The stale recovery copy could not be removed.");
    }
    Settings::setValue(Settings::kWindowGeometry, saveGeometry());
    Settings::setValue(Settings::kWindowState, saveState());
    Settings::sync();
    QMainWindow::closeEvent(event);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    auto* nativeMessage = static_cast<MSG*>(message);
    if (nativeMessage != nullptr && nativeMessage->message == WM_NCHITTEST && !isMaximized()) {
        RECT windowRect{};
        if (GetWindowRect(reinterpret_cast<HWND>(winId()), &windowRect)) {
            const POINT cursor{
                GET_X_LPARAM(nativeMessage->lParam),
                GET_Y_LPARAM(nativeMessage->lParam)
            };
            constexpr int border = 8;
            const bool left = cursor.x < windowRect.left + border;
            const bool right = cursor.x >= windowRect.right - border;
            const bool top = cursor.y < windowRect.top + border;
            const bool bottom = cursor.y >= windowRect.bottom - border;

            if (top && left) {
                *result = HTTOPLEFT;
            } else if (top && right) {
                *result = HTTOPRIGHT;
            } else if (bottom && left) {
                *result = HTBOTTOMLEFT;
            } else if (bottom && right) {
                *result = HTBOTTOMRIGHT;
            } else if (left) {
                *result = HTLEFT;
            } else if (right) {
                *result = HTRIGHT;
            } else if (top) {
                *result = HTTOP;
            } else if (bottom) {
                *result = HTBOTTOM;
            } else {
                return QMainWindow::nativeEvent(eventType, message, result);
            }
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

} // namespace velox::ui
