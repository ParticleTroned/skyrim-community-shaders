set(FFX_RUNTIME_SDK_COMMIT "7539ae8b6568ad29ed78a753404351daf3b5cca7")
include("${CMAKE_CURRENT_LIST_DIR}/RuntimePayload.cmake")
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
    csx_prepare_runtime_asset(
        "${FFX_RUNTIME_BASE_URL}/${_filename}"
        "${_destination}"
        "${_sha256}" _available
    )
    set(FFX_RUNTIME_PAYLOAD_FILES
        ${FFX_RUNTIME_PAYLOAD_FILES}
        "${_destination}"
        PARENT_SCOPE
    )
    if(_available)
        set(FFX_RUNTIME_FILES
            ${FFX_RUNTIME_FILES}
            "${_destination}"
            PARENT_SCOPE
        )
    else()
        set(FFX_RUNTIME_PAYLOAD_MISSING
            ${FFX_RUNTIME_PAYLOAD_MISSING}
            "${_destination}"
            PARENT_SCOPE
        )
    endif()
endfunction()

set(FFX_RUNTIME_FILES "")
set(FFX_RUNTIME_PAYLOAD_FILES "")
set(FFX_RUNTIME_PAYLOAD_MISSING "")
download_ffx_runtime(
    amd_fidelityfx_framegeneration_dx12.dll
    E6843252F58B0341A0D80F6A20BC0D840643EE95BC48C22CAC08745344577BB6
)
download_ffx_runtime(
    amd_fidelityfx_loader_dx12.dll
    E2191D3C0828BE45EB8AFFD4E3EC0378F2293ADC345B772A7170EDCF3B021ECB
)
download_ffx_runtime(
    amd_fidelityfx_upscaler_dx12.dll
    AAD4FCCCCB29084A655F9A4F9F613BD22C8C03FB4A0F8840148CC6543B4CCACE
)
