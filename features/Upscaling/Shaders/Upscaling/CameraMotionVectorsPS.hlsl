Texture2D<float> DepthTexture : register(t0);

// b1 is saved and restored by the fullscreen state scope in Upscaling.cpp.
cbuffer CameraMotionVectorsCB : register(b1)
{
	// Byte layout and mul() convention match FrameBuffer's cb12 matrices.
	row_major float4x4 CurViewProjUnjitteredInverse;
	row_major float4x4 PrevViewProjUnjittered;
};

struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD;
};

// Camera-only reprojection for frames where no geometry pass writes motion vectors
// (the main menu). Only the camera moves there, so depth + the view-proj delta
// fully determine each pixel's motion. Not a substitute for geometry MVs in gameplay.
float2 main(PS_INPUT input) : SV_Target
{
	float depth = DepthTexture.Load(int3(input.Position.xy, 0));
	float4 clipPos = float4(2 * input.TexCoord.x - 1, 1 - 2 * input.TexCoord.y, depth, 1);

	// Homogeneous throughout: the intermediate world position needs no divide.
	float4 world = mul(CurViewProjUnjitteredInverse, clipPos);
	float4 prevClip = mul(PrevViewProjUnjittered, world);
	if (prevClip.w <= 1e-5)
		return 0;

	// GetSSMotionVector convention: NDC delta scaled by (-0.5, 0.5) is a UV-space velocity.
	float2 prevNDC = prevClip.xy / prevClip.w;
	return float2(-0.5, 0.5) * (clipPos.xy - prevNDC);
}
