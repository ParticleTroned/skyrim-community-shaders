#include "Bloom.h"

#include <algorithm>
#include <cmath>

#include "Utils/UI.h"

namespace
{
	constexpr float kEnhancementIntensityMax = 6.0f;
	constexpr float kHaloRadiusMax = 32.0f;
	constexpr float kBloomSaturationMax = 2.0f;
	constexpr float kCompressionCeilingMax = 8.0f;

	void DrawTooltip(const char* a_text)
	{
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", a_text);
	}

	Bloom::Profile& GetSelectedProfile(Bloom::PresetSettings& a_settings)
	{
		switch (a_settings.SelectedPreset) {
		case 1:
			return a_settings.Fantasy;
		case 2:
			return a_settings.Dreamy;
		default:
			return a_settings.Default;
		}
	}

	const Bloom::Profile& GetSelectedProfile(const Bloom::PresetSettings& a_settings)
	{
		switch (a_settings.SelectedPreset) {
		case 1:
			return a_settings.Fantasy;
		case 2:
			return a_settings.Dreamy;
		default:
			return a_settings.Default;
		}
	}

	Bloom::Profile GetPresetDefaults(uint a_preset)
	{
		const Bloom::PresetSettings defaults{};
		switch (a_preset) {
		case 1:
			return defaults.Fantasy;
		case 2:
			return defaults.Dreamy;
		default:
			return defaults.Default;
		}
	}
}

void Bloom::DrawSettings(PresetSettings& a_settings)
{
	bool enabled = a_settings.Enabled != 0;
	if (ImGui::Checkbox("Enable Bloom Enhancement", &enabled))
		a_settings.Enabled = enabled;

	if (ImGui::Button("Default"))
		a_settings.SelectedPreset = 0;
	ImGui::SameLine();
	if (ImGui::Button("Fantasy"))
		a_settings.SelectedPreset = 1;
	ImGui::SameLine();
	if (ImGui::Button("Dreamy"))
		a_settings.SelectedPreset = 2;

	const char* resetLabel = "Reset Preset";
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(resetLabel).x - ImGui::GetStyle().FramePadding.x * 2.0f);
	if (ImGui::Button(resetLabel))
		GetSelectedProfile(a_settings) = GetPresetDefaults(a_settings.SelectedPreset);

	Profile& profile = GetSelectedProfile(a_settings);
	ImGui::BeginDisabled(!a_settings.Enabled);
	ImGui::SliderFloat("Enhancement Intensity", &profile.EnhancementIntensity, 0.0f, kEnhancementIntensityMax, "%.2f");
	DrawTooltip("Multiplies the generated vanilla bloom signal before compression. Raise it to exaggerate weak bloom, such as the sky.");
	ImGui::SliderFloat("Halo Radius", &profile.HaloRadius, 0.0f, kHaloRadiusMax, "%.1f");
	DrawTooltip("Controls the radius of the enhancement's additional bloom samples. Higher values create wider halos.");
	ImGui::SliderFloat("Halo Spread", &profile.HaloSpread, 0.0f, 1.0f, "%.2f");
	DrawTooltip("Blends between the original bloom and the widened halo samples. Higher values make the halo softer and more spread out.");
	ImGui::SliderFloat("Bloom Saturation", &profile.BloomSaturation, 0.0f, kBloomSaturationMax, "%.2f");
	DrawTooltip("Controls the color saturation of the enhanced bloom. Lower values make it whiter; higher values preserve or exaggerate its tint.");
	ImGui::ColorEdit3("Bloom Tint", reinterpret_cast<float*>(&profile.BloomTint));
	DrawTooltip("Colors the bloom halo without changing the underlying scene lighting.");
	ImGui::SliderFloat("Compression Ceiling", &profile.CompressionCeiling, 0.0f, kCompressionCeilingMax, "%.2f");
	DrawTooltip("The soft limiter's maximum bloom level. Bloom above the compression threshold approaches this value instead of continuing to scale. Set to 0 to remove added bloom.");
	ImGui::SliderFloat("Compression Threshold", &profile.CompressionThreshold, 0.0f, profile.CompressionCeiling, "%.2f");
	DrawTooltip("The post-enhancement bloom level where soft compression starts. Bloom below it is unchanged; bloom above it rolls toward Compression Ceiling. Set it equal to Compression Ceiling for a hard cap.");
	ImGui::EndDisabled();

	SanitizeSettings(a_settings);
}

Bloom::Settings Bloom::GetCommonBufferData(const PresetSettings& a_settings, bool a_runtimeEnabled)
{
	const auto& profile = GetSelectedProfile(a_settings);
	return {
		static_cast<uint>(a_runtimeEnabled && a_settings.Enabled != 0),
		profile.EnhancementIntensity,
		profile.HaloRadius,
		profile.HaloSpread,
		profile.BloomSaturation,
		profile.BloomTint,
		profile.CompressionThreshold,
		profile.CompressionCeiling
	};
}

void Bloom::SanitizeSettings(PresetSettings& a_settings)
{
	const PresetSettings defaults{};
	auto clampFiniteOrDefault = [](float a_value, float a_min, float a_max, float a_defaultValue) {
		return std::isfinite(a_value) ? std::clamp(a_value, a_min, a_max) : a_defaultValue;
	};
	auto sanitizeProfile = [&](Profile& a_profile, const Profile& a_defaultProfile) {
		a_profile.EnhancementIntensity = clampFiniteOrDefault(a_profile.EnhancementIntensity, 0.0f, kEnhancementIntensityMax, a_defaultProfile.EnhancementIntensity);
		a_profile.HaloRadius = clampFiniteOrDefault(a_profile.HaloRadius, 0.0f, kHaloRadiusMax, a_defaultProfile.HaloRadius);
		a_profile.HaloSpread = clampFiniteOrDefault(a_profile.HaloSpread, 0.0f, 1.0f, a_defaultProfile.HaloSpread);
		a_profile.BloomSaturation = clampFiniteOrDefault(a_profile.BloomSaturation, 0.0f, kBloomSaturationMax, a_defaultProfile.BloomSaturation);
		a_profile.BloomTint.x = clampFiniteOrDefault(a_profile.BloomTint.x, 0.0f, 1.0f, a_defaultProfile.BloomTint.x);
		a_profile.BloomTint.y = clampFiniteOrDefault(a_profile.BloomTint.y, 0.0f, 1.0f, a_defaultProfile.BloomTint.y);
		a_profile.BloomTint.z = clampFiniteOrDefault(a_profile.BloomTint.z, 0.0f, 1.0f, a_defaultProfile.BloomTint.z);
		a_profile.CompressionCeiling = clampFiniteOrDefault(a_profile.CompressionCeiling, 0.0f, kCompressionCeilingMax, a_defaultProfile.CompressionCeiling);
		const float thresholdDefault = std::min(a_defaultProfile.CompressionThreshold, a_profile.CompressionCeiling);
		a_profile.CompressionThreshold = clampFiniteOrDefault(a_profile.CompressionThreshold, 0.0f, a_profile.CompressionCeiling, thresholdDefault);
	};

	a_settings.Enabled = a_settings.Enabled != 0;
	a_settings.SelectedPreset = std::min(a_settings.SelectedPreset, 2u);
	sanitizeProfile(a_settings.Default, defaults.Default);
	sanitizeProfile(a_settings.Fantasy, defaults.Fantasy);
	sanitizeProfile(a_settings.Dreamy, defaults.Dreamy);
}
