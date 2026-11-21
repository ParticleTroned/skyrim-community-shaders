#ifndef SKYLIGHTING_ROOF_COVERAGE_HLSLI
#define SKYLIGHTING_ROOF_COVERAGE_HLSLI

#include "Common/SharedData.hlsli"

namespace Skylighting
{
	namespace RoofCoverage
	{
		const static uint OPEN = 0;
		const static uint COVERED = 1;
		const static uint UNKNOWN = 2;
		// The lower bits retain eight SH-confidence and five shadow-jitter bits.
		const static uint STATE_SHIFT = 13;

#if defined(SKYLIGHTING_COVERAGE_REGISTER)
		Texture3D<uint> ProbeState : register(SKYLIGHTING_COVERAGE_REGISTER);
		Texture2D<float> ZenithDepth : register(SKYLIGHTING_ZENITH_REGISTER);
#	define SKYLIGHTING_COVERAGE
#elif defined(PSHADER)
		Texture3D<uint> ProbeState : register(t51);
		Texture2D<float> ZenithDepth : register(t52);
#	define SKYLIGHTING_COVERAGE
#endif

		/** Projects an eye-zero-relative position into the captured roof depth. */
		float3 Project(float3 positionMS)
		{
			return mul(SharedData::skylightingSettings.ZenithViewProj,
				float4(positionMS + SharedData::skylightingSettings.ZenithOffset, 1))
			    .xyz;
		}

		/** Returns false while depth is unavailable or the position is outside its capture. */
		bool SampleDepth(Texture2D<float> depthTexture, float3 positionMS, out float positionDepth, out float roofDepth)
		{
			positionDepth = 0;
			roofDepth = 0;
			bool sampled = false;
			[branch] if (SharedData::skylightingSettings.ZenithValid != 0)
			{
				float3 positionOS = Project(positionMS);
				float2 uv = float2(positionOS.x, -positionOS.y) * 0.5 + 0.5;
				positionDepth = positionOS.z;
				[branch] if (all(uv > 0) && all(uv < 1) && positionDepth > 0 && positionDepth < 1)
				{
					uint2 dimensions;
					depthTexture.GetDimensions(dimensions.x, dimensions.y);
					roofDepth = depthTexture.Load(int3(uv * dimensions, 0));
					sampled = true;
				}
			}
			return sampled;
		}

		uint Classify(Texture2D<float> depthTexture, float3 positionMS)
		{
			float positionDepth, roofDepth;
			if (!SampleDepth(depthTexture, positionMS, positionDepth, roofDepth))
				return UNKNOWN;
			return positionDepth > roofDepth ? COVERED : OPEN;
		}

#if defined(SKYLIGHTING_COVERAGE)
		bool ReceiverIsCovered(float3 positionMS, out float roofDepth)
		{
			float positionDepth;
			return SampleDepth(ZenithDepth, positionMS, positionDepth, roofDepth) && positionDepth > roofDepth;
		}

		/** Open probes below the receiver's roof remain valid neighbors. */
		bool ProbeIsAboveRoof(uint3 probeIndex, float3 probePositionMS, float roofDepth)
		{
			return ((ProbeState[probeIndex] >> STATE_SHIFT) & 3u) == OPEN && Project(probePositionMS).z < roofDepth;
		}
#endif
	}
}

#endif
