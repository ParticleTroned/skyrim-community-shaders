#pragma once

namespace WandBeamRenderer
{
	inline constexpr char ShaderSource[] = R"(
cbuffer WandBeamCB : register(b0)
{
	float4 Endpoints;
	float4 Viewport;
	float4 Colour;
	uint2 DispatchOrigin;
	uint2 DispatchSize;
	float Radius;
	float3 Padding;
};

float Coverage(float2 pixel)
{
	float2 segment = Endpoints.zw - Endpoints.xy;
	float lengthSquared = dot(segment, segment);
	float t = lengthSquared > 1e-6f ? saturate(dot(pixel - Endpoints.xy, segment) / lengthSquared) : 0.0f;
	return saturate(Radius + 0.5f - length(pixel - (Endpoints.xy + t * segment))) * Colour.a;
}

float4 VSMain(uint vertex : SV_VertexID) : SV_Position
{
	const float2 corners[6] = {
		float2(0, 0), float2(1, 0), float2(0, 1),
		float2(0, 1), float2(1, 0), float2(1, 1)
	};
	float2 pixel = DispatchOrigin + corners[vertex] * DispatchSize;
	float2 ndc = (pixel - Viewport.xy) / Viewport.zw * float2(2, -2) + float2(-1, 1);
	return float4(ndc, 0, 1);
}

float4 PSMain(float4 position : SV_Position) : SV_Target
{
	return float4(Colour.rgb, Coverage(position.xy));
}

RWTexture2D<float4> Target : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 thread : SV_DispatchThreadID)
{
	if (any(thread.xy >= DispatchSize))
		return;
	uint2 pixel = DispatchOrigin + thread.xy;
	float alpha = Coverage(float2(pixel) + 0.5f);
	if (alpha > 0.0f) {
		float4 scene = Target[pixel];
		Target[pixel] = float4(lerp(scene.rgb, Colour.rgb, alpha), scene.a);
	}
}
)";
}
