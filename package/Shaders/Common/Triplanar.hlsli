#ifndef TRIPLANAR_HLSLI
#define TRIPLANAR_HLSLI

namespace Triplanar
{
	static const float BLEND_SHARPNESS = 6.0;  // Implemented as x^6 for cheaper weight computation.
	static const float STRETCH_CUTOFF = 0.4;   // About cos(66 degrees); lower per-axis alignment visibly stretches.
	static const float STABLE_NOISE_DENSITY = 16.0;

	/// Compute triplanar blend weights using face normal mask and smooth vertex normal blend.
	float3 GetWeights(float3 vertexNormal, float3 faceNormal)
	{
		float3 mask = step(STRETCH_CUTOFF, abs(faceNormal));
		float3 n = abs(vertexNormal);
		float3 n2 = n * n;
		float3 w = n2 * n2 * n2 * mask;
		return w / (dot(w, 1.0) + EPSILON_DIVISION);
	}

	/// Stereo-stable stochastic selector anchored in projected world space.
	float StableNoise(float3 worldPos, float scale)
	{
		float safeScale = max(abs(scale) * STABLE_NOISE_DENSITY, EPSILON_DIVISION);
		float3 p = floor(worldPos * safeScale);
		p = frac(p * 0.1031);
		p += dot(p, p.yzx + 33.33);
		return frac((p.x + p.y) * p.z);
	}

	/// Weighted triplanar sample blending all 3 planes; stable for alpha/fade values.
	float4 Sample(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale)
	{
		return tex.Sample(samp, worldPos.yz * scale) * weights.x +
		       tex.Sample(samp, worldPos.xz * scale) * weights.y +
		       tex.Sample(samp, worldPos.xy * scale) * weights.z;
	}

	/// Compute gradients for stochastic triplanar sampling, pre-computed before branching.
	void ComputeGradients(float3 worldPos, float scale, out float3 dPdx, out float3 dPdy)
	{
		dPdx = ddx(worldPos * scale);
		dPdy = ddy(worldPos * scale);
	}

	/// Stochastic triplanar: select one projection plane via noise, reducing 3 texture reads to 1.
	float4 SampleStochastic(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float noise)
	{
		float3 dPdx, dPdy;
		ComputeGradients(worldPos, scale, dPdx, dPdy);

		if (noise < weights.x)
			return tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz);
		if (noise < weights.x + weights.y)
			return tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz);
		return tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy);
	}

	/// Stochastic triplanar with mip bias via gradient scaling.
	float4 SampleStochasticBias(Texture2D<float4> tex, SamplerState samp, float3 worldPos, float3 weights, float scale, float bias, float noise)
	{
		float3 dPdx, dPdy;
		ComputeGradients(worldPos, scale, dPdx, dPdy);
		float biasScale = exp2(bias);
		dPdx *= biasScale;
		dPdy *= biasScale;

		if (noise < weights.x)
			return tex.SampleGrad(samp, worldPos.yz * scale, dPdx.yz, dPdy.yz);
		if (noise < weights.x + weights.y)
			return tex.SampleGrad(samp, worldPos.xz * scale, dPdx.xz, dPdy.xz);
		return tex.SampleGrad(samp, worldPos.xy * scale, dPdx.xy, dPdy.xy);
	}
}

#endif  // TRIPLANAR_HLSLI
