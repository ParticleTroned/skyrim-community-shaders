if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(READ "${PROJECT_ROOT}/src/EngineFixes/VRShadowBatch.cpp" _source)
string(FIND "${_source}" "namespace VRShadowBatch" _start)
string(FIND "${_source}" "\n\tvoid Install()" _end)
if(_start EQUAL -1 OR _end LESS _start)
    message(FATAL_ERROR "Cannot locate production shadow batch hook bodies")
endif()
math(EXPR _length "${_end} - ${_start}")
string(SUBSTRING "${_source}" ${_start} ${_length} _hooks)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_DIRECTORY}/shadow_batch_hooks.h" "${_hooks}\n}\n")
string(SUBSTRING "${_source}" ${_end} -1 _install)
if(
    NOT _install
        MATCHES
        "void Install\\(\\)[ \t\r\n]*\\{.*\\}[ \t\r\n]*\\}[ \t\r\n]*$"
)
    message(
        FATAL_ERROR
        "Cannot locate complete production shadow batch installer"
    )
endif()
file(
    WRITE "${OUTPUT_DIRECTORY}/shadow_batch_install.h"
    "namespace VRShadowBatch {\n${_install}"
)
