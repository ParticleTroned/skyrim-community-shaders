set(_neural_gpu_timing_dir "${CMAKE_CURRENT_BINARY_DIR}/neural_gpu_timing_test")
add_custom_command(
    OUTPUT "${_neural_gpu_timing_dir}/neural_gpu_timing_under_test.h"
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_neural_gpu_timing_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_neural_gpu_timing.cmake"
    DEPENDS src/Features/Upscaling/NeuralRendering/D3D12Interop.cpp
        tests/extract_neural_gpu_timing.cmake tests/extract_source_region.cmake
    VERBATIM
)
foreach(_mode IN ITEMS production devbench)
    add_controller_test(neural_gpu_timing_${_mode}_test NRGpuTiming-${_mode} tests/neural_gpu_timing_test.cpp)
    target_sources(neural_gpu_timing_${_mode}_test PRIVATE
        "${_neural_gpu_timing_dir}/neural_gpu_timing_under_test.h")
    target_include_directories(neural_gpu_timing_${_mode}_test PRIVATE "${_neural_gpu_timing_dir}")
    target_compile_definitions(neural_gpu_timing_${_mode}_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(_mode STREQUAL "devbench")
        target_compile_definitions(neural_gpu_timing_${_mode}_test PRIVATE DEVBENCH_BRIDGE_ENABLED)
    endif()
    target_link_libraries(neural_gpu_timing_${_mode}_test PRIVATE d3d12 dxgi)
    set_tests_properties(NRGpuTiming-${_mode} PROPERTIES TIMEOUT 30)
endforeach()
