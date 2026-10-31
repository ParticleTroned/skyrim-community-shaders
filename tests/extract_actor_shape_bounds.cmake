if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_FILE are required")
endif()

include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_FILE}" DIRECTORY)
get_filename_component(_output_name "${OUTPUT_FILE}" NAME)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Utils/ActorUtils.cpp" _source)

# Shape bounds use Havok stubs; actor traversal requires the live scene types.
extract_between(
    "${_source}"
    "namespace\n{"
    "\nnamespace Util\n{\n\tvoid ForEachLoadedActor("
    "${_output_name}"
)
