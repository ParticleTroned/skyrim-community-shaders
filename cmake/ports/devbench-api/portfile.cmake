# MIT-licensed cross-plugin API used to register tools with the DevBench SKSE host.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ParticleTroned/devbench
    REF 4b8e6f05ab6cd30f545f203eccf557c51f1b8499
    SHA512 16414395c7cf8d34e7921bda7a6322b10a7056bd6ee0e4f85ff51b0f98453819189669b1ce5430bb776a0a431e5a3627ca6ed6caccb12da7609a1ef50616b3f3
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
