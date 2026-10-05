# velox_ui - the Qt Widgets application shell: main window, theme, dialogs.
add_library(velox_ui STATIC
    AboutDialog.cpp
    DiagnosticsDialog.cpp
    MainWindow.cpp
    Theme.cpp
)
add_library(Velox::Ui ALIAS velox_ui)

target_include_directories(velox_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(velox_ui PUBLIC Velox::Core Velox::Document Velox::Io)
target_link_libraries(velox_ui PUBLIC Velox::Render Velox::Update)
velox_link_qt(velox_ui Core Gui Widgets Concurrent)
velox_enable_qt_automation(velox_ui)