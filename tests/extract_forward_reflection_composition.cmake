include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/package/Shaders/Lighting.hlsl" _lighting)
file(READ "${PROJECT_ROOT}/package/Shaders/ISSAOComposite.hlsl" _composite)
extract_between(
    "${_lighting}"
    "\tdiffuseColor = reflectionDiffuseColor;"
    "\n\tfloat3 fogColor ="
    composition.hlsli
)
extract_between(
    "${_lighting}"
    "\tfloat3 fogColor = Color::Fog(input.FogParam.xyz);"
    "\n\tcolor.xyz = SanitizeFloat3(color.xyz);"
    fog.hlsli
)
extract_between(
    "${_composite}"
    "\t\tcomposedColor.xyz = FogNearColor.w *"
    "\n\t}"
    opaque_fog.hlsli
)
extract_between(
    "${_lighting}"
    "bool IsFiniteFloat3(float3 v)"
    "\nfloat3 SafeNormalize3("
    sanitize.hlsli
)
file(READ "${OUTPUT_DIRECTORY}/composition.hlsli" COMPOSITION)
file(READ "${OUTPUT_DIRECTORY}/fog.hlsli" FOG)
file(READ "${OUTPUT_DIRECTORY}/opaque_fog.hlsli" OPAQUE_FOG)
file(READ "${OUTPUT_DIRECTORY}/sanitize.hlsli" SANITIZE)
configure_file(
    "${PROJECT_ROOT}/tests/forward_reflection_composition.hlsl.in"
    "${OUTPUT_DIRECTORY}/forward_reflection_composition.hlsl"
    @ONLY
)
