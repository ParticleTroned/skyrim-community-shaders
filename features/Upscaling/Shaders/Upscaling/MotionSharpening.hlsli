#ifndef CSX_MOTION_SHARPENING
#define CSX_MOTION_SHARPENING

uint2 MotionSharpeningSourcePixel(uint2 localOutput, uint2 outputSize, uint4 sourceRect)
{
	uint2 pixel = uint2((float2(localOutput) + 0.5) * float2(sourceRect.zw) / float2(outputSize));
	return sourceRect.xy + min(pixel, sourceRect.zw - 1);
}

float MotionSharpeningStrength(float baseStrength, float adjustment, float thresholdPixels,
	float strengthCap, float2 motion, float2 motionToOutputPixels)
{
	// Invalid producer values retain the fixed sharpness for this pixel.
	if (!all(isfinite(motion)))
		return baseStrength;
	float speed = length(motion * motionToOutputPixels);
	float weight = saturate((speed - thresholdPixels) / max(thresholdPixels, 1.0));
	float adjusted = clamp(baseStrength + adjustment * weight, 0.0, strengthCap);
	return adjusted;
}

float MotionSharpeningGain(float strength, float maximumGain)
{
	return strength > 0.0 ? exp2(2.0 * strength - 2.0) * maximumGain : 0.0;
}

int2 MotionSharpeningColorPixel(int2 pixel, uint4 outputRect)
{
	return clamp(pixel, int2(outputRect.xy), int2(outputRect.xy + outputRect.zw) - 1);
}

#endif
