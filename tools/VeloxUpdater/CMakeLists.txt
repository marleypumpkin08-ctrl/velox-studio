# VeloxUpdater - the standalone updater. It is a separate executable so the
# running VeloxStudio.exe is never overwritten while it is executing.
add_executable(VeloxUpdater
    UpdaterMain.cpp
    UpdaterArguments.cpp
)
velox_enable_qt_automation(VeloxUpdater)
target_link_libraries(VeloxUpdater PRIVATE Velox::ProjectOptions Qt6::Core)
velox_qt_deploy_runtime(VeloxUpdater)