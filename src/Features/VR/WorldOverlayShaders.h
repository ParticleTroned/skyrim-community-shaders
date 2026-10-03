#pragma once

namespace CSX::WorldOverlays
{
	inline constexpr char ShaderSource[] = R"(
struct Quad { float4 centerWidth; float4 uv; float4 heightOpacityDepth; };
cbuffer Constants : register(b0) {
    row_major float4x4 colorVP;
    row_major float4x4 depthVP;
    row_major float4x4 depthView;
    row_major float4x4 inverseDepthProjection;
    float4 right;
    float4 up;
    float4 depthRect;
    float4 orientation;
    Quad quads[64];
};
Texture2D<float4> atlas : register(t0);
Texture2D<float> sceneDepth : register(t1);
SamplerState linearSampler : register(s0);
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float3 world : TEXCOORD1; nointerpolation float2 opacityDepth : TEXCOORD2; };
Vertex VSMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    static const float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    Quad q = quads[instance]; float2 corner = corners[vertex];
    Vertex o;
    o.world = q.centerWidth.xyz + right.xyz * ((corner.x - .5) * q.centerWidth.w) + up.xyz * ((.5 - corner.y) * q.heightOpacityDepth.x);
    o.position = mul(colorVP, float4(o.world, 1));
    o.position.xy *= orientation.xy;
    o.uv = lerp(q.uv.xy, q.uv.zw, corner);
    o.opacityDepth = q.heightOpacityDepth.yz;
    return o;
}
float4 PSMain(Vertex input) : SV_TARGET {
    float4 text = atlas.Sample(linearSampler, input.uv) * input.opacityDepth.x;
    clip(text.a - 0.0001);
    if (input.opacityDepth.y > .5) {
        float4 dc = mul(depthVP, float4(input.world, 1));
        if (dc.w > 0) {
            float2 ndc = dc.xy / dc.w;
            float2 uv = ndc * float2(.5,-.5) + .5;
            if (all(uv >= 0) && all(uv < 1)) {
                int2 pixel = int2(depthRect.xy + floor(uv * depthRect.zw));
                float raw = sceneDepth.Load(int3(pixel,0));
                // Native forward-Z clear is one. Invalid/background samples cannot occlude.
                if (isfinite(raw) && raw >= 0 && raw < 0.999999) {
                    float2 sampledNdc = ((float2(pixel) + .5 - depthRect.xy) / depthRect.zw - .5) * float2(2,-2);
                    float4 sampledView = mul(inverseDepthProjection, float4(sampledNdc, raw, 1));
                    float surfaceZ = mul(depthView, float4(input.world,1)).z;
                    float sceneZ = sampledView.z / sampledView.w;
                    if (isfinite(sceneZ) && sceneZ > 0 && surfaceZ > sceneZ + .5) discard;
                }
            }
        }
    }
    return text;
}
)";
}
