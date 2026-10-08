if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
set(_output "")
function(extract_between path start end)
    file(READ "${PROJECT_ROOT}/${path}" _source)
    string(FIND "${_source}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Game settings test cannot find ${start}")
    endif()
    string(SUBSTRING "${_source}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "Game settings test cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _extracted)
    set(_output "${_output}\n${_extracted}" PARENT_SCOPE)
endfunction()

set(_output "namespace SKSE::stl {\n")
extract_between("extern/CommonLibSSE-NG/include/SKSE/Impl/PCH.h" "[[nodiscard]] inline auto utf8_to_utf16(" "\n\t\tinline bool report_and_error(")
set(_output "${_output}\n}\nnamespace Util {\n")
extract_between("src/Utils/GameSetting.h" "struct GameSetting" "\n\t/**")
extract_between("src/Utils/GameSetting.cpp" "static constexpr std::string_view CS_SETTINGS_PATH" "\n\tvoid DumpSettingsOptions")
extract_between("src/Utils/Format.cpp" "bool IEquals(" "\n\tstd::string GetShaderDefinesSuffix(")
set(_output
    "${_output}\nnamespace FileHelpers {\nbool WriteTextFileAtomic(const std::filesystem::path&, std::string_view, std::string&, bool allowDirectFallback = true);\nnamespace {\n"
)
extract_between("src/Utils/FileSystem.cpp" "bool WriteTextFileDirect(" "\n\t\tDeletionResult SafeDelete(")
extract_between("src/Utils/FileSystem.cpp" "bool WriteTextFileAtomic(" "\n\t\tstd::string SanitizeFileName(")
set(_output "${_output}\n}\n")
extract_between("src/Utils/GameSetting.cpp" "\tnamespace\n\t{\n\t\tbool ReadGameSettingsIni(" "}  // namespace Util")
set(_output "${_output}\n}\n")
file(
    WRITE "${OUTPUT_DIRECTORY}/game_setting_persistence_under_test.h"
    "${_output}"
)

set(_output "")
extract_between("src/Features/DynamicCubemaps.cpp" "void DynamicCubemaps::LoadSettings(" "void DynamicCubemaps::SaveSettings(")
extract_between("src/Features/DynamicCubemaps.cpp" "void DynamicCubemaps::OnSettingsSaved()" "void DynamicCubemaps::RestoreDefaultSettings()")
extract_between("src/Features/DynamicCubemaps.cpp" "void DynamicCubemaps::DataLoaded()" "void DynamicCubemaps::PostPostLoad()")
file(
    WRITE "${OUTPUT_DIRECTORY}/cubemap_game_settings_under_test.h"
    "${_output}"
)
