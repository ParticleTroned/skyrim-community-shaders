cmake_minimum_required(VERSION 3.24)
project(CSXNRReplay LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "NR replay requires the admitted Windows D3D12 runtime")
endif()
get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CSX_NR_DEPENDENCY_ROOT "${REPOSITORY_ROOT}" CACHE PATH "Checkout containing populated extern dependencies")
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
set(PROJECT_ROOT "${REPOSITORY_ROOT}")
set(OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
include("${REPOSITORY_ROOT}/tests/extract_d3d_resource_naming.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/extract_runtime_utilities.cmake")
set(_nr_source "${REPOSITORY_ROOT}/src/Features/Upscaling/NeuralRendering")
# Production execution is retained; replay alone can observe creation parameters
# before the unchanged provider call, with a hashed generated-source identity.
foreach(_unit IN ITEMS Runtime D3D12Interop)
    file(READ "${_nr_source}/${_unit}.cpp" _contents)
    if(_unit STREQUAL "Runtime")
        set(_include "#include \"Util.h\"")
    else()
        set(_include "#include \"Utils/D3D.h\"")
    endif()
    string(FIND "${_contents}" "${_include}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "NR replay dependency boundary changed: ${_unit}")
    endif()
    string(REPLACE "${_include}" "#include \"replay_platform.h\"" _contents "${_contents}")
    if(_unit STREQUAL "Runtime")
        set(_creation_call "const auto createResult = createFeature(")
        string(REGEX MATCHALL "const auto createResult = createFeature\\(" _creation_matches "${_contents}")
        list(LENGTH _creation_matches _creation_count)
        if(NOT _creation_count EQUAL 1)
            message(FATAL_ERROR "NR replay feature-creation observation boundary changed")
        endif()
        string(REPLACE "${_creation_call}" "NrReplay::FeatureCreationObserver::Observe(parameters);\n\t\t\t${_creation_call}" _contents "${_contents}")
        string(PREPEND _contents "#include \"FeatureCreationObserver.h\"\n")
    endif()
    file(WRITE "${OUTPUT_DIRECTORY}/${_unit}.cpp" "${_contents}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_nr_source}/${_unit}.cpp")
endforeach()
set(_replay_identity "{}")
file(GLOB _shared_kernel_sources CONFIGURE_DEPENDS "${_nr_source}/DevBench/*.h")
foreach(_shared_source IN LISTS _shared_kernel_sources)
    file(RELATIVE_PATH _shared_key "${REPOSITORY_ROOT}" "${_shared_source}")
    file(SHA256 "${_shared_source}" _shared_hash)
    string(JSON _replay_identity SET "${_replay_identity}" "${_shared_key}" "\"${_shared_hash}\"")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_shared_source}")
endforeach()
foreach(_source IN ITEMS
    tools/nr-replay/main.cpp
    tools/nr-replay/GpuCapture.h
    tools/nr-replay/ProviderFloorContract.h
    tools/nr-replay/ProviderFloorProbe.h
    tools/nr-replay/KernelChainCommandList.h
    tools/nr-replay/KernelCommandCapture.h
    tools/nr-replay/KernelScheduleReadback.h
    tools/nr-replay/FeatureCreationObserver.h
    tools/nr-replay/KernelPairSchedule.h
    tools/nr-replay/KernelReplacementSet.h
    tools/nr-replay/ProviderDescriptorGuardContract.h
    tools/nr-replay/ProviderKernelChainProbe.h
    tools/nr-replay/ProviderKernelChainContract.h
    tools/nr-replay/replay_platform.h
    tools/nr-replay/CMakeLists.txt
    tools/nr-replay/extract_runtime_utilities.cmake
    src/Features/Upscaling/NeuralRendering/NativeEvaluationLayout.h
    src/Features/Upscaling/NeuralRendering/RegionCapacity.h
    src/Features/Upscaling/NeuralRendering/CapacityFallback.h
    src/Features/Upscaling/NeuralRendering/SourceTransport.h
    src/Features/Upscaling/NeuralRendering/SpatialRoiTracks.h
    src/Features/Upscaling/NeuralRendering/Runtime.h
    src/Features/Upscaling/NeuralRendering/D3D12Interop.h
    src/Features/Upscaling/NeuralRendering/ExecutionEvidence.h
    src/Features/Upscaling/NeuralRendering/LifetimeDiagnostics.h
    src/Features/Upscaling/NeuralRendering/SubmissionFenceSnapshot.h
    src/Features/Upscaling/NeuralRendering/ExperimentalKernelBatch.h
    src/Features/Upscaling/NeuralRendering/ExperimentalKernelBatch.cpp
    src/Features/Upscaling/NeuralRendering/ComputeSubrect.h
    src/Features/Upscaling/NeuralRendering/PipelinePolicy.h
    src/Features/Upscaling/NeuralRendering/CharacterMultiRoi.h
    src/Features/Upscaling/NeuralRendering/CharacterComputeSubrect.h
    src/Features/Upscaling/NeuralRendering/CharacterMaskWorkPolicy.h
    src/Features/Upscaling/NeuralRendering/CharacterRegionPolicy.h
    src/Features/Upscaling/NeuralRendering/RoiDescriptor.h
    src/Features/Upscaling/DLSSViewportCrop.h
    src/Utils/PassTimingCapture.h
    src/Utils/ResourceName.h
    tests/extract_d3d_resource_naming.cmake
    src/Utils/CryptoHash.cpp
    src/Utils/StringUtils.h
    src/Utils/CryptoHash.h)
    file(SHA256 "${REPOSITORY_ROOT}/${_source}" _source_hash)
    string(JSON _replay_identity SET "${_replay_identity}" "${_source}" "\"${_source_hash}\"")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPOSITORY_ROOT}/${_source}")
endforeach()
set(_nvapi_header "${CSX_NR_DEPENDENCY_ROOT}/extern/nvapi/nvapi.h")
file(SHA256 "${_nvapi_header}" _nvapi_header_hash)
string(JSON _replay_identity SET "${_replay_identity}" "extern/nvapi/nvapi.h" "\"${_nvapi_header_hash}\"")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_nvapi_header}")
foreach(_source IN ITEMS runtime_utilities.cpp d3d_resource_naming.h Runtime.cpp D3D12Interop.cpp)
    file(SHA256 "${OUTPUT_DIRECTORY}/${_source}" _source_hash)
    string(JSON _replay_identity SET "${_replay_identity}" "generated/${_source}" "\"${_source_hash}\"")
endforeach()
file(SHA256 "${_nr_source}/Runtime.cpp" _runtime_hash)
file(SHA256 "${_nr_source}/D3D12Interop.cpp" _interop_hash)
file(SHA256 "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/external/ngx-sdk/include/nvsdk_ngx.h" _ngx_hash)
file(SHA256 "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/include/sl_core_types.h" _sl_hash)
file(READ "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/include/sl_version.h" _version_header)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/external/ngx-sdk/include/nvsdk_ngx.h"
    "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/include/sl_core_types.h"
    "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/include/sl_version.h")
foreach(_part IN ITEMS MAJOR MINOR PATCH)
    string(REGEX MATCH "#define SL_VERSION_${_part} ([0-9]+)" _match "${_version_header}")
    set(_version_${_part} "${CMAKE_MATCH_1}")
endforeach()
file(WRITE "${OUTPUT_DIRECTORY}/build_identity.h"
    "#pragma once\ninline constexpr char kRuntimeSourceHash[] = \"${_runtime_hash}\";\ninline constexpr char kInteropSourceHash[] = \"${_interop_hash}\";\ninline constexpr char kNgxHeaderHash[] = \"${_ngx_hash}\";\ninline constexpr char kSlHeaderHash[] = \"${_sl_hash}\";\ninline constexpr char kSlVersion[] = \"${_version_MAJOR}.${_version_MINOR}.${_version_PATCH}\";\n")
file(APPEND "${OUTPUT_DIRECTORY}/build_identity.h"
    "inline constexpr char kReplaySourceIdentityJson[] = R\"nr_identity(${_replay_identity})nr_identity\";\n")
file(READ "${REPOSITORY_ROOT}/src/Features/Upscaling/Streamline.cpp" _streamline_source)
string(REGEX MATCH "pref.projectId = \"([^\"]+)\"" _project_match "${_streamline_source}")
if(NOT _project_match)
    message(FATAL_ERROR "NR replay cannot identify the production NGX project")
endif()
file(APPEND "${OUTPUT_DIRECTORY}/build_identity.h"
    "inline constexpr char kNgxProjectId[] = \"${CMAKE_MATCH_1}\";\n")
string(REGEX MATCH "pref.engineVersion = \"([^\"]+)\"" _engine_match "${_streamline_source}")
if(NOT _engine_match)
    message(FATAL_ERROR "NR replay cannot identify the production NGX engine version")
endif()
file(APPEND "${OUTPUT_DIRECTORY}/build_identity.h"
    "inline constexpr char kNgxEngineVersion[] = \"${CMAKE_MATCH_1}\";\n")
set(_ngx_library "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/external/ngx-sdk/lib/Windows_x86_64/nvsdk_ngx_d.lib")
file(SHA256 "${_ngx_library}" _ngx_library_hash)
file(APPEND "${OUTPUT_DIRECTORY}/build_identity.h"
    "inline constexpr char kNgxLibraryHash[] = \"${_ngx_library_hash}\";\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${REPOSITORY_ROOT}/src/Features/Upscaling/Streamline.cpp" "${_ngx_library}")
add_executable(csx_nr_replay main.cpp "${OUTPUT_DIRECTORY}/Runtime.cpp"
    "${OUTPUT_DIRECTORY}/D3D12Interop.cpp" "${OUTPUT_DIRECTORY}/runtime_utilities.cpp"
    "${_nr_source}/ExperimentalKernelBatch.cpp"
    "${REPOSITORY_ROOT}/src/Utils/CryptoHash.cpp")
target_compile_features(csx_nr_replay PRIVATE cxx_std_23)
target_compile_definitions(csx_nr_replay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DEVBENCH_BRIDGE_ENABLED)
target_include_directories(csx_nr_replay PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${OUTPUT_DIRECTORY}"
    "${REPOSITORY_ROOT}/src" "${_nr_source}"
    "${CSX_NR_DEPENDENCY_ROOT}/extern/CommonLibSSE-NG/include"
    "${CSX_NR_DEPENDENCY_ROOT}/extern/FidelityFX-SDK/framework/cauldron/framework/libs/renderdoc/include"
    "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/external/ngx-sdk/include")
target_include_directories(csx_nr_replay PRIVATE "${CSX_NR_DEPENDENCY_ROOT}/extern/nvapi")
target_compile_options(csx_nr_replay PRIVATE /W4 /EHsc /permissive- /FIreplay_platform.h)
target_link_libraries(csx_nr_replay PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog
    d3d11 d3d12 dxgi dxguid bcrypt wintrust version psapi
    "${_ngx_library}")
include(CTest)
if(BUILD_TESTING)
    add_executable(nr_kernel_frame_fence test_kernel_frame_fence.cpp)
    target_compile_features(nr_kernel_frame_fence PRIVATE cxx_std_23)
    target_include_directories(nr_kernel_frame_fence PRIVATE "${REPOSITORY_ROOT}/src")
    target_compile_options(nr_kernel_frame_fence PRIVATE /W4 /EHsc /permissive-)
    add_test(NAME nr_kernel_frame_fence COMMAND nr_kernel_frame_fence)
    add_executable(nr_kernel_replacement_set test_kernel_replacement_set.cpp "${REPOSITORY_ROOT}/src/Utils/CryptoHash.cpp")
    target_compile_features(nr_kernel_replacement_set PRIVATE cxx_std_23)
    target_include_directories(nr_kernel_replacement_set PRIVATE "${REPOSITORY_ROOT}/src")
    target_compile_definitions(nr_kernel_replacement_set PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(nr_kernel_replacement_set PRIVATE /W4 /EHsc /permissive-)
    target_link_libraries(nr_kernel_replacement_set PRIVATE nlohmann_json::nlohmann_json bcrypt)
    add_test(NAME nr_kernel_replacement_set COMMAND nr_kernel_replacement_set)
    add_executable(nr_provider_descriptor_guard test_provider_descriptor_guard.cpp)
    target_include_directories(nr_provider_descriptor_guard PRIVATE "${REPOSITORY_ROOT}/src")
    target_compile_features(nr_provider_descriptor_guard PRIVATE cxx_std_23)
    target_compile_definitions(nr_provider_descriptor_guard PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(nr_provider_descriptor_guard PRIVATE /W4 /EHsc /permissive-)
    add_test(NAME nr_provider_descriptor_guard COMMAND nr_provider_descriptor_guard)
    add_executable(nr_feature_creation_observer test_feature_creation_observer.cpp)
    target_compile_features(nr_feature_creation_observer PRIVATE cxx_std_23)
    target_compile_definitions(nr_feature_creation_observer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(nr_feature_creation_observer PRIVATE /W4 /EHsc /permissive-)
    add_test(NAME nr_feature_creation_observer COMMAND nr_feature_creation_observer)
    add_executable(nr_provider_floor_contract test_provider_floor.cpp "${REPOSITORY_ROOT}/src/Utils/CryptoHash.cpp")
    target_compile_features(nr_provider_floor_contract PRIVATE cxx_std_23)
    target_include_directories(nr_provider_floor_contract PRIVATE "${REPOSITORY_ROOT}/src")
    target_compile_definitions(nr_provider_floor_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(nr_provider_floor_contract PRIVATE /W4 /EHsc /permissive-)
    target_link_libraries(nr_provider_floor_contract PRIVATE bcrypt)
    add_test(NAME nr_provider_floor_contract COMMAND nr_provider_floor_contract)
    add_executable(nr_kernel_chain_command_list test_kernel_chain_command_list.cpp)
    target_include_directories(nr_kernel_chain_command_list PRIVATE "${REPOSITORY_ROOT}/src")
    target_compile_features(nr_kernel_chain_command_list PRIVATE cxx_std_23)
    target_compile_definitions(nr_kernel_chain_command_list PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(nr_kernel_chain_command_list PRIVATE /W4 /EHsc /permissive-)
    add_test(NAME nr_kernel_chain_command_list COMMAND nr_kernel_chain_command_list)
    add_executable(nr_kernel_command_capture test_kernel_command_capture.cpp)
    target_compile_features(nr_kernel_command_capture PRIVATE cxx_std_23)
    target_compile_definitions(nr_kernel_command_capture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(nr_kernel_command_capture PRIVATE /W4 /EHsc /permissive-)
    target_link_libraries(nr_kernel_command_capture PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME nr_kernel_command_capture COMMAND nr_kernel_command_capture)
    add_executable(nr_provider_kernel_chain test_provider_kernel_chain.cpp "${REPOSITORY_ROOT}/src/Utils/CryptoHash.cpp")
    target_compile_features(nr_provider_kernel_chain PRIVATE cxx_std_23)
    target_include_directories(nr_provider_kernel_chain PRIVATE "${REPOSITORY_ROOT}/src" "${CSX_NR_DEPENDENCY_ROOT}/extern/nvapi")
    target_compile_definitions(nr_provider_kernel_chain PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(nr_provider_kernel_chain PRIVATE /W4 /EHsc /permissive-)
    target_link_libraries(nr_provider_kernel_chain PRIVATE nlohmann_json::nlohmann_json bcrypt psapi)
    add_test(NAME nr_provider_kernel_chain COMMAND nr_provider_kernel_chain)
endif()
