include("${CMAKE_CURRENT_LIST_DIR}/NeuralRenderingSDKPin.cmake")
set(NEURAL_RENDERING_REFERENCE_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/neural-provider")
configure_file("${CMAKE_CURRENT_LIST_DIR}/NeuralRenderingReference.h.in"
    "${NEURAL_RENDERING_REFERENCE_INCLUDE_DIR}/NeuralRenderingReference.generated.h" @ONLY)
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
        PATTERNS "LICENSE.txt" "include/*")
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

# NR object code is user-supplied; only the original license is a staging input.
set(_nr_license "${STREAMLINE_RUNTIME_DIRECTORY}/nvngx_dlssnr.license.txt")
file(COPY_FILE "${CMAKE_SOURCE_DIR}/extern/NeuralRenderingSDK/LICENSE.txt"
    "${_nr_license}" ONLY_IF_DIFFERENT)
list(APPEND STREAMLINE_RUNTIME_PAYLOAD_FILES "${_nr_license}")
list(APPEND STREAMLINE_RUNTIME_FILES "${_nr_license}")

# Reject stale payloads before any build can copy them into AIO or deployment inputs.
block()
    set(NR_PACKAGE_ROOT "${STREAMLINE_RUNTIME_FEATURE_ROOT}")
    include("${CMAKE_CURRENT_LIST_DIR}/VerifyNeuralRenderingPayload.cmake")
endblock()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${NEURAL_RENDERING_SDK_INCLUDE_DIR}" "${NEURAL_RENDERING_REFERENCE_INCLUDE_DIR}")
target_link_libraries(${PROJECT_NAME} PRIVATE NgxSdk)
