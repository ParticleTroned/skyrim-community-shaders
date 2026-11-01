#if !(defined(__EYE_APPEARANCE_HLSLI__))
#define __EYE_APPEARANCE_HLSLI__

#include "Common/Color.hlsli"

#if defined(PSHADER)
namespace EyeAppearance
{
	float LightingMultiplier(float value, bool linearSpace)
	{
		return linearSpace || value == 1.0 ? value : Color::IrradianceToGamma(value);
	}

	/// Eye materials in scene and reflection draws share the same profile; previews stay neutral.
	float SpecularMultiplier(bool linearSpace = true)
	{
		float brightness = 1.0;
#if defined(EYE)
		if (Color::IsSceneColorDraw())
			brightness = SharedData::adaptiveBalanceSettings.appearance.eyeSpecularBrightness;
#endif
		return LightingMultiplier(brightness, linearSpace);
	}

	/// Match the renderer's irradiance encoding without changing the neutral path.
	float BrightnessMultiplier(bool linearSpace)
	{
		float brightness = 1.0;
#if defined(EYE)
		if (Color::IsSceneColorDraw())
			brightness = SharedData::adaptiveBalanceSettings.appearance.eyeBrightness;
#endif
		return LightingMultiplier(brightness, linearSpace);
	}
}
#endif

#endif
