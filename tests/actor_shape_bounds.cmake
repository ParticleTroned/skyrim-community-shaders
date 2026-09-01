set(_actor_bounds_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/actor-shape-bounds"
)
set(_actor_bounds_test_header
    "${_actor_bounds_test_dir}/actor_shape_bounds_under_test.h"
)
add_custom_command(
    OUTPUT "${_actor_bounds_test_header}"
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${_actor_bounds_test_header}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_actor_shape_bounds.cmake"
    DEPENDS src/Utils/ActorUtils.cpp tests/extract_actor_shape_bounds.cmake
    VERBATIM
)
add_controller_test(
    actor_shape_bounds_test
    ActorShapeBounds
    tests/actor_shape_bounds_test.cpp
)
target_sources(actor_shape_bounds_test PRIVATE "${_actor_bounds_test_header}")
target_include_directories(
    actor_shape_bounds_test
    PRIVATE "${_actor_bounds_test_dir}"
)
if(MSVC)
    target_compile_options(
        actor_shape_bounds_test
        PRIVATE "$<$<CONFIG:Release>:/fp:fast>"
    )
endif()
