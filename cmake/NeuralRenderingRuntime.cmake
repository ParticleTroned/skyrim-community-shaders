# NR is an optional provider alongside the target branch's normal DLSS runtime.
set(CSX_LOCAL_DLSSNR_RUNTIME_FILE "" CACHE FILEPATH
    "Optional user-supplied 310.8 Neural Rendering runtime")
if(NOT STREAMLINE_RUNTIME_DIRECTORY)
    message(FATAL_ERROR "The standard Streamline runtime must be configured before NR")
endif()
cmake_path(IS_PREFIX CMAKE_CURRENT_BINARY_DIR "${STREAMLINE_RUNTIME_DIRECTORY}" NORMALIZE _nr_inside_build)
if(NOT _nr_inside_build OR STREAMLINE_RUNTIME_DIRECTORY STREQUAL CMAKE_CURRENT_BINARY_DIR)
    message(FATAL_ERROR "NR staging must remain inside the isolated build directory")
endif()
set(_nr_destination "${STREAMLINE_RUNTIME_DIRECTORY}/nvngx_dlssnr.dll")
if(NOT CSX_LOCAL_DLSSNR_RUNTIME_FILE)
    # This exact generated file must not survive an explicit provider removal.
    file(REMOVE "${_nr_destination}")
    return()
endif()
if(NOT EXISTS "${CSX_LOCAL_DLSSNR_RUNTIME_FILE}" OR IS_DIRECTORY "${CSX_LOCAL_DLSSNR_RUNTIME_FILE}")
    message(FATAL_ERROR "CSX_LOCAL_DLSSNR_RUNTIME_FILE must identify a runtime file")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CSX_LOCAL_DLSSNR_RUNTIME_FILE}")
file(SHA256 "${CSX_LOCAL_DLSSNR_RUNTIME_FILE}" _nr_hash)
file(COPY_FILE "${CSX_LOCAL_DLSSNR_RUNTIME_FILE}" "${_nr_destination}" ONLY_IF_DIFFERENT)
list(APPEND STREAMLINE_RUNTIME_FILES "${_nr_destination}")

set(_nr_root "${CMAKE_CURRENT_BINARY_DIR}/neural-runtime")
file(MAKE_DIRECTORY "${_nr_root}")
set(_nr_manifest "${_nr_root}/pinned-runtime-manifest.txt")
set(_nr_manifest_contents "nvngx_dlssnr.dll|${CSX_LOCAL_DLSSNR_RUNTIME_FILE}|${_nr_hash}\n")
foreach(_normal_runtime IN LISTS STREAMLINE_RUNTIME_FILES)
    get_filename_component(_normal_name "${_normal_runtime}" NAME)
    if(_normal_name MATCHES "\\.dll$" AND NOT _normal_name STREQUAL "nvngx_dlssnr.dll")
        file(SHA256 "${_normal_runtime}" _normal_hash)
        string(APPEND _nr_manifest_contents "${_normal_name}|${_normal_runtime}|${_normal_hash}\n")
    endif()
endforeach()
file(WRITE "${_nr_manifest}" "${_nr_manifest_contents}")
file(SHA256 "${_nr_manifest}" _nr_manifest_hash)
set(_nr_verify "${_nr_root}/VerifyNeuralRuntime.cmake")
file(GENERATE OUTPUT "${_nr_verify}" CONTENT
    "set(STAGE_PINNED_RUNTIME_MANIFEST [==[${_nr_manifest}]==])\nset(STAGE_PINNED_RUNTIME_MANIFEST_SHA256 [==[${_nr_manifest_hash}]==])\nset(STAGE_PINNED_RUNTIME_DESTINATION [==[${STREAMLINE_RUNTIME_DIRECTORY}]==])\ninclude([==[${CMAKE_CURRENT_LIST_DIR}/StagePinnedLocalRuntime.cmake]==])\n")
add_custom_target(VerifyNeuralRuntime
    COMMAND "${CMAKE_COMMAND}" -P "${_nr_verify}"
    DEPENDS "${CSX_LOCAL_DLSSNR_RUNTIME_FILE}" "${_nr_manifest}"
    VERBATIM)
add_dependencies(${PROJECT_NAME} VerifyNeuralRuntime)
