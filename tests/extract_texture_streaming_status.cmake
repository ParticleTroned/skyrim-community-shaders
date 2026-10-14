include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming.cpp" source)
extract_between(
    "${source}"
    "\tstruct StatusSnapshot"
    "\n\t/** The caller holds mutex; inventory totals"
    "streaming_status_snapshot_under_test.h"
)
extract_between(
    "${source}"
    "TextureStreaming::State::StatusSnapshot TextureStreaming::State::CopyStatusSnapshot() const"
    "void TextureStreaming::State::InvalidateInventory()"
    "streaming_status_copy_under_test.h"
)
extract_between(
    "${source}"
    "nlohmann::json TextureStreaming::GetStatus() const"
    "void TextureStreaming::DrawSettingsEnabledControl()"
    "streaming_status_under_test.h"
)
extract_between(
    "${source}"
    "\tnlohmann::json DiagnosticStatus("
    "\tenum class ReadKind"
    "streaming_status_diagnostics_under_test.h"
)
extract_between(
    "${source}"
    "\tstruct TerrainReduction"
    "\t} terrainReduction;"
    "streaming_status_terrain_under_test.h"
)
file(
    APPEND "${OUTPUT_DIRECTORY}/streaming_status_terrain_under_test.h"
    "\t};\n"
)
extract_between(
    "${source}"
    "bool TextureStreaming::State::ReductionRequested() const noexcept"
    "void TextureStreaming::State::Select("
    "streaming_status_reduction_under_test.h"
)
