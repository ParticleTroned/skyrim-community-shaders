cmake_minimum_required(VERSION 4.2)

foreach(_required IN ITEMS PROJECT_ROOT TEST_ROOT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()
cmake_path(ABSOLUTE_PATH PROJECT_ROOT NORMALIZE)
cmake_path(ABSOLUTE_PATH TEST_ROOT NORMALIZE)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _run)
set(_root "${TEST_ROOT}/${_run}")
file(MAKE_DIRECTORY "${_root}")

function(run_cmake _expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
    )
    if(_expected STREQUAL "pass")
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "CMake failed: ${ARGN}\n${_out}${_err}")
        endif()
    elseif(_result EQUAL 0 OR NOT "${_out}${_err}" MATCHES "${_expected}")
        message(
            FATAL_ERROR
            "Expected failure '${_expected}': ${ARGN}\n${_out}${_err}"
        )
    endif()
endfunction()

function(expect_content _path _expected)
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing ${_path}")
    endif()
    file(READ "${_path}" _actual)
    if(NOT _actual STREQUAL _expected)
        message(FATAL_ERROR "Unexpected content in ${_path}: ${_actual}")
    endif()
endfunction()

function(expect_absent _path)
    if(EXISTS "${_path}")
        message(FATAL_ERROR "Unexpected file: ${_path}")
    endif()
endfunction()

include("${PROJECT_ROOT}/cmake/RuntimePayload.cmake")
set(SKIP_RUNTIME_DOWNLOADS ON)
set(_asset "${_root}/cached.dll")
file(WRITE "${_asset}" "verified fixture")
file(SHA256 "${_asset}" _hash)
csx_prepare_runtime_asset("invalid://must-not-fetch" "${_asset}" "${_hash}" _available)
if(NOT _available)
    message(FATAL_ERROR "Verified cached asset was not reused")
endif()
file(WRITE "${_asset}" "different cached fixture")
csx_prepare_runtime_asset("invalid://must-not-fetch" "${_asset}" "${_hash}" _available)
if(_available)
    message(FATAL_ERROR "Unverified asset was accepted")
endif()
expect_content("${_asset}" "different cached fixture")
csx_prepare_runtime_asset("invalid://must-not-fetch" "${_root}/absent.dll" "${_hash}" _available)
if(_available)
    message(FATAL_ERROR "Absent asset was accepted")
endif()
expect_absent("${_root}/absent.dll")

# Exercise the ordinary verified downloader with a local URL, without network.
set(SKIP_RUNTIME_DOWNLOADS OFF)
file(WRITE "${_root}/source.dll" "verified fixture")
unset(ENV{CSX_ASSET_DOWNLOADER_PYTHON})
unset(ENV{CSX_ASSET_DOWNLOADER_SCRIPT})
csx_prepare_runtime_asset("file://${_root}/source.dll" "${_asset}" "${_hash}" _available)
if(NOT _available)
    message(FATAL_ERROR "Ordinary asset download failed")
endif()
expect_content("${_asset}" "verified fixture")

function(check_runtime_modules)
    set(CMAKE_CURRENT_BINARY_DIR "${_root}/payload")
    set(SKIP_RUNTIME_DOWNLOADS ON)
    set(BUILD_CONTROLLER_TESTS OFF)
    include("${PROJECT_ROOT}/cmake/FidelityFX-Runtime.cmake")
    include("${PROJECT_ROOT}/cmake/Streamline-Runtime.cmake")
    list(LENGTH FFX_RUNTIME_PAYLOAD_MISSING _ffx_count)
    list(LENGTH STREAMLINE_RUNTIME_PAYLOAD_MISSING _streamline_count)
    if(
        NOT _ffx_count EQUAL 3
        OR NOT _streamline_count EQUAL 11
        OR FFX_RUNTIME_FILES
        OR STREAMLINE_RUNTIME_FILES
    )
        message(
            FATAL_ERROR
            "Cold skip mode must retain all 14 expected payload paths"
        )
    endif()

    # A stale payload and matching stamp cannot make an invalid archive trusted.
    file(
        WRITE "${FFX_RUNTIME_DIRECTORY}/amd_fidelityfx_loader_dx12.dll"
        "old FFX"
    )
    file(WRITE "${STREAMLINE_RUNTIME_ARCHIVE}" "invalid archive")
    file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/bin/x64")
    file(
        WRITE "${STREAMLINE_RUNTIME_EXTRACT_STAMP}"
        "${STREAMLINE_RUNTIME_ARCHIVE_SHA256}\n"
    )
    file(
        WRITE "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/bin/x64/sl.common.dll"
        "unverified SDK"
    )
    file(
        WRITE "${STREAMLINE_RUNTIME_DIRECTORY}/sl.common.dll"
        "old staged runtime"
    )
    include("${PROJECT_ROOT}/cmake/FidelityFX-Runtime.cmake")
    include("${PROJECT_ROOT}/cmake/Streamline-Runtime.cmake")
    list(LENGTH FFX_RUNTIME_PAYLOAD_MISSING _ffx_count)
    list(LENGTH STREAMLINE_RUNTIME_PAYLOAD_MISSING _streamline_count)
    if(
        NOT _ffx_count EQUAL 3
        OR NOT _streamline_count EQUAL 11
        OR FFX_RUNTIME_FILES
        OR STREAMLINE_RUNTIME_FILES
    )
        message(FATAL_ERROR "Stale or unverified runtime payload was accepted")
    endif()
    expect_content("${FFX_RUNTIME_DIRECTORY}/amd_fidelityfx_loader_dx12.dll" "old FFX")
    expect_content("${STREAMLINE_RUNTIME_ARCHIVE}" "invalid archive")
    expect_content("${STREAMLINE_RUNTIME_DIRECTORY}/sl.common.dll" "old staged runtime")
    set(AIO_ZIP_TO_DIST OFF)
    set(ZIP_TO_DIST OFF)
    csx_configure_runtime_payload()
endfunction()
check_runtime_modules()

# A language-free fixture checks install ordering without building any target.
set(_fixture "${_root}/install project")
file(MAKE_DIRECTORY "${_fixture}")
file(
    WRITE "${_fixture}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 4.2)\nproject(RuntimeInstall NONE)\n"
    "install(SCRIPT \"${_root}/payload/runtime_payload_install_guard.cmake\" ALL_COMPONENTS)\n"
    "install(CODE \"file(WRITE \\\"${_root}/install-mutated\\\" \\\"changed\\\")\")\n"
    "install(CODE \"file(WRITE \\\"${_root}/skse-installed\\\" \\\"ok\\\")\" COMPONENT SKSE)\n"
)
run_cmake(pass -S "${_fixture}" -B "${_root}/install build")
run_cmake("FidelityFX runtime payload" --install "${_root}/install build")
expect_absent("${_root}/install-mutated")
run_cmake("FidelityFX runtime payload" --install "${_root}/install build" --component FidelityFXRuntime)
run_cmake("Streamline runtime payload" --install "${_root}/install build" --component StreamlineRuntime)
run_cmake(pass --install "${_root}/install build" --component SKSE)
expect_content("${_root}/skse-installed" "ok")
expect_absent("${_root}/install-mutated")

file(
    WRITE "${_root}/package-policy.cmake"
    "include(\"${PROJECT_ROOT}/cmake/RuntimePayload.cmake\")\n"
    "set(FFX_RUNTIME_PAYLOAD_MISSING absent.dll)\n"
    "csx_configure_runtime_payload()\n"
)
run_cmake("Runtime payloads are missing" -DAIO_ZIP_TO_DIST=ON -DZIP_TO_DIST=OFF -P "${_root}/package-policy.cmake")
run_cmake("Runtime payloads are missing" -DAIO_ZIP_TO_DIST=OFF -DZIP_TO_DIST=ON -P "${_root}/package-policy.cmake")

set(CMAKE_CURRENT_BINARY_DIR "${_root}/payload")
set(FFX_RUNTIME_PAYLOAD_MISSING "")
set(STREAMLINE_RUNTIME_PAYLOAD_MISSING "")
set(FFX_RUNTIME_PAYLOAD_FILES "${_asset}")
set(STREAMLINE_RUNTIME_PAYLOAD_FILES "${_root}/sl-runtime.dll")
file(WRITE "${_root}/sl-runtime.dll" "verified SL fixture")
csx_configure_runtime_payload()
run_cmake(pass --install "${_root}/install build")
expect_content("${_root}/install-mutated" "changed")

# Recheck configured inputs before installation can reset existing staging.
file(WRITE "${_root}/install-mutated" "preserved")
file(WRITE "${_asset}" "modified after configure")
run_cmake("FidelityFX runtime payload" --install "${_root}/install build")
expect_content("${_root}/install-mutated" "preserved")
file(REMOVE "${_asset}")
run_cmake("FidelityFX runtime payload" --install "${_root}/install build" --component FidelityFXRuntime)
run_cmake(pass --install "${_root}/install build" --component SKSE)
expect_content("${_root}/install-mutated" "preserved")
file(WRITE "${_asset}" "verified fixture")
file(WRITE "${_root}/sl-runtime.dll" "modified after configure")
run_cmake("Streamline runtime payload" --install "${_root}/install build")
expect_content("${_root}/install-mutated" "preserved")
file(REMOVE "${_root}/sl-runtime.dll")
run_cmake("Streamline runtime payload" --install "${_root}/install build" --component StreamlineRuntime)
expect_content("${_root}/install-mutated" "preserved")
file(WRITE "${_root}/sl-runtime.dll" "verified SL fixture")
run_cmake(pass --install "${_root}/install build")
expect_content("${_root}/install-mutated" "changed")

set(_source "${_root}/cleanup source")
file(MAKE_DIRECTORY "${_source}/package/Shaders")
file(WRITE "${_source}/package/Shaders/Core.hlsl" "shader")
file(WRITE "${_root}/empty-paths.txt" "")
foreach(_mode IN ITEMS AIO SHADERS)
    set(_aio "${_root}/cleanup ${_mode}")
    file(
        MAKE_DIRECTORY
            "${_aio}/Shaders/Upscaling/FidelityFX"
            "${_aio}/Shaders/Upscaling/Streamline"
    )
    file(
        WRITE "${_aio}/Shaders/Upscaling/FidelityFX/runtime.dll"
        "FFX retained"
    )
    file(
        WRITE "${_aio}/Shaders/Upscaling/Streamline/license.txt"
        "notice retained"
    )
    file(WRITE "${_aio}/Shaders/stale.hlsl" "stale")
    set(_args
        "-DMODE=${_mode}"
        "-DAIO_DIR=${_aio}"
        "-DSOURCE_DIR=${_source}"
        "-DFEATURE_PATHS_FILE=${_root}/empty-paths.txt"
        "-DFEATURE_SHADER_PATHS_FILE=${_root}/empty-paths.txt"
    )
    run_cmake(pass ${_args} -DSKIP_RUNTIME_DOWNLOADS=ON -P "${PROJECT_ROOT}/cmake/CleanupStaleEntries.cmake")
    expect_content("${_aio}/Shaders/Upscaling/FidelityFX/runtime.dll" "FFX retained")
    expect_content("${_aio}/Shaders/Upscaling/Streamline/license.txt" "notice retained")
    expect_absent("${_aio}/Shaders/stale.hlsl")
    run_cmake(pass ${_args} -DSKIP_RUNTIME_DOWNLOADS=OFF -P "${PROJECT_ROOT}/cmake/CleanupStaleEntries.cmake")
    expect_absent("${_aio}/Shaders/Upscaling/FidelityFX/runtime.dll")
    expect_absent("${_aio}/Shaders/Upscaling/Streamline/license.txt")
endforeach()

set(_src "${_root}/deploy source")
set(_dst "${_root}/deploy destination")
set(_manifest "${_root}/deploy.manifest")
file(
    MAKE_DIRECTORY "${_src}/Upscaling/FidelityFX" "${_src}/Upscaling/Streamline"
)
file(WRITE "${_src}/Core.hlsl" "shader")
file(WRITE "${_src}/stale.hlsl" "stale")
file(WRITE "${_src}/modified.hlsl" "original")
file(WRITE "${_src}/Upscaling/FidelityFX/runtime.dll" "deployed FFX")
file(WRITE "${_src}/Upscaling/Streamline/runtime.dll" "deployed SL")
set(_deploy_args
    "-DSRC_DIR=${_src}"
    "-DDST_DIR=${_dst}"
    "-DMANIFEST_FILE=${_manifest}"
)
run_cmake(pass ${_deploy_args} -DSKIP_RUNTIME_DOWNLOADS=OFF -P "${PROJECT_ROOT}/cmake/SyncShaderDeploy.cmake")
file(WRITE "${_dst}/unowned.hlsl" "user file")
file(WRITE "${_dst}/modified.hlsl" "user edit")
file(REMOVE "${_src}/stale.hlsl" "${_src}/modified.hlsl")
file(WRITE "${_src}/Upscaling/FidelityFX/runtime.dll" "different staged FFX")
file(REMOVE "${_src}/Upscaling/Streamline/runtime.dll")
run_cmake(pass ${_deploy_args} -DSKIP_RUNTIME_DOWNLOADS=ON -P "${PROJECT_ROOT}/cmake/SyncShaderDeploy.cmake")
expect_content("${_dst}/Upscaling/FidelityFX/runtime.dll" "deployed FFX")
expect_content("${_dst}/Upscaling/Streamline/runtime.dll" "deployed SL")
expect_content("${_dst}/modified.hlsl" "user edit")
expect_content("${_dst}/unowned.hlsl" "user file")
expect_absent("${_dst}/stale.hlsl")
file(READ "${_manifest}" _ownership)
foreach(_owned IN ITEMS fidelityfx streamline)
    if(NOT _ownership MATCHES "upscaling/${_owned}/runtime.dll")
        message(FATAL_ERROR "Skip mode lost runtime ownership: ${_ownership}")
    endif()
endforeach()

# Returning to normal sync removes only unchanged stale owned runtime files.
file(REMOVE "${_src}/Upscaling/FidelityFX/runtime.dll")
file(WRITE "${_dst}/Upscaling/Streamline/runtime.dll" "user runtime edit")
run_cmake(pass ${_deploy_args} -DSKIP_RUNTIME_DOWNLOADS=OFF -P "${PROJECT_ROOT}/cmake/SyncShaderDeploy.cmake")
expect_absent("${_dst}/Upscaling/FidelityFX/runtime.dll")
expect_content("${_dst}/Upscaling/Streamline/runtime.dll" "user runtime edit")
expect_content("${_dst}/unowned.hlsl" "user file")

include("${PROJECT_ROOT}/cmake/PreservedRuntimePaths.cmake")
set(SKIP_RUNTIME_DOWNLOADS ON)
csx_is_preserved_runtime_path("UPSCALING\\STREAMLINE\\license.txt" _preserve)
csx_is_preserved_runtime_path("Upscaling/StreamlineOther/runtime.dll" _other)
if(NOT _preserve OR _other)
    message(
        FATAL_ERROR
        "Runtime preservation must be case-insensitive and directory-bound"
    )
endif()
message(
    STATUS
    "Runtime download, packaging and deploy policy checks passed: ${_root}"
)
