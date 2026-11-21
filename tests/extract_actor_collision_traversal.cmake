include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Utils/ActorUtils.cpp" source)
extract_between("${source}"
    "namespace\n\t{\n\t\tRE::BSVisit::BSVisitControl TraverseCollisionNode("
    "\n\tvoid ForEachActorGeometry(" "collision_traversal_under_test.h")
file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/N/NiSmartPointer.h" source)
extract_between("${source}" "template <class T>\n\tclass NiPointer"
    "\n\t//static_assert" "collision_pointer_under_test.h")
file(READ "${PROJECT_ROOT}/extern/CommonLibSSE-NG/include/RE/N/NiTArray.h" source)
extract_between("${source}" "template <class T, class Allocator>"
    "static_assert(sizeof(NiTArray" "collision_array_under_test.h")
