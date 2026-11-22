#pragma once

namespace MenuOverlayGeometry
{
	inline constexpr char ShaderSource[] = R"(
cbuffer OverlayCompositeCB : register(b0)
{
	uint2 TargetSize;
	uint2 DispatchOrigin;
	uint2 DispatchSize;
	uint VertexCount;
	uint Padding;
	float4 PolygonPixels[12];
	float4 PolygonUV[12];
	float MenuMipLevel;
	float3 Padding2;
};

Texture2D<float4> MenuTexture : register(t0);
SamplerState MenuSampler : register(s0);
RWTexture2D<float4> Target : register(u0);

bool Barycentric(float2 p, float2 a, float2 b, float2 c, float invDenom, out float3 bary)
{
	float2 v0 = b - a;
	float2 v1 = c - a;
	float2 v2 = p - a;
	if (invDenom == 0.0f) {
		bary = 0.0f;
		return false;
	}

	float u = (v2.x * v1.y - v1.x * v2.y) * invDenom;
	float v = (v0.x * v2.y - v2.x * v0.y) * invDenom;
	float w = 1.0f - u - v;
	bary = float3(w, u, v);
	return u >= 0.0f && v >= 0.0f && w >= 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID.x >= DispatchSize.x || dispatchThreadID.y >= DispatchSize.y) {
		return;
	}

	uint2 targetPixel = DispatchOrigin + dispatchThreadID.xy;
	if (targetPixel.x >= TargetSize.x || targetPixel.y >= TargetSize.y) {
		return;
	}

	float2 p = float2(targetPixel) + 0.5f;
	float2 uv = 0.0f;
	bool covered = false;
	for (uint i = 1; i + 1 < min(VertexCount, 12u); ++i) {
		float3 bary;
		if (!Barycentric(p, PolygonPixels[0].xy, PolygonPixels[i].xy, PolygonPixels[i + 1].xy, PolygonUV[i].z, bary))
			continue;
		float invW = dot(bary, float3(PolygonPixels[0].z, PolygonPixels[i].z, PolygonPixels[i + 1].z));
		if (invW <= 0.0f)
			return;
		uv = (bary.x * PolygonUV[0].xy + bary.y * PolygonUV[i].xy + bary.z * PolygonUV[i + 1].xy) / invW;
		covered = true;
		break;
	}
	if (!covered)
		return;

	uv = saturate(uv);

	float4 menuColor = MenuTexture.SampleLevel(MenuSampler, uv, MenuMipLevel);
	menuColor.a = saturate(menuColor.a);
	if (menuColor.a <= 0.001f) {
		return;
	}

	float4 sceneColor = Target[targetPixel];
	Target[targetPixel] = float4(lerp(sceneColor.rgb, menuColor.rgb, menuColor.a), sceneColor.a);
}
)";
}
