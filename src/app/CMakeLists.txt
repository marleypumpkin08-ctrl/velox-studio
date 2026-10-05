# VeloxStudio - the native Qt/Vulkan application executable.
add_executable(VeloxStudio WIN32
    main.cpp
)
target_link_libraries(VeloxStudio PRIVATE Velox::Ui)
velox_link_qt(VeloxStudio Core Gui Widgets)
velox_enable_qt_automation(VeloxStudio)
velox_qt_deploy_runtime(VeloxStudio)