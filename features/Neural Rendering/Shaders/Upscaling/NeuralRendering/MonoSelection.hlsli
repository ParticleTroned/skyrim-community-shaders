#ifndef NR_MONO_SELECTION
#define NR_MONO_SELECTION
Texture2D<unorm float2> Categories : register(t4);
Texture2D<float> AuthoredDepth : register(t5);
Texture2D<float> CurrentDepth : register(t6);

float SelectionWeight(uint2 pixel)
{
	if (Options.x == 0u) return 1.0;
	const uint2 authored = uint2(clamp((float2(pixel) + 0.5) * CategorySize / SourceSize + Sampling.xy, 0.0, float2(CategorySize - 1u)));
	const float2 tuple = Categories.Load(int3(authored, 0));
	const uint category = CharacterCategoryMask::DecodeCategory(tuple);
	if (category == 0u || category > 5u) return Selection.y;
	if ((Options.y & (1u << min(category, 5u))) == 0u) return Selection.y;
	const uint2 guide = uint2(clamp((float2(pixel) + 0.5) * GuideSize / SourceSize + Sampling.xy, 0.0, float2(GuideSize - 1u)));
	const float current = NeuralRenderingVisibility::LinearDepth(CurrentDepth.Load(int3(guide, 0)), DepthLinearization);
	const float captured = NeuralRenderingVisibility::LinearDepth(AuthoredDepth.Load(int3(authored, 0)), DepthLinearization);
	if (!NeuralRenderingVisibility::Visible(current, captured, Selection.w)) return Selection.y;
	const float strength = CharacterCategoryMask::Strength(category, CategoryStrengths, Selection.x);
	const float focus = CharacterCategoryMask::DecodeFocusWeight(tuple);
	return round(saturate(lerp(Selection.y, strength, focus)) * 255.0) / 255.0;
}

#endif
