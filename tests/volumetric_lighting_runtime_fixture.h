#pragma once

// Substitute game context while exercising extracted production selection and upload code.
namespace Runtime
{
	namespace RE
	{
		struct BSShader
		{
			enum class Type
			{
				ImageSpace = 1
			};
		};
	}

	struct State
	{
		bool enabledClasses[1]{ true };
		bool enablePShaders = true;
	};

	struct AdaptiveBrightness
	{
		struct Appearance
		{
			float godrayIntensity = 1.0f;
			float godrayOpacity = 1.0f;
			float godraySaturation = 1.0f;
			struct Tint
			{
				float x = 1.0f, y = 1.0f, z = 1.0f;
			} godrayTint;
			float godrayTintAmount = 0.0f;
		};
		struct EffectiveSettings
		{
			Appearance appearance;
		} effective;
		bool enabled = true;
		struct
		{
			AdaptiveBalanceGodray::Profiles godrays;
		} settings;
		bool IsRuntimeEnabled() const { return enabled; }
		VolumetricLightingTuning::Profile GetEffectiveGodrayProfile(bool) const;
		float volumetricClarity = 0.0f;
		float GetEffectiveVolumetricClarity() const { return enabled ? volumetricClarity : 0.0f; }
		EffectiveSettings GetEffectiveSharedLightingSettings() const { return enabled ? effective : EffectiveSettings{}; }
	};

	struct VolumetricLighting
	{
		using GodrayProfile = VolumetricLightingTuning::Profile;
#include "volumetric_lighting_settings_under_test.h"
		Settings settings;
		Settings runtimeSettings;
		mutable std::mutex settingsMutex;
		bool initialised = false;
		bool runtimeEnabled = false;
		bool inInterior = false;
		bool loaded = true;
		bool TryGetActiveGodrayProfile(GodrayProfile& profile) const;
		GodrayProfile GetRuntimeGodrayProfile() const;
		bool IsPerformanceToggleEnabled() const;
	};

	namespace globals
	{
		inline State replacementState;
		inline State* state = &replacementState;
		namespace features
		{
			inline VolumetricLighting volumetricLighting;
			inline AdaptiveBrightness adaptiveBrightness;
		}
	}

	namespace LocationContext
	{
		inline bool interior = false;
		inline bool sun = false;
		bool HasInteriorCell() { return interior; }
		bool IsInteriorWithSun() { return interior && sun; }
	}

#include "volumetric_lighting_runtime_under_test.h"
}
