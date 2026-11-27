include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/package/Shaders/Lighting.hlsl" _lighting)
extract_between(
    "${_lighting}"
    "\t\tfloat2 glintUV = uv;"
    "\n\t}\n#\tendif"
    glint_precompute.hlsli
)
file(READ "${OUTPUT_DIRECTORY}/glint_precompute.hlsli" GLINT_PRECOMPUTE)
configure_file(
    "${PROJECT_ROOT}/tests/glint_parallax.hlsl.in"
    "${OUTPUT_DIRECTORY}/glint_parallax.hlsl"
    @ONLY
)
