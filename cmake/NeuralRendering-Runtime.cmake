include("${CMAKE_CURRENT_LIST_DIR}/NeuralRenderingSDKPin.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/RuntimePayload.cmake")
file(SHA256 "${CMAKE_SOURCE_DIR}/extern/NeuralRenderingSDK/LICENSE.txt" _nr_license_hash)
if(NOT _nr_license_hash STREQUAL NEURAL_RENDERING_SDK_LICENSE_SHA256)
    message(FATAL_ERROR "NVIDIA SDK license differs from its original")
endif()

set(NEURAL_RENDERING_SDK_WORK_ROOT "${CMAKE_CURRENT_BINARY_DIR}/neural-rendering-sdk")
set(NEURAL_RENDERING_SDK_ARCHIVE
    "${NEURAL_RENDERING_SDK_WORK_ROOT}/${NEURAL_RENDERING_SDK_PACKAGE}")
set(NEURAL_RENDERING_SDK_EXTRACT_ROOT "${NEURAL_RENDERING_SDK_WORK_ROOT}/sdk")
set(_nr_extract_stamp "${NEURAL_RENDERING_SDK_EXTRACT_ROOT}/.archive-sha256")
csx_prepare_runtime_asset("${NEURAL_RENDERING_SDK_URL}"
    "${NEURAL_RENDERING_SDK_ARCHIVE}" "${NEURAL_RENDERING_SDK_ARCHIVE_SHA256}"
    _nr_archive_available)

set(_nr_extracted FALSE)
if(EXISTS "${_nr_extract_stamp}")
    file(READ "${_nr_extract_stamp}" _nr_extracted_hash)
    string(STRIP "${_nr_extracted_hash}" _nr_extracted_hash)
    if(_nr_extracted_hash STREQUAL NEURAL_RENDERING_SDK_ARCHIVE_SHA256)
        set(_nr_extracted TRUE)
    endif()
endif()
if(NOT SKIP_RUNTIME_DOWNLOADS)
    file(ARCHIVE_EXTRACT INPUT "${NEURAL_RENDERING_SDK_ARCHIVE}"
        DESTINATION "${NEURAL_RENDERING_SDK_EXTRACT_ROOT}"
        PATTERNS "LICENSE.txt" "include/*" "lib/Windows_x86_64/rel/*")
    file(WRITE "${_nr_extract_stamp}" "${NEURAL_RENDERING_SDK_ARCHIVE_SHA256}\n")
    set(_nr_extracted TRUE)
endif()

set(NEURAL_RENDERING_SDK_INCLUDE_DIR "${NEURAL_RENDERING_SDK_EXTRACT_ROOT}/include")
set(_nr_header "${NEURAL_RENDERING_SDK_INCLUDE_DIR}/nvsdk_ngx_defs_dlssnr.h")
if(NOT _nr_archive_available OR NOT _nr_extracted OR NOT EXISTS "${_nr_header}")
    message(FATAL_ERROR
        "Official NR SDK headers are unavailable. Configure once with SKIP_RUNTIME_DOWNLOADS=OFF "
        "to populate the verified SDK cache before offline DLL development.")
endif()
file(SHA256 "${_nr_header}" _nr_header_hash)
if(NOT _nr_header_hash STREQUAL NEURAL_RENDERING_SDK_HEADER_SHA256)
    message(FATAL_ERROR "NR SDK parameter header differs from the pinned original")
endif()

foreach(_entry IN ITEMS
        "lib/Windows_x86_64/rel/nvngx_dlssnr.dll|nvngx_dlssnr.dll"
        "LICENSE.txt|nvngx_dlssnr.license.txt")
    string(REPLACE "|" ";" _paths "${_entry}")
    list(GET _paths 0 _relative)
    list(GET _paths 1 _name)
    set(_source "${NEURAL_RENDERING_SDK_EXTRACT_ROOT}/${_relative}")
    set(_destination "${STREAMLINE_RUNTIME_DIRECTORY}/${_name}")
    list(APPEND STREAMLINE_RUNTIME_PAYLOAD_FILES "${_destination}")
    if(NOT _nr_archive_available OR NOT _nr_extracted OR NOT EXISTS "${_source}")
        if(NOT SKIP_RUNTIME_DOWNLOADS)
            message(FATAL_ERROR "Missing official NR SDK payload: ${_relative}")
        endif()
        list(APPEND STREAMLINE_RUNTIME_PAYLOAD_MISSING "${_destination}")
        continue()
    endif()
    if(_name STREQUAL "nvngx_dlssnr.dll")
        file(SHA256 "${_source}" _hash)
        if(NOT _hash STREQUAL NEURAL_RENDERING_RUNTIME_SHA256)
            message(FATAL_ERROR "NR runtime differs from the pinned official SDK")
        endif()
    else()
        file(SHA256 "${_source}" _hash)
        if(NOT _hash STREQUAL NEURAL_RENDERING_SDK_LICENSE_SHA256)
            message(FATAL_ERROR "NR SDK license differs from the pinned original")
        endif()
    endif()
    file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
    list(APPEND STREAMLINE_RUNTIME_FILES "${_destination}")
endforeach()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${NEURAL_RENDERING_SDK_INCLUDE_DIR}")
target_link_libraries(${PROJECT_NAME} PRIVATE NgxSdk)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    CSX_NR_RUNTIME_SHA256="${NEURAL_RENDERING_RUNTIME_SHA256}")
