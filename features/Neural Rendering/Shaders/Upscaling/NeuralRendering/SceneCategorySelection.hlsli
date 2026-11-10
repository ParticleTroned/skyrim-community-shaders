#include "Common/CharacterCategoryMask.hlsli"
#include "Common/NeuralRenderingVisibility.hlsli"

cbuffer NRSceneCategoryCB : register(b2)
{
	uint4 SceneSourceMapping;
	uint4 SceneAuthoredRegion;
	float4 SceneReductions;
	float4 SceneOptions;
	float4 SceneDepthLinearization;
};

Texture2D<unorm float2> SceneCategories : register(t5);
Texture2D<float> SceneAuthoredDepth : register(t6);
Texture2D<float> SceneCurrentDepth : register(t7);

float SceneCategorySelectionWeight(uint2 pixel)
{
	const uint2 source = SceneSourceMapping.xy + pixel;
	const uint2 eyePixel = source - uint2(SceneSourceMapping.z, 0u);
	if (any(eyePixel < SceneAuthoredRegion.xy) || any(eyePixel >= SceneAuthoredRegion.xy + SceneAuthoredRegion.zw))
		return 1.0;
	const float2 tuple = SceneCategories.Load(int3(source, 0));
	const uint category = CharacterCategoryMask::DecodeCategory(tuple);
	const float reduction = CharacterCategoryMask::Strength(category, SceneReductions, SceneOptions.x);
	if (reduction <= 0.0)
		return 1.0;
	if (SceneOptions.y != 0.0) {
		const float current = NeuralRenderingVisibility::LinearDepth(SceneCurrentDepth.Load(int3(pixel, 0)), SceneDepthLinearization);
		const float authored = NeuralRenderingVisibility::LinearDepth(SceneAuthoredDepth.Load(int3(source, 0)), SceneDepthLinearization);
		if (!NeuralRenderingVisibility::Visible(current, authored, SceneOptions.z))
			return 1.0;
	}
	// Match the materialized R8 mask before applying the reconstructed edit.
	return round(saturate(1.0 - reduction * CharacterCategoryMask::DecodeFocusWeight(tuple)) * 255.0) / 255.0;
}
