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

	struct VolumetricLighting
	{
		using GodrayProfile = VolumetricLightingTuning::Profile;
#include "volumetric_lighting_settings_under_test.h"
		Settings settings;
		bool loaded = true;
		bool TryGetActiveGodrayProfile(GodrayProfile& profile) const;
		GodrayProfile GetRuntimeGodrayProfile() const;
	};

	namespace globals
	{
		inline State replacementState;
		inline State* state = &replacementState;
		namespace features
		{
			inline VolumetricLighting volumetricLighting;
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
