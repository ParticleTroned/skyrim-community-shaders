#include "../Upscaling.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kPerfModeScaleThreshold = 0.99f;

	uint32_t ClampQualityMode(uint32_t a_qualityMode)
	{
		return std::min<uint32_t>(a_qualityMode, Upscaling::kQualityModeMaxIndex);
	}

	uint32_t ScaleDimension(uint32_t a_dimension, float a_scale)
	{
		if (!std::isfinite(a_scale))
			return a_dimension;

		const float scaled = static_cast<float>(a_dimension) * std::clamp(a_scale, 0.1f, 1.0f);
		return std::clamp<uint32_t>(
			static_cast<uint32_t>(std::floor(scaled)),
			1u,
			std::max<uint32_t>(a_dimension, 1u));
	}

	bool IsVendorMethod(Upscaling::UpscaleMethod a_method)
	{
		return a_method == Upscaling::UpscaleMethod::kDLSS ||
		       a_method == Upscaling::UpscaleMethod::kFSR;
	}
}

void Upscaling::PerfModeState::ResetBootLatch()
{
	boot = {};
	restartRequired = false;
	displaySizeChanged = false;
}

void Upscaling::PerfModeState::ResetResources()
{
}

void Upscaling::PerfModeState::RecordTrueHMDSize(uint32_t a_eyeWidth, uint32_t a_eyeHeight)
{
	if (!a_eyeWidth || !a_eyeHeight)
		return;

	trueHMDEyeWidth = a_eyeWidth;
	trueHMDEyeHeight = a_eyeHeight;
	if (boot.valid && boot.active) {
		displaySizeChanged = boot.displayEyeWidth != a_eyeWidth || boot.displayEyeHeight != a_eyeHeight;
		if (displaySizeChanged)
			restartRequired = true;
	}
}

bool Upscaling::PerfModeState::IsRequested(const Settings& a_settings) const
{
	return std::min<uint32_t>(a_settings.perfMode, 1u) != 0;
}

bool Upscaling::PerfModeState::IsEligible(const Settings& a_settings, UpscaleMethod a_method) const
{
	if (!REL::Module::IsVR())
		return false;

	if (!IsRequested(a_settings))
		return false;

	if (!IsVendorMethod(a_method))
		return false;

	const uint32_t qualityMode = ClampQualityMode(a_settings.qualityMode);
	return Upscaling::GetQualityModeResolutionScale(qualityMode) < kPerfModeScaleThreshold;
}

bool Upscaling::PerfModeState::EnsureBootLatch(const Settings& a_settings, UpscaleMethod a_method, bool a_allowCreate)
{
	const uint32_t qualityMode = ClampQualityMode(a_settings.qualityMode);
	const bool requestedNow = IsRequested(a_settings);
	const bool eligibleNow = IsEligible(a_settings, a_method);

	if (boot.valid) {
		restartRequired =
			boot.active &&
			(!requestedNow ||
			 displaySizeChanged ||
			 !eligibleNow ||
			 boot.method != a_method ||
			 boot.qualityMode != qualityMode);
		return boot.active;
	}

	restartRequired = !a_allowCreate && requestedNow && eligibleNow && trueHMDEyeWidth && trueHMDEyeHeight;
	if (!eligibleNow)
		return false;

	if (!trueHMDEyeWidth || !trueHMDEyeHeight)
		return false;

	if (!a_allowCreate)
		return false;

	const float renderScale = Upscaling::GetQualityModeResolutionScale(qualityMode);
	if (!std::isfinite(renderScale) || renderScale <= 0.0f || renderScale >= kPerfModeScaleThreshold)
		return false;

	boot.valid = true;
	boot.active = true;
	boot.method = a_method;
	boot.qualityMode = qualityMode;
	boot.renderScale = renderScale;
	boot.displayEyeWidth = trueHMDEyeWidth;
	boot.displayEyeHeight = trueHMDEyeHeight;
	boot.renderEyeWidth = ScaleDimension(trueHMDEyeWidth, renderScale);
	boot.renderEyeHeight = ScaleDimension(trueHMDEyeHeight, renderScale);

	logger::info(
		"[PerfMode] Boot-latched {} quality {} at display {}x{} per eye -> render {}x{} per eye.",
		magic_enum::enum_name(a_method),
		qualityMode,
		boot.displayEyeWidth,
		boot.displayEyeHeight,
		boot.renderEyeWidth,
		boot.renderEyeHeight);

	return true;
}

bool Upscaling::PerfModeState::IsActive(const Settings& a_settings, UpscaleMethod a_method) const
{
	(void)a_settings;
	(void)a_method;
	return boot.valid && boot.active;
}

bool Upscaling::PerfModeState::TryGetOpenVRRenderTargetSize(const Settings& a_settings, UpscaleMethod a_method, uint32_t& a_width, uint32_t& a_height, bool a_allowCreate)
{
	if (!EnsureBootLatch(a_settings, a_method, a_allowCreate))
		return false;

	if (!boot.renderEyeWidth || !boot.renderEyeHeight)
		return false;

	a_width = boot.renderEyeWidth;
	a_height = boot.renderEyeHeight;
	return true;
}

float2 Upscaling::PerfModeState::GetDisplayScreenSize() const
{
	if (boot.valid && boot.displayEyeWidth && boot.displayEyeHeight)
		return { static_cast<float>(boot.displayEyeWidth * 2u), static_cast<float>(boot.displayEyeHeight) };

	if (trueHMDEyeWidth && trueHMDEyeHeight)
		return { static_cast<float>(trueHMDEyeWidth * 2u), static_cast<float>(trueHMDEyeHeight) };

	return { 0.0f, 0.0f };
}

float2 Upscaling::PerfModeState::GetRenderScreenSize() const
{
	if (!boot.valid || !boot.renderEyeWidth || !boot.renderEyeHeight)
		return { 0.0f, 0.0f };

	return { static_cast<float>(boot.renderEyeWidth * 2u), static_cast<float>(boot.renderEyeHeight) };
}
