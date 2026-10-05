# ---------------------------------------------------------------------------
# VeloxShaders.cmake
#
# Compiles GLSL to SPIR-V with the Vulkan SDK's glslc, tracking dependencies so
# only changed shaders are rebuilt.
#
#   velox_add_shaders(<target>
#       SHADERS  canvas.vert canvas.frag checker.comp
#       [OUTPUT_NAME_DIR shaders]
#   )
#
# Output lands in <runtime output dir>/shaders so the application resolves it
# relative to its own executable.
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

function(velox_find_glslc out_var)
    if (VELOX_GLSLC AND EXISTS "${VELOX_GLSLC}")
        set(${out_var} "${VELOX_GLSLC}" PARENT_SCOPE)
        return()
    endif()

    find_program(_glslc NAMES glslc glslc.exe
        HINTS "$ENV{VULKAN_SDK}/Bin" "${VULKAN_SDK}/Bin"
        PATHS "C:/VulkanSDK" PATH_SUFFIXES Bin
    )
    if (_glslc)
        set(VELOX_GLSLC "${_glslc}" CACHE FILEPATH "glslc shader compiler")
        set(${out_var} "${_glslc}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(velox_add_shaders target)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME_DIR" "SHADERS" ${ARGN})

    if (NOT ARG_SHADERS)
        message(FATAL_ERROR "velox_add_shaders(${target}): no SHADERS were listed")
    endif()

    velox_find_glslc(_glslc)
    if (NOT _glslc)
        message(FATAL_ERROR
            "glslc is required to build Vulkan shaders for '${target}'. "
            "Run scripts/bootstrap-deps.ps1 to install the Vulkan SDK.")
    endif()

    if (NOT ARG_OUTPUT_NAME_DIR)
        set(ARG_OUTPUT_NAME_DIR "shaders")
    endif()

    set(_out_dir "${VELOX_RUNTIME_OUTPUT_DIR}/${ARG_OUTPUT_NAME_DIR}")
    file(MAKE_DIRECTORY "${_out_dir}")

    set(_outputs "")
    set(_shader_target "${target}_shaders")

    foreach(_shader IN LISTS ARG_SHADERS)
        get_filename_component(_abs "${_shader}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        get_filename_component(_name "${_shader}" NAME)

        if (NOT EXISTS "${_abs}")
            message(FATAL_ERROR "velox_add_shaders(${target}): '${_abs}' does not exist")
        endif()

        # The stage is inferred from the extension, which glslc does natively,
        # so the same source can be compiled for several stages when needed.
        set(_spv "${_out_dir}/${_name}.spv")

        add_custom_command(
            OUTPUT  "${_spv}"
            COMMAND "${_glslc}"
                    --target-env=vulkan1.1
                    -O
                    # Emit the source as a comment so RenderDoc captures are readable.
                    -g
                    "${_abs}" -o "${_spv}"
            DEPENDS "${_abs}"
            COMMENT "glslc: ${_name} -> ${ARG_OUTPUT_NAME_DIR}/${_name}.spv"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )
        list(APPEND _outputs "${_spv}")

        # Recompile when the shader source changes; the .spv is a build artifact.
        set_property(SOURCE "${_abs}" APPEND PROPERTY OBJECT_DEPENDS "${_spv}")
    endforeach()

    add_custom_target(${_shader_target} ALL DEPENDS ${_outputs})
    add_dependencies(${target} ${_shader_target})

    # Expose the shader directory to C++ so the renderer never hardcodes paths.
    target_compile_definitions(${target} PRIVATE
        VELOX_SHADER_DIR="${ARG_OUTPUT_NAME_DIR}"
    )
endfunction()