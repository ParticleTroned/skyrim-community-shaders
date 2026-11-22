# MIT-licensed cross-plugin API used to register tools with the devbench SKSE host.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ParticleTroned/devbench
    REF 096e67b00852c32ac11b35f9e50e6d89cfa7c2ce
    SHA512 edcdbcf80dd20e2d76720303174c356d2124d66e24f2bb90f453c1b827e1aff6b225f0d5c0e84f10e311fa5d90a5c82e0ef608b8d8259d515d8f792f78490811
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
