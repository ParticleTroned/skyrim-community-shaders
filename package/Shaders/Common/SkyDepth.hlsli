#ifndef __SKY_DEPTH_HLSLI__
#define __SKY_DEPTH_HLSLI__

#include "Common/Permutation.hlsli"
#include "Common/ReverseZ.hlsli"

namespace SkyDepth
{
	// Float rasterizer bias near zero is too small to reliably separate the sky layers.
	static const float depthBiasUnit24 = 1.0 / 16777216.0;
	static const float moonMaskBiasUnits = 3.0;
	static const float moonAndCloudBiasUnits = 9.0;

	/** Set sky clip depth; clouds receive their reversed ordering offset only after clipping. */
	float FarPlaneClipZ(float clipW, uint eyeIndex)
	{
#ifdef REVERSE_Z
#	if defined(MOONMASK)
		float biasUnits = moonMaskBiasUnits;
#	elif defined(CLOUDS)
		float biasUnits = 0.0;
#	elif defined(TEX)
		float biasUnits = (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::IsMoon) ? moonAndCloudBiasUnits : 0.0;
#	else
		float biasUnits = 0.0;
#	endif
		return FrameBuffer::IsReverseProjection(eyeIndex) ? clipW * (biasUnits * depthBiasUnit24) : clipW;
#else
		return clipW;
#endif
	}

	/** Apply cloud ordering after clipping while retaining standard-projection pixel depth. */
	float CloudPixelDepth(float pixelDepth, uint eyeIndex)
	{
		return FrameBuffer::IsReverseProjection(eyeIndex) ? moonAndCloudBiasUnits * depthBiasUnit24 : pixelDepth;
	}
}

#endif
