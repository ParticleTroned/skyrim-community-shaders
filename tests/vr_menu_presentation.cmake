set(_vr_menu_presentation_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vr-menu-presentation")
set(_vr_menu_presentation_headers)
foreach(_part IN ITEMS scale path route native_scale matrix_conversion native_relative)
    list(APPEND _vr_menu_presentation_headers
        "${_vr_menu_presentation_dir}/vr_menu_${_part}_under_test.h")
endforeach()
add_custom_command(
    OUTPUT ${_vr_menu_presentation_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_vr_menu_presentation_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_vr_menu_presentation.cmake"
    DEPENDS
        src/Features/VR.h src/Features/VR.cpp
        src/Utils/VRUtils.h src/Utils/VRUtils.cpp
        tests/extract_source_region.cmake tests/extract_vr_menu_presentation.cmake
    VERBATIM
)
add_controller_test(vr_menu_presentation_test VRMenuPresentation tests/vr_menu_presentation_test.cpp)
target_sources(vr_menu_presentation_test PRIVATE ${_vr_menu_presentation_headers})
target_include_directories(vr_menu_presentation_test PRIVATE
    "${_vr_menu_presentation_dir}"
    "${PROJECT_SOURCE_DIR}/extern/CommonLibSSE-NG/extern/openvr/headers")
target_link_libraries(vr_menu_presentation_test PRIVATE Microsoft::DirectXTK)
