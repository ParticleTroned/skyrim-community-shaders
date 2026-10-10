include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming/GeometryDemand.cpp" source)
extract_between("${source}" "RE::BSLightingShaderMaterialBase* StaticMaterial("
    "double UnitsPerUV(" "streaming_material_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming.cpp" source)
extract_between("${source}" "void TextureStreaming::Configure("
    "nlohmann::json TextureStreaming::GetStatus()" "streaming_settings_under_test.h")
extract_between("${source}" "void TextureStreaming::DrawSettingsEnabledControl()"
    "void TextureStreaming::DrawSettings()" "streaming_enabled_under_test.h")
string(FIND "${source}" "void TextureStreaming::DrawSettings()" start)
string(SUBSTRING "${source}" ${start} -1 draw)
file(WRITE "${OUTPUT_DIRECTORY}/streaming_ui_under_test.h" "${draw}")
file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/B/BSShaderProperty.h" source)
extract_between("${source}" "enum class EShaderPropertyFlag :" "enum class EShaderPropertyFlag8 :" "streaming_flags_under_test.h")
file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/B/BSShaderMaterial.h" source)
extract_between("${source}" "enum class Feature" "virtual ~BSShaderMaterial()" "streaming_features_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/SettingsPage.h" source)
extract_between("${source}" "struct Section" "/** One mutually exclusive option" "streaming_sections_under_test.h")
