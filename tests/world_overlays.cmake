add_d3d_shader_test(
    world_overlays_test
    world_overlays
    tests/world_overlays_test.cpp
)
target_include_directories(
    world_overlays_test
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(world_overlays_test PRIVATE dxguid.lib runtimeobject.lib)
