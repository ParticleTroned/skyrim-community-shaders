include("${CMAKE_CURRENT_LIST_DIR}/CsxDownload.cmake")

# Updating the official provider requires a reviewed source change. Cache
# overrides must not be able to redirect or weaken this trust boundary.
set(CSX_OFFICIAL_STREAMLINE_VERSION "2.12.0")
set(
    CSX_OFFICIAL_STREAMLINE_ARCHIVE_SHA256
    "F5C0A3D870707DDDC3570FB4BCD3655CF48A8A68C3A9D342910CFA21B77DCF48"
)
set(STREAMLINE_RUNTIME_SHA256_nvngx_dlss_dll
    "BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E"
)
set(STREAMLINE_RUNTIME_SHA256_sl_common_dll
    "C57930EF5A8A3FE9BE85EFDF71A61D8107C1148E8A6AED456464547128F7F4AE"
)
set(STREAMLINE_RUNTIME_SHA256_sl_dlss_dll
    "A997022D2B93601E0EEFC3DDB3067C36DF386DD3163AE71E11095191FB14F8E4"
)
set(STREAMLINE_RUNTIME_SHA256_sl_interposer_dll
    "2A79DB6857AE8C75BBD871A9489C48BC6A39F7FCC88B9B02AFD53D0376CBEC66"
)
set(STREAMLINE_RUNTIME_SHA256_sl_pcl_dll
    "699AB461E64E95189A7FE6A21C79AD237CF56B60EA748CB6C840CD5431BA91D1"
)
set(STREAMLINE_RUNTIME_SHA256_sl_reflex_dll
    "7E6E4CCC4B561BD449FB0DA90709D9B96B08C3F6F4697362CAAA359E72A58A67"
)

set(
    _streamline_official_url
    "https://github.com/NVIDIA-RTX/Streamline/releases/download/v${CSX_OFFICIAL_STREAMLINE_VERSION}/streamline-sdk-v${CSX_OFFICIAL_STREAMLINE_VERSION}.zip"
)
set(
    _streamline_official_root
    "${CMAKE_CURRENT_BINARY_DIR}/streamline-official-runtime"
)
set(
    _streamline_official_archive
    "${_streamline_official_root}/streamline-sdk-v${CSX_OFFICIAL_STREAMLINE_VERSION}.zip"
)
set(
    _streamline_official_extract_root
    "${_streamline_official_root}/sdk"
)
set(
    STREAMLINE_OFFICIAL_RUNTIME_DIRECTORY
    "${_streamline_official_root}/runtime"
)
file(MAKE_DIRECTORY "${_streamline_official_root}")

message(STATUS
    "Explicit official Streamline runtime fetch enabled for ${_streamline_official_url}"
)
csx_download_verified_asset(
    "${_streamline_official_url}"
    "${_streamline_official_archive}"
    "${CSX_OFFICIAL_STREAMLINE_ARCHIVE_SHA256}"
)

# Recreate the SDK tree from the verified archive on every explicit configure.
# Per-DLL pins must never trust mutable bytes from a prior build tree.
file(REMOVE_RECURSE "${_streamline_official_extract_root}")
file(MAKE_DIRECTORY "${_streamline_official_extract_root}")
file(
    ARCHIVE_EXTRACT
    INPUT "${_streamline_official_archive}"
    DESTINATION "${_streamline_official_extract_root}"
)

file(
    GLOB_RECURSE _streamline_official_archive_files
    LIST_DIRECTORIES FALSE
    "${_streamline_official_extract_root}/*"
)
file(REMOVE_RECURSE "${STREAMLINE_OFFICIAL_RUNTIME_DIRECTORY}")
file(MAKE_DIRECTORY "${STREAMLINE_OFFICIAL_RUNTIME_DIRECTORY}")

foreach(_filename IN LISTS STREAMLINE_NORMAL_DLSS_RUNTIME_FILENAMES)
    set(_production_matches "")
    foreach(_candidate IN LISTS _streamline_official_archive_files)
        get_filename_component(_candidate_name "${_candidate}" NAME)
        if(NOT _candidate_name STREQUAL _filename)
            continue()
        endif()

        get_filename_component(_candidate_directory "${_candidate}" DIRECTORY)
        get_filename_component(
            _candidate_directory_name "${_candidate_directory}" NAME
        )
        get_filename_component(_candidate_parent "${_candidate_directory}" DIRECTORY)
        get_filename_component(_candidate_parent_name "${_candidate_parent}" NAME)
        if(_candidate_directory_name STREQUAL "x64" AND
           _candidate_parent_name STREQUAL "bin")
            list(APPEND _production_matches "${_candidate}")
        endif()
    endforeach()

    list(LENGTH _production_matches _production_match_count)
    if(NOT _production_match_count EQUAL 1)
        message(FATAL_ERROR
            "Expected one production ${_filename} in official Streamline ${CSX_OFFICIAL_STREAMLINE_VERSION}, found ${_production_match_count}"
        )
    endif()

    list(GET _production_matches 0 _source)
    string(MAKE_C_IDENTIFIER "${_filename}" _hash_key)
    set(_expected_hash "${STREAMLINE_RUNTIME_SHA256_${_hash_key}}")
    if(_expected_hash STREQUAL "")
        message(FATAL_ERROR "No official SHA-256 pin is declared for ${_filename}")
    endif()
    file(SHA256 "${_source}" _source_hash)
    string(TOUPPER "${_source_hash}" _source_hash)
    if(NOT _source_hash STREQUAL _expected_hash)
        message(FATAL_ERROR
            "Rejected official ${_filename}: expected SHA-256 ${_expected_hash}, got ${_source_hash}"
        )
    endif()

    set(_destination "${STREAMLINE_OFFICIAL_RUNTIME_DIRECTORY}/${_filename}")
    file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
    file(SHA256 "${_destination}" _runtime_hash)
    string(TOUPPER "${_runtime_hash}" _runtime_hash)
    if(NOT _runtime_hash STREQUAL _expected_hash)
        message(FATAL_ERROR
            "Staged official ${_filename} hash mismatch: ${_runtime_hash}"
        )
    endif()
endforeach()
