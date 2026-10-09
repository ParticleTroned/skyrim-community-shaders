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
