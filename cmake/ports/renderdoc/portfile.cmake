include("${CMAKE_CURRENT_LIST_DIR}/pin.cmake")

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ParticleTroned/renderdoc
    REF ${CSX_RENDERDOC_COMMIT}
    SHA512 ${CSX_RENDERDOC_SOURCE_SHA512}
    HEAD_REF dev
)

file(INSTALL
    "${SOURCE_PATH}/renderdoc/api/app/renderdoc_app.h"
    "${SOURCE_PATH}/renderdoc/api/app/renderdoc_d3d11.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/Renderdoc")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage"
"RenderDoc application and D3D11 extension headers from ParticleTroned/renderdoc.
CSX packages the matching runtime from features/RenderDoc/Renderdoc.
See docs/development/renderdoc.md for runtime updates and compatibility checks.
")
