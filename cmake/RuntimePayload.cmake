include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/CsxDownload.cmake")

# Skip mode may reuse verified assets, but must not fetch or discard a cache.
function(csx_prepare_runtime_asset _url _destination _sha256 _available)
    if(NOT SKIP_RUNTIME_DOWNLOADS)
        csx_download_verified_asset("${_url}" "${_destination}" "${_sha256}")
        set(${_available} TRUE PARENT_SCOPE)
        return()
    endif()

    set(_verified FALSE)
    if(EXISTS "${_destination}" AND NOT IS_DIRECTORY "${_destination}")
        file(SHA256 "${_destination}" _actual_hash)
        string(TOLOWER "${_sha256}" _expected_hash)
        if(_actual_hash STREQUAL _expected_hash)
            set(_verified TRUE)
        endif()
    endif()
    set(${_available} ${_verified} PARENT_SCOPE)
endfunction()

function(csx_configure_runtime_payload)
    set(_missing
        ${FFX_RUNTIME_PAYLOAD_MISSING}
        ${STREAMLINE_RUNTIME_PAYLOAD_MISSING}
    )
    if(_missing)
        if(AIO_ZIP_TO_DIST OR ZIP_TO_DIST)
            message(
                FATAL_ERROR
                "Runtime payloads are missing or unverified. Reconfigure with "
                "SKIP_RUNTIME_DOWNLOADS=OFF to fetch them, or disable both "
                "AIO_ZIP_TO_DIST and ZIP_TO_DIST for a DLL-only build."
            )
        endif()
        message(
            WARNING
            "Runtime payloads are missing or unverified. DLL-only builds are "
            "available; runtime installation and packaging require a configure "
            "with SKIP_RUNTIME_DOWNLOADS=OFF. Other build dependencies are still required."
        )
    endif()
    foreach(_provider IN ITEMS FFX STREAMLINE)
        set(${_provider}_RUNTIME_PAYLOAD_HASHES "")
        foreach(_file IN LISTS ${_provider}_RUNTIME_PAYLOAD_FILES)
            set(_hash "missing")
            if(EXISTS "${_file}" AND NOT IS_DIRECTORY "${_file}")
                file(SHA256 "${_file}" _hash)
            endif()
            list(APPEND ${_provider}_RUNTIME_PAYLOAD_HASHES "${_hash}")
        endforeach()
    endforeach()
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RuntimePayloadInstallGuard.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/runtime_payload_install_guard.cmake"
        @ONLY
    )
endfunction()
