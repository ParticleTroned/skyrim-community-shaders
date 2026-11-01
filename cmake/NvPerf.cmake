# SDK payloads are supplied explicitly; the build never downloads the SDK.
function(csx_configure_nvperf target)
    if(NOT NVPERF_SUPPORT)
        return()
    endif()
    find_path(NVPERF_INCLUDE_DIR nvperf_d3d11_target.h
        PATHS "${NVPERF_SDK_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
    find_library(NVPERF_LIBRARY nvperf_grfx_host
        PATHS "${NVPERF_SDK_ROOT}/lib/x64" "${NVPERF_SDK_ROOT}/bin/x64" NO_DEFAULT_PATH REQUIRED)
    find_file(NVPERF_DLL nvperf_grfx_host.dll
        PATHS "${NVPERF_SDK_ROOT}/bin/x64" NO_DEFAULT_PATH REQUIRED)
    target_include_directories(${target} SYSTEM PRIVATE "${NVPERF_INCLUDE_DIR}")
    target_link_libraries(${target} PRIVATE "${NVPERF_LIBRARY}" delayimp)
    target_link_options(${target} PRIVATE "/DELAYLOAD:nvperf_grfx_host.dll")
    target_compile_definitions(${target} PRIVATE NVPERF_ENABLED)
    install(FILES "${NVPERF_DLL}" DESTINATION SKSE/Plugins/CommunityShaders/NvPerf COMPONENT SKSE)
endfunction()

function(csx_stage_nvperf target)
    if(NOT NVPERF_SUPPORT)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AIO_DIR}/SKSE/Plugins/CommunityShaders/NvPerf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NVPERF_DLL}"
            "${AIO_DIR}/SKSE/Plugins/CommunityShaders/NvPerf/nvperf_grfx_host.dll"
        COMMENT "Staging optional developer hardware-counter runtime"
        VERBATIM)
endfunction()
