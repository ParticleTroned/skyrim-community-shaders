#pragma once

#include <cstddef>

struct WaterAppearance
{
	struct Profile
	{
		static constexpr float kIdentityScale = 1.0f;
		static constexpr float kIdentityFresnelMin = 0.0f;
		static constexpr float kIdentityFresnelMax = 1.0f;
		static constexpr int kDefaultParallaxQuality = 16;
		static constexpr int kMinParallaxQuality = 4;
		static constexpr int kMaxParallaxQuality = 64;

		float WaterBrightness = kIdentityScale;
		float GlobalReflectionAmount = kIdentityScale;
		float RefractionAmount = kIdentityScale;
		float SunSpecularMultiplier = kIdentityScale;
		float WaveAmplitude = kIdentityScale;
		float FresnelMin = kIdentityFresnelMin;
		float FresnelMax = kIdentityFresnelMax;
		float Muddiness = kIdentityScale;
		float CausticsStrength = kIdentityScale;
		float CausticsTiling = kIdentityScale;
		float CausticsSpeed = kIdentityScale;
		float CausticsDispersion = kIdentityScale;
		float ParallaxStrength = kIdentityScale;
		int ParallaxQuality = kDefaultParallaxQuality;
	};

	struct alignas(16) Settings
	{
		uint Enabled = false;
		float WaterBrightness = Profile::kIdentityScale;
		float GlobalReflectionAmount = Profile::kIdentityScale;
		float RefractionAmount = Profile::kIdentityScale;

		float SunSpecularMultiplier = Profile::kIdentityScale;
		float WaveAmplitude = Profile::kIdentityScale;
		float FresnelMin = Profile::kIdentityFresnelMin;
		float FresnelMax = Profile::kIdentityFresnelMax;

		float Muddiness = Profile::kIdentityScale;
		float CausticsStrength = Profile::kIdentityScale;
		float CausticsTiling = Profile::kIdentityScale;
		float CausticsSpeed = Profile::kIdentityScale;
		float CausticsDispersion = Profile::kIdentityScale;
		float ParallaxStrength = Profile::kIdentityScale;
		uint ParallaxQuality = Profile::kDefaultParallaxQuality;
		float pad{};
	};
	static_assert(alignof(Settings) == 16);
	static_assert(sizeof(Settings) == 64);
	static_assert(offsetof(Settings, CausticsStrength) == 36);
	static_assert(offsetof(Settings, ParallaxQuality) == 56);

	static void DrawProfileControls(Profile& a_profile);
	/** Draws the base wave scale independently of optional wind modulation. */
	static void DrawWaveAmplitudeControl(Profile& a_profile);
	static void DrawAdvancedProfileSettings(Profile& a_profile);
	static Settings GetCommonBufferData(const Profile& a_profile);
	static Profile LerpProfiles(const Profile& a_a, const Profile& a_b, float a_t);
	static void SanitizeProfile(Profile& a_profile);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WaterAppearance::Profile,
	WaterBrightness,
	GlobalReflectionAmount,
	RefractionAmount,
	SunSpecularMultiplier,
	WaveAmplitude,
	FresnelMin,
	FresnelMax,
	Muddiness,
	CausticsStrength,
	CausticsTiling,
	CausticsSpeed,
	CausticsDispersion,
	ParallaxStrength,
	ParallaxQuality)
