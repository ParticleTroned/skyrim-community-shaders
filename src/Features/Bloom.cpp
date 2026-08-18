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

	Bloom::Profile& GetMutableSelectedProfile(Bloom::PresetSettings& a_settings)
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
		return Bloom::GetPresetProfile(defaults, a_preset);
	}

	void SanitizeProfileWithDefaults(Bloom::Profile& a_profile, const Bloom::Profile& a_defaults)
	{
		const auto clampFiniteOrDefault = [](float a_value, float a_min, float a_max, float a_defaultValue) {
			return std::isfinite(a_value) ? std::clamp(a_value, a_min, a_max) : a_defaultValue;
		};

		a_profile.EnhancementIntensity = clampFiniteOrDefault(a_profile.EnhancementIntensity, 0.0f, kEnhancementIntensityMax, a_defaults.EnhancementIntensity);
		a_profile.HaloRadius = clampFiniteOrDefault(a_profile.HaloRadius, 0.0f, kHaloRadiusMax, a_defaults.HaloRadius);
		a_profile.HaloSpread = clampFiniteOrDefault(a_profile.HaloSpread, 0.0f, 1.0f, a_defaults.HaloSpread);
		a_profile.BloomSaturation = clampFiniteOrDefault(a_profile.BloomSaturation, 0.0f, kBloomSaturationMax, a_defaults.BloomSaturation);
		a_profile.BloomTint.x = clampFiniteOrDefault(a_profile.BloomTint.x, 0.0f, 1.0f, a_defaults.BloomTint.x);
		a_profile.BloomTint.y = clampFiniteOrDefault(a_profile.BloomTint.y, 0.0f, 1.0f, a_defaults.BloomTint.y);
		a_profile.BloomTint.z = clampFiniteOrDefault(a_profile.BloomTint.z, 0.0f, 1.0f, a_defaults.BloomTint.z);
		a_profile.CompressionCeiling = clampFiniteOrDefault(a_profile.CompressionCeiling, 0.0f, kCompressionCeilingMax, a_defaults.CompressionCeiling);
		a_profile.CompressionThreshold = clampFiniteOrDefault(
			a_profile.CompressionThreshold,
			0.0f,
			a_profile.CompressionCeiling,
			std::min(a_defaults.CompressionThreshold, a_profile.CompressionCeiling));
	}
}

void Bloom::DrawSettings(PresetSettings& a_settings)
{
	bool enabled = a_settings.Enabled != 0;
	if (ImGui::Checkbox("Enable Bloom Enhancement", &enabled))
		a_settings.Enabled = enabled;

	ImGui::TextUnformatted("Bloom Preset");
	if (ImGui::BeginTable("##BloomPresetButtons", 3, ImGuiTableFlags_SizingStretchProp)) {
		for (uint preset = 0; preset < 3; ++preset)
			ImGui::TableSetupColumn(GetPresetName(preset), ImGuiTableColumnFlags_WidthStretch, 1.0f);

		ImGui::TableNextRow();
		for (uint preset = 0; preset < 3; ++preset) {
			ImGui::TableNextColumn();
			[[maybe_unused]] auto presetStyle = Util::PresetButtonStyle(a_settings.SelectedPreset == preset);
			if (ImGui::Button(GetPresetName(preset), ImVec2(-1.0f, 0.0f)))
				a_settings.SelectedPreset = preset;
		}
		ImGui::EndTable();
	}

	if (ImGui::Button("Reset Selected Preset"))
		GetMutableSelectedProfile(a_settings) = GetPresetDefaults(a_settings.SelectedPreset);

	Profile& profile = GetMutableSelectedProfile(a_settings);
	ImGui::BeginDisabled(!a_settings.Enabled);
	DrawProfileSettings(profile);
	ImGui::EndDisabled();

	SanitizeSettings(a_settings);
}

void Bloom::DrawProfileSettings(Profile& a_profile, bool a_showAdvancedControls)
{
	ImGui::SliderFloat("Enhancement Intensity", &a_profile.EnhancementIntensity, 0.0f, kEnhancementIntensityMax, "%.2f");
	DrawTooltip("Multiplies the generated vanilla bloom signal before compression. Raise it to exaggerate weak bloom, such as the sky.");

	if (a_showAdvancedControls) {
		ImGui::SliderFloat("Halo Radius", &a_profile.HaloRadius, 0.0f, kHaloRadiusMax, "%.1f");
		DrawTooltip("Controls the radius of the enhancement's additional bloom samples. Higher values create wider halos.");
		ImGui::SliderFloat("Halo Spread", &a_profile.HaloSpread, 0.0f, 1.0f, "%.2f");
		DrawTooltip("Blends between the original bloom and the widened halo samples. Higher values make the halo softer and more spread out.");
		ImGui::SliderFloat("Bloom Saturation", &a_profile.BloomSaturation, 0.0f, kBloomSaturationMax, "%.2f");
		DrawTooltip("Controls the color saturation of the enhanced bloom. Lower values make it whiter; higher values preserve or exaggerate its tint.");
		ImGui::ColorEdit3("Bloom Tint", reinterpret_cast<float*>(&a_profile.BloomTint));
		DrawTooltip("Colors the bloom halo without changing the underlying scene lighting.");
		ImGui::SliderFloat("Compression Ceiling", &a_profile.CompressionCeiling, 0.0f, kCompressionCeilingMax, "%.2f");
		DrawTooltip("The soft limiter's maximum bloom level. Bloom above the compression threshold approaches this value instead of continuing to scale. Set to 0 to remove added bloom.");
		ImGui::SliderFloat("Compression Threshold", &a_profile.CompressionThreshold, 0.0f, a_profile.CompressionCeiling, "%.2f");
		DrawTooltip("The post-enhancement bloom level where soft compression starts. Bloom below it is unchanged; bloom above it rolls toward Compression Ceiling. Set it equal to Compression Ceiling for a hard cap.");
	}

	SanitizeProfile(a_profile);
}

Bloom::Settings Bloom::GetCommonBufferData(const Profile& a_profile, float a_blendWeight)
{
	auto profile = a_profile;
	SanitizeProfile(profile);
	const float blendWeight = std::clamp(std::isfinite(a_blendWeight) ? a_blendWeight : 0.0f, 0.0f, 1.0f);
	return {
		static_cast<uint>(blendWeight > 0.0f),
		profile.EnhancementIntensity,
		profile.HaloRadius,
		profile.HaloSpread,
		profile.BloomSaturation,
		profile.BloomTint,
		profile.CompressionThreshold,
		profile.CompressionCeiling,
		blendWeight
	};
}

const char* Bloom::GetPresetName(uint a_preset)
{
	switch (a_preset) {
	case 1:
		return "Fantasy";
	case 2:
		return "Dreamy";
	default:
		return "Default";
	}
}

const Bloom::Profile& Bloom::GetSelectedProfile(const PresetSettings& a_settings)
{
	return GetPresetProfile(a_settings, a_settings.SelectedPreset);
}

const Bloom::Profile& Bloom::GetPresetProfile(const PresetSettings& a_settings, uint a_preset)
{
	switch (a_preset) {
	case 1:
		return a_settings.Fantasy;
	case 2:
		return a_settings.Dreamy;
	default:
		return a_settings.Default;
	}
}

Bloom::Profile Bloom::LerpProfiles(const Profile& a_a, const Profile& a_b, float a_t)
{
	const float t = std::clamp(std::isfinite(a_t) ? a_t : 0.0f, 0.0f, 1.0f);
	const auto lerp = [&](float a_start, float a_end) {
		return std::lerp(a_start, a_end, t);
	};

	Profile result{
		lerp(a_a.EnhancementIntensity, a_b.EnhancementIntensity),
		lerp(a_a.HaloRadius, a_b.HaloRadius),
		lerp(a_a.HaloSpread, a_b.HaloSpread),
		lerp(a_a.BloomSaturation, a_b.BloomSaturation),
		{ lerp(a_a.BloomTint.x, a_b.BloomTint.x),
			lerp(a_a.BloomTint.y, a_b.BloomTint.y),
			lerp(a_a.BloomTint.z, a_b.BloomTint.z) },
		lerp(a_a.CompressionThreshold, a_b.CompressionThreshold),
		lerp(a_a.CompressionCeiling, a_b.CompressionCeiling)
	};
	SanitizeProfile(result);
	return result;
}

void Bloom::SanitizeProfile(Profile& a_profile)
{
	SanitizeProfileWithDefaults(a_profile, Profile{});
}

void Bloom::SanitizeSettings(PresetSettings& a_settings)
{
	const PresetSettings defaults{};

	a_settings.Enabled = a_settings.Enabled != 0;
	a_settings.SelectedPreset = std::min(a_settings.SelectedPreset, 2u);
	SanitizeProfileWithDefaults(a_settings.Default, defaults.Default);
	SanitizeProfileWithDefaults(a_settings.Fantasy, defaults.Fantasy);
	SanitizeProfileWithDefaults(a_settings.Dreamy, defaults.Dreamy);
}
