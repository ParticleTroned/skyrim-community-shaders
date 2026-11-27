#ifndef SKIN_TRANSMISSION_HLSLI
#define SKIN_TRANSMISSION_HLSLI

namespace SkinTransmission
{
	struct Gaussian
	{
		float red, green, blue, variance;
	};

	// Shared by mapped HLSL evaluation and the CPU unit-thickness cache.
	static const Gaussian Gaussians[6] = {
		{ 0.233f, 0.455f, 0.649f, 0.0064f },
		{ 0.1f, 0.336f, 0.344f, 0.0484f },
		{ 0.118f, 0.198f, 0.0f, 0.187f },
		{ 0.113f, 0.007f, 0.007f, 0.567f },
		{ 0.358f, 0.004f, 0.0f, 1.99f },
		{ 0.078f, 0.0f, 0.0f, 7.41f }
	};
	static const float DistanceScale = 8.25f;
	static const float MinimumWidth = 1e-4f;
}

#endif
