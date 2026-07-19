set(FFX_API_VK OFF)
set(FFX_API_DX12 OFF)
set(FFX_ALL OFF)
set(FFX_FSR3 ON)
set(FFX_FSR ON)
set(FFX_AUTO_COMPILE_SHADERS 1)

add_subdirectory(${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/sdk)

# The vendored SDK hardcodes its static libs' output to a single shared
# ${CMAKE_HOME_DIRECTORY}/bin/ffx_sdk directory (see its CMakeLists.txt).
# Separate presets and configurations can then overwrite an archive while
# another build tree still considers its own objects up to date. Besides
# mixing /GL and non-/GL objects, this can feed MSVC LTCG objects produced by
# a different compiler version to the linker (C1047). Keep every archive in
# the build tree and configuration that produced it.
if(MSVC)
  set(_ffx_archive_root "${CMAKE_BINARY_DIR}/ffx_sdk")

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
        ARCHIVE_OUTPUT_DIRECTORY "${_ffx_archive_root}"
      )

      foreach(_ffx_config IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${_ffx_config}" _ffx_config_upper)
        set_target_properties(
          ${_ffx_target} PROPERTIES
          "ARCHIVE_OUTPUT_DIRECTORY_${_ffx_config_upper}" "${_ffx_archive_root}/${_ffx_config}"
        )
      endforeach()
    endif()
  endforeach()

  unset(_ffx_archive_root)
  unset(_ffx_config)
  unset(_ffx_config_upper)
  unset(_ffx_target)
endif()

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  ffx_backend_dx11_x64
  ffx_fsr3_x64
)
