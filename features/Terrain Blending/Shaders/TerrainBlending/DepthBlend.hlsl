RWTexture2D<float> BlendedDepthTexture : register(u0);
RWTexture2D<float> BlendedDepthTexture16 : register(u1);

Texture2D<float> MainDepthTexture : register(t0);
Texture2D<float> TerrainDepthTexture : register(t1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Robustness: never read/write out of bounds if dispatch doesn't match texture dimensions
    uint w, h;
    MainDepthTexture.GetDimensions(w, h);
    if (DTid.x >= w || DTid.y >= h)
        return;

    const float a = MainDepthTexture[DTid.xy];
    const float b = TerrainDepthTexture[DTid.xy];

#ifdef DEPTHBLEND_USE_MAX
    const float mixedDepth = max(a, b);
#else
    const float mixedDepth = min(a, b);
#endif

    BlendedDepthTexture[DTid.xy] = mixedDepth;
    BlendedDepthTexture16[DTid.xy] = mixedDepth;
}
