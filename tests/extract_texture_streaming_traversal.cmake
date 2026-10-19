include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming.cpp" source)
extract_between("${source}" "template <class Children, class Visitor>"
    "std::atomic_bool originHookReady" "streaming_children_under_test.h")
file(READ "${PROJECT_ROOT}/src/Features/TextureStreaming/Preview.cpp" source)
extract_between("${source}" "void AddRoot("
    "\n\t}\n\tvoid Install()" "streaming_preview_roots_under_test.h")
file(READ "${PROJECT_ROOT}/src/Utils/Game.h" source)
extract_between("${source}" "[[nodiscard]] inline bool IsLikelyValidPointer("
    "inline constexpr float DirectionalLightDiscontinuityThreshold" "streaming_pointer_under_test.h")
file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/N/NiTArray.h" source)
extract_between("${source}" "template <class T, class Allocator>"
    "static_assert(sizeof(NiTArray" "streaming_array_under_test.h")
