# MIT-licensed cross-plugin API used to register tools with the devbench SKSE host.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/devbench
    REF 5bc4ad7c2a11063747ed4388405c30c3a3595c9b
    SHA512 0ca6ced4427904fbd3da6e22f7509564e0b3d35bbef1952e60e551196d20f78212e7f970c5e85bd3d84f936f0df3db600d05bfabb30508435d26287b60fdea08
    HEAD_REF main
)

file(INSTALL "${SOURCE_PATH}/include/DevBenchAPI.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/include/DevBenchAPI.cpp"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/src")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/devbench-api-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${SOURCE_PATH}/include/DevBenchAPI.LICENSE.txt"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
