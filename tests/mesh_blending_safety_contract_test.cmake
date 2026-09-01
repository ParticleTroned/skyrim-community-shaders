if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/MeshBlending.cpp" _implementation)
file(READ "${PROJECT_ROOT}/src/Features/MeshBlending.h" _header)
file(READ "${PROJECT_ROOT}/src/Utils/FileSystem.cpp" _file_system)

foreach(_required IN ITEMS
    "Form is not a portable 0xLOCALID~Plugin selector"
    "WriteTextFileAtomicIfUnchanged"
    "a_state.sourceContents"
    "cachedSignature == signature"
    "HasOpaqueSibling"
    "currentReceiver"
    "cachedReceiver"
    "maximumQuadrantObjects = 32u"
    "kMultiTextureLandscape"
)
    string(FIND "${_implementation}${_header}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Mesh Blending safety contract is missing: ${_required}")
    endif()
endforeach()

string(FIND "${_implementation}" "children[0]" _child_zero)
if(NOT _child_zero EQUAL -1)
    message(FATAL_ERROR "LAND capture must not assume quadrant geometry is child zero")
endif()

foreach(_required IN ITEMS
    "currentContents != expectedContents"
    "The destination changed after it was read"
    "Could not conditionally replace the destination"
)
    string(FIND "${_file_system}" "${_required}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Conditional file persistence is missing: ${_required}")
    endif()
endforeach()
