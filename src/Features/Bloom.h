#pragma once

struct Bloom
{
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

	static void DrawProfileControls(Profile& a_profile);
	static bool DrawAdvancedProfileSettings(Profile& a_profile);
	static Settings GetCommonBufferData(const Profile& a_profile, float a_blendWeight);
	static const char* GetPresetName(uint a_preset);
	static Profile GetPresetProfile(uint a_preset);
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
