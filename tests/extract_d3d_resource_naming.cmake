if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

# Standalone D3D tests reuse the production helper without game dependencies.
file(READ "${PROJECT_ROOT}/src/Utils/D3D.cpp" _source)
string(FIND "${_source}" "\tGUID WKPDID_D3DDebugObjectNameT =" _start)
string(FIND "${_source}" "\n\tID3D11DeviceChild* CompileShader(" _end)
if(_start EQUAL -1 OR _end LESS _start)
    message(FATAL_ERROR "Cannot locate the D3D resource naming implementation")
endif()
math(EXPR _length "${_end} - ${_start}")
string(SUBSTRING "${_source}" ${_start} ${_length} _naming)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(
    WRITE "${OUTPUT_DIRECTORY}/d3d_resource_naming.h"
    "#pragma once\n#include <d3d11.h>\n#include <cstdarg>\n#include <cstdio>\nnamespace Util {\n${_naming}\n}\n"
)
