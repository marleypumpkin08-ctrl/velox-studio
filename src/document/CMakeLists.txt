# velox_document - the document model: documents, layers, blend modes and strokes.
add_library(velox_document STATIC
    BlendMode.cpp
    Document.cpp
    Layer.cpp
)

add_library(Velox::Document ALIAS velox_document)

target_include_directories(velox_document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(velox_document PUBLIC Velox::Core)
velox_link_qt(velox_document Core Gui)
velox_enable_qt_automation(velox_document)