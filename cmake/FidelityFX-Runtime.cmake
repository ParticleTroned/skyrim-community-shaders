set(FFX_RUNTIME_SDK_COMMIT "60f4ea81909200d8542eca14dccb2628b763a9a3")
include("${CMAKE_CURRENT_LIST_DIR}/CsxDownload.cmake")
set(
    FFX_RUNTIME_BASE_URL
    "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/${FFX_RUNTIME_SDK_COMMIT}/Kits/FidelityFX/signedbin"
)
set(FFX_RUNTIME_FEATURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/ffx-runtime")
set(FFX_RUNTIME_SHADER_ROOT "${FFX_RUNTIME_FEATURE_ROOT}/Shaders")
set(
    FFX_RUNTIME_DIRECTORY
    "${FFX_RUNTIME_SHADER_ROOT}/Upscaling/FidelityFX"
)
file(MAKE_DIRECTORY "${FFX_RUNTIME_DIRECTORY}")

function(download_ffx_runtime _filename _sha256)
    set(_destination "${FFX_RUNTIME_DIRECTORY}/${_filename}")
    csx_download_verified_asset(
        "${FFX_RUNTIME_BASE_URL}/${_filename}"
        "${_destination}"
        "${_sha256}"
    )
    set(FFX_RUNTIME_FILES ${FFX_RUNTIME_FILES} "${_destination}" PARENT_SCOPE)
endfunction()

set(FFX_RUNTIME_FILES "")
download_ffx_runtime(
    amd_fidelityfx_framegeneration_dx12.dll
    02297BEEDD285E822D3A64F314CF00FAF378DCEC0EDC47FF0C4DD71B3A8C2F18
)
download_ffx_runtime(
    amd_fidelityfx_loader_dx12.dll
    E2D85AA05A9BD9ED8B38935FDF5199372CCA6F74C12015143BB6F945EE1608AA
)
download_ffx_runtime(
    amd_fidelityfx_upscaler_dx12.dll
    D0DCCCC74A43C44BA435B7A369B456E0970D8A4464E4BD683119B374F2C9FB46
)
