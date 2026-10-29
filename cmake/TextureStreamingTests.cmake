add_controller_test(texture_streaming_policy_test TextureStreamingPolicy tests/texture_streaming_policy_test.cpp)
target_compile_definitions(texture_streaming_policy_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(texture_streaming_policy_test PRIVATE nlohmann_json::nlohmann_json)

set(_streaming_traversal_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/streaming-traversal")
set(_streaming_traversal_headers)
foreach(_header children pointer array preview_roots)
    list(APPEND _streaming_traversal_headers "${_streaming_traversal_dir}/streaming_${_header}_under_test.h")
endforeach()
add_custom_command(OUTPUT ${_streaming_traversal_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_streaming_traversal_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_texture_streaming_traversal.cmake"
    DEPENDS src/Features/TextureStreaming.cpp src/Features/TextureStreaming/Preview.cpp src/Utils/Game.h
        extern/CommonLibSSE-NG/include/RE/N/NiTArray.h
        tests/extract_source_region.cmake tests/extract_texture_streaming_traversal.cmake
    VERBATIM)
add_controller_test(texture_streaming_traversal_test TextureStreamingTraversal tests/texture_streaming_traversal_test.cpp)
target_sources(texture_streaming_traversal_test PRIVATE ${_streaming_traversal_headers})
target_include_directories(texture_streaming_traversal_test PRIVATE "${_streaming_traversal_dir}")

add_d3d_shader_test(texture_streaming_gpu_test TextureStreamingWARP tests/texture_streaming_gpu_test.cpp)
target_sources(texture_streaming_gpu_test PRIVATE
    src/Features/TextureStreaming/TextureData.cpp
    src/Utils/GpuMemoryBudget.cpp)
target_compile_definitions(texture_streaming_gpu_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(texture_streaming_gpu_test PRIVATE Microsoft::DirectXTex dxgi)

set(_streaming_gate_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/streaming-gate")
set(_streaming_gate_headers
    "${_streaming_gate_dir}/texture_streaming_gate_under_test.h"
    "${_streaming_gate_dir}/texture_streaming_states_under_test.h")
add_custom_command(OUTPUT ${_streaming_gate_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_streaming_gate_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_texture_streaming_gate.cmake"
    DEPENDS src/Features/Upscaling.cpp src/Features/Upscaling.h
        tests/extract_source_region.cmake tests/extract_texture_streaming_gate.cmake
    VERBATIM)
add_controller_test(texture_streaming_gate_test TextureStreamingGate tests/texture_streaming_gate_test.cpp)
target_sources(texture_streaming_gate_test PRIVATE ${_streaming_gate_headers})
target_include_directories(texture_streaming_gate_test PRIVATE "${_streaming_gate_dir}")

set(_streaming_categories_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/streaming-categories")
set(_streaming_categories_headers)
foreach(_header material demand settings ui flags features sections mode geometry_types startup conflict landscape_lookup)
    list(APPEND _streaming_categories_headers "${_streaming_categories_dir}/streaming_${_header}_under_test.h")
endforeach()
add_custom_command(OUTPUT ${_streaming_categories_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_streaming_categories_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_texture_streaming_categories.cmake"
    DEPENDS src/Features/TextureStreaming.cpp src/Features/TextureStreaming/GeometryDemand.cpp
        src/TruePBR/BSLightingShaderMaterialPBRLandscape.cpp
        src/Menu/SettingsPage.h extern/CommonLibSSE-NG/include/RE/B/BSShaderMaterial.h
        extern/CommonLibSSE-NG/include/RE/B/BSShaderProperty.h
        extern/CommonLibSSE-NG/include/RE/B/BSGeometry.h
        tests/extract_source_region.cmake tests/extract_texture_streaming_categories.cmake
    VERBATIM)
add_controller_test(texture_streaming_categories_test TextureStreamingCategories tests/texture_streaming_categories_test.cpp)
target_sources(texture_streaming_categories_test PRIVATE ${_streaming_categories_headers})
target_include_directories(texture_streaming_categories_test PRIVATE "${_streaming_categories_dir}"
    "${PROJECT_SOURCE_DIR}/extern/CommonLibSSE-NG/include")
target_link_libraries(texture_streaming_categories_test PRIVATE nlohmann_json::nlohmann_json Microsoft::DirectXTex)
add_controller_test(texture_streaming_categories_debug_test TextureStreamingCategoriesDebug tests/texture_streaming_categories_test.cpp)
target_sources(texture_streaming_categories_debug_test PRIVATE ${_streaming_categories_headers})
target_include_directories(texture_streaming_categories_debug_test PRIVATE "${_streaming_categories_dir}"
    "${PROJECT_SOURCE_DIR}/extern/CommonLibSSE-NG/include")
target_link_libraries(texture_streaming_categories_debug_test PRIVATE nlohmann_json::nlohmann_json Microsoft::DirectXTex)
target_compile_definitions(texture_streaming_categories_debug_test PRIVATE DEVBENCH_BRIDGE_ENABLED)

set(_streaming_publication_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/streaming-publication")
set(_streaming_publication_headers
    "${_streaming_publication_dir}/streaming_bindings_under_test.h"
    "${_streaming_publication_dir}/streaming_publication_under_test.h"
    "${_streaming_publication_dir}/streaming_reader_under_test.h"
    "${_streaming_publication_dir}/streaming_path_under_test.h"
    "${_streaming_publication_dir}/streaming_observation_under_test.h"
    "${_streaming_publication_dir}/streaming_preview_protection_under_test.h"
    "${_streaming_publication_dir}/streaming_defer_under_test.h")
add_custom_command(OUTPUT ${_streaming_publication_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_streaming_publication_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_texture_streaming_publication.cmake"
    DEPENDS src/Features/TextureStreaming.cpp tests/extract_source_region.cmake
        tests/extract_texture_streaming_publication.cmake
    VERBATIM)
target_sources(texture_streaming_gpu_test PRIVATE tests/texture_streaming_publication_checks.h tests/texture_streaming_reader_checks.h tests/texture_streaming_inventory_checks.h ${_streaming_publication_headers})
target_include_directories(texture_streaming_gpu_test PRIVATE "${_streaming_publication_dir}")

add_d3d_shader_test(texture_streaming_gpu_debug_test TextureStreamingWARPDebug tests/texture_streaming_gpu_test.cpp)
target_sources(texture_streaming_gpu_debug_test PRIVATE
    src/Features/TextureStreaming/TextureData.cpp
    src/Utils/GpuMemoryBudget.cpp
    tests/texture_streaming_publication_checks.h
    tests/texture_streaming_reader_checks.h tests/texture_streaming_inventory_checks.h
    ${_streaming_publication_headers})
target_include_directories(texture_streaming_gpu_debug_test PRIVATE "${_streaming_publication_dir}")
target_compile_definitions(texture_streaming_gpu_debug_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN DEVBENCH_BRIDGE_ENABLED)
target_link_libraries(texture_streaming_gpu_debug_test PRIVATE Microsoft::DirectXTex dxgi)

set(_streaming_load_header "${CMAKE_CURRENT_BINARY_DIR}/generated/streaming-load/streaming_load_under_test.h")
add_custom_command(OUTPUT "${_streaming_load_header}"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_HEADER=${_streaming_load_header}" -P "${PROJECT_SOURCE_DIR}/tests/extract_texture_streaming_load.cmake"
    DEPENDS src/Features/TextureStreaming/LoadHooks.cpp src/Features/TextureStreaming.cpp tests/extract_texture_streaming_load.cmake
    VERBATIM)
add_d3d_shader_test(texture_streaming_load_test TextureStreamingRememberedLoads tests/texture_streaming_load_test.cpp)
target_sources(texture_streaming_load_test PRIVATE "${_streaming_load_header}" src/Features/TextureStreaming/TextureData.cpp)
target_include_directories(texture_streaming_load_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/streaming-load")
target_compile_definitions(texture_streaming_load_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(texture_streaming_load_test PRIVATE nlohmann_json::nlohmann_json Microsoft::DirectXTex)
target_link_libraries(texture_streaming_gpu_test PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(texture_streaming_gpu_debug_test PRIVATE nlohmann_json::nlohmann_json)

add_controller_test(texture_streaming_pressure_test TextureStreamingPressure tests/texture_streaming_pressure_test.cpp)
target_compile_definitions(texture_streaming_pressure_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN DEVBENCH_BRIDGE_ENABLED)
target_link_libraries(texture_streaming_pressure_test PRIVATE nlohmann_json::nlohmann_json)

add_controller_test(sps_followups_policy_test SPSFollowupsPolicy tests/sps_followups_policy_test.cpp)
target_link_libraries(sps_followups_policy_test PRIVATE nlohmann_json::nlohmann_json)

set(_shadow_startup_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shadow-startup")
set(_shadow_startup_header "${_shadow_startup_dir}/shadow_startup_under_test.h")
add_custom_command(OUTPUT "${_shadow_startup_header}"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_shadow_startup_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_sps_followups.cmake"
    DEPENDS src/Features/LightLimitFix/ShadowCulling.cpp tests/extract_source_region.cmake
        tests/extract_sps_followups.cmake
    VERBATIM)
target_sources(sps_followups_policy_test PRIVATE "${_shadow_startup_header}")
target_include_directories(sps_followups_policy_test PRIVATE "${_shadow_startup_dir}")
