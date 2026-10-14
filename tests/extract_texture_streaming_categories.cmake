include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/TruePBR/BSLightingShaderMaterialPBRLandscape.cpp" source)
extract_between("${source}" "const BSLightingShaderMaterialPBRLandscape* BSLightingShaderMaterialPBRLandscape::GetRegistered("
    "bool BSLightingShaderMaterialPBRLandscape::HasGlint()" "streaming_landscape_lookup_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming/GeometryDemand.cpp" source)
extract_between("${source}" "static bool SharesTextureResource("
    "double RequiredEdge(" "streaming_material_under_test.h")
extract_between("${source}" "double RequiredEdge("
    "\n}" "streaming_demand_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming.cpp" source)
extract_between("${source}" "void TextureStreaming::PostPostLoad()"
    "void TextureStreaming::Reset()" "streaming_startup_under_test.h")
extract_between("${source}" "constexpr auto streamingConflictWarning"
    "constexpr std::size_t maximumRecords" "streaming_conflict_under_test.h")
extract_between("${source}" "bool TextureStreaming::State::ReductionRequested() const noexcept"
    "void TextureStreaming::State::Select(" "streaming_mode_under_test.h")
extract_between("${source}" "void TextureStreaming::State::InvalidateInventory()"
    "void TextureStreaming::State::MarkInventoryRetired(" "streaming_invalidation_under_test.h")
extract_between("${source}" "void TextureStreaming::State::UpdateConfigurationDetail()"
    "nlohmann::json TextureStreaming::GetStatus()" "streaming_settings_under_test.h")
string(FIND "${source}" "void TextureStreaming::DrawSettingsEnabledControl()" start)
string(SUBSTRING "${source}" ${start} -1 draw)
file(WRITE "${OUTPUT_DIRECTORY}/streaming_ui_under_test.h" "${draw}")
file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/B/BSShaderProperty.h" source)
extract_between("${source}" "enum class EShaderPropertyFlag :" "enum class EShaderPropertyFlag8 :" "streaming_flags_under_test.h")
file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/B/BSShaderMaterial.h" source)
extract_between("${source}" "enum class Feature" "virtual ~BSShaderMaterial()" "streaming_features_under_test.h")
file(READ "${PROJECT_ROOT}/src/Menu/SettingsPage.h" source)
extract_between("${source}" "struct Section" "/** One mutually exclusive option" "streaming_sections_under_test.h")

file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/B/BSGeometry.h" source)
extract_between("${source}" "enum class Type" "struct MODEL_DATA" "streaming_geometry_types_under_test.h")
