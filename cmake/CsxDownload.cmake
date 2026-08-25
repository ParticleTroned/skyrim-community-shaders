include_guard(GLOBAL)

function(csx_download_verified_asset URL DESTINATION EXPECTED_SHA256)
    string(TOLOWER "${EXPECTED_SHA256}" _expected_sha256)
    if(EXISTS "${DESTINATION}")
        file(SHA256 "${DESTINATION}" _existing_sha256)
        if(_existing_sha256 STREQUAL _expected_sha256)
            return()
        endif()
        file(REMOVE "${DESTINATION}")
    endif()

    get_filename_component(_destination_directory "${DESTINATION}" DIRECTORY)
    file(MAKE_DIRECTORY "${_destination_directory}")

    if(DEFINED ENV{CSX_ASSET_DOWNLOADER_PYTHON} AND
       DEFINED ENV{CSX_ASSET_DOWNLOADER_SCRIPT})
        execute_process(
            COMMAND
                "$ENV{CSX_ASSET_DOWNLOADER_PYTHON}"
                "$ENV{CSX_ASSET_DOWNLOADER_SCRIPT}"
                "${URL}"
                "${DESTINATION}"
                "${EXPECTED_SHA256}"
            RESULT_VARIABLE _download_result
            ERROR_VARIABLE _download_error
        )
        if(NOT _download_result EQUAL 0)
            file(REMOVE "${DESTINATION}")
            message(FATAL_ERROR
                "Failed to download ${URL}: ${_download_error}")
        endif()

        file(SHA256 "${DESTINATION}" _downloaded_sha256)
        if(NOT _downloaded_sha256 STREQUAL _expected_sha256)
            file(REMOVE "${DESTINATION}")
            message(FATAL_ERROR
                "Downloaded asset hash mismatch for ${URL}: "
                "expected ${_expected_sha256}, got ${_downloaded_sha256}")
        endif()
        return()
    endif()

    file(
        DOWNLOAD "${URL}" "${DESTINATION}"
        EXPECTED_HASH "SHA256=${EXPECTED_SHA256}"
        STATUS _download_status
        TLS_VERIFY ON
        TIMEOUT 120
        INACTIVITY_TIMEOUT 20
    )
    list(GET _download_status 0 _status_code)
    list(GET _download_status 1 _status_message)
    if(NOT _status_code EQUAL 0)
        file(REMOVE "${DESTINATION}")
        message(FATAL_ERROR "Failed to download ${URL}: ${_status_message}")
    endif()
endfunction()
