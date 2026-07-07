set(FFX_API_VK OFF)
set(FFX_API_DX12 OFF)
set(FFX_ALL OFF)
set(FFX_FSR3 ON)
set(FFX_FSR ON)
set(FFX_AUTO_COMPILE_SHADERS 1)

add_subdirectory(${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/sdk)

# The vendored SDK hardcodes its static libs' output to a single shared
# ${CMAKE_HOME_DIRECTORY}/bin/ffx_sdk directory (see its CMakeLists.txt),
# reused across every preset's separate build tree. Non-LTO presets
# (Dev-Fast, PR) compile these objects without /GL, but Ninja's staleness
# check only looks at its own object mtimes, not the flags baked into an
# existing output; if a shipping preset (IPO ON, /GL) links to that shared
# path afterward, a later non-LTO build can see its own (older) objects as
# up to date and silently reuse the shipping /GL archive -> LNK4075
# ("restarting link with /LTCG") -> LNK1218 under /WX. Give non-LTO presets
# a build-tree-local output so they never share the shipping archive.
if(MSVC AND NOT CMAKE_INTERPROCEDURAL_OPTIMIZATION)
  foreach(
    _ffx_target
    ffx_backend_dx11_x64
    ffx_fsr3_x64
    ffx_fsr3upscaler_x64
    ffx_frameinterpolation_x64
    ffx_opticalflow_x64
  )
    if(TARGET ${_ffx_target})
      set_target_properties(
        ${_ffx_target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ffx_sdk"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/ffx_sdk"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/ffx_sdk"
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/ffx_sdk"
      )
    endif()
  endforeach()
endif()

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  ffx_backend_dx11_x64
  ffx_fsr3_x64
)
