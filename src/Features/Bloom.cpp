#include "Bloom.h"

#include <algorithm>
#include <cmath>

#include "I18n/I18n.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.adaptive_balance.bloom."

namespace
{
	constexpr float kEnhancementIntensityMax = 6.0f;
	constexpr float kHaloRadiusMax = 14.0f;
	constexpr float kBloomSaturationMax = 2.0f;
	constexpr float kCompressionCeilingMax = 1.5f;
	constexpr uint kPresetCount = 3;

	void DrawTooltip(const char* a_text)
	{
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", a_text);
	}

	bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::abs(a_lhs - a_rhs) <= 0.0001f;
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

void Bloom::DrawProfileControls(Profile& a_profile)
{
	SanitizeProfile(a_profile);
	ImGui::PushID(&a_profile);

	ImGui::SliderFloat(T(TKEY("amount"), "Bloom"), &a_profile.EnhancementIntensity, 0.0f, kEnhancementIntensityMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	DrawTooltip(T(TKEY("amount_tooltip"), "Bloom enhancement strength for this profile. 0 turns the enhancement off and keeps Skyrim's vanilla Bloom unchanged; values through 1 blend into the configured enhancement, and higher values amplify it before compression."));

	if (ImGui::BeginTable("##BloomPresets", 4, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn(T(TKEY("presets"), "Bloom Presets"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		for (uint preset = 0; preset < kPresetCount; ++preset) {
			const auto* name = GetPresetName(preset);
			const float width = ImGui::CalcTextSize(name).x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::TableSetupColumn(name, ImGuiTableColumnFlags_WidthFixed, width);
		}

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(T(TKEY("presets"), "Bloom Presets"));

		for (uint preset = 0; preset < kPresetCount; ++preset) {
			ImGui::TableNextColumn();
			[[maybe_unused]] auto presetStyle = Util::PresetButtonStyle(IsPreset(a_profile, preset));
			if (ImGui::Button(GetPresetName(preset)))
				a_profile = GetPresetProfile(preset);
		}
		ImGui::EndTable();
	}

	ImGui::PopID();
	SanitizeProfile(a_profile);
}

bool Bloom::DrawAdvancedProfileSettings(Profile& a_profile)
{
	bool changed = false;
	changed |= ImGui::SliderFloat(T(TKEY("halo_radius"), "Halo Radius"), &a_profile.HaloRadius, 0.0f, kHaloRadiusMax, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	DrawTooltip(T(TKEY("halo_radius_tooltip"), "Controls the radius of the enhancement's additional bloom samples. Higher values create wider halos."));
	changed |= ImGui::SliderFloat(T(TKEY("halo_spread"), "Halo Spread"), &a_profile.HaloSpread, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	DrawTooltip(T(TKEY("halo_spread_tooltip"), "Blends between the original bloom and the widened halo samples. Higher values make the halo softer and more spread out."));
	changed |= ImGui::SliderFloat(T(TKEY("saturation"), "Bloom Saturation"), &a_profile.BloomSaturation, 0.0f, kBloomSaturationMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	DrawTooltip(T(TKEY("saturation_tooltip"), "Controls the color saturation of the enhanced bloom. Lower values make it whiter; higher values preserve or exaggerate its tint."));
	changed |= ImGui::ColorEdit3(T(TKEY("tint"), "Bloom Tint"), reinterpret_cast<float*>(&a_profile.BloomTint));
	DrawTooltip(T(TKEY("tint_tooltip"), "Colors the bloom halo without changing the underlying scene lighting."));
	changed |= ImGui::SliderFloat(T(TKEY("compression_ceiling"), "Compression Ceiling"), &a_profile.CompressionCeiling, 0.0f, kCompressionCeilingMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	DrawTooltip(T(TKEY("compression_ceiling_tooltip"), "Maximum Bloom luminance approached by the soft limiter after tint and enhancement strength are applied."));
	changed |= ImGui::SliderFloat(T(TKEY("compression_threshold"), "Compression Threshold"), &a_profile.CompressionThreshold, 0.0f, a_profile.CompressionCeiling, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	DrawTooltip(T(TKEY("compression_threshold_tooltip"), "Bloom luminance where soft compression starts after tint and enhancement strength are applied."));

	SanitizeProfile(a_profile);
	return changed;
}

Bloom::Settings Bloom::GetCommonBufferData(const Profile& a_profile, float a_blendWeight)
{
	auto profile = a_profile;
	SanitizeProfile(profile);
	const float blendWeight = std::clamp(std::isfinite(a_blendWeight) ? a_blendWeight : 0.0f, 0.0f, 1.0f);
	const float enhancementBlend = std::clamp(profile.EnhancementIntensity, 0.0f, 1.0f);
	const bool enhancementEnabled = blendWeight > 0.0f && enhancementBlend > 0.0f;
	// Amount 0 is exact vanilla passthrough. Between 0 and 1, crossfade to this
	// profile's unit-strength result so profile/time transitions remain continuous.
	// At 1 and above, preserve Open Shaders' enhancement-intensity semantics.
	const float processingIntensity = enhancementEnabled ? std::max(profile.EnhancementIntensity, 1.0f) : 0.0f;
	const float effectiveBlendWeight = enhancementEnabled ? blendWeight * enhancementBlend : 0.0f;
	return {
		static_cast<uint>(enhancementEnabled),
		processingIntensity,
		profile.HaloRadius,
		profile.HaloSpread,
		profile.BloomSaturation,
		profile.BloomTint,
		profile.CompressionThreshold,
		profile.CompressionCeiling,
		effectiveBlendWeight
	};
}

const char* Bloom::GetPresetName(uint a_preset)
{
	switch (a_preset) {
	case 1:
		return T(TKEY("preset_fantasy"), "Fantasy");
	case 2:
		return T(TKEY("preset_dreamy"), "Dreamy");
	default:
		return T(TKEY("preset_default"), "Default");
	}
}

Bloom::Profile Bloom::GetPresetProfile(uint a_preset)
{
	switch (a_preset) {
	case 1:
		return { 4.0f, 5.0f, 1.0f, 1.3f, { 1.0f, 0.98f, 0.94f }, 0.0f, 0.67f };
	case 2:
		return { 2.5f, 4.0f, 0.72f, 0.85f, { 165.0f / 255.0f, 205.0f / 255.0f, 1.0f }, 0.08f, 0.9f };
	default: {
		auto profile = Profile{};
		profile.EnhancementIntensity = 1.0f;
		return profile;
	}
	}
}

bool Bloom::IsPreset(const Profile& a_profile, uint a_preset)
{
	auto profile = a_profile;
	auto preset = GetPresetProfile(a_preset);
	SanitizeProfile(profile);
	SanitizeProfile(preset);
	return NearlyEqual(profile.EnhancementIntensity, preset.EnhancementIntensity) &&
	       NearlyEqual(profile.HaloRadius, preset.HaloRadius) &&
	       NearlyEqual(profile.HaloSpread, preset.HaloSpread) &&
	       NearlyEqual(profile.BloomSaturation, preset.BloomSaturation) &&
	       NearlyEqual(profile.BloomTint.x, preset.BloomTint.x) &&
	       NearlyEqual(profile.BloomTint.y, preset.BloomTint.y) &&
	       NearlyEqual(profile.BloomTint.z, preset.BloomTint.z) &&
	       NearlyEqual(profile.CompressionThreshold, preset.CompressionThreshold) &&
	       NearlyEqual(profile.CompressionCeiling, preset.CompressionCeiling);
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

#undef I18N_KEY_PREFIX
