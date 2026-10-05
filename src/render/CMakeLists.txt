find_package(Vulkan REQUIRED)

add_library(velox_render STATIC
    VulkanWindow.cpp
)
add_library(Velox::Render ALIAS velox_render)
target_include_directories(velox_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(velox_render PUBLIC Velox::Document)
target_link_libraries(velox_render PUBLIC Vulkan::Vulkan)
velox_link_qt(velox_render Gui)
velox_enable_qt_automation(velox_render)
velox_add_shaders(velox_render
    SHADERS
        shaders/canvas.vert
        shaders/canvas.frag
)