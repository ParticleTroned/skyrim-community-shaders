cmake_minimum_required(VERSION 3.21)

add_library(Streamline INTERFACE)

target_include_directories(
  Streamline INTERFACE
    ${CMAKE_SOURCE_DIR}/extern/Streamline-DX12/include
    ${CMAKE_SOURCE_DIR}/extern/Streamline-DX12/external/ngx-sdk/include)

# The NGX declarations are compile-time inputs only. Runtime delivery remains
# the separate, version-pinned policy in Streamline-Runtime.cmake.

add_library(NgxSdk INTERFACE)
target_include_directories(NgxSdk INTERFACE
    ${CMAKE_SOURCE_DIR}/extern/Streamline-DX12/external/ngx-sdk/include)
target_link_libraries(NgxSdk INTERFACE
    optimized ${CMAKE_SOURCE_DIR}/extern/Streamline-DX12/external/ngx-sdk/lib/Windows_x86_64/nvsdk_ngx_d.lib
    debug ${CMAKE_SOURCE_DIR}/extern/Streamline-DX12/external/ngx-sdk/lib/Windows_x86_64/nvsdk_ngx_d_dbg.lib)

if(MSVC)
  target_compile_options(Streamline INTERFACE /wd5103)
endif()
