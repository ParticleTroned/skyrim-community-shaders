# UI metadata is generated during the build, never by drawing hidden pages.
set(CSX_CONTROL_CATALOG_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/control-catalog")
set(CSX_CONTROL_CATALOG "${CSX_CONTROL_CATALOG_DIR}/ControlCatalog.generated.cpp")
file(GLOB_RECURSE CSX_CONTROL_CATALOG_INPUTS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
)
add_custom_command(
    OUTPUT "${CSX_CONTROL_CATALOG_DIR}/catalog.stamp"
    BYPRODUCTS "${CSX_CONTROL_CATALOG}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate-control-catalog.py"
        --source-dir "${PROJECT_SOURCE_DIR}" --output "${CSX_CONTROL_CATALOG}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CSX_CONTROL_CATALOG_DIR}/catalog.stamp"
    DEPENDS ${CSX_CONTROL_CATALOG_INPUTS} "${PROJECT_SOURCE_DIR}/tools/generate-control-catalog.py"
    VERBATIM
)
add_custom_target(csx_control_catalog DEPENDS "${CSX_CONTROL_CATALOG_DIR}/catalog.stamp")
add_dependencies("${PROJECT_NAME}" csx_control_catalog)
target_sources("${PROJECT_NAME}" PRIVATE "${CSX_CONTROL_CATALOG}")
