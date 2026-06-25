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
#
# Only the ffx-api headers live in the submodule; the signed DLL itself is FETCHED at
# configure time from the pinned v1.1.4 release tag (see file(DOWNLOAD) below) rather than
# bundled in the repo.

set(FFX_API_INCLUDE_DIR
    "${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/ffx-api/include"
    CACHE PATH
    "FidelityFX FFX-API public headers (descriptor structs for the prebuilt amd_fidelityfx_vk.dll)"
)

# Fetch the official prebuilt, signed FFX API Vulkan DLL at configure time from the pinned
# v1.1.4 release tag (the DLL is NOT bundled in the repo — only the ffx-api headers are).
# Verified against a known SHA256 over TLS. The DLL is staged into the CS mod subfolder
# (SKSE/Plugins/CommunityShaders/dxvk) by the FFX install COMPONENT in CMakeLists.txt and
# loaded at runtime from there (never the game root).
set(FFX_API_VK_DLL_URL
    "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/v1.1.4/PrebuiltSignedDLL/amd_fidelityfx_vk.dll"
)
set(FFX_API_VK_DLL_SHA256 "a1624cc4238fef046f30c4d80ce3f47be63fc5f5373f49e3ee9edb9960f54c78")
set(FFX_API_VK_DLL_DEST "${CMAKE_BINARY_DIR}/_deps/ffx/amd_fidelityfx_vk.dll")

if(NOT EXISTS "${FFX_API_VK_DLL_DEST}")
    message(STATUS "Fetching FidelityFX prebuilt VK DLL from ${FFX_API_VK_DLL_URL}")
    file(
        DOWNLOAD
        "${FFX_API_VK_DLL_URL}"
        "${FFX_API_VK_DLL_DEST}"
        EXPECTED_HASH SHA256=${FFX_API_VK_DLL_SHA256}
        TLS_VERIFY ON
        STATUS _ffx_dl_status
    )
    list(GET _ffx_dl_status 0 _ffx_dl_code)
    if(NOT _ffx_dl_code EQUAL 0)
        list(GET _ffx_dl_status 1 _ffx_dl_msg)
        file(REMOVE "${FFX_API_VK_DLL_DEST}")
        message(
            FATAL_ERROR
            "Failed to download FidelityFX prebuilt VK DLL from '${FFX_API_VK_DLL_URL}' "
            "(status ${_ffx_dl_code}: ${_ffx_dl_msg})."
        )
    endif()
endif()

# FORCE so a stale cached value (e.g. the old extern/FidelityFX-SDK/PrebuiltSignedDLL path
# from before the DLL was fetched, which no longer exists) is overridden by the fetched dest.
set(FFX_API_VK_DLL
    "${FFX_API_VK_DLL_DEST}"
    CACHE FILEPATH
    "Official prebuilt signed FFX API Vulkan DLL (fetched at configure time) to stage and load at runtime"
    FORCE
)

# ffx-api headers only — no native FFX libs are built and nothing is linked. The VK FFX-API
# header pulls in <vulkan/vulkan.h>, satisfied by Vulkan::Vulkan on the CS link line.
target_include_directories(${PROJECT_NAME} PRIVATE "${FFX_API_INCLUDE_DIR}")
