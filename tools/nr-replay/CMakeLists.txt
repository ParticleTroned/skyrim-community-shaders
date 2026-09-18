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
# Only dependency includes differ; all runtime admission and execution code is
# compiled from the current production sources, with configure dependencies.
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
    file(WRITE "${OUTPUT_DIRECTORY}/${_unit}.cpp" "${_contents}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_nr_source}/${_unit}.cpp")
endforeach()
set(_replay_identity "{}")
foreach(_source IN ITEMS
    tools/nr-replay/main.cpp
    tools/nr-replay/replay_platform.h
    tools/nr-replay/CMakeLists.txt
    tools/nr-replay/extract_runtime_utilities.cmake
    tests/extract_d3d_resource_naming.cmake
    src/Utils/CryptoHash.cpp
    src/Utils/CryptoHash.h)
    file(SHA256 "${REPOSITORY_ROOT}/${_source}" _source_hash)
    string(JSON _replay_identity SET "${_replay_identity}" "${_source}" "\"${_source_hash}\"")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPOSITORY_ROOT}/${_source}")
endforeach()
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
add_executable(csx_nr_replay main.cpp "${OUTPUT_DIRECTORY}/Runtime.cpp"
    "${OUTPUT_DIRECTORY}/D3D12Interop.cpp" "${OUTPUT_DIRECTORY}/runtime_utilities.cpp"
    "${REPOSITORY_ROOT}/src/Utils/CryptoHash.cpp")
target_compile_features(csx_nr_replay PRIVATE cxx_std_23)
target_compile_definitions(csx_nr_replay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_include_directories(csx_nr_replay PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${OUTPUT_DIRECTORY}"
    "${REPOSITORY_ROOT}/src" "${_nr_source}"
    "${CSX_NR_DEPENDENCY_ROOT}/extern/CommonLibSSE-NG/include"
    "${CSX_NR_DEPENDENCY_ROOT}/extern/Streamline-DX12/external/ngx-sdk/include")
target_compile_options(csx_nr_replay PRIVATE /W4 /EHsc /permissive- /FIreplay_platform.h)
target_link_libraries(csx_nr_replay PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog
    d3d11 d3d12 dxgi dxguid bcrypt wintrust version psapi)
