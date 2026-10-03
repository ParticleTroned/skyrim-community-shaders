function(csx_configure_tracy target)
    if(TRACY_SUPPORT)
        target_compile_definitions(${target} PRIVATE TRACY_SUPPORT)
        target_link_libraries(${target} PRIVATE Tracy::TracyClient)
    else()
        # Disabled Tracy macros need headers, without the client's transitive
        # definitions, library, or startup code.
        target_include_directories(
            ${target}
            SYSTEM PRIVATE
                "$<TARGET_PROPERTY:Tracy::TracyClient,INTERFACE_INCLUDE_DIRECTORIES>"
        )
    endif()
endfunction()
