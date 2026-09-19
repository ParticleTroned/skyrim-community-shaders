#include "Features/FoveatedCommon.h"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

struct float2
{
	float x, y;
};

namespace globals::game
{
	bool isVR = true;
}

struct Upscaling
{
	struct Settings
	{
		float foveatedCenterArea = 0.6f;
		float periphery_taa_center_area = 0.3f;
		float foveatedCenterHorizontalScale = 1.0f;
		float foveatedLeftEyeMaskOffsetX = 0.0f;
		float foveatedLeftEyeMaskOffsetY = 0.0f;
		float foveatedRightEyeMaskOffsetX = 0.0f;
		float foveatedRightEyeMaskOffsetY = 0.0f;
	} settings;

	float2 GetDefaultFoveatedMaskCenterOffset(uint32_t eyeIndex) const;
	float2 GetResolvedFoveatedMaskCenterOffset(uint32_t eyeIndex, bool usePeripheryTAAProfile = false) const;
	std::array<float2, 2> GetResolvedFoveatedMaskCenterOffsets(bool usePeripheryTAAProfile = false) const;
};

constexpr float kFoveatedMaskOffsetAdjustMin = -0.30f;
constexpr float kFoveatedMaskOffsetAdjustMax = 0.30f;
constexpr float kFoveatedMaskOffsetResolvedMin = -0.30f;
constexpr float kFoveatedMaskOffsetResolvedMax = 0.30f;
float ClampFoveatedCenterScale(float value) { return FoveatedCommon::ClampCenterScale(value); }
float ClampFoveatedCenterHorizontalScale(float value) { return FoveatedCommon::ClampCenterHorizontalScale(value); }

#include "foveated_mask_geometry_under_test.h"

namespace
{
	void Require(bool condition)
	{
		if (!condition)
			throw std::runtime_error("FOV mask geometry contract failed");
	}
	bool Near(float left, float right) { return std::abs(left - right) < 1.0e-6f; }
}

int main()
{
	Upscaling upscaling;
	auto offsets = upscaling.GetResolvedFoveatedMaskCenterOffsets();
	Require(Near(offsets[0].x, 0.0f) && Near(offsets[1].x, 0.0f));
	upscaling.settings.foveatedLeftEyeMaskOffsetX = 0.07f;
	upscaling.settings.foveatedLeftEyeMaskOffsetY = -0.02f;
	upscaling.settings.foveatedRightEyeMaskOffsetX = -0.08f;
	upscaling.settings.foveatedRightEyeMaskOffsetY = 0.03f;
	const auto before = upscaling.GetResolvedFoveatedMaskCenterOffsets();
	upscaling.settings.foveatedCenterHorizontalScale = 1.5f;
	offsets = upscaling.GetResolvedFoveatedMaskCenterOffsets();
	Require(Near(offsets[0].x, -0.08f) && Near(offsets[1].x, 0.07f));
	Require(Near(offsets[0].y, -0.02f) && Near(offsets[1].y, 0.03f));
	// Outward expansion preserves the nasal edge until the bounded offset saturates.
	Require(Near(before[0].x + 0.3f, offsets[0].x + 0.45f));
	Require(Near(before[1].x - 0.3f, offsets[1].x - 0.45f));
	const auto taa = upscaling.GetResolvedFoveatedMaskCenterOffsets(true);
	Require(Near(taa[0].x, -0.005f) && Near(taa[1].x, -0.005f));
	Require(Near(taa[0].y, offsets[0].y) && Near(taa[1].y, offsets[1].y));
	globals::game::isVR = false;
	const auto flat = upscaling.GetResolvedFoveatedMaskCenterOffsets();
	Require(Near(flat[0].x, before[0].x) && Near(flat[1].x, before[1].x));
	globals::game::isVR = true;
	upscaling.settings.foveatedCenterHorizontalScale = 2.0f;
	upscaling.settings.foveatedLeftEyeMaskOffsetX = -0.3f;
	upscaling.settings.foveatedRightEyeMaskOffsetX = 0.3f;
	upscaling.settings.foveatedLeftEyeMaskOffsetY = std::numeric_limits<float>::quiet_NaN();
	upscaling.settings.foveatedRightEyeMaskOffsetY = std::numeric_limits<float>::infinity();
	const auto clamped = upscaling.GetResolvedFoveatedMaskCenterOffsets();
	Require(Near(clamped[0].x, -0.3f) && Near(clamped[1].x, 0.3f));
	Require(Near(clamped[0].y, 0.0f) && Near(clamped[1].y, 0.0f));
	for (uint32_t eye = 0; eye < 2; ++eye) {
		const auto single = upscaling.GetResolvedFoveatedMaskCenterOffset(eye);
		Require(Near(single.x, clamped[eye].x) && Near(single.y, clamped[eye].y));
	}
	return 0;
}
