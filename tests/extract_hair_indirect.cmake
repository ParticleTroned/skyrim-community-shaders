include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/package/Shaders/Lighting.hlsl" _lighting)
extract_between(
    "${_lighting}"
    "#\tif defined(CS_HAIR_SHADING)\n\tif (SharedData::hairSpecularSettings.Enabled) {\n\t\tif (SharedData::hairSpecularSettings.HairMode == 1)"
    "\n\tfloat3 directionalAmbientColor ="
    hair_ambient.hlsli
)
file(READ "${OUTPUT_DIRECTORY}/hair_ambient.hlsli" HAIR_AMBIENT)
extract_between(
    "${_lighting}"
    "\tindirectContext = CreateIndirectLightingContext(ambientNormal,"
    "\n#\tif defined(WETTERNESS)"
    hair_indirect_context.hlsli
)
file(READ "${OUTPUT_DIRECTORY}/hair_indirect_context.hlsli" HAIR_CONTEXT)
file(READ "${PROJECT_ROOT}/package/Shaders/Common/LightingEval.hlsli" _eval)
extract_between(
    "${_eval}"
    "float3 SafeNormalizeLighting("
    "\n#if defined(TRUE_PBR)"
    safe_normalize.hlsli
)
extract_between(
    "${_eval}"
    "IndirectContext CreateIndirectLightingContext("
    "\nfloat3 VanillaSpecular("
    indirect_factory.hlsli
)
extract_between(
    "${_eval}"
    "void GetIndirectLobeWeights("
    "\n#if defined(WETTERNESS)"
    indirect_lobes.hlsli
)
file(READ "${OUTPUT_DIRECTORY}/safe_normalize.hlsli" SAFE_NORMALIZE)
file(READ "${OUTPUT_DIRECTORY}/indirect_factory.hlsli" INDIRECT_FACTORY)
file(READ "${OUTPUT_DIRECTORY}/indirect_lobes.hlsli" INDIRECT_LOBES)
configure_file(
    "${PROJECT_ROOT}/tests/hair_indirect.hlsl.in"
    "${OUTPUT_DIRECTORY}/hair_indirect.hlsl"
    @ONLY
)
