add_controller_test(
    game_setting_persistence_test
    GameSettingPersistence
    tests/game_setting_persistence_test.cpp
)
set(_game_setting_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/game-setting-persistence"
)
set(_game_setting_test_headers
    "${_game_setting_test_dir}/game_setting_persistence_under_test.h"
    "${_game_setting_test_dir}/cubemap_game_settings_under_test.h"
)
add_custom_command(
    OUTPUT ${_game_setting_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_game_setting_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_game_setting_persistence.cmake"
    DEPENDS
        src/Utils/GameSetting.cpp
        src/Utils/GameSetting.h
        src/Utils/FileSystem.cpp
        src/Utils/Format.cpp
        src/Features/DynamicCubemaps.cpp
        extern/CommonLibSSE-NG/include/SKSE/Impl/PCH.h
        tests/extract_game_setting_persistence.cmake
    VERBATIM
)
target_sources(
    game_setting_persistence_test
    PRIVATE ${_game_setting_test_headers}
)
target_include_directories(
    game_setting_persistence_test
    PRIVATE "${_game_setting_test_dir}" "${CLIB_UTIL_INCLUDE_DIRS}"
)
set_tests_properties(GameSettingPersistence PROPERTIES TIMEOUT 30)
