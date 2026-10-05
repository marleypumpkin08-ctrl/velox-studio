# ---------------------------------------------------------------------------
# VeloxQt.cmake
#
# Locates Qt 6 and provides helpers for building Qt-aware targets and for
# deploying the Qt runtime next to the produced executables.
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

# QtSvg renders the Velox Studio logo (assets/icons/velox-logo.svg); QtNetwork
# backs the GitHub release checker; QtConcurrent complements velox::core's own
# worker pool for Qt-native async paths.
set(VELOX_QT_COMPONENTS Core Gui Widgets Network Svg Concurrent)

find_package(Qt6 6.8 REQUIRED COMPONENTS ${VELOX_QT_COMPONENTS})

message(STATUS "Velox Studio: using Qt ${Qt6_VERSION} from ${Qt6_DIR}")

# ---------------------------------------------------------------------------
# velox_enable_qt_automation(<target>)
#
# Turns on moc/uic/rcc for a target and links the Qt libraries it needs.
# ---------------------------------------------------------------------------
function(velox_enable_qt_automation target)
    set_target_properties(${target} PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )
endfunction()

# ---------------------------------------------------------------------------
# velox_link_qt(<target> <component>...)
#
# Links Qt components and the project-wide compile options. Third-party code
# must never be routed through this helper.
# ---------------------------------------------------------------------------
function(velox_link_qt target)
    if (ARGC LESS 2)
        message(FATAL_ERROR "velox_link_qt(${target}) needs at least one Qt component")
    endif()
    foreach(_component IN LISTS ARGN)
        if (NOT TARGET Qt6::${_component})
            message(FATAL_ERROR
                "Qt component '${_component}' is not available. "
                "Add it to VELOX_QT_COMPONENTS or install the addon with aqt.")
        endif()
        target_link_libraries(${target} PUBLIC Qt6::${_component})
    endforeach()
    target_link_libraries(${target} PRIVATE Velox::ProjectOptions)
endfunction()

# ---------------------------------------------------------------------------
# velox_qt_deploy_runtime(<target>)
#
# Copies the Qt runtime (DLLs, plugins, translations) next to the executable so
# the build-tree binary can be launched directly. In an installed tree the same
# files are provided by the installer.
# ---------------------------------------------------------------------------
option(VELOX_QT_DEPLOY_RUNTIME "Run windeployqt after linking GUI executables" ON)

function(velox_qt_deploy_runtime target)
    if (NOT VELOX_QT_DEPLOY_RUNTIME)
        return()
    endif()
    if (NOT WIN32)
        return()
    endif()

    get_target_property(_exe_path ${target} RUNTIME_OUTPUT_DIRECTORY)
    if (NOT _exe_path)
        set(_exe_path "${CMAKE_BINARY_DIR}/bin")
    endif()

    find_program(VELOX_WINDEPLOYQT
        NAMES windeployqt6 windeployqt
        HINTS "${VELOX_QT_DIR}/bin" "${Qt6_DIR}/../../../bin"
    )
    if (NOT VELOX_WINDEPLOYQT)
        message(WARNING
            "windeployqt was not found; ${target} will not be runnable from the "
            "build tree unless the Qt bin directory is on PATH.")
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${VELOX_WINDEPLOYQT}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-quick-import
                --no-compiler-runtime
                "$<TARGET_FILE:${target}>"
        COMMENT "Deploying the Qt runtime for ${target}"
        VERBATIM
    )
endfunction()