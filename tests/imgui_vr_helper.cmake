foreach(_helper_contract scene_packet host_policy)
    set(_helper_target "imgui_vr_helper_${_helper_contract}_test")
    set(_helper_source "tests/${_helper_target}.cpp")
    set(_helper_test "ImGuiVRHelper_${_helper_contract}")
    add_controller_test(${_helper_target} ${_helper_test} ${_helper_source})

    if(MSVC)
        set(_helper_fast_target "${_helper_target}_fast")
        add_controller_test(
            ${_helper_fast_target}
            "${_helper_test}_FastMath"
            ${_helper_source}
        )
        target_compile_options(${_helper_fast_target} PRIVATE /fp:fast)
    endif()
endforeach()
