namespace TerrainShadows
{
	Texture2D<float2> ShadowHeightTexture : register(t60);

	float2 GetTerrainShadowUV(float2 xy)
	{
		return xy * fd.terraOccSettings.Scale.xy + fd.terraOccSettings.Offset.xy;
	}

	float GetTerrainZ(float norm_z)
	{
		return lerp(fd.terraOccSettings.ZRange.x, fd.terraOccSettings.ZRange.y, norm_z) - 256;
	}

	float2 GetTerrainZ(float2 norm_z)
	{
		return float2(GetTerrainZ(norm_z.x), GetTerrainZ(norm_z.y));
	}

	float GetTerrainShadow(const float3 worldPos, SamplerState samp)
	{
		if (!fd.terraOccSettings.EnableTerrainShadow)
			return 1.0;
		float2 shadowHeight = GetTerrainZ(ShadowHeightTexture.SampleLevel(samp, GetTerrainShadowUV(worldPos.xy), 0));
		return saturate((worldPos.z - shadowHeight.y) / (shadowHeight.x - shadowHeight.y));
		;
	}
}
