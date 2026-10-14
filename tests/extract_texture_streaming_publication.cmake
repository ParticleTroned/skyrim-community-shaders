include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming.cpp" source)
extract_between("${source}" "template <class Get, class Set>"
    "\n}\n\nstruct TextureStreaming::State" "streaming_bindings_under_test.h")
extract_between("${source}" "void TextureStreaming::State::Publish("
    "void TextureStreaming::State::CancelTransaction()" "streaming_publication_under_test.h")

extract_between("${source}" "enum class ReadKind"
    "template <class Get, class Set>" "streaming_reader_under_test.h")

extract_between("${source}" "std::optional<std::string> TexturePath("
    "nlohmann::json DiagnosticStatus(" "streaming_path_under_test.h")
extract_between("${source}" "void TextureStreaming::State::InvalidateInventory()"
    "void TextureStreaming::State::MarkInventoryRetired(" "streaming_observation_under_test.h")
file(READ "${OUTPUT_DIRECTORY}/streaming_observation_under_test.h" invalidation_source)
extract_between("${source}" "void TextureStreaming::State::ObserveTexture("
    "void TextureStreaming::State::SuspendPreviewReductions(" "streaming_observation_under_test.h")
file(READ "${OUTPUT_DIRECTORY}/streaming_observation_under_test.h" observation_source)
file(WRITE "${OUTPUT_DIRECTORY}/streaming_observation_under_test.h" "${invalidation_source}\n${observation_source}")

extract_between("${source}" "\tvoid Defer(Block reason, const Record& record)"
    "\tstd::vector<RE::NiPointer<RE::NiAVObject>> previewRoots" "streaming_defer_under_test.h")

extract_between("${source}" "void TextureStreaming::State::SuspendPreviewReductions("
    "void TextureStreaming::State::RefreshPreviews()" "streaming_preview_protection_under_test.h")

extract_between(
    "${source}"
    "\tauto TrackRecordStatus("
    "\tvoid SuspendPreviewReductions("
    "streaming_status_tracking_under_test.h"
)
