add_controller_test(
    virtual_function_hook_test
    VirtualFunctionHook
    tests/virtual_function_hook_test.cpp
)
target_include_directories(
    virtual_function_hook_test
    PRIVATE ${DETOURS_INCLUDE_DIRS}
)
target_compile_definitions(
    virtual_function_hook_test
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(virtual_function_hook_test PRIVATE ${DETOURS_LIBRARY})
set_tests_properties(VirtualFunctionHook PROPERTIES TIMEOUT 30)
