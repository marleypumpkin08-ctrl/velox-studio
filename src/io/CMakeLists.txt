# velox_io - image import/export and the native .vlx document container.
# Pixel codecs are provided by Qt's bundled libpng/libjpeg through QImageReader
# and QImageWriter; the WebP and TIFF codecs come from the qtimageformats addon.
add_library(velox_io STATIC
    VlxFormat.cpp
)

add_library(Velox::Io ALIAS velox_io)

target_include_directories(velox_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(velox_io PUBLIC Velox::Document)
velox_link_qt(velox_io Core Gui)
velox_enable_qt_automation(velox_io)