set(_collision_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/actor-collision")
set(_collision_test_headers
    "${_collision_test_dir}/collision_traversal_under_test.h"
    "${_collision_test_dir}/collision_pointer_under_test.h"
    "${_collision_test_dir}/collision_array_under_test.h"
)
add_custom_command(
    OUTPUT ${_collision_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_collision_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_actor_collision_traversal.cmake"
    DEPENDS
        src/Utils/ActorUtils.cpp
        extern/CommonLibSSE-NG/include/RE/N/NiSmartPointer.h
        extern/CommonLibSSE-NG/include/RE/N/NiTArray.h
        tests/extract_actor_collision_traversal.cmake
        tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(
    actor_collision_traversal_test
    ActorCollisionTraversal
    tests/actor_collision_traversal_test.cpp
)
target_sources(actor_collision_traversal_test PRIVATE ${_collision_test_headers})
target_include_directories(actor_collision_traversal_test PRIVATE "${_collision_test_dir}")
