file(READ "${PROJECT_ROOT}/src/EngineFixes/VRGrassLifetimeFix.cpp" _source)
string(REPLACE "#include \"VRGrassLifetimeFix.h\"" "" _source "${_source}")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_DIRECTORY}/vr_grass_lifetime_under_test.h" "${_source}")
