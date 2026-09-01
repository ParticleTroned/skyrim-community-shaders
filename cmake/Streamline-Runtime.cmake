set(STREAMLINE_RUNTIME_VERSION "2.12.0")
set(STREAMLINE_RUNTIME_ARCHIVE_SHA256
    "F5C0A3D870707DDDC3570FB4BCD3655CF48A8A68C3A9D342910CFA21B77DCF48"
)
set(STREAMLINE_RUNTIME_ARCHIVE_URL
    "https://github.com/NVIDIA-RTX/Streamline/releases/download/v${STREAMLINE_RUNTIME_VERSION}/streamline-sdk-v${STREAMLINE_RUNTIME_VERSION}.zip"
)

include("${CMAKE_CURRENT_LIST_DIR}/CsxDownload.cmake")

set(STREAMLINE_RUNTIME_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/streamline-runtime"
)
set(STREAMLINE_RUNTIME_ARCHIVE
    "${STREAMLINE_RUNTIME_ROOT}/streamline-sdk-v${STREAMLINE_RUNTIME_VERSION}.zip"
)
set(STREAMLINE_RUNTIME_EXTRACT_ROOT
    "${STREAMLINE_RUNTIME_ROOT}/sdk"
)
set(STREAMLINE_RUNTIME_FEATURE_ROOT
    "${STREAMLINE_RUNTIME_ROOT}/payload"
)
set(STREAMLINE_RUNTIME_SHADER_ROOT
    "${STREAMLINE_RUNTIME_FEATURE_ROOT}/Shaders"
)
set(STREAMLINE_RUNTIME_RELATIVE_DIRECTORY
    "Shaders/Upscaling/Streamline"
)
set(STREAMLINE_RUNTIME_DIRECTORY
    "${STREAMLINE_RUNTIME_FEATURE_ROOT}/${STREAMLINE_RUNTIME_RELATIVE_DIRECTORY}"
)
set(STREAMLINE_DLSSG_RUNTIME_RELATIVE_DIRECTORY
    "Shaders/Upscaling/StreamlineDX12"
)
set(STREAMLINE_DLSSG_RUNTIME_DIRECTORY
    "${STREAMLINE_RUNTIME_FEATURE_ROOT}/${STREAMLINE_DLSSG_RUNTIME_RELATIVE_DIRECTORY}"
)

file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_ROOT}")
csx_download_verified_asset(
    "${STREAMLINE_RUNTIME_ARCHIVE_URL}"
    "${STREAMLINE_RUNTIME_ARCHIVE}"
    "${STREAMLINE_RUNTIME_ARCHIVE_SHA256}"
)

# Re-extract the verified archive so stale or modified SDK files can never be
# staged merely because a previous extraction stamp still matches the ZIP.
file(REMOVE_RECURSE "${STREAMLINE_RUNTIME_EXTRACT_ROOT}")
file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_EXTRACT_ROOT}")
file(
    ARCHIVE_EXTRACT
    INPUT "${STREAMLINE_RUNTIME_ARCHIVE}"
    DESTINATION "${STREAMLINE_RUNTIME_EXTRACT_ROOT}"
)

file(
    GLOB_RECURSE _streamline_archive_files
    LIST_DIRECTORIES FALSE
    "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/*"
)

function(stage_streamline_runtime _filename)
    set(_destination_directory "${STREAMLINE_RUNTIME_DIRECTORY}")
    if(ARGC GREATER 1)
        set(_destination_directory "${ARGV1}")
    endif()
    set(_production_matches "")
    foreach(_candidate IN LISTS _streamline_archive_files)
        get_filename_component(_candidate_name "${_candidate}" NAME)
        if(NOT _candidate_name STREQUAL _filename)
            continue()
        endif()

        get_filename_component(_candidate_directory "${_candidate}" DIRECTORY)
        get_filename_component(
            _candidate_directory_name
            "${_candidate_directory}"
            NAME
        )
        get_filename_component(
            _candidate_parent
            "${_candidate_directory}"
            DIRECTORY
        )
        get_filename_component(
            _candidate_parent_name
            "${_candidate_parent}"
            NAME
        )
        if(
            _candidate_directory_name STREQUAL "x64"
            AND _candidate_parent_name STREQUAL "bin"
        )
            list(APPEND _production_matches "${_candidate}")
        endif()
    endforeach()

    list(LENGTH _production_matches _production_match_count)
    if(NOT _production_match_count EQUAL 1)
        message(
            FATAL_ERROR
            "Expected one production ${_filename} in Streamline ${STREAMLINE_RUNTIME_VERSION}, found ${_production_match_count}"
        )
    endif()

    list(GET _production_matches 0 _source)
    set(_destination "${_destination_directory}/${_filename}")
    file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
    set(STREAMLINE_RUNTIME_FILES
        ${STREAMLINE_RUNTIME_FILES}
        "${_destination}"
        PARENT_SCOPE
    )
    if(
        "${_destination_directory}"
        STREQUAL "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}"
    )
        set(STREAMLINE_DLSSG_RUNTIME_FILES
            ${STREAMLINE_DLSSG_RUNTIME_FILES}
            "${_destination}"
            PARENT_SCOPE
        )
    else()
        set(STREAMLINE_DX11_RUNTIME_FILES
            ${STREAMLINE_DX11_RUNTIME_FILES}
            "${_destination}"
            PARENT_SCOPE
        )
    endif()
endfunction()

function(stage_streamline_notice _relative_path)
    set(_source "${STREAMLINE_RUNTIME_EXTRACT_ROOT}/${_relative_path}")
    if(NOT EXISTS "${_source}")
        message(
            FATAL_ERROR
            "Streamline ${STREAMLINE_RUNTIME_VERSION} archive is missing ${_relative_path}"
        )
    endif()

    get_filename_component(_name "${_source}" NAME)
    foreach(
        _destination_directory
        IN ITEMS
            "${STREAMLINE_RUNTIME_DIRECTORY}"
            "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}"
    )
        set(_destination "${_destination_directory}/${_name}")
        file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
        list(APPEND STREAMLINE_RUNTIME_FILES "${_destination}")
        if(
            "${_destination_directory}"
            STREQUAL "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}"
        )
            list(APPEND STREAMLINE_DLSSG_RUNTIME_FILES "${_destination}")
        else()
            list(APPEND STREAMLINE_DX11_RUNTIME_FILES "${_destination}")
        endif()
    endforeach()

    set(STREAMLINE_RUNTIME_FILES ${STREAMLINE_RUNTIME_FILES} PARENT_SCOPE)
    set(
        STREAMLINE_DX11_RUNTIME_FILES
        ${STREAMLINE_DX11_RUNTIME_FILES}
        PARENT_SCOPE
    )
    set(
        STREAMLINE_DLSSG_RUNTIME_FILES
        ${STREAMLINE_DLSSG_RUNTIME_FILES}
        PARENT_SCOPE
    )
endfunction()

file(REMOVE_RECURSE "${STREAMLINE_RUNTIME_FEATURE_ROOT}")
file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_DIRECTORY}")
file(MAKE_DIRECTORY "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}")

set(STREAMLINE_RUNTIME_FILES "")
set(STREAMLINE_DX11_RUNTIME_FILES "")
set(STREAMLINE_DLSSG_RUNTIME_FILES "")
set(
    _streamline_dx11_runtime_names
    nvngx_dlss.dll
    sl.common.dll
    sl.dlss.dll
    sl.interposer.dll
    sl.pcl.dll
    sl.reflex.dll
)
set(
    _streamline_dlssg_runtime_names
    nvngx_dlssg.dll
    sl.common.dll
    sl.dlss_g.dll
    sl.interposer.dll
    sl.pcl.dll
    sl.reflex.dll
)
foreach(_runtime_name IN LISTS _streamline_dx11_runtime_names)
    stage_streamline_runtime("${_runtime_name}")
endforeach()
foreach(_runtime_name IN LISTS _streamline_dlssg_runtime_names)
    stage_streamline_runtime(
        "${_runtime_name}"
        "${STREAMLINE_DLSSG_RUNTIME_DIRECTORY}"
    )
endforeach()

foreach(
    _license_relative_path
    IN ITEMS
        license.txt
        3rd-party-licenses.md
        bin/x64/nvngx_dlss.license.txt
        bin/x64/reflex.license.txt
)
    stage_streamline_notice("${_license_relative_path}")
endforeach()
