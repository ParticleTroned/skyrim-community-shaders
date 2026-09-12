#ifndef CURRENT_FRAME_DEPTH_CULLING_HLSLI
#define CURRENT_FRAME_DEPTH_CULLING_HLSLI

namespace CurrentFrameDepthCulling
{
#if defined(VR) && defined(VSHADER)
	// Skyrim exposes the stereo OBB result as a structured uint buffer. Keep the
	// shader declaration identical to that native resource contract.
	StructuredBuffer<uint> Visibility : register(t127);

	bool IsOccluded()
	{
		[branch] if ((Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::CurrentFrameDepthCulling) != 0)
		{
			const uint objectIndex =
				(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::CurrentFrameDepthCullingObjectIndex) >>
				Permutation::ExtraFlags::CurrentFrameDepthCullingObjectIndexShift;
			return Visibility[objectIndex] == 0;
		}

		return false;
	}
#else
	bool IsOccluded() { return false; }
#endif
}

#endif
