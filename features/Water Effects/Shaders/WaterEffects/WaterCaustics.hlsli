Texture2D<float4> WaterCaustics : register(t65);

namespace WaterEffects
{
	float2 PanCausticsUV(float2 uv, float speed, float tiling)
	{
		return frac((float2(1, 0) * SharedData::Timer * speed * (SharedData::waterAppearanceSettings.Enabled ? SharedData::waterAppearanceSettings.CausticsSpeed : 1.0)) + (uv * tiling));
	}

	float SampleCaustics(float2 uv, float2 uvDdx, float2 uvDdy)
	{
		return WaterCaustics.SampleGrad(SampColorSampler, uv, uvDdx, uvDdy).x;
	}

	// Approximate wavelength-dependent refraction by offsetting red/blue around green.
	float3 SampleCausticsDispersion(float2 uv, float2 dispersionOffset, float2 uvDdx, float2 uvDdy, float2 dispersionDdx, float2 dispersionDdy, bool hasDispersion)
	{
		float center = SampleCaustics(uv, uvDdx, uvDdy);
		float3 result = center.xxx;
		[branch] if (hasDispersion)
		{
			float3 dispersed = float3(
				SampleCaustics(uv - dispersionOffset * 0.75, uvDdx - dispersionDdx * 0.75, uvDdy - dispersionDdy * 0.75),
				center,
				SampleCaustics(uv + dispersionOffset, uvDdx + dispersionDdx, uvDdy + dispersionDdy));
			result = lerp(center.xxx, dispersed, 0.5);
		}
		return result;
	}

	float3 ComputeCaustics(float4 waterData, float3 worldPosition, uint eyeIndex)
	{
		float3 result = 1.0.xxx;
		float strength = SharedData::waterAppearanceSettings.Enabled ? SharedData::waterAppearanceSettings.CausticsStrength : 1.0;
		[branch] if (strength > 0.0)
		{
			float causticsDistToWater = waterData.w - worldPosition.z;
			float shoreFactorCaustics = saturate(causticsDistToWater / 64.0);

			float tiling = SharedData::waterAppearanceSettings.Enabled ? SharedData::waterAppearanceSettings.CausticsTiling : 1.0;
			float dispersion = SharedData::waterAppearanceSettings.Enabled ? SharedData::waterAppearanceSettings.CausticsDispersion : 1.0;
			bool hasDispersion = dispersion > 0.0;
			float2 causticsUV = (worldPosition.xy + FrameBuffer::CameraPosAdjust[eyeIndex].xy) * (0.005 * tiling);
			float dispersionAmount = 0.025 * shoreFactorCaustics * saturate(causticsDistToWater / 256.0) * dispersion;
			float2 dispersionOffset = float2(0.6, 0.8) * dispersionAmount;

			// Use unwrapped gradients before varying branches to avoid mip seams and undefined derivatives.
			float2 causticsUVDdx = ddx(causticsUV);
			float2 causticsUVDdy = ddy(causticsUV);
			float2 dispersionDdx = float2(0.6, 0.8) * ddx(dispersionAmount);
			float2 dispersionDdy = float2(0.6, 0.8) * ddy(dispersionAmount);

			[branch] if (shoreFactorCaustics > 0.0)
			{
				float causticsFade = 1.0 - saturate(causticsDistToWater / 1024.0);
				causticsFade *= causticsFade;

				float3 causticsHigh = 1.0.xxx;
				[branch] if (causticsFade > 0.0)
				{
					float2 causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.2, 1.0);
					float2 causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.2, -0.5);
					causticsHigh = min(
									   SampleCausticsDispersion(causticsUV1, dispersionOffset, causticsUVDdx, causticsUVDdy, dispersionDdx, dispersionDdy, hasDispersion),
									   SampleCausticsDispersion(causticsUV2, dispersionOffset, causticsUVDdx * -0.5, causticsUVDdy * -0.5, dispersionDdx, dispersionDdy, hasDispersion)) *
					               4.0;
				}

				causticsUV *= 0.5;
				dispersionOffset *= 0.5;
				causticsUVDdx *= 0.5;
				causticsUVDdy *= 0.5;
				dispersionDdx *= 0.5;
				dispersionDdy *= 0.5;

				float3 causticsLow = 1.0.xxx;
				[branch] if (causticsFade < 1.0)
				{
					float2 causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.1, 1.0);
					float2 causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.1, -0.5);
					causticsLow = min(
									  SampleCausticsDispersion(causticsUV1, dispersionOffset, causticsUVDdx, causticsUVDdy, dispersionDdx, dispersionDdy, hasDispersion),
									  SampleCausticsDispersion(causticsUV2, dispersionOffset, causticsUVDdx * -0.5, causticsUVDdy * -0.5, dispersionDdx, dispersionDdy, hasDispersion)) *
					              4.0;
				}

				const float3 caustics = lerp(causticsLow, causticsHigh, causticsFade);
				result = max(0.0, lerp(1.0.xxx, caustics, shoreFactorCaustics * strength));
			}
		}

		return result;
	}
}
