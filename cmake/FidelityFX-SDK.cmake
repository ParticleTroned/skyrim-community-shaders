# FidelityFX SDK — official GPUOpen build v1.1.4 (FSR 3.1.4), Vulkan backend only.
#
# We drive FFX FSR3 (upscaling + frame generation) through the Vulkan backend on
# DXVK's own VkDevice (see src/Features/Upscaling/DxvkInterop). No DX11/DX12 FFX
# backend: the DX11 fork's FSR3 could not do frame generation, and everything must
# render on the single DXVK Vulkan device with no interop.

# FFX_API_BACKEND is the string variable that actually selects the backend in
# sdk/CMakeLists.txt (VK_X64 -> builds ffx_backend_vk_x64, project FidelityFX-SDK_VK_x64).
set(FFX_API_BACKEND VK_X64 CACHE STRING "FidelityFX SDK backend" FORCE)
set(FFX_API_VK ON CACHE BOOL "" FORCE)
set(FFX_API_DX12 OFF CACHE BOOL "" FORCE)
set(FFX_ALL OFF CACHE BOOL "" FORCE)
# FFX_FSR3 ON auto-enables FFX_FSR3UPSCALER + FFX_FI (frame interpolation) + FFX_OF
# (optical flow), which together provide upscaling and frame generation.
set(FFX_FSR3 ON CACHE BOOL "" FORCE)
set(FFX_AUTO_COMPILE_SHADERS ON CACHE BOOL "" FORCE)
# Static link the FFX libs into CommunityShaders.dll.
set(FFX_BUILD_AS_DLL OFF CACHE BOOL "" FORCE)

# Note: extern/FidelityFX-SDK/sdk/CMakeLists.txt detects x64 via an exact
# STREQUAL on CMAKE_EXE_LINKER_FLAGS == "/machine:x64" when
# CMAKE_GENERATOR_PLATFORM is unset (Ninja). The ninja preset sets exactly
# that value; appending anything else to the variable breaks the configure.

add_subdirectory(${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/sdk)

# Upstream bug: the FFX dx11 backend's compile_shaders() leaks literal
# out-variable names (e.g. "FSR2_PERMUTATION_OUTPUTS") into the dependency
# list of the phony ffx_shader_permutations_dx11 target. The VS generator
# ignores the bogus deps; Ninja fails with "missing and no known rule".
# Pre-creating empty placeholder files at those paths satisfies Ninja without
# triggering rebuilds. If an SDK update adds a new leaked name, add it here.
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  set(_ffx_dx11_bindir
      "${CMAKE_BINARY_DIR}/extern/FidelityFX-SDK/sdk/src/backends/dx11"
  )
  foreach(
    _ffx_bogus_dep
    FSR1_PERMUTATION_OUTPUTS
    FSR2_PERMUTATION_OUTPUTS
    FSR3UPSCALER_PERMUTATION_OUTPUTS
    FRAMEINTERPOLATION_PERMUTATION_OUTPUTS
    OPTICALFLOW_PERMUTATION_OUTPUTS
  )
    if(NOT EXISTS "${_ffx_dx11_bindir}/${_ffx_bogus_dep}")
      file(
        WRITE "${_ffx_dx11_bindir}/${_ffx_bogus_dep}"
        "placeholder for upstream FFX CMake dependency-name leak; see cmake/FidelityFX-SDK.cmake\n"
      )
    endif()
  endforeach()
endif()

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  ffx_backend_vk_x64
  ffx_fsr3_x64
)

# CS bypasses the SDK's top-level CMake (which normally defines FFX_API_VK for the
# C++ host headers), so define it ourselves to expose the VK host-header symbols.
target_compile_definitions(${PROJECT_NAME} PRIVATE FFX_API_VK)
