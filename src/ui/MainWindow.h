#pragma once

#include "VlxFormat.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QUrl>

class QAction;
class QComboBox;
class QCloseEvent;
class QLabel;
class QListWidget;
class QMenuBar;
class QProgressBar;
class QPushButton;
class QPropertyAnimation;
class QSequentialAnimationGroup;
class QSlider;
class QToolBar;
class QTextBrowser;
class QVulkanInstance;
class QTimer;
class QToolButton;
class VulkanWindow;

namespace velox {
class UpdateService;
}

namespace velox::ui {

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QVulkanInstance* instance, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void createShell();
    void createMenus();
    void refreshDocumentUi();
    bool maybeSaveChanges();
    bool saveDocument(bool saveAs = false);
    bool persistRecoveryDocument();
    void finishAutosave();
    void animateWorkspaceIn();
    void animateUpdateBannerIn();
    void scheduleAutosave();
    void setEditorLocked(bool locked);
    void showUpdate(QString version, QString notes, QUrl downloadUrl,
                    QString digest, QUrl releaseUrl);
    void setDownloadProgress(qint64 received, qint64 total);

    QVulkanInstance* m_vulkanInstance;
    QMenuBar* m_menuBar = nullptr;
    VulkanWindow* m_vulkanWindow = nullptr;
    QWidget* m_canvasContainer = nullptr;
    velox::UpdateService* m_updateService = nullptr;
    QWidget* m_updateBanner = nullptr;
    QWidget* m_toolPanel = nullptr;
    QWidget* m_layerPanel = nullptr;
    QLabel* m_updateHeading = nullptr;
    QLabel* m_brushSizeLabel = nullptr;
    QTextBrowser* m_changelog = nullptr;
    QListWidget* m_layerList = nullptr;
    QListWidget* m_historyList = nullptr;
    QComboBox* m_brushColor = nullptr;
    QComboBox* m_layerBlendMode = nullptr;
    QComboBox* m_toolbarTool = nullptr;
    QSlider* m_brushSize = nullptr;
    QToolButton* m_brushToolButton = nullptr;
    QToolButton* m_selectionToolButton = nullptr;
    QProgressBar* m_downloadProgress = nullptr;
    QPushButton* m_updateButton = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QPropertyAnimation* m_progressAnimation = nullptr;
    QPropertyAnimation* m_updateBannerAnimation = nullptr;
    QSequentialAnimationGroup* m_introAnimation = nullptr;
    QTimer* m_autosaveTimer = nullptr;
    QFutureWatcher<velox::VlxFormat::SaveResult>* m_autosaveWatcher = nullptr;
    QUrl m_downloadUrl;
    QString m_updateDigest;
    QString m_releaseVersion;
    QString m_projectPath;
    QString m_recoveryPath;
    bool m_editorLocked = false;
    bool m_refreshingDocumentUi = false;
    bool m_autosavePending = false;
    bool m_introAnimationPlayed = false;
};

} // namespace velox::ui
