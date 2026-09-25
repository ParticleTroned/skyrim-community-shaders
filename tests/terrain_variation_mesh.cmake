set(_terrain_variation_mesh_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/terrain-variation-mesh"
)
set(_terrain_variation_mesh_test_header
    "${_terrain_variation_mesh_test_dir}/terrain_variation_mesh_under_test.h"
)
add_custom_command(
    OUTPUT "${_terrain_variation_mesh_test_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_terrain_variation_mesh_test_header}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_terrain_variation_mesh.cmake"
    DEPENDS
        src/Features/TerrainVariation.cpp
        tests/extract_terrain_variation_mesh.cmake
    VERBATIM
)
add_controller_test(
    terrain_variation_mesh_test
    TerrainVariationMesh
    tests/terrain_variation_mesh_test.cpp
)
target_sources(
    terrain_variation_mesh_test
    PRIVATE "${_terrain_variation_mesh_test_header}"
)
target_include_directories(
    terrain_variation_mesh_test
    PRIVATE "${_terrain_variation_mesh_test_dir}"
)
