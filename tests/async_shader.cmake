add_d3d_shader_test(async_shader_test AsyncShader tests/async_shader_test.cpp)
target_link_libraries(
    async_shader_test
    PRIVATE CommonLibSSE::CommonLibSSE spdlog::spdlog windowsapp.lib
)
target_compile_definitions(async_shader_test PRIVATE NOMINMAX)
include("${PROJECT_SOURCE_DIR}/tests/standalone_preparation.cmake")
