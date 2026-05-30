cmake_minimum_required(VERSION 3.21)

set(NVAPI_ROOT "${CMAKE_SOURCE_DIR}/extern/nvapi")
set(NVAPI_LIBRARY "${NVAPI_ROOT}/amd64/nvapi64.lib")

if(NOT EXISTS "${NVAPI_ROOT}/nvapi.h" OR NOT EXISTS "${NVAPI_LIBRARY}")
    message(FATAL_ERROR "NVAPI submodule is missing. Run: git submodule update --init extern/nvapi")
endif()

add_library(NVAPI INTERFACE)

target_include_directories(
    NVAPI
    INTERFACE
        "${NVAPI_ROOT}"
)

target_link_libraries(
    NVAPI
    INTERFACE
        "${NVAPI_LIBRARY}"
)

if(MSVC)
    target_compile_options(NVAPI INTERFACE /wd4828)
endif()
