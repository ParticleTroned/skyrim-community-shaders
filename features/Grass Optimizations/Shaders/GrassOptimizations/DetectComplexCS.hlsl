#include "Common/GrassTexture.hlsli"

Texture2D<float4> BaseTexture : register(t0);
RWStructuredBuffer<float> NormalLength : register(u0);

[numthreads(1, 1, 1)] void main() {
	NormalLength[0] = GetGrassTextureNormalLength(BaseTexture);
}
