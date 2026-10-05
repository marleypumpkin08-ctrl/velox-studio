# velox_plugins - the versioned plugin API and the plugin host.
add_library(velox_plugins STATIC
    PluginManager.cpp
)
add_library(Velox::Plugins ALIAS velox_plugins)
target_include_directories(velox_plugins PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(velox_plugins PUBLIC Velox::Core)
velox_link_qt(velox_plugins Core)
velox_enable_qt_automation(velox_plugins)