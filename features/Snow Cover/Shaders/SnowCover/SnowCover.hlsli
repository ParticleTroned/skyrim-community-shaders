#ifndef SNOW_COVER_HLSLI
#define SNOW_COVER_HLSLI

#include "Common/Color.hlsli"
#include "Common/ColorHSV.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/LightingCommon.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#if defined(PSHADER)

namespace SnowCover
{

	static const uint MaxFireMeltSources = 8;
	static const uint WorldMaterialFlags = Permutation::ExtraFlags::InWorld | Permutation::ExtraFlags::InReflection;
	static const float WeatherDensityScale = 500.0;
	static const float WeatherFadeWobble = 1000.0;
	static const float WeatherNoiseScale = 0.001;
	static const float FoliageTransitionWidth = 5000.0;
	static const float TextureWorldScale = 100.0;
	static const float WaterFadeDepth = 64.0;
	static const float WaterFadeHeight = 8.0;
	static const float DisplacementInfluence = 5.0;
	static const float MaterialNoiseScale = 0.01;
	static const float MaterialNoisePhase = 0.025;
	static const float MaterialNoiseStrength = 0.05;
	static const float MinFoliageCoverage = 0.01;
	static const float LegacySpecularScale = 12500.0;
	static const float DisabledGlintDensity = 1.0;
	static const float GlintEnableThreshold = 1.1;
	static const float SkyVisibilityThreshold = 0.75;
	static const float GrassBaseVisibility = 0.55;
	static const float TreeBaseVisibility = 0.15;
	static const float FoliageNormalBias = 0.5;
	static const float NormalBlendStrength = 0.75;
	static const float CoolFoliageHue = 0.55;
	static const float AutumnHue = 0.1;
	static const float MinSeasonalSaturation = 0.25;
	static const float RooftopHeightFadeStart = 0.5;

	Texture2D<float4> SnowAlbedo : register(t112);
	Texture2D<float3> SnowNormal : register(t113);
	Texture2D<float4> SnowRmaos : register(t114);
	Texture2D<float4> IceAlbedo : register(t115);
	Texture2D<float3> IceNormal : register(t116);
	Texture2D<float4> IceRmaos : register(t117);
	Texture2D<float> SnowMap : register(t118);

	bool IsActive()
	{
		return SharedData::snowCoverSettings.EnableSnowCover && (Permutation::ExtraShaderDescriptor & WorldMaterialFlags) != 0;
	}

	float GetWeatherRange(float3 p, float viewDist)
	{
		float fadeStart = SharedData::snowCoverSettings.WeatherFadeStart;
		float fadeEnd = max(fadeStart + 1, SharedData::snowCoverSettings.WeatherFadeEnd);
		float wobble = WeatherFadeWobble * sin(p.z * WeatherNoiseScale + cos(p.x * p.y * WeatherNoiseScale));
		return 1 - smoothstep(fadeStart, max(fadeStart + 1, fadeEnd + wobble), viewDist);
	}
	float GetObjectFade(float viewDist)
	{
		float amount = SharedData::snowCoverSettings.ObjectFadeAmount;
		if (amount <= 0)
			return 1;
		float fadeStart = SharedData::snowCoverSettings.ObjectFadeStart;
		float fadeEnd = max(fadeStart + 1, SharedData::snowCoverSettings.ObjectFadeEnd);
		return 1 - smoothstep(fadeStart, fadeEnd, viewDist) * amount;
	}

	float GetFireMelt(float3 p, uint count, float strength, float radiusScale, float4 spheres[8])
	{
		float melt = 0;
		uint fireCount = min(count, MaxFireMeltSources);
		[loop] for (uint i = 0; i < fireCount; i++)
		{
			float4 fire = spheres[i];
			float radius = max(0.001, fire.w * radiusScale);
			float3 delta = p - fire.xyz;
			melt = max(melt, 1 - smoothstep(radius * radius * 0.25, radius * radius, dot(delta, delta)));
		}
		return melt * strength;
	}

	float GetFireMelt(float3 p)
	{
		return GetFireMelt(p, SharedData::snowCoverSettings.FireMeltCount,
			SharedData::snowCoverSettings.FireMeltStrength, SharedData::snowCoverSettings.FireMeltRadiusScale,
			SharedData::snowCoverSettings.FireMeltSpheres);
	}

	struct CoverageInputs
	{
		float3 position;
		float environment;
		float weatherRange;
		float weatherUpness;
		float waterFade;
		float scale;
		uint foliage;
	};

	float GetAreaMask(float3 position)
	{
		return SnowExclusions::ContainsAny(SharedData::snowCoverSettings.snowExclusions, position.x, position.y) ? 0 : 1;
	}

	float GetCoverage(CoverageInputs footprint, float3 state, float fire)
	{
		float environment = footprint.environment + state.z / SharedData::snowCoverSettings.BlendSmoothness;
		float weather = footprint.weatherRange * state.x * max(WeatherDensityScale, state.y) / WeatherDensityScale;
		float coverage;
		if (footprint.foliage)
			coverage = saturate(SharedData::snowCoverSettings.MainTint.a * saturate(environment) + weather);
		else {
			weather = clamp(weather * state.x * state.x * footprint.weatherUpness, -1, 1);
			coverage = saturate(max(environment, weather)) - footprint.waterFade;
		}
		return saturate(footprint.scale * coverage * (1 - fire));
	}

	// Callers include the shared area mask in skylight for both coverage and melting.
	CoverageInputs GetSurfaceFootprint(float3 normal, float disp, float3 p, float skylight,
		float waterDist, float viewDist, float lift, float environment)
	{
		CoverageInputs footprint;
		footprint.position = p;
		footprint.environment = environment + (lift - SharedData::snowCoverSettings.SeasonalAltitude) / SharedData::snowCoverSettings.BlendSmoothness + disp * DisplacementInfluence;
		footprint.weatherRange = GetWeatherRange(p, viewDist);
		footprint.weatherUpness = max(SharedData::snowCoverSettings.minAngle, normal.z);
		footprint.waterFade = smoothstep(-WaterFadeDepth, WaterFadeHeight, -waterDist - disp);
		footprint.scale = skylight * smoothstep(SharedData::snowCoverSettings.minAngle, SharedData::snowCoverSettings.maxAngle, normal.z);
#	if !defined(LANDSCAPE) && !defined(LOD) && !defined(LODLANDSCAPE) && !defined(LODLANDNOISE)
		footprint.scale *= GetObjectFade(viewDist);
#	endif
		footprint.foliage = 0;
		return footprint;
	}

	float GetHeightMult(float3 p)
	{
		float2 scale = SharedData::snowCoverSettings.mapScale;
		float2 offset = SharedData::snowCoverSettings.mapOffset;
		float2 uv = p.xy * scale + offset;
		float height_tresh = p.z - SharedData::snowCoverSettings.SnowHeightOffset + (SnowMap.SampleLevel(SampColorSampler, uv, 0) - 0.5) * SharedData::snowCoverSettings.mapZscale;
		return height_tresh;
	}

	float GetEnvironmentalMultiplier(float3 p)
	{
		return (GetHeightMult(p) + SharedData::snowCoverSettings.SeasonalAltitude) / SharedData::snowCoverSettings.BlendSmoothness;
	}

	float GetRooftopLift(float modelHeight, float skyVisibility)
	{
		float height = SharedData::snowCoverSettings.RooftopHeight;
		float exposure = smoothstep(SharedData::snowCoverSettings.RooftopExposure, 1.0, skyVisibility);
		return SharedData::snowCoverSettings.RooftopSnow * exposure * smoothstep(RooftopHeightFadeStart * height, height, modelHeight);
	}

	void ApplyFoliageColor(inout float3 color, float env_mult)
	{
		float gmult = saturate(env_mult - SharedData::snowCoverSettings.FoliageHeightOffset / FoliageTransitionWidth);
		if (gmult <= 0)
			return;
		float3 hsv = Color::RGBtoHSV(color);
		if (hsv.x > CoolFoliageHue)
			hsv.x = frac(lerp(hsv.x, 1 + AutumnHue, gmult));
		else
			hsv.x = lerp(hsv.x, AutumnHue, gmult);
		hsv.y *= lerp(1, MinSeasonalSaturation, 4.0 * gmult * (1.0 - gmult));
		color = Color::HSVtoRGB(hsv);
	}

	// The visibility factors include the area mask; environmental tint remains independent.
	float GetFoliageSnowBase(float3 worldNormal, float3 p, float viewDist, float distMult, out float env_mult)
	{
		env_mult = GetEnvironmentalMultiplier(p);
		float weatherMult = GetWeatherRange(p, viewDist) * SharedData::snowCoverSettings.TimeSnowing * max(WeatherDensityScale, SharedData::snowCoverSettings.SnowingDensity) / WeatherDensityScale;
		float mult = SharedData::snowCoverSettings.MainTint.a * saturate(env_mult);
		return distMult * saturate(mult + weatherMult) * smoothstep(SharedData::snowCoverSettings.minAngle, SharedData::snowCoverSettings.maxAngle, worldNormal.z);
	}

	CoverageInputs GetFoliageFootprint(float3 normal, float3 p, float viewDist, float environment, float scale)
	{
		CoverageInputs footprint;
		footprint.position = p;
		footprint.environment = environment - SharedData::snowCoverSettings.SeasonalAltitude / SharedData::snowCoverSettings.BlendSmoothness;
		footprint.weatherRange = GetWeatherRange(p, viewDist);
		footprint.weatherUpness = 1;
		footprint.waterFade = 0;
		footprint.scale = scale * smoothstep(SharedData::snowCoverSettings.minAngle, SharedData::snowCoverSettings.maxAngle, normal.z);
		footprint.foliage = 1;
		return footprint;
	}

	void ApplySnowFoliageBase(inout float3 color, float3 worldNormal, float3 p, float env_mult, float mult, bool pbrMaterial, bool applyDiffuseControls = true)
	{
		[branch] if (mult > 0)
			mult *= 1 - GetFireMelt(p);
#	if defined(GRASS)
		if (SharedData::snowCoverSettings.AffectGrassTint) {
#	else
		if (SharedData::snowCoverSettings.AffectTreeTint) {
#	endif
			ApplyFoliageColor(color, env_mult);
		}
		if (mult < MinFoliageCoverage)
			return;
		float2 uv = frac(SharedData::snowCoverSettings.UVScale * (p.xy + worldNormal.xy) / TextureWorldScale);
		float3 snowSample = SnowAlbedo.Sample(SampColorSampler, uv).rgb;
		float3 diffuse = pbrMaterial ? snowSample : Color::LinearToSrgb(snowSample);
		if (applyDiffuseControls)
			diffuse = Color::Diffuse(diffuse, pbrMaterial);
		diffuse *= SharedData::snowCoverSettings.MainTint.rgb * Color::PBRLightingScale;

		color = lerp(color, diffuse, mult);
	}

#	if defined(BASIC_SNOW_COVER)
	void ApplyDistantTreeSnow(inout float3 color, float3 normal, float3 relativeWorldPos, float2 uv, uint eyeIndex, Texture2D<float4> diffuseTexture)
	{
		if (IsActive()) {
			float3 snowPosition = relativeWorldPos + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
			float3 snowNormal = normalize(normal + float3(0, 0, FoliageNormalBias));
			float viewDist = length(mul(FrameBuffer::CameraView[eyeIndex], float4(relativeWorldPos, 1)).xyz);
			float environment;
			float coverage = GetFoliageSnowBase(snowNormal, snowPosition, viewDist, GetObjectFade(viewDist) * GetAreaMask(snowPosition), environment);
			float skylight = TreeBaseVisibility;
			[branch] if (coverage > 0 && SharedData::snowCoverSettings.EnableExpensiveFoliage)
			{
				uint width, height;
				diffuseTexture.GetDimensions(width, height);
				skylight = 1 - diffuseTexture.Sample(SampColorSampler, uv - float2(0, 2.0 / max(height, 1u))).a;
			}
			skylight *= saturate(relativeWorldPos.z - SharedData::GetWaterData(relativeWorldPos, eyeIndex).w);
			ApplySnowFoliageBase(color, snowNormal, snowPosition, environment, coverage * skylight,
#		if defined(TRUE_PBR)
				true
#		else
				false
#		endif
			);
		}
	}

	struct GrassSnowState
	{
		CoverageInputs footprint;
		float3 normal;
		float environment;
		float coverage;
	};

	GrassSnowState GetGrassSnowState(float3 normal, float3 relativeWorldPos, float2 uv, float3 viewPosition, float skyVisibility, bool complex, uint eyeIndex, Texture2D<float4> diffuseTexture, bool pbrMaterial = false)
	{
		float3 snowPosition = relativeWorldPos + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
		float3 snowNormal = float3(uv, normal.z * (1 - FoliageNormalBias) + FoliageNormalBias);
		float snowEnvironment;
		float areaMask = GetAreaMask(snowPosition);
		float snowCoverage = GetFoliageSnowBase(snowNormal, snowPosition, length(viewPosition), areaMask, snowEnvironment);
		float snowOcclusion = smoothstep(0, SkyVisibilityThreshold, skyVisibility) * GrassBaseVisibility;
		bool needsSnowFootprint = snowCoverage > 0;
#		if defined(WETTERNESS)
		needsSnowFootprint = needsSnowFootprint || SharedData::snowMeltSettings.enabled != 0;
#		endif
		[branch] if (needsSnowFootprint && SharedData::snowCoverSettings.EnableExpensiveFoliage)
		{
			float rx;
			float ry;
			diffuseTexture.GetDimensions(rx, ry);
			if (!pbrMaterial && complex) {
				snowOcclusion = max(snowOcclusion, 1 - diffuseTexture.SampleBias(SampColorSampler, float2(uv.x, uv.y * 0.5 - 0.5 / ry), SharedData::MipBias).a);
			} else {
				snowOcclusion = max(snowOcclusion, 1 - diffuseTexture.SampleBias(SampColorSampler, uv - float2(0, 1. / ry), SharedData::MipBias).a);
			}
		}
		snowOcclusion *= areaMask * saturate(relativeWorldPos.z - SharedData::GetWaterData(relativeWorldPos, eyeIndex).w);
		GrassSnowState state;
		state.footprint = GetFoliageFootprint(snowNormal, snowPosition, length(viewPosition), snowEnvironment, snowOcclusion);
		state.normal = snowNormal;
		state.environment = snowEnvironment;
		state.coverage = snowCoverage * snowOcclusion;
		return state;
	}

	void ApplyGrassSnow(inout float3 color, GrassSnowState state, bool pbrMaterial = false, bool applyDiffuseControls = true)
	{
		ApplySnowFoliageBase(color, state.normal, state.footprint.position, state.environment, state.coverage, pbrMaterial, applyDiffuseControls);
	}
#	endif

#	if !defined(BASIC_SNOW_COVER)
	float ApplySnowBase(float3 worldNormal, inout float2 uv, out bool alt, float disp, float3 p, float skylight, float waterDist, float viewDist, float lift, float environment)
	{
		CoverageInputs footprint = GetSurfaceFootprint(worldNormal, disp, p, skylight, waterDist, viewDist, lift, environment);
		float weatherMult = GetWeatherRange(p, viewDist) * (SharedData::snowCoverSettings.TimeSnowing * SharedData::snowCoverSettings.TimeSnowing * SharedData::snowCoverSettings.TimeSnowing) * max(WeatherDensityScale, SharedData::snowCoverSettings.SnowingDensity) / WeatherDensityScale;
		weatherMult = clamp((weatherMult)*max(SharedData::snowCoverSettings.minAngle, worldNormal.z), -1, 1);
		float3 state = float3(SharedData::snowCoverSettings.TimeSnowing, SharedData::snowCoverSettings.SnowingDensity, SharedData::snowCoverSettings.SeasonalAltitude);
		float mult = GetCoverage(footprint, state, 0);
		[branch] if (mult > 0)
			mult *= 1 - GetFireMelt(p);
		if (mult <= 0) {
			alt = false;
			return 0;
		}
		float main_mult = (1 - abs(worldNormal.z - SharedData::snowCoverSettings.peakMainAngle)) + min(0, weatherMult) * SharedData::snowCoverSettings.minAngle;
		float alt_mult = (1 - abs(worldNormal.z - SharedData::snowCoverSettings.peakAltAngle)) + sin(p.z * MaterialNoiseScale + cos(p.x * p.y * MaterialNoiseScale) * MaterialNoisePhase) * MaterialNoiseStrength;
		alt = alt_mult > main_mult;
		uv = frac(SharedData::snowCoverSettings.UVScale * (p.xy / TextureWorldScale + worldNormal.xy * disp));
		return min(1, mult);
	}

	void SampleSnowMaterial(bool alt, float2 uv, out float3 albedo, out float3 normal, out float4 rmaos, out float4 tint, out float specular)
	{
		if (alt) {
			albedo = IceAlbedo.Sample(SampColorSampler, uv).rgb;
			normal = TransformNormal(IceNormal.Sample(SampNormalSampler, uv).rgb);
			rmaos = IceRmaos.Sample(SampColorSampler, uv);
			tint = SharedData::snowCoverSettings.AltTint;
			specular = SharedData::snowCoverSettings.altSpec;
		} else {
			albedo = SnowAlbedo.Sample(SampColorSampler, uv).rgb;
			normal = TransformNormal(SnowNormal.Sample(SampNormalSampler, uv).rgb);
			rmaos = SnowRmaos.Sample(SampColorSampler, uv);
			tint = SharedData::snowCoverSettings.MainTint;
			specular = SharedData::snowCoverSettings.mainSpec;
		}
	}

#		if defined(TRUE_PBR)
	float ApplySnowPBR(inout MaterialProperties material, inout float3 worldNormal, float disp, float3 p, float skylight, float waterDist, float viewDist, float2 uv, float lift, float environment)
	{
		bool alt;
		float mult = ApplySnowBase(worldNormal, uv, alt, disp, p, skylight, waterDist, viewDist, lift, environment);
		if (mult <= 0)
			return mult;
		float3 albedo;
		float4 rmaos, tint;
		float specular;
		SampleSnowMaterial(alt, uv, albedo, worldNormal, rmaos, tint, specular);
		material.BaseColor = lerp(material.BaseColor, Color::Diffuse(albedo) * tint.rgb, mult * tint.a);
		material.F0 = lerp(material.F0, rmaos.w * specular, mult);
		material.Roughness = lerp(material.Roughness, clamp(rmaos.x, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness), mult);
		material.Metallic = lerp(material.Metallic, rmaos.y, mult);
		material.AO = lerp(material.AO, rmaos.z, mult * 0.5);
		if (!SharedData::snowCoverSettings.EnableGlints)
			return mult;
		if (material.GlintLogMicrofacetDensity > GlintEnableThreshold) {
			material.GlintScreenSpaceScale = lerp(material.GlintScreenSpaceScale, SharedData::snowCoverSettings.Glint.x, mult);
			material.GlintLogMicrofacetDensity = lerp(material.GlintLogMicrofacetDensity, SharedData::snowCoverSettings.Glint.y, mult);
			material.GlintMicrofacetRoughness = lerp(material.GlintMicrofacetRoughness, SharedData::snowCoverSettings.Glint.z, mult);
			material.GlintDensityRandomization = lerp(material.GlintDensityRandomization, SharedData::snowCoverSettings.Glint.w, mult);
		} else {
			material.GlintScreenSpaceScale = SharedData::snowCoverSettings.Glint.x;
			material.GlintLogMicrofacetDensity = lerp(PBR::Constants::MaxGlintDensity, SharedData::snowCoverSettings.Glint.y, mult);
			material.GlintMicrofacetRoughness = SharedData::snowCoverSettings.Glint.z;
			material.GlintDensityRandomization = SharedData::snowCoverSettings.Glint.w;
		}
		return mult;
	}
#		else

	float ApplySnow(inout MaterialProperties material, inout float3 worldNormal, float disp, float3 p, float skylight, float waterDist, float viewDist, float2 uv, float lift, float environment)
	{
		bool alt;
		float mult = ApplySnowBase(worldNormal, uv, alt, disp, p, skylight, waterDist, viewDist, lift, environment);
		if (mult <= 0.0)
			return 0;
		float3 albedo;
		float4 rmaos, tint;
		float specular;
		SampleSnowMaterial(alt, uv, albedo, worldNormal, rmaos, tint, specular);
		albedo = Color::Diffuse(Color::LinearToSrgb(albedo)) * tint.rgb * Color::PBRLightingScale;
		material.Roughness = lerp(material.Roughness, rmaos.x, mult);
		material.Shininess = lerp(material.Shininess, LegacySpecularScale * specular * rmaos.w, mult);
		material.BaseColor = lerp(material.BaseColor, rmaos.z * albedo, mult * tint.a);
		material.SpecularColor = lerp(material.SpecularColor, lerp(1.0, albedo, rmaos.y), mult);
		material.F0 = lerp(material.F0, rmaos.w * specular, mult);
		return mult;
	}
#		endif
#	endif

}
#endif

#endif
