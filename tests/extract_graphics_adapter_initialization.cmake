include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Hooks.cpp" _hooks)
file(READ "${PROJECT_ROOT}/src/State.cpp" _state)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between("${_hooks}"
    "\tvoid InitializeGraphicsAdapterInfoOnce("
    "\n\tbool UseNativeWaterShaders("
    "graphics_adapter_initialization_under_test.h")
extract_between("${_state}"
    "void State::SetAdapterDescription("
    "\nvoid State::UpdateSharedData("
    "graphics_adapter_name_under_test.h")
