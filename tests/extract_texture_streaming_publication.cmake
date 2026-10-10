include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming.cpp" source)
extract_between("${source}" "template <class Get, class Set>"
    "\n}\n\nstruct TextureStreaming::State" "streaming_bindings_under_test.h")
extract_between("${source}" "void TextureStreaming::State::Publish("
    "void TextureStreaming::State::CancelTransaction()" "streaming_publication_under_test.h")
