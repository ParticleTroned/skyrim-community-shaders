if(NOT DEFINED MARKER_SOURCE OR NOT EXISTS "${MARKER_SOURCE}")
    message(FATAL_ERROR "SyncCSXMarker requires an existing MARKER_SOURCE")
endif()

if(NOT DEFINED MARKER_DESTINATION)
    message(FATAL_ERROR "SyncCSXMarker requires MARKER_DESTINATION")
endif()

get_filename_component(_marker_directory "${MARKER_DESTINATION}" DIRECTORY)
file(MAKE_DIRECTORY "${_marker_directory}")

file(GLOB _existing_markers "${_marker_directory}/CSX*.marker")
foreach(_existing_marker IN LISTS _existing_markers)
    if(NOT _existing_marker STREQUAL MARKER_DESTINATION)
        file(REMOVE "${_existing_marker}")
    endif()
endforeach()

file(COPY_FILE "${MARKER_SOURCE}" "${MARKER_DESTINATION}" ONLY_IF_DIFFERENT)
