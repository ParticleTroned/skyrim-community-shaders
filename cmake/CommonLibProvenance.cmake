function(
    csx_configure_commonlib_provenance
    target
    commonlib_target
    commonlib_relative_path
)
    set(_commonlib_source_dir
        "${CMAKE_SOURCE_DIR}/${commonlib_relative_path}"
    )
    set(_generated_header
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/CommonLibProvenance.h"
    )
    set(_generated_resource
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/CommonLibProvenance.rc"
    )
    set(_updater "${CMAKE_SOURCE_DIR}/cmake/UpdateCommonLibProvenance.cmake")
    set(_header_template
        "${CMAKE_SOURCE_DIR}/cmake/CommonLibProvenance.h.in"
    )
    set(_resource_template
        "${CMAKE_SOURCE_DIR}/cmake/CommonLibProvenance.rc.in"
    )

    get_target_property(_commonlib_is_imported ${commonlib_target} IMPORTED)
    if(_commonlib_is_imported)
        set(_commonlib_linkage "prebuilt")
    else()
        set(_commonlib_linkage "source")
    endif()

    set(_update_command
        "${CMAKE_COMMAND}"
        "-DCSX_SOURCE_ROOT:PATH=${CMAKE_SOURCE_DIR}"
        "-DCSX_COMMONLIB_SOURCE_DIR:PATH=${_commonlib_source_dir}"
        "-DCSX_COMMONLIB_RELATIVE_PATH:STRING=${commonlib_relative_path}"
        "-DCSX_COMMONLIB_PROVENANCE_POLICY:STRING=${CSX_COMMONLIB_PROVENANCE_POLICY}"
        "-DCSX_COMMONLIB_ARCHIVE_REVISION:STRING=${CSX_COMMONLIB_ARCHIVE_REVISION}"
        "-DCSX_COMMONLIB_LINKAGE:STRING=${_commonlib_linkage}"
        "-DCSX_GIT_EXECUTABLE:FILEPATH=${GIT_EXECUTABLE}"
        "-DCSX_PROVENANCE_HEADER_TEMPLATE:FILEPATH=${_header_template}"
        "-DCSX_PROVENANCE_RESOURCE_TEMPLATE:FILEPATH=${_resource_template}"
        "-DCSX_PROVENANCE_HEADER_OUTPUT:FILEPATH=${_generated_header}"
        "-DCSX_PROVENANCE_RESOURCE_OUTPUT:FILEPATH=${_generated_resource}"
        -P "${_updater}"
    )

    # Materialize the files for generators that inspect source lists during
    # configuration, then refresh them before every plugin build so a later
    # checkout, gitlink, or dirty-state change cannot leave stale metadata.
    execute_process(
        COMMAND ${_update_command}
        RESULT_VARIABLE _configure_result
    )
    if(NOT _configure_result EQUAL 0)
        message(FATAL_ERROR "Failed to establish CommonLib build provenance.")
    endif()

    add_custom_target(
        csx_commonlib_provenance
        COMMAND ${_update_command}
        BYPRODUCTS "${_generated_header}" "${_generated_resource}"
        COMMENT "Refreshing CommonLibSSE-NG build provenance"
        VERBATIM
    )
    add_dependencies(${target} csx_commonlib_provenance)
    if(NOT _commonlib_is_imported)
        add_dependencies(${commonlib_target} csx_commonlib_provenance)
    endif()

    set_property(
        SOURCE "${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc"
        APPEND
        PROPERTY OBJECT_DEPENDS "${_generated_resource}"
    )

endfunction()
