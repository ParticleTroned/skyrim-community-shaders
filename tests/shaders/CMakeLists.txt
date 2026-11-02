# Shader Tests using ShaderTestFramework
cmake_minimum_required(VERSION 4.2)

# Option to enable/disable shader tests
option(BUILD_SHADER_TESTS "Build shader unit tests (runs automatically before packaging)" ON)

if(NOT BUILD_SHADER_TESTS)
    message(STATUS "Shader tests disabled")
    return()
endif()

message(STATUS "Configuring shader unit tests...")

# Keep test-framework development files out of the game install tree.
include(FetchContent)

FetchContent_Declare(
    ShaderTestFramework
    GIT_REPOSITORY https://github.com/KStocky/ShaderTestFramework.git
    # Pinned to specific commit for build reproducibility (Dec 14, 2025)
    GIT_TAG dcbe56c46d474948c27134f570c50ff7110648f6
    GIT_CONFIG core.longpaths=true  # Enable long path support for Windows
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(ShaderTestFramework)

# Get the ShaderTestFramework source directory to access its CMake utilities
FetchContent_GetProperties(ShaderTestFramework SOURCE_DIR STF_SOURCE_DIR)

# Add STF's cmake library and our project's cmake to CMAKE_MODULE_PATH
list(APPEND CMAKE_MODULE_PATH ${STF_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Include STF's helper modules
include(AssetDependencyManagement)
include(STF)

# Check for Windows Graphics Tools (required for D3D12 shader tests)
include(DetectGraphicsTools)

# Set shader source paths
set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/package/Shaders)
set(SHADER_SOURCE_REL_DIR "Shaders")

# Set feature shader source paths (for Hair Specular, etc.)
set(FEATURES_DIR ${CMAKE_SOURCE_DIR}/features)

# ============================================================================
# RUNTIME DISCOVERY: No code generation needed!
# ============================================================================
# Tests are discovered by scanning HLSL files at runtime.
# No build-time generation, no external tools, just pure C++ reflection.

# Create shader test executable
add_executable(shader_tests
    minimal_test.cpp                   # Provides main()
    runtime_discovered_tests.cpp       # Single test that discovers & runs all HLSL tests
    shader_permutation_compile_tests.cpp # Production FXC compile coverage for runtime permutations
    reverse_z_tests.cpp               # Native depth order, scene targets and shader conventions
    grass_billboard_tests.cpp         # Far billboard lighting on both faces
    upscaling_depth_tests.cpp          # Mono DLSS/FSR depth encoding and group boundaries
    sss_skin_color_tests.cpp           # Saturation energy, masks and preserved SE skin routing

    # Headers (listed for IDE integration and dependency tracking)
    test_common.h                      # Common test utilities
    test_helpers_unified.h             # Unified test helper macros
    runtime_test_discovery.h           # Runtime HLSL test discovery
)

# Set C++23 standard (required by ShaderTestFramework)
set_property(TARGET shader_tests PROPERTY CXX_STANDARD 23)
set_property(TARGET shader_tests PROPERTY CXX_STANDARD_REQUIRED ON)

if(MSVC)
    if(TARGET ShaderTestFramework)
        # Suppress C4864 in tuplet (via ShaderTestFramework) for VS 2026 migration
        target_compile_options(ShaderTestFramework PUBLIC /wd4864)
        # Suppress C5285 in float16_fetch (via ShaderTestFramework) - MSVC 14.51+ forbids
        # specializing std::is_floating_point_v/is_arithmetic_v/is_signed_v per N5014,
        # but float16_t.hpp needs these specializations for its float16_t type.
        target_compile_options(ShaderTestFramework PUBLIC /wd5285)
    endif()
endif()

# Set VS debugger working directory
set_target_properties(shader_tests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:shader_tests>"
)

# Initialize asset dependency management for shader files
asset_dependency_init(shader_tests)

# Add the shader source directory to be copied relative to the exe
target_add_asset_directory(shader_tests ${SHADER_SOURCE_DIR} "/${SHADER_SOURCE_REL_DIR}")

# Add feature shader directories (Hair Specular, etc.) to virtual shader paths
# These are mapped so that #include "/Shaders/Hair/Hair.hlsli" works in tests
file(GLOB FEATURE_SHADER_DIRS
    LIST_DIRECTORIES true
    "${FEATURES_DIR}/*/Shaders"
)
foreach(FEATURE_SHADER_DIR ${FEATURE_SHADER_DIRS})
    if(IS_DIRECTORY ${FEATURE_SHADER_DIR})
        target_add_asset_directory(shader_tests ${FEATURE_SHADER_DIR} "/${SHADER_SOURCE_REL_DIR}")
    endif()
endforeach()

# Optional: Make shader tests depend on HLSL test files for automatic rebuild triggers
# This ensures that changing HLSL test files triggers a relink (to update copied assets)
file(GLOB_RECURSE HLSL_TEST_FILES "${SHADER_SOURCE_DIR}/Tests/Test*.hlsl")
list(LENGTH HLSL_TEST_FILES HLSL_TEST_COUNT)
target_sources(shader_tests PRIVATE ${HLSL_TEST_FILES})
set_source_files_properties(${HLSL_TEST_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

message(STATUS "Found ${HLSL_TEST_COUNT} HLSL test files:")
foreach(TEST_FILE ${HLSL_TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME)
    message(STATUS "  - ${TEST_NAME}")
endforeach()

# Link ShaderTestFramework BEFORE copying assets (so dependencies are resolved)
target_link_libraries(shader_tests PRIVATE ShaderTestFramework)
target_link_libraries(shader_tests PRIVATE d3d11 d3dcompiler)

# Copy all dependent assets (DLLs, shaders, etc.) - must be AFTER target_link_libraries
copy_all_dependent_assets(shader_tests)

# Share the packaged headers and library with the host tests to keep their ABI aligned.
if(NOT TARGET Catch2::Catch2)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()
target_link_libraries(shader_tests PRIVATE Catch2::Catch2)

# Define USE_PIX for PIX capture support
target_compile_definitions(shader_tests PRIVATE USE_PIX)

# ============================================================================
# CTest Integration
# ============================================================================
# Register with CTest so CI can discover and run tests
enable_testing()
add_test(
    NAME ShaderTests
    COMMAND shader_tests --reporter compact
    WORKING_DIRECTORY $<TARGET_FILE_DIR:shader_tests>
)

# Set test properties for better CI output
set_tests_properties(ShaderTests PROPERTIES
    TIMEOUT 1200  # Include optimized FXC permutations alongside GPU unit tests.
    LABELS "Shaders;UnitTests"
)

message(STATUS "Shader tests configured successfully")
message(STATUS "  Tests will run automatically before packaging and deployment")
