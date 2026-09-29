set(TRACY_SOURCE_REF a8db9bd8445343ee171439b9479c0a594183bf62)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO wolfpld/tracy
    REF ${TRACY_SOURCE_REF}
    SHA512 9b8697d8e5f1188975802b2f4d9ddd1fb85d744eddd1ca3a03842a4ecb98da8a898a8faa52a27140ba69268087a383906c9d0fe03e3a863107aef24030da1ffe
    HEAD_REF master
    PATCHES build-tools.patch
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        cli-tools VCPKG_CLI_TOOLS
        gui-tools VCPKG_GUI_TOOLS
        on-demand TRACY_ON_DEMAND
        fibers	  TRACY_FIBERS
        verbose   TRACY_VERBOSE
    INVERTED_FEATURES
        crash-handler TRACY_NO_CRASH_HANDLER
)

set(TOOLS_OPTIONS "")
if("cli-tools" IN_LIST FEATURES OR "gui-tools" IN_LIST FEATURES)
    vcpkg_find_acquire_program(PKGCONFIG)
    list(APPEND TOOLS_OPTIONS "-DPKG_CONFIG_EXECUTABLE=${PKGCONFIG}")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS
        -DDOWNLOAD_CAPSTONE=OFF
        -DLEGACY=ON
        -DTRACY_ENABLE=ON
        -DTRACY_GIT_REF=${TRACY_SOURCE_REF}
        ${FEATURE_OPTIONS}
    OPTIONS_RELEASE
        ${TOOLS_OPTIONS}
    OPTIONS_DEBUG
        -DVCPKG_CLI_TOOLS=OFF
        -DVCPKG_GUI_TOOLS=OFF
    MAYBE_UNUSED_VARIABLES
        DOWNLOAD_CAPSTONE
        LEGACY
        TRACY_GIT_REF
)
vcpkg_cmake_install()

vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME Tracy CONFIG_PATH lib/cmake/Tracy)

function(tracy_copy_tool tool_name tool_dir)
    vcpkg_copy_tools(
        TOOL_NAMES "${tool_name}"
        SEARCH_DIR "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/vcpkg-tools/${tool_dir}"
        AUTO_CLEAN
    )
endfunction()

if("cli-tools" IN_LIST FEATURES)
    tracy_copy_tool(tracy-capture capture)
    tracy_copy_tool(tracy-capture-daemon capture)
    tracy_copy_tool(tracy-csvexport csvexport)
    tracy_copy_tool(tracy-import-chrome import)
    tracy_copy_tool(tracy-import-fuchsia import)
    tracy_copy_tool(tracy-update update)
endif()
if("gui-tools" IN_LIST FEATURES)
    tracy_copy_tool(tracy-profiler profiler)
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
