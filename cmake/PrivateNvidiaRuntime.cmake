option(
    CSX_ENABLE_PRIVATE_NVIDIA_RUNTIME
    "Enable hash-pinned, local-only DLSS Neural Rendering runtime staging"
    OFF
)
set(
    CSX_PRIVATE_STREAMLINE_213_DIR
    ""
    CACHE PATH
    "Local directory containing the six pinned Streamline 2.13 DLSS files"
)
set(
    CSX_PRIVATE_DLSSNR_DLL
    ""
    CACHE FILEPATH
    "Local path to the pinned nvngx_dlssnr.dll"
)
set(
    CSX_PRIVATE_SHADER_CACHE_ROOT
    ""
    CACHE PATH
    "Optional shared SE ShaderCache root used to generate a private FOMOD AIO"
)
mark_as_advanced(
    CSX_PRIVATE_STREAMLINE_213_DIR
    CSX_PRIVATE_DLSSNR_DLL
    CSX_PRIVATE_SHADER_CACHE_ROOT
)

set(CSX_PRIVATE_RUNTIME_PAYLOAD "")
set(CSX_PRIVATE_RUNTIME_FILES "")
set(CSX_PRIVATE_AIO_TARGET "")
if(NOT CSX_ENABLE_PRIVATE_NVIDIA_RUNTIME)
    message(STATUS "Private NVIDIA runtime staging is disabled")
    return()
endif()

if(NOT WIN32)
    message(FATAL_ERROR "Private NVIDIA runtime validation requires Windows")
endif()
if(NOT IS_DIRECTORY "${CSX_PRIVATE_STREAMLINE_213_DIR}")
    message(FATAL_ERROR "The private Streamline 2.13 directory is not configured")
endif()
if(NOT EXISTS "${CSX_PRIVATE_DLSSNR_DLL}")
    message(FATAL_ERROR "The private nvngx_dlssnr.dll is not configured")
endif()
if(NOT CSX_PRIVATE_AIO_LABEL MATCHES [[^(Vincent|Gogh)-se$]])
    message(FATAL_ERROR "CSX_PRIVATE_AIO_LABEL must be Vincent-se or Gogh-se")
endif()

set(
    CSX_PRIVATE_RUNTIME_CONTRACT
    "${CMAKE_SOURCE_DIR}/cmake/PrivateNvidiaRuntime.json"
)
set(
    CSX_PRIVATE_RUNTIME_NOTICE
    "${CMAKE_SOURCE_DIR}/cmake/INTERNAL-NVIDIA-RUNTIME-NOTICE.txt"
)
set(CSX_PRIVATE_AIO_SCRIPT "${CMAKE_SOURCE_DIR}/tools/private_aio.py")
set(
    CSX_PRIVATE_RUNTIME_PAYLOAD
    "${CMAKE_CURRENT_BINARY_DIR}/private-runtime/payload"
)
set(_private_streamline_names
    nvngx_dlss.dll
    sl.common.dll
    sl.dlss.dll
    sl.interposer.dll
    sl.pcl.dll
    sl.reflex.dll
)
set(_private_runtime_sources "${CSX_PRIVATE_DLSSNR_DLL}")
foreach(_runtime_name IN LISTS _private_streamline_names)
    set(_runtime_source "${CSX_PRIVATE_STREAMLINE_213_DIR}/${_runtime_name}")
    if(NOT EXISTS "${_runtime_source}")
        message(FATAL_ERROR "The private runtime set is missing ${_runtime_name}")
    endif()
    list(APPEND _private_runtime_sources "${_runtime_source}")
endforeach()
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS ${_private_runtime_sources}
)

execute_process(
    COMMAND
        "${Python3_EXECUTABLE}" -B "${CSX_PRIVATE_AIO_SCRIPT}" stage-runtime
        --contract "${CSX_PRIVATE_RUNTIME_CONTRACT}" --runtime-dir
        "${CSX_PRIVATE_STREAMLINE_213_DIR}" --nr-file
        "${CSX_PRIVATE_DLSSNR_DLL}" --destination
        "${CSX_PRIVATE_RUNTIME_PAYLOAD}" --local-root
        "${CMAKE_CURRENT_BINARY_DIR}" --notice "${CSX_PRIVATE_RUNTIME_NOTICE}"
    RESULT_VARIABLE _private_runtime_result
    OUTPUT_VARIABLE _private_runtime_output
    ERROR_VARIABLE _private_runtime_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT _private_runtime_result EQUAL 0)
    if(_private_runtime_error STREQUAL "")
        set(_private_runtime_error "validation failed without a diagnostic")
    endif()
    message(FATAL_ERROR "Private NVIDIA runtime rejected: ${_private_runtime_error}")
endif()
message(STATUS "${_private_runtime_output}")

foreach(_runtime_name IN LISTS _private_streamline_names)
    list(
        APPEND
        CSX_PRIVATE_RUNTIME_FILES
        "${CSX_PRIVATE_RUNTIME_PAYLOAD}/Shaders/Upscaling/Streamline/${_runtime_name}"
    )
endforeach()
list(
    APPEND
    CSX_PRIVATE_RUNTIME_FILES
    "${CSX_PRIVATE_RUNTIME_PAYLOAD}/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll"
    "${CSX_PRIVATE_RUNTIME_PAYLOAD}/CSX.PrivateRuntime.json"
    "${CSX_PRIVATE_RUNTIME_PAYLOAD}/INTERNAL-NO-REDISTRIBUTION.txt"
)

function(csx_add_private_aio_target)
    if(NOT CSX_ENABLE_PRIVATE_NVIDIA_RUNTIME)
        return()
    endif()
    if(NOT TARGET AIO)
        message(FATAL_ERROR "The private AIO target requires the normal AIO target")
    endif()

    set(_private_output_root "${CMAKE_CURRENT_BINARY_DIR}/local-artifacts")
    set(
        _private_archive
        "${_private_output_root}/CSX_AIO-${CSX_PRIVATE_AIO_LABEL}-INTERNAL-NO-REDISTRIBUTION.7z"
    )
    set(_private_cache_args "")
    set(_private_cache_files "")
    if(NOT "${CSX_PRIVATE_SHADER_CACHE_ROOT}" STREQUAL "")
        if(NOT IS_DIRECTORY "${CSX_PRIVATE_SHADER_CACHE_ROOT}")
            message(FATAL_ERROR "The shared private shader-cache root is invalid")
        endif()
        list(
            APPEND
            _private_cache_args
            --shader-cache-root
            "${CSX_PRIVATE_SHADER_CACHE_ROOT}"
        )
        file(
            GLOB_RECURSE
            _private_cache_files
            LIST_DIRECTORIES FALSE
            CONFIGURE_DEPENDS
            "${CSX_PRIVATE_SHADER_CACHE_ROOT}/*"
        )
    endif()

    add_custom_command(
        OUTPUT "${_private_archive}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_private_output_root}"
        COMMAND
            "${Python3_EXECUTABLE}" -B "${CSX_PRIVATE_AIO_SCRIPT}"
            assemble-aio --contract "${CSX_PRIVATE_RUNTIME_CONTRACT}"
            --source-root "${CMAKE_SOURCE_DIR}" --build-root
            "${CMAKE_CURRENT_BINARY_DIR}" --base-aio "${AIO_DIR}"
            --private-runtime "${CSX_PRIVATE_RUNTIME_PAYLOAD}"
            --output-root "${_private_output_root}" --label
            "${CSX_PRIVATE_AIO_LABEL}" --cmake "${CMAKE_COMMAND}"
            ${_private_cache_args}
        DEPENDS
            AIO
            ${PROJECT_NAME}
            ${CSX_PRIVATE_RUNTIME_FILES}
            ${_private_cache_files}
            "${CSX_PRIVATE_RUNTIME_CONTRACT}"
            "${CSX_PRIVATE_RUNTIME_NOTICE}"
            "${CSX_PRIVATE_AIO_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/tools/build-shader-cache.py"
        COMMENT
            "Creating local-only ${CSX_PRIVATE_AIO_LABEL} AIO outside dist"
        VERBATIM
    )
    set(_private_target "Package-Private-AIO-${CSX_PRIVATE_AIO_LABEL}")
    add_custom_target("${_private_target}" DEPENDS "${_private_archive}")
    set(CSX_PRIVATE_AIO_TARGET "${_private_target}" PARENT_SCOPE)
    set(CSX_PRIVATE_AIO_ARCHIVE "${_private_archive}" PARENT_SCOPE)
endfunction()
