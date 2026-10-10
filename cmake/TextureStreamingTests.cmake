add_controller_test(texture_streaming_policy_test TextureStreamingPolicy tests/texture_streaming_policy_test.cpp)
target_compile_definitions(texture_streaming_policy_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(texture_streaming_policy_test PRIVATE nlohmann_json::nlohmann_json)
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
foreach(_header material settings enabled ui flags features sections)
    list(APPEND _streaming_categories_headers "${_streaming_categories_dir}/streaming_${_header}_under_test.h")
endforeach()
add_custom_command(OUTPUT ${_streaming_categories_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_streaming_categories_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_texture_streaming_categories.cmake"
    DEPENDS src/Features/TextureStreaming.cpp src/Features/TextureStreaming/GeometryDemand.cpp
        src/Menu/SettingsPage.h extern/CommonLibSSE-NG/include/RE/B/BSShaderMaterial.h
        extern/CommonLibSSE-NG/include/RE/B/BSShaderProperty.h
        tests/extract_source_region.cmake tests/extract_texture_streaming_categories.cmake
    VERBATIM)
add_controller_test(texture_streaming_categories_test TextureStreamingCategories tests/texture_streaming_categories_test.cpp)
target_sources(texture_streaming_categories_test PRIVATE ${_streaming_categories_headers})
target_include_directories(texture_streaming_categories_test PRIVATE "${_streaming_categories_dir}")
target_link_libraries(texture_streaming_categories_test PRIVATE nlohmann_json::nlohmann_json)

set(_streaming_publication_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/streaming-publication")
set(_streaming_publication_headers
    "${_streaming_publication_dir}/streaming_bindings_under_test.h"
    "${_streaming_publication_dir}/streaming_publication_under_test.h")
add_custom_command(OUTPUT ${_streaming_publication_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_streaming_publication_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_texture_streaming_publication.cmake"
    DEPENDS src/Features/TextureStreaming.cpp tests/extract_source_region.cmake
        tests/extract_texture_streaming_publication.cmake
    VERBATIM)
target_sources(texture_streaming_gpu_test PRIVATE tests/texture_streaming_publication_checks.h ${_streaming_publication_headers})
target_include_directories(texture_streaming_gpu_test PRIVATE "${_streaming_publication_dir}")
