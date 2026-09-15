# Refresh provenance immediately before every DLL compilation. This is an
# always-run target by design: configure-time git metadata becomes stale after
# a checkout, submodule update, or local edit in an existing build directory.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_CSX_SUPPORTED_RUNTIMES)
foreach(_runtime IN ITEMS SE AE VR)
    if(ENABLE_SKYRIM_${_runtime})
        list(APPEND _CSX_SUPPORTED_RUNTIMES "${_runtime}")
    endif()
endforeach()
list(JOIN _CSX_SUPPORTED_RUNTIMES ";" _CSX_PROVENANCE_RUNTIME)
set(CSX_PROVENANCE_SCRIPT "${CMAKE_SOURCE_DIR}/tools/build_provenance.py")
set(CSX_PROVENANCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildProvenance/$<CONFIG>")
set(CSX_PROVENANCE_HEADER "${CSX_PROVENANCE_DIR}/BuildProvenance.generated.h")
set(CSX_PROVENANCE_BASE_MANIFEST "${CSX_PROVENANCE_DIR}/CSX.BuildManifest.base.json")
set(CSX_PROVENANCE_FINAL_MANIFEST "$<TARGET_FILE_DIR:${PROJECT_NAME}>/CSX.BuildManifest.json")
set(_CSX_PROVENANCE_CLEAN_ARG)
if(CSX_REQUIRE_CLEAN_PROVENANCE)
    set(_CSX_PROVENANCE_CLEAN_ARG --require-clean)
endif()
add_custom_target(
    refresh_build_provenance
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CSX_PROVENANCE_DIR}"
    COMMAND
        "${Python3_EXECUTABLE}" "${CSX_PROVENANCE_SCRIPT}" generate
        --source-dir "${CMAKE_SOURCE_DIR}"
        --output-header "${CSX_PROVENANCE_HEADER}"
        --output-manifest "${CSX_PROVENANCE_BASE_MANIFEST}"
        --runtime "${_CSX_PROVENANCE_RUNTIME}"
        --plugin-version "${PROJECT_VERSION}"
        --configuration "$<CONFIG>"
        --artifact-name "$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        --compiler-id "${CMAKE_CXX_COMPILER_ID}"
        --compiler-version "${CMAKE_CXX_COMPILER_VERSION}"
        --compiler-path "${CMAKE_CXX_COMPILER}"
        --generator "${CMAKE_GENERATOR}"
        --generator-platform "${CMAKE_GENERATOR_PLATFORM}"
        --generator-toolset "${CMAKE_GENERATOR_TOOLSET}"
        --windows-sdk-version "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
        --cmake-version "${CMAKE_VERSION}"
        --target-triplet "${VCPKG_TARGET_TRIPLET}"
        --toolchain-file "${CMAKE_TOOLCHAIN_FILE}"
        --overlay-dir "${VCPKG_OVERLAY_PORTS}"
        --shader-contract-file "include/FeatureVersions.h"
        --shader-contract-file "src/ShaderCache.cpp"
        --shader-contract-file "src/ShaderCache.h"
        --build-option "DEVBENCH_BRIDGE=${DEVBENCH_BRIDGE}"
        --build-option "TRACY_SUPPORT=${TRACY_SUPPORT}"
        --build-option "ENABLE_SKYRIM_AE=${ENABLE_SKYRIM_AE}"
        --build-option "ENABLE_SKYRIM_SE=${ENABLE_SKYRIM_SE}"
        --build-option "ENABLE_SKYRIM_VR=${ENABLE_SKYRIM_VR}"
        --build-option "MSVC_RUNTIME=${CMAKE_MSVC_RUNTIME_LIBRARY}"
        ${_CSX_PROVENANCE_CLEAN_ARG}
    VERBATIM
    COMMENT "Refreshing canonical CSX build provenance"
)
add_dependencies(${PROJECT_NAME} refresh_build_provenance)
target_include_directories(${PROJECT_NAME} PRIVATE "${CSX_PROVENANCE_DIR}")
add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND
        "${Python3_EXECUTABLE}" "${CSX_PROVENANCE_SCRIPT}" finalize
        --base-manifest "${CSX_PROVENANCE_BASE_MANIFEST}"
        --artifact "$<TARGET_FILE:${PROJECT_NAME}>"
        --output-manifest "${CSX_PROVENANCE_FINAL_MANIFEST}"
    VERBATIM
    COMMENT "Binding CSX build manifest to linked DLL"
)
