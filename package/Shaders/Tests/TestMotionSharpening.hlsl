#include "/Shaders/Upscaling/MotionSharpening.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags upscaling, sharpening, motion
[numthreads(1, 1, 1)] void TestMotionSharpeningStaticStrength() {
	float strength = MotionSharpeningStrength(0.7, -0.5, 2.0, 1.0, float2(0, 0), float2(1920, 1080));
	ASSERT(IsTrue, abs(strength - 0.7) < 1e-6);
	ASSERT(IsTrue, abs(MotionSharpeningGain(strength, 1.15457) - exp2(2.0 * 0.7 - 2.0) * 1.15457) < 1e-6);
	ASSERT(IsTrue, MotionSharpeningGain(0.0, 1.15457) == 0.0);
	ASSERT(IsTrue, abs(MotionSharpeningGain(1.0, 1.15457) - 1.15457) < 1e-6);
}

	/// @tags upscaling, sharpening, motion
	[numthreads(1, 1, 1)] void TestMotionSharpeningSignedAdjustment()
{
	float2 scale = float2(1920, 1080);
	float atThreshold = MotionSharpeningStrength(0.7, -0.5, 2.0, 1.0, float2(2.0 / 1920.0, 0), scale);
	float atHalf = MotionSharpeningStrength(0.7, -0.5, 2.0, 1.0, float2(3.0 / 1920.0, 0), scale);
	float atFull = MotionSharpeningStrength(0.7, -0.5, 2.0, 1.0, float2(4.0 / 1920.0, 0), scale);
	ASSERT(IsTrue, abs(atThreshold - 0.7) < 1e-6);
	ASSERT(IsTrue, abs(atHalf - 0.45) < 1e-6);
	ASSERT(IsTrue, abs(atFull - 0.2) < 1e-6);
	float positive = MotionSharpeningStrength(0.7, 0.2, 2.0, 1.0, float2(-4.0 / 1920.0, 0), scale);
	ASSERT(IsTrue, abs(positive - 0.9) < 1e-6);
}

/// @tags upscaling, sharpening, motion
[numthreads(1, 1, 1)] void TestMotionSharpeningCapAndZeroThreshold() {
	float capped = MotionSharpeningStrength(0.7, 0.5, 0.0, 0.8, float2(1, 0), float2(1920, 1080));
	float off = MotionSharpeningStrength(0.7, -1.0, 0.0, 1.0, float2(1, 0), float2(1920, 1080));
	float still = MotionSharpeningStrength(0.7, -0.5, 0.0, 1.0, float2(0, 0), float2(1920, 1080));
	ASSERT(IsTrue, abs(capped - 0.8) < 1e-6);
	ASSERT(IsTrue, off == 0.0);
	ASSERT(IsTrue, abs(still - 0.7) < 1e-6);
}

	/// @tags upscaling, sharpening, motion
	[numthreads(1, 1, 1)] void TestMotionSharpeningInvalidMotionFallback()
{
	float invalid = asfloat(0x7fc00000u);
	float result = MotionSharpeningStrength(0.7, 1.0, 0.0, 0.1, float2(invalid, 0), float2(1920, 1080));
	ASSERT(IsTrue, abs(result - 0.7) < 1e-6);
	result = MotionSharpeningStrength(0.7, -1.0, 0.0, 0.1, float2(0, asfloat(0x7f800000u)), float2(1920, 1080));
	ASSERT(IsTrue, abs(result - 0.7) < 1e-6);
}

/// @tags upscaling, sharpening, vr, stereo
[numthreads(1, 1, 1)] void TestMotionSharpeningSourceMapping() {
	uint4 rightEye = uint4(1280, 0, 1280, 720);
	uint2 size = uint2(1920, 1080);
	ASSERT(IsTrue, all(MotionSharpeningSourcePixel(uint2(0, 0), size, rightEye) == uint2(1280, 0)));
	ASSERT(IsTrue, all(MotionSharpeningSourcePixel(size - 1, size, rightEye) == uint2(2559, 719)));
	uint4 crop = uint4(1440, 90, 960, 540);
	ASSERT(IsTrue, all(MotionSharpeningSourcePixel(uint2(0, 0), size, crop) == crop.xy));
	ASSERT(IsTrue, all(MotionSharpeningSourcePixel(size - 1, size, crop) == uint2(2399, 629)));
}

	/// @tags upscaling, sharpening, vr, stereo
	[numthreads(1, 1, 1)] void TestMotionSharpeningEyeEdgeClamping()
{
	uint4 rightEye = uint4(1920, 0, 1920, 1080);
	ASSERT(IsTrue, all(MotionSharpeningColorPixel(int2(1919, 500), rightEye) == int2(1920, 500)));
	ASSERT(IsTrue, all(MotionSharpeningColorPixel(int2(3840, 1080), rightEye) == int2(3839, 1079)));
	ASSERT(IsTrue, all(MotionSharpeningColorPixel(int2(2000, -1), rightEye) == int2(2000, 0)));
}

/// @tags upscaling, sharpening, vr, motion
[numthreads(1, 1, 1)] void TestMotionSharpeningOutputPixelUnits() {
	float2 motion = float2(0.001, 0.0);
	float fullEye = MotionSharpeningStrength(0.7, -0.5, 2.0, 1.0, motion, float2(1920, 1080));
	float halfCrop = MotionSharpeningStrength(0.7, -0.5, 2.0, 1.0, motion, float2(3840, 2160));
	ASSERT(IsTrue, abs(fullEye - 0.7) < 1e-6);
	ASSERT(IsTrue, abs(halfCrop - 0.24) < 1e-5);
}
