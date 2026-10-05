# ---------------------------------------------------------------------------
# VeloxCompilerOptions.cmake
#
# Central definition of the language standard, warning levels and hardening
# flags used by every Velox Studio target.
#
# Targets link `velox_project_options` (an INTERFACE library) rather than
# duplicating flags, so a policy change happens in exactly one place.
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

add_library(velox_project_options INTERFACE)
add_library(Velox::ProjectOptions ALIAS velox_project_options)

# ---------------------------------------------------------------------------
# Language
# ---------------------------------------------------------------------------
target_compile_features(velox_project_options INTERFACE cxx_std_20)
set_target_properties(velox_project_options PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF           # -std=c++20, never gnu++20
)

# Export a compile definition other code can branch on.
target_compile_definitions(velox_project_options INTERFACE
    $<$<CONFIG:Debug>:VELOX_DEBUG_BUILD=1>
    $<$<NOT:$<CONFIG:Debug>>:VELOX_RELEASE_BUILD=1>
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    # Qt is consumed through its own headers; this silences the noisy
    # "Qt requires a C++17 compiler" banner in MSVC.
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

# ---------------------------------------------------------------------------
# Warnings - applied to first-party code only.
#
# third_party sources and generated code link velox_third_party_options, which
# contributes no warning flags, so upstream code never breaks our build.
# ---------------------------------------------------------------------------
add_library(velox_third_party_options INTERFACE)
add_library(Velox::ThirdPartyOptions ALIAS velox_third_party_options)

if (MSVC)
    # /W4          high warning level
    # /permissive- strict conformance
    # /utf-8       source and execution charset are UTF-8 (required for the
    #              Japanese strings and the 速 glyph used in the branding)
    # /Zc:__cplusplus            report the real C++ standard in __cplusplus
    # /Zc:preprocessor           conformant preprocessor
    # /Zc:externConstexpr        needed by some header-only libraries
    # /MP          parallel compilation
    # /EHsc        standard C++ exception semantics
    # /bigobj      large translation units (templates, generated registries)
    target_compile_options(velox_project_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
        /Zc:externConstexpr /MP /EHsc /bigobj
    )
    target_compile_options(velox_third_party_options INTERFACE
        /utf-8 /MP /EHsc /bigobj /W0 /wd4996
    )

    # Warnings are errors for first-party code so problems are caught at build
    # time rather than verified later. They are deliberately NOT enabled for
    # third-party code.
    if (VELOX_WARNINGS_AS_ERRORS)
        target_compile_options(velox_project_options INTERFACE /WX)
    endif()

    # Link-time code generation in Release for a faster shipping binary.
    if (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE)
        target_compile_options(velox_project_options INTERFACE
            $<$<CONFIG:Release>:/Gw>          # whole-program optimisation data
            $<$<CONFIG:Release>:/Gy>          # function-level linking
            $<$<CONFIG:RelWithDebInfo>:/Zi>
            $<$<CONFIG:RelWithDebInfo>:/Gw>
        )
        target_link_options(velox_project_options INTERFACE
            $<$<CONFIG:Release>:/LTCG>
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
        )
    endif()

    # Enable the incremental debugger-friendly PDB naming.
    target_compile_options(velox_project_options INTERFACE
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Debug>:/RTC1>
    )
else()
    target_compile_options(velox_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wdouble-promotion -Wformat=2
    )
    if (VELOX_WARNINGS_AS_ERRORS)
        target_compile_options(velox_project_options INTERFACE -Werror)
    endif()
    target_compile_options(velox_third_party_options INTERFACE -w)
endif()

# ---------------------------------------------------------------------------
# Position independent code - required for the plugin DLLs.
# ---------------------------------------------------------------------------
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Default build type when a single-config generator is used without a preset.
# ---------------------------------------------------------------------------
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Output layout: binaries, libraries and shaders in predictable folders so the
# installer can package them without guessing.
# ---------------------------------------------------------------------------
if (NOT DEFINED VELOX_RUNTIME_OUTPUT_DIR)
    set(VELOX_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
endif()
if (NOT DEFINED VELOX_LIBRARY_OUTPUT_DIR)
    set(VELOX_LIBRARY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
endif()
if (NOT DEFINED VELOX_ARCHIVE_OUTPUT_DIR)
    set(VELOX_ARCHIVE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib")
endif()