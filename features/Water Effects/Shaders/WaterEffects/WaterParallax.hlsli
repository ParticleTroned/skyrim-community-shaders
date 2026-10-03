namespace WaterEffects
{
	// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
	// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

	// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
	// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
	// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

	static const int fullParallaxSteps = 16;

	/** Returns the configured sample count for water parallax. */
	int GetFullWaterParallaxSteps()
	{
		return SharedData::waterAppearanceSettings.Enabled ? (int)clamp(SharedData::waterAppearanceSettings.ParallaxQuality, 4u, 64u) : fullParallaxSteps;
	}

	float GetWaterParallaxStrength()
	{
		return SharedData::waterAppearanceSettings.Enabled ? SharedData::waterAppearanceSettings.ParallaxStrength : 1.0;
	}

	/** Projects a finite, non-grazing view vector onto the water surface. */
	bool TryGetProjectedParallaxDirection(float3 viewDirection, out float2 projectedDirection, out float viewDotUp)
	{
		projectedDirection = 0.0.xx;
		viewDotUp = -viewDirection.z;
		float strength = GetWaterParallaxStrength();
		if (!all(isfinite(viewDirection)) || !isfinite(strength) || viewDotUp < 0.05 || strength <= 0.0)
			return false;

		projectedDirection = viewDirection.xy / viewDotUp * strength;
		if (!all(isfinite(projectedDirection))) {
			projectedDirection = 0.0.xx;
			return false;
		}
		return true;
	}

	/** Resolves the final parallax intersection without propagating degenerate divisions. */
	float ResolveParallaxAmount(float currBound, float prevBound, float currHeight, float prevHeight)
	{
		float delta2 = prevBound - prevHeight;
		float delta1 = currBound - currHeight;
		float denominator = delta2 - delta1;
		float numerator = currBound * delta2 - prevBound * delta1;
		if (!isfinite(denominator) || !isfinite(numerator) || abs(denominator) <= 1e-6)
			return currBound;

		float amount = numerator / denominator;
		return isfinite(amount) ? clamp(amount, prevBound, currBound) : currBound;
	}

	float GetMipLevel(float2 coords, Texture2D<float4> tex, float screenNoise)
	{
		// Compute the current gradients:
		float2 actualTextureDims;
		tex.GetDimensions(actualTextureDims.x, actualTextureDims.y);

		// Use hardcoded 512x512 for mip calculation
		float2 textureDims = float2(512.0, 512.0);

		float2 texCoordsPerSize = coords * textureDims;

		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);

		// Find min of change in u and v across quad: compute du and dv magnitude across quad
		float2 dTexCoords = dxSize * dxSize + dySize * dySize;

		// Standard mipmapping uses max here
		float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);

		// Compute the current mip level  (* 0.5 is effectively computing a square root before )
		float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

		// Offset mip level to sample as if texture were 512x512
		float mipOffset = log2(actualTextureDims.x / 512.0);

		mipLevel = max(mipLevel + mipOffset, 0.0);

		// Stochastic mip selection: use screen noise to select between adjacent mip levels
		mipLevel = floor(mipLevel) + (screenNoise < frac(mipLevel) ? 1.0 : 0.0);

		return mipLevel;
	}

	float GetHeight(PS_INPUT input, float2 currentOffset, float3 normalScalesRcp, float3 mipLevels)
	{
		float3 heights;
		heights.x = Normals01Tex.SampleLevel(Normals01Sampler, input.TexCoord1.xy + currentOffset * normalScalesRcp.x, mipLevels.x).w;
		heights.y = Normals02Tex.SampleLevel(Normals02Sampler, input.TexCoord1.zw + currentOffset * normalScalesRcp.y, mipLevels.y).w;
		heights.z = Normals03Tex.SampleLevel(Normals03Sampler, input.TexCoord2.xy + currentOffset * normalScalesRcp.z, mipLevels.z).w;
		heights = 1.0 - heights;
		heights *= NormalsAmplitude.xyz;
		return heights.x + heights.y + heights.z;
	}

	float2 GetParallaxOffset(PS_INPUT input, float3 normalScalesRcp)
	{
		float3 viewDirection = normalize(input.WPosition.xyz);
		float2 parallaxOffsetTS;
		float viewDotUp;
		if (!TryGetProjectedParallaxDirection(viewDirection, parallaxOffsetTS, viewDotUp))
			return 0.0.xx;

		// Parallax scale is also multiplied by normalScalesRcp.
		parallaxOffsetTS *= 20.0;

		float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

		float3 mipLevels;
		mipLevels.x = GetMipLevel(input.TexCoord1.xy, Normals01Tex, screenNoise);
		mipLevels.y = GetMipLevel(input.TexCoord1.zw, Normals02Tex, screenNoise);
		mipLevels.z = GetMipLevel(input.TexCoord2.xy, Normals03Tex, screenNoise);

		int parallaxSteps = GetFullWaterParallaxSteps();
		float stepSize = rcp((float)parallaxSteps);
		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] while (currHeight > currBound)
		{
			prevHeight = currHeight;
			currBound += stepSize;
			currHeight = GetHeight(input, currBound * parallaxOffsetTS.xy, normalScalesRcp, mipLevels);
		}

		float prevBound = currBound - stepSize;

		float parallaxAmount = ResolveParallaxAmount(currBound, prevBound, currHeight, prevHeight);

		return parallaxOffsetTS.xy * parallaxAmount;
	}

#if defined(FLOWMAP)
	float GetFlowmapHeight(PS_INPUT input, float2 uvShift, float multiplier, float offset, float mipLevel)
	{
		FlowmapData flowData = GetFlowmapDataUV(input, uvShift);
		float2 baseUV = offset + (flowData.flowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));
		return FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, baseUV, mipLevel).w;
	}

	float GetFlowmapBlendedHeight(PS_INPUT input, float2 normalMul, float2 uvShift, float mipLevel)
	{
		float height0 = GetFlowmapHeight(input, uvShift, 9.92, 0, mipLevel);
		float height1 = GetFlowmapHeight(input, float2(0, uvShift.y), 10.64, 0.27, mipLevel);
		float height2 = GetFlowmapHeight(input, 0.0.xx, 8, 0, mipLevel);
		float height3 = GetFlowmapHeight(input, float2(uvShift.x, 0), 8.48, 0.62, mipLevel);

		float blendedHeight =
			normalMul.y * (normalMul.x * height2 + (1 - normalMul.x) * height3) +
			(1 - normalMul.y) * (normalMul.x * height1 + (1 - normalMul.x) * height0);

		return blendedHeight;
	}

	bool TryGetFlowmapParallaxDirection(float3 viewDirection, out float2 parallaxDirection, out float viewDotUp)
	{
		if (!TryGetProjectedParallaxDirection(viewDirection, parallaxDirection, viewDotUp))
			return false;

		parallaxDirection.y = -parallaxDirection.y;
		parallaxDirection *= 0.008 * saturate(viewDotUp * 2.0);
		return true;
	}

	/** Keeps grazing and disabled flowmap parallax finite for both marching and UV displacement. */
	float2 GetFlowmapParallaxDirection(float3 viewDirection)
	{
		float2 parallaxDirection;
		float viewDotUp;
		return TryGetFlowmapParallaxDirection(viewDirection, parallaxDirection, viewDotUp) ? parallaxDirection : 0.0.xx;
	}

	float GetFlowmapParallaxAmount(PS_INPUT input, float2 flowmapDims, float3 viewDirection)
	{
		float2 parallaxDir;
		float viewDotUp;
		if (!TryGetFlowmapParallaxDirection(viewDirection, parallaxDir, viewDotUp))
			return 0.0;

		float2 uvShiftPx = 1 / (128 * flowmapDims);

		float quality = (float)GetFullWaterParallaxSteps();
		int numSteps = (int)lerp(quality * 2.0, quality * 0.5, viewDotUp);
		float stepSize = rcp((float)numSteps);

		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] for (int i = 0; i < numSteps && currHeight > currBound; i++)
		{
			prevHeight = currHeight;
			currBound += stepSize;

			PS_INPUT offsetInput = input;
			offsetInput.TexCoord3.xy = input.TexCoord3.xy + currBound * parallaxDir;

			float2 cellBlend = 0.5 + -(-0.5 + abs(frac(offsetInput.TexCoord2.zw * (64 * flowmapDims)) * 2 - 1));
			currHeight = 1.0 - GetFlowmapBlendedHeight(offsetInput, cellBlend, uvShiftPx, 0);
		}

		float prevBound = currBound - stepSize;
		return ResolveParallaxAmount(currBound, prevBound, currHeight, prevHeight);
	}

	float GetFlowmapParallaxHeight(PS_INPUT input, float2 currentOffset, float3 normalScalesRcp, float mipLevel)
	{
		float height = Normals01Tex.SampleLevel(Normals01Sampler, input.TexCoord1.xy + currentOffset * normalScalesRcp.x, mipLevel).w;
		height = 1.0 - height;
		height *= NormalsAmplitude.x;
		return height;
	}

	float2 GetFlowmapParallaxUVOffset(PS_INPUT input, float3 viewDirection, float3 normalScalesRcp)
	{
		float2 parallaxOffsetTS;
		float viewDotUp;
		if (!TryGetProjectedParallaxDirection(viewDirection, parallaxOffsetTS, viewDotUp))
			return 0.0.xx;

		parallaxOffsetTS *= 80.0;

		float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);
		float mipLevel = GetMipLevel(input.TexCoord1.xy, Normals01Tex, screenNoise);

		int parallaxSteps = GetFullWaterParallaxSteps();
		float stepSize = rcp((float)parallaxSteps);
		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] while (currHeight > currBound)
		{
			prevHeight = currHeight;
			currBound += stepSize;
			currHeight = GetFlowmapParallaxHeight(input, currBound * parallaxOffsetTS.xy, normalScalesRcp, mipLevel);
		}

		float prevBound = currBound - stepSize;
		float parallaxAmount = ResolveParallaxAmount(currBound, prevBound, currHeight, prevHeight);

		return parallaxOffsetTS.xy * parallaxAmount;
	}

	float2 GetFlowmapParallaxOffset(PS_INPUT input, float2 flowmapDimensions, float3 viewDirection, float3 normalScalesRcp)
	{
		return GetFlowmapParallaxUVOffset(input, viewDirection, normalScalesRcp);
	}
#endif
}
