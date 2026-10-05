#include "Theme.h"

#include <QApplication>
#include <QPalette>

namespace velox::ui::Theme {

void apply(QApplication& application)
{
    const QColor background(QStringLiteral("#080d1b"));
    const QColor surface(QStringLiteral("#0d1428"));
    const QColor raised(QStringLiteral("#111c35"));
    const QColor accent(QStringLiteral("#7544ff"));
    const QColor foreground(QStringLiteral("#e4eaff"));
    QPalette palette;
    palette.setColor(QPalette::Window, background);
    palette.setColor(QPalette::WindowText, foreground);
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#091020")));
    palette.setColor(QPalette::AlternateBase, surface);
    palette.setColor(QPalette::Text, foreground);
    palette.setColor(QPalette::Button, raised);
    palette.setColor(QPalette::ButtonText, foreground);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Mid, QColor(QStringLiteral("#263450")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#7786a7")));
    application.setPalette(palette);
    application.setStyleSheet(QStringLiteral(
        "QMainWindow { background: #080d1b; }"
        "QWidget { color: #e4eaff; font-family: 'Segoe UI'; font-size: 9pt; }"
        "QWidget#editorCentral { background: #080d1b; }"
        "QWidget#canvasContainer { background: #080d1b; border: 1px solid #172440; }"
        "QLabel#brandMark { color: #8c68ff; background: #171c40; border: 1px solid #5741bb;"
        " border-radius: 7px; }"
        "QLabel#brandName { color: #edf1ff; font-size: 10pt; font-weight: 700;"
        " letter-spacing: 1px; }"
        "QLabel#appVersion { color: #9aa9cc; padding: 0 10px; }"
        "QWidget#brandDivider { background: #273653; }"
        "QLabel#gpuStatus { color: #71e5ae; padding: 0 12px; font-weight: 600; }"
        "QMenuBar { background: #0b1223; color: #d9e1f8; border-bottom: 1px solid #1a2743;"
        " padding: 2px 8px; }"
        "QMenuBar::item { padding: 6px 9px; background: transparent; border-radius: 4px; }"
        "QMenuBar::item:selected, QMenu::item:selected { background: #263457; color: white; }"
        "QMenu { background: #111a30; color: #e4eaff; border: 1px solid #334267;"
        " padding: 5px; }"
        "QMenu::item { padding: 6px 26px 6px 20px; border-radius: 3px; }"
        "QMenu::separator { height: 1px; background: #263450; margin: 5px 8px; }"
        "QToolBar { background: #0b1223; border: 0; border-bottom: 1px solid #1d2a49;"
        " spacing: 5px; padding: 5px 8px; }"
        "QToolBar::separator { background: #263450; width: 1px; margin: 3px 5px; }"
        "QToolBar QToolButton { min-width: 28px; min-height: 27px; padding: 3px 7px;"
        " border: 1px solid transparent; border-radius: 5px; color: #cbd7f4; }"
        "QToolBar QToolButton:hover { background: #192747; border-color: #32466f; }"
        "QToolBar QToolButton:checked { background: #25234c; border-color: #7650ff;"
        " color: #ffffff; }"
        "QComboBox#toolbarToolSelector { min-width: 112px; }"
        "QSlider#toolbarBrushSize { min-width: 90px; }"
        "QDockWidget { background: #0b1223; border: 1px solid #1d2a49; }"
        "QDockWidget::title { background: #101a30; color: #cbd7f4; padding: 8px 10px;"
        " border-bottom: 1px solid #1d2a49; font-weight: 600; }"
        "QDockWidget::close-button, QDockWidget::float-button { border: 0; }"
        "QStatusBar { background: #0a1121; color: #aab9db; border-top: 1px solid #1c2946; }"
        "QStatusBar::item { border: 0; }"
        "QListWidget, QTreeWidget, QTableWidget { background: #0a1224;"
        " alternate-background-color: #0d172c; border: 1px solid #1b2a49;"
        " border-radius: 5px; outline: 0; padding: 3px; }"
        "QListWidget::item, QTreeWidget::item { min-height: 28px; padding: 3px 5px;"
        " border-radius: 4px; }"
        "QListWidget::item:selected, QTreeWidget::item:selected { background: #292550;"
        " border: 1px solid #6547d9; color: white; }"
        "QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { background: #0b1428;"
        " border: 1px solid #263758; border-radius: 5px; padding: 5px 8px;"
        " selection-background-color: #7544ff; }"
        "QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover {"
        " border-color: #536ca0; }"
        "QComboBox QAbstractItemView { background: #111a30; border: 1px solid #334267;"
        " selection-background-color: #44327d; }"
        "QSlider::groove:horizontal { height: 4px; background: #273653; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #7544ff; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
        " background: #d9caff; border: 2px solid #7544ff; }"
        "QToolButton { border: 1px solid transparent; border-radius: 5px; padding: 6px; }"
        "QToolButton:hover { background: #192747; border-color: #31466e; }"
        "QToolButton:checked { background: #292550; border-color: #7544ff; }"
        "QPushButton { background: #633cff; border: 1px solid #8c6aff;"
        " border-radius: 5px; padding: 7px 12px; color: white; font-weight: 600; }"
        "QPushButton:hover { background: #7958ff; border-color: #a187ff; }"
        "QPushButton:pressed { background: #4d2ccc; }"
        "QPushButton:disabled { background: #222b40; border-color: #303c56; color: #7887a6; }"
        "QProgressBar { border: 1px solid #283858; border-radius: 4px;"
        " text-align: center; background: #091020; color: #e4eaff; }"
        "QProgressBar::chunk { background: #7544ff; border-radius: 3px; }"
        "QScrollBar:vertical { width: 10px; background: #0b1223; margin: 2px; }"
        "QScrollBar::handle:vertical { min-height: 24px; background: #34456b;"
        " border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #526a9b; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QToolTip { background: #17233e; color: #eef2ff; border: 1px solid #586fa4;"
        " padding: 5px; }"
        "QMessageBox, QDialog { background: #0c1427; }"));
}

} // namespace velox::ui::Theme
