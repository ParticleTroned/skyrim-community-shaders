# FidelityFX SDK — official GPUOpen prebuilt, signed FFX API DLL (amd_fidelityfx_vk.dll),
# Vulkan backend only.
#
# We drive FFX FSR3 (upscaling + frame generation) through the higher-level FFX **API**
# (ffxCreateContext/ffxConfigure/ffxDispatch/ffxQuery/ffxDestroyContext) against DXVK's own
# VkDevice (see src/Features/Upscaling/DxvkInterop). The DLL manages its own Vulkan backend
# memory — there is no FfxInterface, no scratch buffers, and no native FFX static libs built
# from source. No DX12 backend: everything renders on the single DXVK Vulkan device with no
# interop (amd_fidelityfx_dx12.dll is NOT used).
#
# The 5 entry points (ffxConfigure/ffxCreateContext/ffxDestroyContext/ffxDispatch/ffxQuery)
# are resolved at runtime via LoadLibrary + GetProcAddress (ffx-api/include/ffx_api/
# ffx_api_loader.h), so CS links NO import lib — it only needs the ffx-api headers (for the
# descriptor structs/enums) and the DLL staged next to DXVK's csd3d11.dll / csgi.dll. The
# install/staging of the DLL lives in the top-level CMakeLists.txt (FFX install COMPONENT).

set(FFX_API_INCLUDE_DIR
    "${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/ffx-api/include"
    CACHE PATH
    "FidelityFX FFX-API public headers (descriptor structs for the prebuilt amd_fidelityfx_vk.dll)"
)

# Path to the official prebuilt, signed FFX API Vulkan DLL. Staged into the CS mod subfolder
# (SKSE/Plugins/CommunityShaders/dxvk) by the FFX install COMPONENT in CMakeLists.txt and
# loaded at runtime from there (never the game root).
set(FFX_API_VK_DLL
    "${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/PrebuiltSignedDLL/amd_fidelityfx_vk.dll"
    CACHE FILEPATH
    "Official prebuilt signed FFX API Vulkan DLL to stage and load at runtime"
)

if(NOT EXISTS "${FFX_API_VK_DLL}")
    message(
        FATAL_ERROR
        "FidelityFX prebuilt VK DLL not found at '${FFX_API_VK_DLL}'. "
        "Expected extern/FidelityFX-SDK/PrebuiltSignedDLL/amd_fidelityfx_vk.dll."
    )
endif()

# ffx-api headers only — no native FFX libs are built and nothing is linked. The VK FFX-API
# header pulls in <vulkan/vulkan.h>, satisfied by Vulkan::Vulkan on the CS link line.
target_include_directories(${PROJECT_NAME} PRIVATE "${FFX_API_INCLUDE_DIR}")
