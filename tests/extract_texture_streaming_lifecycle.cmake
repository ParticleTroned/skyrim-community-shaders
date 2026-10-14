include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming.cpp" source)
extract_between("${source}" "\tconstexpr std::size_t maximumInventoryRetirementSteps"
    "\tconstexpr std::uint64_t uploadBytesPerFrame" "streaming_retirement_under_test.h")
file(READ "${OUTPUT_DIRECTORY}/streaming_retirement_under_test.h" constants_source)
extract_between("${source}" "void TextureStreaming::State::InvalidateInventory()"
    "void TextureStreaming::State::AdoptLoads(" "streaming_retirement_under_test.h")
file(READ "${OUTPUT_DIRECTORY}/streaming_retirement_under_test.h" retirement_source)
file(WRITE "${OUTPUT_DIRECTORY}/streaming_retirement_under_test.h" "${constants_source}\n${retirement_source}")
extract_between("${source}" "void TextureStreaming::State::Scan("
    "\tif (frontier.empty()) {" "streaming_scan_gate_under_test.h")
file(APPEND "${OUTPUT_DIRECTORY}/streaming_scan_gate_under_test.h" "\tEnterPhase(MaintenancePhase::WorldScan, budget);\n\t++scans;\n}\n")
extract_between("${source}" "std::uint32_t TextureStreaming::State::Demand("
    "bool TextureStreaming::State::ReductionRequested()" "streaming_lifecycle_demand_under_test.h")
extract_between("${source}" "void TextureStreaming::State::Tick()"
    "void TextureStreaming::State::UpdatePressureDiagnostics(" "streaming_tick_under_test.h")
extract_between("${source}" "void TextureStreaming::State::Prune("
    "TextureStreaming::TextureStreaming()" "streaming_prune_under_test.h")
extract_between("${source}" "void TextureStreaming::State::RefreshPreviews()"
    "void TextureStreaming::State::ScanPreviews(" "streaming_preview_state_under_test.h")
extract_between("${source}" "\tstd::uint32_t Target("
    "\tbool ReductionRequested()" "streaming_target_policy_under_test.h")
extract_between("${source}" "\tconst auto found = records.find(work.payload.request.serial);"
    "\tif (!work.reservation) {" "streaming_transaction_validation_under_test.h")
extract_between("${source}" "\t\tconst bool refill = target < record.drop;"
    "\t\tconst bool required = demanded < record.drop;" "streaming_selection_guard_under_test.h")
