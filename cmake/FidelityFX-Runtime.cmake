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
    set(_url "${FFX_RUNTIME_BASE_URL}/${_filename}")
    if(ARGC GREATER 2)
        set(_url "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/${FFX_RUNTIME_SDK_COMMIT}/${ARGV2}")
    endif()
    csx_prepare_runtime_asset(
        "${_url}"
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
download_ffx_runtime(
    FidelityFX-Runtime.LICENSE.md
    410F9C138E766E846ACF2DC93DB768198DEF0293A017D7E3296815B451688DBE
    docs/license.md
)
download_ffx_runtime(
    FidelityFX-Runtime.NOTICES.md
    3B69B1A1011622557A81B55A9A211DD797D01B8160924A5EC51B4C3432D33A4F
    3rdpartynotice.md
)

# Statically linked AMD code retains the notices from its own SDK revision.
foreach(_sdk IN ITEMS FidelityFX-SDK AntiLag2-SDK)
    csx_stage_runtime_notice(FFX "${FFX_RUNTIME_DIRECTORY}"
        "${CMAKE_CURRENT_LIST_DIR}/../extern/${_sdk}/LICENSE.txt" "${_sdk}.LICENSE.txt")
endforeach()
csx_stage_runtime_notice(FFX "${FFX_RUNTIME_DIRECTORY}"
    "${CMAKE_CURRENT_LIST_DIR}/../extern/FidelityFX-SDK/Third_party_notices.txt"
    FidelityFX-SDK.NOTICES.txt
)
