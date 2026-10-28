include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming.cpp" source)
extract_between("${source}" "StreamingTextures::InventoryWork TextureStreaming::State::InventoryMode()"
    "void TextureStreaming::State::UpdatePressureDiagnostics(" "streaming_tick_under_test.h")
extract_between("${source}" "void TextureStreaming::State::Select("
    "void TextureStreaming::State::Publish(" "streaming_select_under_test.h")
