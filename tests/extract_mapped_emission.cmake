include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/package/Shaders/Lighting.hlsl" _lighting)
extract_between(
    "${_lighting}"
    "\tfloat3 emitColor = Color::EmitColor(EmitColor);"
    "\n#\tif !defined(TRUE_PBR)\n\tdiffuseColor += emitColor.xyz;"
    mapped_emission.hlsli
)
file(READ "${OUTPUT_DIRECTORY}/mapped_emission.hlsli" MAPPED_EMISSION)
configure_file(
    "${PROJECT_ROOT}/tests/mapped_emission.hlsl.in"
    "${OUTPUT_DIRECTORY}/mapped_emission.hlsl"
    @ONLY
)
