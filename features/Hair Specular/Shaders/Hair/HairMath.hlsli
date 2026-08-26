#ifndef __HAIR_MATH_DEPENDENCY_HLSL__
#define __HAIR_MATH_DEPENDENCY_HLSL__

#include "Common/Color.hlsli"
#include "Common/Math.hlsli"

namespace Hair
{
	float3 ReorientTangent(float3 T, float3 N)
	{
		// Reorient tangent to be orthogonal to normal
		float3 T_reoriented = normalize(T - N * dot(T, N));
		return T_reoriented;
	}

	// [Kajiya et al. 1989, "Rendering fur with three dimensional textures."]
	// https://doi.org/10.1145/74334.74361
	float3 D_KajiyaKay(float3 T, float3 H, float n)
	{
		float TH = dot(T, H);
		float sinTH = saturate(1 - TH * TH);
		float dirAtten = saturate(TH + 1);
		float norm = (n + 2) / (2 * Math::PI);
		return dirAtten * norm * pow(sinTH, 0.5 * n);
	}

	float3 HairF0()
	{
		const float n = 1.55;
		const float F0 = pow((1 - n) / (1 + n), 2);
		return F0.xxx;
	}

	float3 ShiftTangent(float3 T, float3 N, float shift)
	{
		return normalize(T + N * shift);
	}

	float3 ShiftNormal(float3 T, float3 N, float shift)
	{
		float3 T_shifted = ShiftTangent(T, N, shift);
		float3 N_shifted = normalize(cross(T_shifted, cross(N, T_shifted)));
		return N_shifted;
	}

	float Hair_g(float B, float Theta)
	{
		return exp(-0.5 * Theta * Theta / (B * B)) / (sqrt(Math::TAU) * B);
	}

	float3 GetHairDiffuseAttenuationKajiyaKay(
		float3 N,
		float3 V,
		float3 L,
		float shadow,
		float3 baseColor)
	{
		float NdotL = dot(N, L);
		float NdotV = dot(N, V);
		float3 S = 0;

		float diffuseKajiya = 1 - abs(NdotL);

		float3 fakeN = normalize(V - N * NdotV);
		const float wrap = 1;
		float wrappedNdotL = saturate((dot(fakeN, L) + wrap) / ((1 + wrap) * (1 + wrap)));
		float diffuseScatter = (1 / Math::PI) * lerp(wrappedNdotL, diffuseKajiya, 0.33);
		float luma = max(Color::RGBToLuminance(baseColor), 1e-4);
		float3 scatterTint = shadow < 1 ? pow(abs(baseColor / luma), 1 - shadow) : 1;
		S += sqrt(baseColor) * diffuseScatter * scatterTint;

		return max(S, 0);
	}

	float3 Saturation(float3 color, float saturation)
	{
		float luminance = Color::RGBToLuminance(color);
		return saturate(lerp(float3(luminance, luminance, luminance), color, saturation));
	}
}

#endif  // __HAIR_MATH_DEPENDENCY_HLSL__
