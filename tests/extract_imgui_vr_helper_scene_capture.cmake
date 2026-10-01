if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(
    READ "${PROJECT_ROOT}/src/Features/VR/ImGuiVRHelperSceneCapture.cpp"
    _source
)
string(FIND "${_source}" "Scene::Matrix4x4 Identity()" _start)
string(FIND "${_source}" "bool ReadDepthView(" _end)
if(_start EQUAL -1 OR _end EQUAL -1 OR _end LESS_EQUAL _start)
    message(FATAL_ERROR "Cannot locate the production scene-camera math")
endif()
math(EXPR _length "${_end} - ${_start}")
string(SUBSTRING "${_source}" ${_start} ${_length} _math)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(
    WRITE "${OUTPUT_DIRECTORY}/imgui_vr_helper_scene_capture_under_test.h"
    "#ifdef _MSC_VER\n#pragma float_control(precise, on, push)\n#endif\n${_math}\n#ifdef _MSC_VER\n#pragma float_control(pop)\n#endif\n"
)
