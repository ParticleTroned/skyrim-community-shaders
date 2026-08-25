#pragma once

struct WaterAppearance
{
	struct Profile
	{
		static constexpr float kIdentityScale = 1.0f;
		static constexpr float kIdentityFresnelMin = 0.0f;
		static constexpr float kIdentityFresnelMax = 1.0f;

		float WaterBrightness = kIdentityScale;
		float GlobalReflectionAmount = kIdentityScale;
		float RefractionAmount = kIdentityScale;
		float SunSpecularMultiplier = kIdentityScale;
		float WaveAmplitude = kIdentityScale;
		float FresnelMin = kIdentityFresnelMin;
		float FresnelMax = kIdentityFresnelMax;
		float Muddiness = kIdentityScale;
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
		float3 pad{};
	};
	static_assert(alignof(Settings) == 16);
	static_assert(sizeof(Settings) == 48);

	static void DrawProfileControls(Profile& a_profile);
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
	Muddiness)
