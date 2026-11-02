#include "Upscaling/UpscaleVS.hlsl"

#ifdef PSHADER

#	include "Common/ReverseZ.hlsli"

Texture2D<float> InputTexture : register(t0);

float main(VS_OUTPUT input) :
	SV_Target
{
	// SE frame-generation contexts are created before scene depth activation.
	// Keep their shared resource and camera constants in a stable standard convention.
	return FrameBuffer::ToStandardDepth(InputTexture.Load(int3(input.Position.xy, 0)));
}

#endif
