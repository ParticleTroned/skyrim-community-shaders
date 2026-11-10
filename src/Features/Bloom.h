#pragma once

#include <array>

struct Bloom
{
	static constexpr float kEnhancementIntensityMax = 6.0f;
	static constexpr float kHaloRadiusMax = 14.0f;
	static constexpr float kBloomSaturationMax = 2.0f;
	static constexpr float kCompressionCeilingMax = 1.5f;
	static constexpr uint kPresetCount = 3;
	struct Profile
	{
		float EnhancementIntensity = 0.0f;
		float HaloRadius = 3.5f;
		float HaloSpread = 0.85f;

		float BloomSaturation = 0.9f;
		float3 BloomTint = { 1.0f, 0.98f, 0.94f };
		float CompressionThreshold = 0.0f;
		float CompressionCeiling = 1.5f;
	};

	struct Settings
	{
		uint Enabled = false;
		float EnhancementIntensity = 0.0f;
		float HaloRadius = 3.5f;
		float HaloSpread = 0.85f;

		float BloomSaturation = 0.9f;
		float3 BloomTint = { 1.0f, 0.98f, 0.94f };
		float CompressionThreshold = 0.0f;
		float CompressionCeiling = 1.5f;
		float BlendWeight = 0.0f;
		float pad = 0.0f;
	};
	static_assert(sizeof(Settings) == 48);

	static const char* GetPresetName(uint a_preset);
	static Profile GetPresetProfile(uint a_preset);
	/** Retains edits independently when switching a layer's bloom preset. */
	struct PresetSlots
	{
		uint selected = 0;
		std::array<Profile, kPresetCount> profiles{ GetPresetProfile(0), GetPresetProfile(1), GetPresetProfile(2) };
	};

	static void DrawProfileControls(Profile& a_profile, PresetSlots& a_presets);
	static bool DrawAdvancedProfileSettings(Profile& a_profile);
	static Settings GetCommonBufferData(const Profile& a_profile, float a_blendWeight);
	/** Save the active slot before selecting another; does not enable bloom. */
	static void SelectPreset(Profile& a_profile, PresetSlots& a_presets, uint a_selected);
	/** Restore only the selected slot and its active values. */
	static void ResetPreset(Profile& a_profile, PresetSlots& a_presets);
	/** Bounds selection and every stored slot before use. */
	static void SanitizePresets(PresetSlots& a_presets);
	static bool IsPreset(const Profile& a_profile, uint a_preset);
	static Profile LerpProfiles(const Profile& a_a, const Profile& a_b, float a_t);
	static void SanitizeProfile(Profile& a_profile);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Bloom::Profile,
	EnhancementIntensity,
	HaloRadius,
	HaloSpread,
	BloomSaturation,
	BloomTint,
	CompressionThreshold,
	CompressionCeiling)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Bloom::PresetSlots,
	selected,
	profiles)
