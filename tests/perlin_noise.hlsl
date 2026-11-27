#include "Common/Finite.hlsli"
#include "Skin/Skin.hlsli"
#include "Wetterness/PuddleMask.hlsli"

RWTexture2D<float4> Output : register(u0);
SamplerState PuddleSampler : register(s0);

float Error(float actual, float expected)
{
	// max() can discard NaNs, so convert non-finite results before reduction.
	return all(Finite::IsFinite(float2(actual, expected))) ? abs(actual - expected) : 1e30;
}

float ReferenceGradient(uint hash, float3 offset)
{
	static const float3 gradients[16] = {
		float3(1, 1, 0), float3(-1, 1, 0), float3(1, -1, 0), float3(-1, -1, 0),
		float3(1, 0, 1), float3(-1, 0, 1), float3(1, 0, -1), float3(-1, 0, -1),
		float3(0, 1, 1), float3(0, -1, 1), float3(0, 1, -1), float3(0, -1, -1),
		float3(1, 1, 0), float3(-1, 1, 0), float3(0, -1, 1), float3(0, -1, -1)
	};
	return dot(gradients[hash & 15u], offset);
}

float ReferenceNoise(float3 position, uint seed = 0x578437ADu)
{
	float3 lattice = floor(position);
	float3 f = position - lattice;
	uint3 i = asuint(int3(lattice));
	float values[8];
	[unroll] for (uint corner = 0; corner < 8; ++corner)
	{
		uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, corner >> 2u);
		values[corner] = ReferenceGradient(Random::murmur3(i + offset, seed), f - offset);
	}
	float3 u = f * f * (3 - 2 * f);
	// The second row deliberately uses the unfaded x coordinate.
	return lerp(lerp(lerp(values[0], values[1], u.x), lerp(values[2], values[3], f.x), u.y),
		lerp(lerp(values[4], values[5], u.x), lerp(values[6], values[7], f.x), u.y), u.z);
}

float ReferenceFBM(float2 uv, float scale, float lacunarity, float persistence)
{
	float total = 0, amplitude = 1, amplitudeSum = 0;
	for (uint octave = 0; octave < 5; ++octave) {
		total += amplitude * (ReferenceNoise(float3(uv * scale, octave * 7.375)) + 1) * .5;
		amplitudeSum += amplitude;
		amplitude *= persistence;
		scale *= lacunarity;
	}
	return total / amplitudeSum;
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	uint index = id.y * 64 + id.x;
	uint hash = Random::murmur3(uint3(index, id.x, id.y), 17u);
	float3 offset = float3(hash & 1023u, (hash >> 10u) & 1023u, hash >> 20u) / 1024.0 - 1;
	float3 f = frac(offset) * 2 - 1;
	float3 position = float3(int(id.x) - 32, int(id.y) - 32, int(index & 31u) - 16);
	if ((index & 3u) == 1u)
		position += offset;
	if ((index & 3u) == 2u)
		position += 1e-5;
	if ((index & 3u) == 3u)
		position -= 1e-5;
	float gradientError = 0;
	[unroll] for (uint gradient = 0; gradient < 16; ++gradient)
	{
		gradientError = max(gradientError, Error(Random::PerlinGradientDot(gradient, f), ReferenceGradient(gradient, f)));
		gradientError = max(gradientError, Error(Random::PerlinGradientDot(gradient, float3(0, -0.0, 0)), 0));
	}
	float noiseError = Error(Random::perlinNoise(position, hash), ReferenceNoise(position, hash));
	float lacunarity = (index & 1u) == 0 ? .7 : (index & 2u) == 0 ? 2 :
	                                                                0;
	float persistence = (index & 4u) == 0 ? 10 : (index & 8u) == 0 ? 20 :
	                                                                 0;
	float fbmError = Error(Skin::FBM(position.xy / 1024, 1024, 5, lacunarity, persistence, 7.375), ReferenceFBM(position.xy / 1024, 1024, lacunarity, persistence));
	float endpointError = Error(Skin::PerlinNoise(position.xy, 1024, lacunarity, persistence, 0), 0) + Error(Skin::PerlinNoise(position.xy, 1024, lacunarity, persistence, 1), 1);
	float warp = ReferenceNoise(position * lerp(.22, .72, .4) + float3(12.7, 19.1, 23.3) * .7) * 2 - 1;
	float3 puddlePosition = position * 2 + warp * float3(.20, .14, .18) * lerp(0, .38, .4) + float3(31, 17, 43) * .4;
	float puddleError = Error(Wetterness::GetPuddleNoiseSignal(position, .7, .4, 2, Wetterness::PUDDLE_MASK_LEGACY_PROCEDURAL, PuddleSampler), ReferenceNoise(puddlePosition) * .5 + .5);
	Output[id.xy] = float4(gradientError, noiseError, fbmError, max(endpointError, puddleError));
}
