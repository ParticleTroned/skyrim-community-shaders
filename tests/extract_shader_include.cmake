if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between(
    "${_source}"
    "\tclass TrackingIncludeHandler : public ID3DInclude"
    "\n\tnamespace SShaderCache"
    "tracking_include_under_test.h"
)
