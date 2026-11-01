# Keep the production bounds implementation intact; the fixture supplies only
# the native object/accessor boundary declared by ActorUtils.h.
if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_FILE are required")
endif()
file(READ "${PROJECT_ROOT}/src/Utils/ActorUtils.cpp" source)
set(native_include "#include \"ActorUtils.h\"")
string(FIND "${source}" "${native_include}" include_offset)
if(NOT include_offset EQUAL 0)
    message(FATAL_ERROR "ActorUtils.cpp no longer starts with its native header; update the bounds fixture")
endif()
string(LENGTH "${native_include}" include_length)
string(SUBSTRING "${source}" ${include_length} -1 implementation)
get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}"
    "// Generated from src/Utils/ActorUtils.cpp; do not edit.\n${implementation}")
