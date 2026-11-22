namespace ParaboloidShadow
{
	static const float GuardTexels = 1.5;

	struct SamplingBounds
	{
		float2 center;
		float2 scale;
		float rimRadius;
		float rimRadiusSquared;
	};

	// The coarser axis must leave room for the comparison filter's footprint.
	SamplingBounds GetSamplingBounds(Texture2DArray<float4> shadowMap, bool dual, bool lowerHalf)
	{
		uint width, height, layers;
		shadowMap.GetDimensions(width, height, layers);
		SamplingBounds bounds;
		bounds.center = float2(0.5, dual ? (lowerHalf ? 0.75 : 0.25) : 0.5);
		bounds.scale = float2(2.0, dual ? 4.0 : 2.0);
		const float2 texelSize = bounds.scale / max(float2(width, height), 1.0);
		bounds.rimRadius = saturate(1.0 - GuardTexels * max(texelSize.x, texelSize.y));
		bounds.rimRadiusSquared = bounds.rimRadius * bounds.rimRadius;
		return bounds;
	}

	// Square-map corners contain no paraboloid depth and otherwise brighten seam shadows.
	float2 ClampTap(float2 uv, SamplingBounds bounds)
	{
		const float2 delta = uv - bounds.center;
		const float2 q = delta * bounds.scale;
		const float radiusSquared = dot(q, q);
		[branch] if (radiusSquared > bounds.rimRadiusSquared)
		{
			uv = bounds.center + delta * (bounds.rimRadius * rsqrt(radiusSquared));
		}
		return uv;
	}
}

float SampleParaboloidShadow(float2 baseUV, float layerIndex, float compareValue, float2x2 rotationMatrix, float radius, bool dual, bool lowerHalf)
{
#if SHADOWFILTER == 0
	const ParaboloidShadow::SamplingBounds bounds = ParaboloidShadow::GetSamplingBounds(TexShadowMapSampler, dual, lowerHalf);
	const float2 uv = ParaboloidShadow::ClampTap(baseUV, bounds);
	return TexShadowMapSampler.Sample(SampShadowMapSampler, float3(uv, layerIndex)).x >= compareValue ? 1.0 : 0.0;
#elif SHADOWFILTER == 1 || SHADOWFILTER == 3
	const ParaboloidShadow::SamplingBounds bounds = ParaboloidShadow::GetSamplingBounds(TexShadowMapSamplerComp, dual, lowerHalf);
#	if SHADOWFILTER == 1
	const float2 uv = ParaboloidShadow::ClampTap(baseUV, bounds);
	return TexShadowMapSamplerComp.SampleCmpLevelZero(SampShadowMapSamplerComp, float3(uv, layerIndex), compareValue).x;
#	else
	float visibility = 0.0;
	[unroll] for (int i = 0; i < 8; ++i)
	{
		const float2 offset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix) * radius;
		const float2 uv = ParaboloidShadow::ClampTap(baseUV + offset, bounds);
		visibility += TexShadowMapSamplerComp.SampleCmpLevelZero(SampShadowMapSamplerComp, float3(uv, layerIndex), compareValue).x;
	}
	return visibility / 8.0;
#	endif
#else
	return 0.0;
#endif
}
