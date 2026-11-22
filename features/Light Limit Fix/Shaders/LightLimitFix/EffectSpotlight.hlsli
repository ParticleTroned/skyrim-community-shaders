#ifndef LLF_EFFECT_SPOTLIGHT_HLSLI
#define LLF_EFFECT_SPOTLIGHT_HLSLI

namespace LightLimitFix
{
	struct EffectSpotlight
	{
		float4 clipX;
		float4 clipY;
		float4 clipW;
	};

	float GetEffectSpotlightFootprint(EffectSpotlight projection, float halfFalloff, float3 absolutePosition)
	{
		float4 position = float4(absolutePosition, 1.0);
		float3 clip = float3(dot(projection.clipX, position), dot(projection.clipY, position), dot(projection.clipW, position));
		float visibility = 0.0;
		[branch] if (clip.z > 0.0 && all(isfinite(clip)))
		{
			float2 projected = clip.xy / clip.z;
			float radiusSquared = dot(projected, projected);
			[branch] if (radiusSquared < 1.0)
			{
				visibility = 1.0;
				// Match the native falloff without a square root or shadow-depth sample.
				[branch] if (radiusSquared != 0.0)
				{
					[branch] if (halfFalloff == 1.0)
						visibility = 1.0 - radiusSquared;
					else visibility = 1.0 - pow(radiusSquared, halfFalloff);
				}
			}
		}
		return visibility;
	}
}
#endif
