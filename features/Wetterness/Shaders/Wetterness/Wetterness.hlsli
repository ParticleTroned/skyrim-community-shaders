#include "Common/BRDF.hlsli"
#include "Wetterness/WetternessLighting.hlsli"
#include "Wetterness/optimized-ggx.hlsli"

namespace Wetterness
{
	float3 GetWetnessSpecular(float3 N, float3 L, float3 V, float3 lightColor, float roughness)
	{
		return LightingFuncGGX_OPT3(N, V, L, roughness, 0.02) * lightColor;
	}

// Debug visualization functions for Wetterness. Keep DEBUG_WETNESS_EFFECTS
// accepted so existing shader debug toggles still work with the separate feature.
#if defined(DEBUG_WETTERNESS) || defined(DEBUG_WETNESS_EFFECTS)
	/**
	 * Calculates ripple and splash effect intensities from water ripple info
	 *
	 * @param rippleInfo float4 containing scaled ripple normal (xyz) and splash intensity (w)
	 *                   Note: xyz = normalized ripple normal * intensity multiplier
	 * @param rippleMultiplier Multiplier for ripple effect intensity
	 * @param splashMultiplier Multiplier for splash effect intensity
	 * @return float2 where x=ripple effect, y=splash effect
	 */
	float2 GetDebugEffectIntensities(float4 rippleInfo, float rippleMultiplier, float splashMultiplier)
	{
		// rippleInfo.xyz is a scaled normal vector (normalized normal * intensity)
		// length() gives us the intensity/magnitude of the ripple effect
		float rippleEffect = saturate(length(rippleInfo.xyz) * rippleMultiplier);
		float splashEffect = saturate(rippleInfo.w * splashMultiplier);
		return float2(rippleEffect, splashEffect);
	}

	/**
	 * Generates debug color visualization for wetness effects
	 *
	 * @param effectIntensities float2 from GetDebugEffectIntensities()
	 * @param rippleColor Color to use for ripple visualization
	 * @param splashColor Color to use for splash visualization
	 * @param baseColor Base color to start with (default black)
	 * @param brightnessMultiplier Multiplier for effect brightness
	 * @return float3 Debug color, or (0,0,0) if no effects are active
	 */
	float3 GetDebugWetnessColor(float2 effectIntensities, float3 rippleColor, float3 splashColor, float3 baseColor = float3(0, 0, 0), float brightnessMultiplier = 1.0)
	{
		float threshold = 0.01;
		float rippleEffect = effectIntensities.x;
		float splashEffect = effectIntensities.y;

		if (rippleEffect > threshold || splashEffect > threshold) {
			float3 debugColor = baseColor;
			if (rippleEffect > threshold) {
				debugColor += rippleColor * (rippleEffect * brightnessMultiplier);
			}
			if (splashEffect > threshold) {
				debugColor += splashColor * (splashEffect * brightnessMultiplier);
			}
			return saturate(debugColor);
		}
		return float3(0, 0, 0);  // No debug override
	}

	/**
	 * Convenience function for standard water debug colors
	 */
	float3 GetDebugWetnessColorStandard(float4 rippleInfo, float rippleMultiplier, float splashMultiplier)
	{
		float2 effects = GetDebugEffectIntensities(rippleInfo, rippleMultiplier, splashMultiplier);
		float3 rippleColor = float3(1.0, 0.0, 1.0);  // BRIGHT MAGENTA
		float3 splashColor = float3(0.0, 1.0, 0.0);  // BRIGHT GREEN
		return GetDebugWetnessColor(effects, rippleColor, splashColor);
	}

	/**
	 * Convenience function for specular debug colors (extra bright)
	 */
	float3 GetDebugWetnessColorSpecular(float4 rippleInfo, float rippleMultiplier, float splashMultiplier)
	{
		float2 effects = GetDebugEffectIntensities(rippleInfo, rippleMultiplier, splashMultiplier);
		float3 rippleColor = float3(1.0, 0.0, 1.0);                                            // BRIGHT MAGENTA
		float3 splashColor = float3(0.0, 1.0, 0.0);                                            // BRIGHT GREEN
		return GetDebugWetnessColor(effects, rippleColor, splashColor, float3(0, 0, 0), 1.5);  // Extra bright
	}

	/**
	 * Convenience function for underwater debug colors (darker)
	 */
	float3 GetDebugWetnessColorUnderwater(float4 rippleInfo, float rippleMultiplier, float splashMultiplier)
	{
		float2 effects = GetDebugEffectIntensities(rippleInfo, rippleMultiplier, splashMultiplier);
		float3 rippleColor = float3(0.7, 0.0, 0.7);                                         // DARK MAGENTA
		float3 splashColor = float3(0.0, 0.7, 0.0);                                         // DARK GREEN
		return GetDebugWetnessColor(effects, rippleColor, splashColor, float3(0, 0, 0.2));  // Dark blue base
	}
#endif

	/**
	 * Calculates flow-aware ripple positioning with proper timing synchronization
	 *
	 * @param worldFlowVector Flow vector in world coordinate space
	 * @param flowStrength Flow strength (0-1) from flowmap alpha channel
	 * @param reflectionTimingScale Timing scale factor (typically 0.001 * ReflectionColor.w)
	 * @param avgFlowmapMultiplier Average multiplier from flowmap normal calculations
	 * @param uvToWorldScale Scale factor converting UV coordinates to world positioning (typically 1/8)
	 * @return float2 Flow offset to apply to ripple positioning
	 *
	 * @details This function synchronizes ripple movement timing with flowmap normal animations
	 *          by using the same mathematical relationship and dual-phase smoothstep timing.
	 *          The timing creates natural flow-based ripple movement that matches the water surface animation.
	 */
	float2 GetFlowAwareRippleOffset(float2 worldFlowVector, float flowStrength, float reflectionTimingScale, float avgFlowmapMultiplier = 9.26, float uvToWorldScale = 0.125)
	{
		// Calculate flow timing scale matching flowmap normal timing
		// Mathematical relationship: avgMultiplier × uvToWorldScale gives base flow scaling
		// uvToWorldScale (1/8) relates to the 64× texture coordinate scaling: 64 × (1/8) = 8
		float baseFlowMultiplier = avgFlowmapMultiplier * uvToWorldScale;  // ≈ 1.16
		float flowTimeScale = baseFlowMultiplier * reflectionTimingScale;

		// Calculate base flow offset with strength modulation
		float2 flowOffset = worldFlowVector * (flowTimeScale * flowStrength);

		// Apply dual-phase smoothstep timing for natural flow animation
		// This creates the essential dual-phase animation pattern used in flowmap blending
		float smoothTime = smoothstep(0.0, 1.0, frac(flowTimeScale));
		smoothTime = lerp(0.15, 1.0, smoothTime);  // Range: 0.15→1.0→0.15 (avoids complete stops)

		return flowOffset * smoothTime;
	}

}
