cmake_minimum_required(VERSION 3.25)

project(VeloxStudio
    VERSION      0.1.0
    DESCRIPTION  "Velox Studio - a Vulkan-powered professional image editor for Windows"
    HOMEPAGE_URL "https://github.com/marleypumpkin08-ctrl/velox-studio"
    LANGUAGES    CXX
)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(VELOX_BUILD_TESTS        "Build the Velox Studio unit tests"         ON)
option(VELOX_BUILD_UPDATER      "Build the standalone VeloxUpdater utility" ON)
option(VELOX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"         ON)
set(VELOX_APP_VERSION "${PROJECT_VERSION}-beta.1" CACHE STRING
    "Semantic application version, including an optional prerelease suffix")
if (NOT VELOX_APP_VERSION MATCHES "^${PROJECT_VERSION}(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR
        "VELOX_APP_VERSION must use ${PROJECT_VERSION} with an optional prerelease suffix")
endif()

set(VELOX_GITHUB_REPOSITORY "marleypumpkin08-ctrl/velox-studio" CACHE STRING
    "GitHub owner/repository used by the release updater")

# ---------------------------------------------------------------------------
# Project-wide configuration
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Keep every artifact in predictable, per-configuration folders so the installer
# and the deployment scripts never have to guess paths.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# ---------------------------------------------------------------------------
# Shared modules: compiler policy, versioning, Qt and shader helpers.
# ---------------------------------------------------------------------------
include(VeloxCompilerOptions)
include(VeloxVersion)
include(VeloxQt)
include(VeloxShaders)

# ---------------------------------------------------------------------------
# Third-party dependencies (vendored, no network access required)
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# ---------------------------------------------------------------------------
# Application modules
# ---------------------------------------------------------------------------
add_subdirectory(src)

if (VELOX_BUILD_UPDATER)
    add_subdirectory(tools/VeloxUpdater)
endif()

if (VELOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "Velox Studio ${PROJECT_VERSION} configured")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  tests      : ${VELOX_BUILD_TESTS}")
message(STATUS "  output     : ${CMAKE_BINARY_DIR}")
message(STATUS "")