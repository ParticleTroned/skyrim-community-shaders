#ifndef GRASS_TEXTURE_HLSLI
#define GRASS_TEXTURE_HLSLI

float GetGrassTextureNormalLength(Texture2D<float4> sourceTexture)
{
	uint width, height;
	sourceTexture.GetDimensions(width, height);
	float3 marker = sourceTexture.Load(int3(0, int(height) - 1, 0)).xyz * 2.0 - 1.0;
	return length(marker);
}

#endif
