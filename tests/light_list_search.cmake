add_controller_test(
    light_list_search_test
    LightListSearch
    tests/light_list_search_test.cpp
)
target_sources(
    light_list_search_test
    PRIVATE src/Utils/PointerSearch.cpp src/Utils/PointerSearchAvx2.cpp
)
target_compile_definitions(
    light_list_search_test
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
)
