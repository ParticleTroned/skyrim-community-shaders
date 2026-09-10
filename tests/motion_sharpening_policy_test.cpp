#include "Features/Upscaling/MotionSharpeningPolicy.h"

#include <limits>

namespace
{
	using namespace MotionSharpening;

	constexpr bool CoversGeometry()
	{
		const Region flat{ { 0, 0, 1920, 1080 }, { 0, 0, 1280, 720 }, { 0, 0, 1280, 720 } };
		const Region rightEye{ { 1920, 0, 1920, 1080 }, { 1280, 0, 1280, 720 }, { 1280, 0, 1280, 720 } };
		const Region croppedSubmit{ { 0, 0, 1920, 1080 }, { 1440, 90, 960, 540 }, { 1280, 0, 1280, 720 } };
		const Region crossesEye{ { 0, 0, 1920, 1080 }, { 1279, 0, 1280, 720 }, { 1280, 0, 1280, 720 } };
		const Region oversizedOutput{ { 1919, 0, 1920, 1080 }, { 0, 0, 1280, 720 }, { 0, 0, 1280, 720 } };
		const Region overflowing{ { 0, 0, 1920, 1080 }, { UINT32_MAX, 0, 2, 720 }, { 0, 0, 1280, 720 } };
		return IsValid(flat, 1920, 1080, 1280, 720) &&
		       IsValid(rightEye, 3840, 1080, 2560, 720) &&
		       IsValid(croppedSubmit, 1920, 1080, 2560, 720) &&
		       !IsValid(crossesEye, 1920, 1080, 2560, 720) &&
		       !IsValid(oversizedOutput, 1920, 1080, 1280, 720) &&
		       !IsValid(overflowing, 1920, 1080, 1280, 720) &&
		       !IsValid(flat, 1920, 1080, 1279, 720) &&
		       !IsValid({}, 1920, 1080, 2560, 720);
	}
	static_assert(CoversGeometry());
}

int main()
{
	using namespace MotionSharpening;
	const auto defaults = Sanitize({});
	const auto clamped = Sanitize({ true, -2.0f, 100.0f, 3.0f });
	const auto nonfinite = Sanitize({ true, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() });
	const bool settings = !defaults.enabled && defaults.adjustment < 0.0f && IsValid(defaults) &&
	                      clamped.enabled && clamped.adjustment == -1.0f && clamped.thresholdPixels == 64.0f && clamped.strengthCap == 1.0f &&
	                      IsValid(nonfinite) && nonfinite.adjustment == defaults.adjustment &&
	                      nonfinite.thresholdPixels == defaults.thresholdPixels && nonfinite.strengthCap == defaults.strengthCap &&
	                      !IsValid({ true, 1.01f, 2.0f, 1.0f }) && !IsValid({ true, 0.0f, -0.01f, 1.0f }) &&
	                      !IsValid({ true, 0.0f, 2.0f, std::numeric_limits<float>::quiet_NaN() });
	const bool units = MotionToOutputPixels(1280, 1280, 1920) == 1920.0f &&
	                   MotionToOutputPixels(1280, 640, 1920) == 3840.0f &&
	                   MotionToOutputPixels(720, 540, 1080) == 1440.0f &&
	                   MotionToOutputPixels(1280, 0, 1920) == 0.0f;
	return CoversGeometry() && settings && units ? 0 : 1;
}
