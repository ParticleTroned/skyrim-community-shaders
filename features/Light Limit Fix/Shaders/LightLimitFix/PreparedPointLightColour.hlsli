// Included inside LightLimitFix after Common.hlsli and Color.hlsli.
StructuredBuffer<PreparedPointLightColour> preparedPointLightColours : register(t41);

bool UsePreparedPointLightColour(bool inReflection)
{
	return Color::IsSceneColorDraw() && !inReflection &&
	       (SharedData::lightLimitFixSettings.ClusteredLightFlags & ClusteredLightFlags::PreparedColours) != 0;
}

float3 GetPointLightColour(Light light, uint globalLightIndex, bool usePreparedColour)
{
	float3 colour;
	[branch] if (usePreparedColour)
	{
#if defined(TRUE_PBR)
		colour = preparedPointLightColours[globalLightIndex].pbr.xyz;
#else
		colour = preparedPointLightColours[globalLightIndex].nonPBR.xyz;
#endif
	}
	else
	{
		colour = Color::PointLight(light.color, (light.lightFlags & LightFlags::Linear) != 0, light.lightFlags);
	}
	return colour;
}
