function(csx_resolve_release_version value core_version out_version out_label)
    if(
        NOT
            "${value}"
            MATCHES
            [[^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$]]
    )
        message(
            FATAL_ERROR
            "CSX_RELEASE_VERSION must be a canonical major.minor.patch version."
        )
    endif()
    set(_major "${CMAKE_MATCH_1}")
    set(_minor "${CMAKE_MATCH_2}")
    set(_patch "${CMAKE_MATCH_3}")
    if(_major GREATER 65535 OR _minor GREATER 65535 OR _patch GREATER 65535)
        message(
            FATAL_ERROR
            "CSX_RELEASE_VERSION exceeds the Windows resource version range."
        )
    endif()
    if(NOT "${core_version}" MATCHES "^${_major}\\.${_minor}-(SE|VR)$")
        message(
            FATAL_ERROR
            "Release version and core compatibility version must share a major/minor line."
        )
    endif()
    set("${out_version}" "${value}" PARENT_SCOPE)
    set("${out_label}" "${value}-${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
