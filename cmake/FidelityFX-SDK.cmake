set(FFX_API_VK OFF)
set(FFX_API_DX12 OFF)
set(FFX_ALL OFF)
set(FFX_FSR3 ON)
set(FFX_FSR ON)
set(FFX_AUTO_COMPILE_SHADERS 1)

add_subdirectory(${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/sdk)

# The vendored SDK hardcodes its static libs' output to a single shared
# ${CMAKE_HOME_DIRECTORY}/bin/ffx_sdk directory (see its CMakeLists.txt),
# reused across every preset's separate build tree. A target's object
# staleness is tracked inside its build tree, so a different preset or
# toolset can otherwise overwrite the shared archive with incompatible
# compile options while the original tree still considers its objects up to
# date. Always keep MSVC archives local to the tree; this covers both LTO and
# non-LTO configurations without relying on the global IPO default.
if(MSVC)
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
