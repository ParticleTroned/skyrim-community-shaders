#include "Common/GBuffer.hlsli"
#include "Common/Game.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Random.hlsli"
#include "Common/Math.hlsli"

// [Per H. Christensen, Brent Burley 2015, "Approximate Reflectance Profiles for Efficient Subsurface Scattering"]
// https://graphics.pixar.com/library/ApproxBSSRDF/paper.pdf
float3 GetBurleyCDF(float3 d, float3 r, float rand)
{
    return 1 - 0.25 * exp(-r / d) - 0.75 * exp(-r / (3 * d)) - rand;
}

float GetBurleyPDF(float r, float l, float s)
{
    float d = l / s;
    float pdf = 0.25 / d * (exp(-r / d) + exp(-r / (3 * d))); // cdf dr
    return max(pdf, 1e-5f);
}

// [Tiantian Xie et al. 2020, "Real-time subsurface scattering with single pass variance-guided adaptive importance sampling"]
// https://thisistian.github.io/publication/spvg_xie_2020_I3D_small.pdf
// Also check https://zero-radiance.github.io/post/sampling-diffusion/
float RadiusApprox(float d, float rand)
{
	// g(ξ) = d((2 − c)ξ − 2)log(1 − ξ)
	// minimal mean squared error when c = 2.5715
    return d * ((2 - 2.5715f) * rand - 2) * log(1 - rand);
}

float3 GetBurleyProfile(float3 l, float3 s, float radius)
{
	// Returns the *radial* (angle-integrated) Burley profile p_r(r), consistent with GetBurleyPDF().
	// With d_phys = l / s:
	// p_r(r) = 1 / (4 * d_phys) * (exp(-r / d_phys) + exp(-r / (3 * d_phys))).
	//
	// Note: We work in normalized space (divide r and d by l), so d = 1 / s and r = radius / l.

    float3 d = 1.f / s;
    float3 r = radius / l;
    float3 negRbyD = -r / d;

	// 1 / (4 * d_phys) == 1 / (4 * (l / s)) == 1 / (4 * d * l) since d = 1 / s
    return max((exp(negRbyD) + exp(negRbyD / 3.0f)) / (d * l * 4.0f), 1e-12f);
}

float3 GetScalingFactor(float3 albedo)
{
	// we have three methods for calculating the scaling factor
	// d = l / (1.85 − A + 7|A − 0.8|^3)
	// d = l / (1.9 − A + 3.5(A − 0.8)^2)
	// d = l / (3.5 + 100(A − 0.33)^4)
	// here we choose the third to use diffuse mean free path as parameter.
    float3 value = albedo - 0.33f;
    return 3.5f + 100.f * pow(abs(value), 4);
}

float4 BurleyNormalizedSS(uint2 DTid, float2 texCoord, uint eyeIndex, float sssAmount, bool humanProfile, bool isFemale)
{
    float centerDepth = SharedData::GetScreenDepth(DepthTexture[DTid].x);

    float4 centerColor = ColorTexture[DTid];
    if (sssAmount == 0 || centerDepth <= 0)
    {
        return centerColor;
    }

    float4 surfaceAlbedo = AlbedoTexture[DTid];
    float3 originalColor = Color::GammaToLinear(centerColor.xyz / max(surfaceAlbedo.xyz, EPSILON_SSS_ALBEDO));

    float4 mfp = humanProfile ? MeanFreePathHuman : MeanFreePathBase;

	// MeanFreePath*.xyz = relative per-channel "color", MeanFreePath*.w = distance scalar.
	// BurleyNormalizedSS works in millimeters (see uvScale and deltaDepth conversion below).
    float3 mfpColor = max(mfp.xyz, 1e-5f.xxx);
    float mfpDist = max(mfp.w, 1e-5f);

	// Convert to per-channel mean free path *lengths* (mm)
    float3 diffuseMeanFreePathRGB = mfpColor * mfpDist;

	// Scalar mean free path used for sampling distribution (mm)
    float dmfpForSampling = mfpDist;

    float s = GetScalingFactor(surfaceAlbedo.www).x;
    float d = dmfpForSampling / s;
    float3 s3d = GetScalingFactor(surfaceAlbedo.xyz);
    float3 d3d = diffuseMeanFreePathRGB / s3d;

    const float3 normalVS = GBuffer::DecodeNormal(NormalTexture[DTid].xy);
    const float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

    float3 weightSum = 0.0f;
    float3 colorSum = 0.0f;

    float2 uvScale = (GAME_UNIT_TO_CM * 0.1f * (0.5f / tan(0.5 * radians(SSSS_FOVY)))) / centerDepth; // Scale in mm

	// center sample weight
    float centerRadius = 0.5f * (SharedData::BufferDim.z / uvScale.x + SharedData::BufferDim.w / uvScale.y);
    float centerRadiusCDF = GetBurleyCDF(d, centerRadius, 0).x;
    float3 centerWeight = GetBurleyCDF(d3d, centerRadius, 0);

    int3 seed = int3(DTid.xy, 0);
    int seedStart = Random::pcg3d(int3(seed.xy, SharedData::FrameCount)).x;

	[loop]
    for (int i = 0; i < BurleySamples; ++i)
    {
        seed.z = seedStart++;
        float2 rand = float2(Random::pcg3d(seed).xy) / 4294967296.0f; // to [0, 1)

        rand.x = centerRadiusCDF + rand.x * (1.0f - centerRadiusCDF);

		// generate radius & angle for sampling
        float radius = RadiusApprox(d, rand.x);
        float angle = 2.0f * Math::PI * rand.y;
        float2 offset = radius * float2(cos(angle), sin(angle));

        float2 sampleUV = texCoord + offset * uvScale;
        float2 sampleUVClamped = saturate(sampleUV);

        int2 sampleCoord = int2(sampleUVClamped * SharedData::BufferDim.xy);
        if (any(sampleCoord < 0) || any(sampleCoord >= SharedData::BufferDim.xy))
        {
            continue;
        }

        float sampleDepth = SharedData::GetScreenDepth(DepthTexture[sampleCoord].x);
        if (sampleDepth <= 0)
        {
            continue;
        }

        float2 sampleNormalEncoded = NormalTexture[sampleCoord].xy;
        float3 sampleNormalVS = GBuffer::DecodeNormal(sampleNormalEncoded);
        float3 sampleNormalWS = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(sampleNormalVS, 0)).xyz);

        float deltaDepth = (sampleDepth - centerDepth) * 10.f / GAME_UNIT_TO_CM; // convert to mm
        float radiusSampledInMM = sqrt(radius * radius + deltaDepth * deltaDepth);

        float4 sampleAlbedo = AlbedoTexture[sampleCoord];
        float4 sampleColor = ColorTexture[sampleCoord];
        sampleColor.xyz = Color::GammaToLinear(sampleColor.xyz / max(sampleAlbedo.xyz, EPSILON_SSS_ALBEDO));

        float maskSample = MaskTexture[sampleCoord].x;
        if (maskSample <= 0)
        {
            continue;
        }

        float pdf = GetBurleyPDF(radius, dmfpForSampling, s);

        float3 diffusionProfile = GetBurleyProfile(diffuseMeanFreePathRGB, s3d, radiusSampledInMM);
        float normalWeight = sqrt(saturate(dot(sampleNormalWS, normalWS) * 0.5f + 0.5f));
        float3 sampleWeight = (diffusionProfile / pdf) * normalWeight * maskSample;

        colorSum += sampleWeight * sampleColor.xyz;
        weightSum += sampleWeight;
    }

    colorSum /= max(weightSum, 1e-6f.xxx);

    colorSum = lerp(colorSum, originalColor, saturate(centerWeight));

    float3 color = Color::LinearToGamma(colorSum) * AlbedoTexture[DTid.xy].xyz;
    color = lerp(centerColor.xyz, color, saturate(sssAmount));

	// --- human skin controls (real-time uniforms) ---
    if (humanProfile)
    {
        float3 base0 = centerColor.xyz;

		// Clamp user controls to sane ranges (boost allowed for intensity)
        float intensity = clamp(isFemale ? SharedData::SSSHumanFemaleIntensity : SharedData::SSSHumanMaleIntensity, 0.0f, 2.0f);
        float brightness = clamp(isFemale ? SharedData::SSSHumanFemaleBrightness : SharedData::SSSHumanMaleBrightness, 0.0f, 2.0f);
        float baseSat = clamp(isFemale ? SharedData::SSSHumanFemaleBaseSaturation : SharedData::SSSHumanMaleBaseSaturation, 0.0f, 2.0f);
        float finalSat = clamp(isFemale ? SharedData::SSSHumanFemaleSaturation : SharedData::SSSHumanMaleSaturation, 0.0f, 2.0f);

		// Brightness / base saturation adjustments on the *scattered* result
        float luma0 = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        float3 base = lerp(luma0.xxx, color, baseSat) * brightness;

		// Apply intensity as a delta from original (keeps output stable)
        float3 delta = base - base0;
        color = base0 + delta * intensity;

		// Final saturation control on the output
        float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        color = lerp(luma.xxx, color, finalSat);

		// Prevent negative values (avoids odd artifacts in later passes)
        color = max(color, 0.0f.xxx);
    }

    float4 outColor = float4(color, ColorTexture[DTid.xy].w);
    return outColor;
}