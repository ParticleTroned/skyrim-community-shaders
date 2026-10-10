#pragma once

#include <bit>
#include <cstdint>
#include <sl_consts.h>

namespace UpscalingDLSS
{
	/// Compare all version-two values without struct padding or quantization.
	[[nodiscard]] inline bool SameStreamlineConstants(const sl::Constants& a, const sl::Constants& b) noexcept
	{
		if (a.next || b.next || a.structVersion != sl::kStructVersion2 || b.structVersion != sl::kStructVersion2)
			return false;
		const auto same = [](float x, float y) { return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y); };
		const auto same2 = [&](const sl::float2& x, const sl::float2& y) { return same(x.x, y.x) && same(x.y, y.y); };
		const auto same3 = [&](const sl::float3& x, const sl::float3& y) { return same(x.x, y.x) && same(x.y, y.y) && same(x.z, y.z); };
		const auto matrix = [&](const sl::float4x4& x, const sl::float4x4& y) {
			for (std::uint32_t row = 0; row < 4; ++row)
				if (!same(x[row].x, y[row].x) || !same(x[row].y, y[row].y) || !same(x[row].z, y[row].z) || !same(x[row].w, y[row].w))
					return false;
			return true;
		};
		return matrix(a.cameraViewToClip, b.cameraViewToClip) && matrix(a.clipToCameraView, b.clipToCameraView) &&
		       matrix(a.clipToLensClip, b.clipToLensClip) && matrix(a.clipToPrevClip, b.clipToPrevClip) && matrix(a.prevClipToClip, b.prevClipToClip) &&
		       same2(a.jitterOffset, b.jitterOffset) && same2(a.mvecScale, b.mvecScale) && same2(a.cameraPinholeOffset, b.cameraPinholeOffset) &&
		       same3(a.cameraPos, b.cameraPos) && same3(a.cameraUp, b.cameraUp) && same3(a.cameraRight, b.cameraRight) && same3(a.cameraFwd, b.cameraFwd) &&
		       same(a.cameraNear, b.cameraNear) && same(a.cameraFar, b.cameraFar) && same(a.cameraFOV, b.cameraFOV) &&
		       same(a.cameraAspectRatio, b.cameraAspectRatio) && same(a.motionVectorsInvalidValue, b.motionVectorsInvalidValue) &&
		       same(a.minRelativeLinearDepthObjectSeparation, b.minRelativeLinearDepthObjectSeparation) &&
		       a.depthInverted == b.depthInverted && a.cameraMotionIncluded == b.cameraMotionIncluded && a.motionVectors3D == b.motionVectors3D &&
		       a.reset == b.reset && a.orthographicProjection == b.orthographicProjection && a.motionVectorsDilated == b.motionVectorsDilated &&
		       a.motionVectorsJittered == b.motionVectorsJittered;
	}
}
