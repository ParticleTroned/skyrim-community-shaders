include("${CMAKE_CURRENT_LIST_DIR}/RuntimePayload.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ports/renderdoc/pin.cmake")

set(RENDERDOC_RUNTIME_FEATURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/renderdoc-runtime/${CSX_RENDERDOC_COMMIT}")
configure_file("${CMAKE_SOURCE_DIR}/features/RenderDoc/CORE"
    "${RENDERDOC_RUNTIME_FEATURE_ROOT}/CORE" COPYONLY)
set(_renderdoc_source
    "${RENDERDOC_RUNTIME_FEATURE_ROOT}/Renderdoc/Source/renderdoc-${CSX_RENDERDOC_COMMIT}.tar.gz")
csx_prepare_runtime_asset(
    "https://github.com/ParticleTroned/renderdoc/archive/${CSX_RENDERDOC_COMMIT}.tar.gz"
    "${_renderdoc_source}" "${CSX_RENDERDOC_SOURCE_SHA256}" _renderdoc_source_available)
set(RENDERDOC_RUNTIME_PAYLOAD_FILES "${_renderdoc_source}")
set(RENDERDOC_RUNTIME_PAYLOAD_MISSING "")
if(NOT _renderdoc_source_available)
    list(APPEND RENDERDOC_RUNTIME_PAYLOAD_MISSING "${_renderdoc_source}")
endif()

set(_renderdoc_payload "${CMAKE_SOURCE_DIR}/features/RenderDoc/Renderdoc")
file(SHA256 "${_renderdoc_payload}/renderdoc.dll" _renderdoc_dll_sha256)
if(NOT _renderdoc_dll_sha256 STREQUAL CSX_RENDERDOC_DLL_SHA256)
    message(FATAL_ERROR "RenderDoc DLL does not match the pinned fork build. See docs/development/renderdoc.md.")
endif()
list(APPEND RENDERDOC_RUNTIME_PAYLOAD_FILES
    "${_renderdoc_payload}/renderdoc.dll"
    "${_renderdoc_payload}/LICENSE.md"
    "${_renderdoc_payload}/COPYING"
    "${_renderdoc_payload}/Source/Directory.Build.props"
)
