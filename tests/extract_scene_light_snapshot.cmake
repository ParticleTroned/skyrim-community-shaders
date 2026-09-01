if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix.cpp" _source)
string(
    FIND "${_source}"
    "const LightLimitFix::SceneLightSnapshot* LightLimitFix::GetSceneLightSnapshot("
    _start
)
string(
    FIND "${_source}"
    "void LightLimitFix::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights("
    _end
)
if(_start EQUAL -1 OR _end LESS _start)
    message(
        FATAL_ERROR
        "Scene light snapshot integration cannot find capture function"
    )
endif()
math(EXPR _length "${_end} - ${_start}")
string(SUBSTRING "${_source}" ${_start} ${_length} _capture)
string(
    FIND "${_source}"
    "CS_PROFILE_CPU_SCOPE(\"LightLimitFix::SceneLightsCPU\");"
    _start
)
string(
    FIND "${_source}"
    "\n\t}\n\n\t{\n\t\tCS_PROFILE_CPU_SCOPE(\"LightLimitFix::ParticleLightsCPU\");"
    _end
)
if(_start EQUAL -1 OR _end LESS _start)
    message(
        FATAL_ERROR
        "Scene light snapshot integration cannot find enumeration block"
    )
endif()
math(EXPR _length "${_end} - ${_start}")
string(SUBSTRING "${_source}" ${_start} ${_length} _enumeration)
string(
    APPEND _capture
    "\ntemplate<class Consumer>\nvoid EnumerateSceneLights(RE::ShadowSceneNode* shadowSceneNode, const Snapshot* snapshot, Consumer addLight)\n{\n${_enumeration}\n}\n"
)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(
    WRITE "${OUTPUT_DIRECTORY}/scene_light_snapshot_under_test.h"
    "${_capture}"
)
