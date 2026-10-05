# velox_core - foundation services: paths, logging, settings, versioning,
# hashing and build information. Depends on Qt Core only.
add_library(velox_core STATIC
    BuildInfo.cpp
    Logging.cpp
    Paths.cpp
    Settings.cpp
    Sha256.cpp
    Version.cpp
)

add_library(Velox::Core ALIAS velox_core)

target_include_directories(velox_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
velox_link_qt(velox_core Core)
velox_generate_version_header(velox_core)
velox_enable_qt_automation(velox_core)