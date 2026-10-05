add_executable(VeloxTests
    UpdateTests.cpp
    "${CMAKE_SOURCE_DIR}/tools/VeloxUpdater/UpdaterArguments.cpp"
)
target_include_directories(VeloxTests PRIVATE "${CMAKE_SOURCE_DIR}/tools/VeloxUpdater")
target_link_libraries(VeloxTests PRIVATE
    Velox::Io
    Velox::Render
    Velox::Update
    Velox::ProjectOptions
)
add_test(NAME VeloxTests COMMAND VeloxTests)
