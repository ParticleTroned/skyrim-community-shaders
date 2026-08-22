#ifndef CURRENT_FRAME_DEPTH_CULLING_HLSLI
#define CURRENT_FRAME_DEPTH_CULLING_HLSLI

namespace CurrentFrameDepthCulling
{
#if defined(VR) && defined(VSHADER)
	Buffer<uint> Visibility : register(t127);

	bool IsOccluded()
	{
		[branch]
		if ((Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::CurrentFrameDepthCulling) != 0) {
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
