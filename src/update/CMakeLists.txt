# velox_update - GitHub release manifest parsing and update verification.
add_library(velox_update STATIC
    UpdateManifest.cpp
    UpdateService.cpp
)
add_library(Velox::Update ALIAS velox_update)
target_include_directories(velox_update PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(velox_update PUBLIC Velox::Core)
velox_link_qt(velox_update Core Network)
velox_enable_qt_automation(velox_update)
target_compile_definitions(velox_update PRIVATE
    "VELOX_GITHUB_REPOSITORY=\"${VELOX_GITHUB_REPOSITORY}\""
)