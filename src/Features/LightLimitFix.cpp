#include "LightLimitFix.h"
#include "CSEditor/LightEditor.h"
#include "Features/InverseSquareLighting/Common.h"
#include "Globals.h"
#include "InverseSquareLighting.h"
#include "LinearLighting.h"
#include "LocationContext.h"

#include "Menu/ThemeManager.h"
#include "Shadercache.h"
#include "State.h"
#include "Util.h"
#include "Utils/ExternalEmittance.h"
#include "Utils/UI.h"

#include "RE/B/BSMultiBoundRoom.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

// Per-cluster visible-light cap. Must match MAX_CLUSTER_LIGHTS in
// features/Light Limit Fix/Shaders/LightLimitFix/Common.hlsli because this
// sizes the global lightIndexList pool.
static constexpr uint CLUSTER_MAX_LIGHTS = 256;
static constexpr uint CONTACT_SHADOW_MAX_LIGHTS = 8;
static constexpr uint MAX_LIGHTS = 1024;

namespace
{
	class ScopedPerfEvent
	{
	public:
		explicit ScopedPerfEvent(std::string_view a_name)
		{
			if (globals::state) {
				globals::state->BeginPerfEvent(a_name);
				active = true;
			}
		}

		~ScopedPerfEvent()
		{
			if (active && globals::state) {
				globals::state->EndPerfEvent();
			}
		}

		ScopedPerfEvent(const ScopedPerfEvent&) = delete;
		ScopedPerfEvent& operator=(const ScopedPerfEvent&) = delete;

	private:
		bool active = false;
	};

#define CS_CONCAT_IMPL(a, b) a##b
#define CS_CONCAT(a, b) CS_CONCAT_IMPL(a, b)
#define CS_PROFILE_SCOPE(name) \
	ScopedPerfEvent CS_CONCAT(_csProfileScope, __LINE__) { name }
#define CS_PROFILE_CPU_SCOPE(name) \
	ScopedPerfEvent CS_CONCAT(_csProfileCpuScope, __LINE__) { name }

	constexpr uint kContactShadowFlagPoint = 1u << 0;
	constexpr uint kContactShadowFlagParticle = 1u << 1;
	constexpr uint kLightsVisualisationModeMax = 3;
	constexpr uint kContactShadowQualityMax = 2;
	constexpr int kContactShadowQualityOptionCount = static_cast<int>(kContactShadowQualityMax) + 1;
	constexpr uint kContactShadowClusterBudgetMin = 0;
	constexpr uint kContactShadowClusterBudgetMax = CONTACT_SHADOW_MAX_LIGHTS;
	constexpr uint kParticleContactShadowBudgetMax = 4;
	constexpr uint kStrictContactShadowBudgetMax = CONTACT_SHADOW_MAX_LIGHTS;

	constexpr float kParticleLightsSaturationMin = 1.0f;
	constexpr float kParticleLightsSaturationMax = 2.0f;
	constexpr float kParticleBrightnessMin = 0.0f;
	constexpr float kParticleBrightnessMax = 10.0f;
	constexpr float kParticleRadiusMin = 0.0f;
	constexpr float kParticleRadiusMax = 10.0f;
	constexpr float kBillboardBrightnessMin = 0.0f;
	constexpr float kBillboardBrightnessMax = 10.0f;
	constexpr float kBillboardRadiusMin = 0.0f;
	constexpr float kBillboardRadiusMax = 10.0f;
	constexpr float kParticleClusterThresholdMin = 8.0f;
	constexpr float kParticleClusterThresholdMax = 128.0f;
	constexpr int kMaxParticlesPerEmitterMin = 32;
	constexpr int kMaxParticlesPerEmitterMax = 2048;
	constexpr float kMaxParticleDistanceMin = 0.0f;
	constexpr float kMaxParticleDistanceMax = 20000.0f;
	constexpr float kParticleConfigSaturationMin = 0.0f;
	constexpr float kParticleConfigSaturationMax = 10.0f;
	constexpr float kJsonPlacedLightIntensityMin = 0.0f;
	constexpr float kJsonPlacedLightIntensityMax = 8.0f;
	constexpr std::uint32_t kParticleLightCacheSweepInterval = 120;
	constexpr std::uint32_t kParticleLightCacheMaxIdleFrames = 600;
	constexpr std::size_t kParticleLightCacheMaxEntries = static_cast<std::size_t>(MAX_LIGHTS) * 32u;
	constexpr std::size_t kMaxQueuedParticleLights = static_cast<std::size_t>(MAX_LIGHTS) * 16u;
	constexpr std::size_t kDirectionalNiLightEngineReadSize = sizeof(RE::NiDirectionalLight);

	void DrawHeatWarpStrengthSetting()
	{
		ImGui::SliderFloat(
			"Heat Warp Strength",
			&globals::state->refractionScale,
			0.0f,
			2.0f,
			"%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Scales ImageSpace refraction (heat shimmer around fire/heat sources).\n"
				"Lower values reduce warping; 0 disables it.");
		}
	}

	bool IsPlausibleRenderPointer(const void* a_ptr)
	{
		const auto value = reinterpret_cast<std::uintptr_t>(a_ptr);
		return value >= 0x10000 && value < 0x800000000000ull && (value & 0x7) == 0;
	}

	bool IsReadableRange(const void* a_ptr, std::size_t a_size) noexcept
	{
		if (!a_ptr || a_size == 0) {
			return false;
		}

		MEMORY_BASIC_INFORMATION memoryInfo{};
		if (::VirtualQuery(a_ptr, &memoryInfo, sizeof(memoryInfo)) == 0) {
			return false;
		}
		if (memoryInfo.State != MEM_COMMIT) {
			return false;
		}

		constexpr DWORD kReadableProtection = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
		                                      PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if ((memoryInfo.Protect & kReadableProtection) == 0 || (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
			return false;
		}

		const auto base = reinterpret_cast<std::uintptr_t>(memoryInfo.BaseAddress);
		const auto ptr = reinterpret_cast<std::uintptr_t>(a_ptr);
		if (ptr < base) {
			return false;
		}

		const auto offset = ptr - base;
		if (offset > memoryInfo.RegionSize) {
			return false;
		}

		const auto available = memoryInfo.RegionSize - offset;
		return a_size <= available;
	}

	bool IsSafeDirectionalNiLight(const RE::NiLight* a_light)
	{
		static const RE::NiLight* validated = nullptr;
		static uint32_t validatedFrame = std::numeric_limits<uint32_t>::max();

		if (!IsPlausibleRenderPointer(a_light)) {
			return false;
		}

		const auto* state = globals::state;
		const uint32_t frame = state ? state->frameCount : 0;
		if (state && a_light == validated && frame == validatedFrame) {
			return true;
		}
		if (!IsReadableRange(a_light, kDirectionalNiLightEngineReadSize)) {
			return false;
		}

		// Before State exists there is no advancing frame identity, so caching a
		// successful probe would otherwise trust this address indefinitely.
		validated = state ? a_light : nullptr;
		validatedFrame = state ? frame : std::numeric_limits<uint32_t>::max();
		return true;
	}

	bool IsDirectionalSceneLightSafe(RE::BSRenderPass* a_pass, uint32_t& a_outNumLights, RE::BSLight*& a_outLight, RE::NiLight*& a_outNiLight)
	{
		a_outNumLights = 0;
		a_outLight = nullptr;
		a_outNiLight = nullptr;

		if (!a_pass) {
			return true;
		}

#if defined(_MSC_VER)
		__try
#endif
		{
			a_outNumLights = a_pass->numLights;
			if (a_outNumLights == 0 || !a_pass->sceneLights) {
				return false;
			}

			a_outLight = a_pass->sceneLights[0];
			if (!IsPlausibleRenderPointer(a_outLight)) {
				return false;
			}

			a_outNiLight = a_outLight->light.get();
			return IsSafeDirectionalNiLight(a_outNiLight);
		}
#if defined(_MSC_VER)
		__except (1) {
			a_outLight = nullptr;
			a_outNiLight = nullptr;
			return false;
		}
#endif
	}

	float ClampFiniteOrDefault(float a_value, float a_min, float a_max, float a_default)
	{
		if (!std::isfinite(a_value)) {
			return a_default;
		}
		return std::clamp(a_value, a_min, a_max);
	}

	void SanitizeSettings(LightLimitFix::Settings& a_settings)
	{
		a_settings.LightsVisualisationMode = std::min(a_settings.LightsVisualisationMode, kLightsVisualisationModeMax);
		a_settings.ContactShadowQuality = std::min(a_settings.ContactShadowQuality, kContactShadowQualityMax);
		a_settings.ContactShadowClusterBudget = std::clamp(a_settings.ContactShadowClusterBudget, kContactShadowClusterBudgetMin, kContactShadowClusterBudgetMax);
		a_settings.ParticleContactShadowBudget = std::min(a_settings.ParticleContactShadowBudget, kParticleContactShadowBudgetMax);
		a_settings.StrictContactShadowBudget = std::min(a_settings.StrictContactShadowBudget, kStrictContactShadowBudgetMax);
		a_settings.ParticleLightsSaturation =
			ClampFiniteOrDefault(a_settings.ParticleLightsSaturation, kParticleLightsSaturationMin, kParticleLightsSaturationMax, 1.0f);
		a_settings.ParticleBrightness =
			ClampFiniteOrDefault(a_settings.ParticleBrightness, kParticleBrightnessMin, kParticleBrightnessMax, 1.0f);
		a_settings.ParticleRadius =
			ClampFiniteOrDefault(a_settings.ParticleRadius, kParticleRadiusMin, kParticleRadiusMax, 1.0f);
		a_settings.BillboardBrightness =
			ClampFiniteOrDefault(a_settings.BillboardBrightness, kBillboardBrightnessMin, kBillboardBrightnessMax, 1.0f);
		a_settings.BillboardRadius =
			ClampFiniteOrDefault(a_settings.BillboardRadius, kBillboardRadiusMin, kBillboardRadiusMax, 1.0f);
		a_settings.ParticleClusterThreshold =
			ClampFiniteOrDefault(a_settings.ParticleClusterThreshold, kParticleClusterThresholdMin, kParticleClusterThresholdMax, 32.0f);
		a_settings.MaxParticlesPerEmitter = std::clamp(a_settings.MaxParticlesPerEmitter, kMaxParticlesPerEmitterMin, kMaxParticlesPerEmitterMax);
		a_settings.MaxParticleDistance =
			ClampFiniteOrDefault(a_settings.MaxParticleDistance, kMaxParticleDistanceMin, kMaxParticleDistanceMax, 6000.0f);
		a_settings.JsonPlacedLightIntensity =
			ClampFiniteOrDefault(a_settings.JsonPlacedLightIntensity, kJsonPlacedLightIntensityMin, kJsonPlacedLightIntensityMax, 1.0f);
	}

	uint PackContactShadowFlags(const LightLimitFix::Settings& a_settings)
	{
		if (!LocationContext::AllowsInteriorOnly(a_settings.ContactShadowsInteriorsOnly)) {
			return 0;
		}

		const uint clusterBudget = std::clamp(a_settings.ContactShadowClusterBudget, kContactShadowClusterBudgetMin, kContactShadowClusterBudgetMax);
		const uint particleBudget = std::min(a_settings.ParticleContactShadowBudget, kParticleContactShadowBudgetMax);
		const uint strictBudget = std::min(a_settings.StrictContactShadowBudget, kStrictContactShadowBudgetMax);

		uint flags = 0;
		if (a_settings.EnableContactShadows && (clusterBudget > 0 || strictBudget > 0)) {
			flags |= kContactShadowFlagPoint;
		}
		if (a_settings.EnableParticleContactShadows && clusterBudget > 0 && particleBudget > 0) {
			flags |= kContactShadowFlagParticle;
		}
		return flags;
	}

	uint PackContactShadowParams(const LightLimitFix::Settings& a_settings)
	{
		const uint quality = std::min(a_settings.ContactShadowQuality, kContactShadowQualityMax);
		const uint particleBudget = std::min(a_settings.ParticleContactShadowBudget, kParticleContactShadowBudgetMax);
		const uint clusterBudget = std::clamp(a_settings.ContactShadowClusterBudget, kContactShadowClusterBudgetMin, kContactShadowClusterBudgetMax);
		const uint strictBudget = std::min(a_settings.StrictContactShadowBudget, kStrictContactShadowBudgetMax);

		return (quality & 0xFFu) |
		       ((particleBudget & 0xFFu) << 8) |
		       ((clusterBudget & 0xFFu) << 16) |
		       ((strictBudget & 0xFFu) << 24);
	}

	char ToLowerAscii(char a_char)
	{
		return static_cast<char>(std::tolower(static_cast<unsigned char>(a_char)));
	}

	float HashToUnitFloat(std::uint32_t a_value)
	{
		a_value ^= a_value >> 16;
		a_value *= 0x7feb352du;
		a_value ^= a_value >> 15;
		a_value *= 0x846ca68bu;
		a_value ^= a_value >> 16;
		return static_cast<float>(a_value & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
	}

	// Smooth deterministic legacy flicker without the removed external PerlinNoise header.
	float LegacyFlickerNoiseSigned(std::uint32_t a_seed, float a_time)
	{
		constexpr float tau = 6.28318530717958647692f;
		const float phase = HashToUnitFloat(a_seed) * tau;
		const float frequency = 0.85f + (HashToUnitFloat(a_seed ^ 0x9E3779B9u) * 0.5f);
		const float wave1 = std::sin((a_time * frequency) + phase);
		const float wave2 = std::sin((a_time * frequency * 1.93f) + (phase * 1.67f));
		const float wave3 = std::sin((a_time * frequency * 3.17f) + (phase * 2.31f));
		return std::clamp((wave1 * 0.6f) + (wave2 * 0.3f) + (wave3 * 0.1f), -1.0f, 1.0f);
	}

	float LegacyFlickerNoise01(std::uint32_t a_seed, float a_time)
	{
		return std::clamp((LegacyFlickerNoiseSigned(a_seed, a_time) * 0.5f) + 0.5f, 0.0f, 1.0f);
	}

	void ApplyLegacyParticleLightFlicker(LightLimitFix::LightData& a_light, const LightLimitFix::ResolvedBillboardLight& a_billboardLight)
	{
		if (!a_billboardLight.flicker || !globals::state) {
			return;
		}

		const auto seed = a_billboardLight.flickerSeed;
		const float scaledTimer = globals::state->timer * a_billboardLight.flickerSpeed;
		a_light.positionWS.data.x += LegacyFlickerNoiseSigned(seed, scaledTimer) * a_billboardLight.flickerMovement;
		a_light.positionWS.data.y += LegacyFlickerNoiseSigned(seed + 1u, scaledTimer) * a_billboardLight.flickerMovement;
		a_light.positionWS.data.z += LegacyFlickerNoiseSigned(seed + 2u, scaledTimer) * a_billboardLight.flickerMovement;

		// Legacy LLF applied flicker after distance dimming. The current path keeps fade
		// separate from color, so convert back into pre-fade color space first.
		const float fadeCompensation = 1.0f / std::max(a_light.fade, 1e-4f);
		const float flickerIntensity = LegacyFlickerNoise01(seed + 3u, scaledTimer) * a_billboardLight.flickerIntensity * fadeCompensation;
		a_light.color.x = std::max(0.0f, a_light.color.x - flickerIntensity);
		a_light.color.y = std::max(0.0f, a_light.color.y - flickerIntensity);
		a_light.color.z = std::max(0.0f, a_light.color.z - flickerIntensity);
	}

	float ResolveParticleSaturation(float a_globalSaturation, float a_configSaturation)
	{
		const float configSaturation = ClampFiniteOrDefault(
			a_configSaturation,
			kParticleConfigSaturationMin,
			kParticleConfigSaturationMax,
			1.0f);
		return a_globalSaturation * configSaturation;
	}

	bool IsParticleEmitterBeyondDistance(
		const RE::BSGeometry* a_geometry,
		const RE::NiPoint3& a_eyePosition,
		float a_maxDistance)
	{
		if (!a_geometry || a_maxDistance <= 0.0f) {
			return false;
		}

		const auto& bound = a_geometry->worldBound;
		const float conservativeDistance = a_maxDistance + std::max(bound.radius, 0.0f);
		const float dx = bound.center.x - a_eyePosition.x;
		const float dy = bound.center.y - a_eyePosition.y;
		const float dz = bound.center.z - a_eyePosition.z;
		return (dx * dx) + (dy * dy) + (dz * dz) > conservativeDistance * conservativeDistance;
	}

	bool EndsWithDdsInsensitive(std::string_view a_filename)
	{
		if (a_filename.size() < 4) {
			return false;
		}
		const std::string_view ext = a_filename.substr(a_filename.size() - 4);
		return ToLowerAscii(ext[0]) == '.' &&
		       ToLowerAscii(ext[1]) == 'd' &&
		       ToLowerAscii(ext[2]) == 'd' &&
		       ToLowerAscii(ext[3]) == 's';
	}

	void ClearStrictLightData(LightLimitFix::StrictLightDataCB& a_data, bool a_resetRoomIndex) noexcept
	{
		a_data.NumStrictLights = 0;
		a_data.ShadowBitMask = 0;
		if (a_resetRoomIndex) {
			a_data.RoomIndex = -1;
		}
	}

	bool IsNearWhiteTint(const RE::NiColorA& a_color)
	{
		const float avg = (a_color.red + a_color.green + a_color.blue) / 3.0f;
		return std::abs(a_color.red - avg) < 0.02f &&
		       std::abs(a_color.green - avg) < 0.02f &&
		       std::abs(a_color.blue - avg) < 0.02f &&
		       avg > 0.92f;
	}

	struct EmissiveTintCandidate
	{
		bool valid = false;
		float distanceSq = std::numeric_limits<float>::max();
		float luma = -1.0f;
		RE::NiColorA tint{};
	};

	void UpdateEmissiveTintCandidate(
		EmissiveTintCandidate& a_candidate,
		float a_distanceSq,
		float a_luma,
		const RE::NiColorA& a_tint)
	{
		const bool isCloser = a_distanceSq + 1e-3f < a_candidate.distanceSq;
		const bool sameDistance = std::abs(a_distanceSq - a_candidate.distanceSq) <= 1e-3f;
		if (!a_candidate.valid || isCloser || (sameDistance && a_luma > a_candidate.luma)) {
			a_candidate.valid = true;
			a_candidate.distanceSq = a_distanceSq;
			a_candidate.luma = a_luma;
			a_candidate.tint = a_tint;
		}
	}

	RE::NiColorA BuildBillboardFallbackTint(
		const ParticleLights::Config& a_config,
		bool a_hasGradientConfig,
		const ParticleLights::GradientConfig& a_gradientConfig)
	{
		RE::NiColorA fallback{ 1.0f, 1.0f, 1.0f, 1.0f };

		if (a_hasGradientConfig) {
			fallback.red = a_gradientConfig.color.red;
			fallback.green = a_gradientConfig.color.green;
			fallback.blue = a_gradientConfig.color.blue;
		} else {
			fallback.red = a_config.colorMult.red;
			fallback.green = a_config.colorMult.green;
			fallback.blue = a_config.colorMult.blue;
		}
		return fallback;
	}

	RE::BSLightingShaderProperty* GetLightingShaderProperty(RE::NiProperty* a_property)
	{
		if (!a_property || a_property->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get()) {
			return nullptr;
		}
		return static_cast<RE::BSLightingShaderProperty*>(a_property);
	}

	void ConsiderLightingEmissiveTint(
		RE::BSGeometry* a_geometry,
		RE::BSGeometry* a_ignoreGeometry,
		const RE::NiPoint3& a_targetPosition,
		EmissiveTintCandidate& a_bestAnyTint,
		EmissiveTintCandidate& a_bestNonWhiteTint)
	{
		if (!a_geometry || a_geometry == a_ignoreGeometry) {
			return;
		}

		auto* lightingProperty = GetLightingShaderProperty(a_geometry->GetGeometryRuntimeData().shaderProperty.get());

		if (!lightingProperty || !lightingProperty->emissiveColor || lightingProperty->emissiveMult <= 1e-4f) {
			return;
		}

		RE::NiColorA emissiveTint{
			std::max(lightingProperty->emissiveColor->red, 0.0f) * lightingProperty->emissiveMult,
			std::max(lightingProperty->emissiveColor->green, 0.0f) * lightingProperty->emissiveMult,
			std::max(lightingProperty->emissiveColor->blue, 0.0f) * lightingProperty->emissiveMult,
			1.0f
		};

		const float emissiveLuma =
			std::max(emissiveTint.red, 0.0f) +
			std::max(emissiveTint.green, 0.0f) +
			std::max(emissiveTint.blue, 0.0f);
		if (emissiveLuma <= 1e-4f) {
			return;
		}

		const auto& center = a_geometry->worldBound.center;
		const float dx = center.x - a_targetPosition.x;
		const float dy = center.y - a_targetPosition.y;
		const float dz = center.z - a_targetPosition.z;
		const float distanceSq = (dx * dx) + (dy * dy) + (dz * dz);
		UpdateEmissiveTintCandidate(a_bestAnyTint, distanceSq, emissiveLuma, emissiveTint);
		if (!IsNearWhiteTint(emissiveTint)) {
			UpdateEmissiveTintCandidate(a_bestNonWhiteTint, distanceSq, emissiveLuma, emissiveTint);
		}
	}

	void CollectNearbyLightingTint(
		RE::NiNode* a_root,
		RE::BSGeometry* a_ignoreGeometry,
		std::uint32_t a_depthRemaining,
		const RE::NiPoint3& a_targetPosition,
		EmissiveTintCandidate& a_bestAnyTint,
		EmissiveTintCandidate& a_bestNonWhiteTint)
	{
		if (!a_root) {
			return;
		}

		for (const auto& child : a_root->GetChildren()) {
			auto* childObject = child.get();
			if (!childObject) {
				continue;
			}

			if (auto* childGeometry = childObject->AsGeometry()) {
				ConsiderLightingEmissiveTint(childGeometry, a_ignoreGeometry, a_targetPosition, a_bestAnyTint, a_bestNonWhiteTint);
			}

			if (a_depthRemaining > 0) {
				if (auto* childNode = childObject->AsNode()) {
					CollectNearbyLightingTint(childNode, a_ignoreGeometry, a_depthRemaining - 1, a_targetPosition, a_bestAnyTint, a_bestNonWhiteTint);
				}
			}
		}
	}

	bool TryGetBillboardSiblingEmissiveTint(RE::BSGeometry* a_billboardGeometry, RE::NiColorA& a_outTint)
	{
		if (!a_billboardGeometry) {
			return false;
		}

		auto* billboardParentNode = a_billboardGeometry->parent ? a_billboardGeometry->parent->AsNode() : nullptr;
		if (!billboardParentNode) {
			return false;
		}

		RE::NiNode* searchRoot = billboardParentNode;
		if (auto* ownerNode = billboardParentNode->parent ? billboardParentNode->parent->AsNode() : nullptr) {
			searchRoot = ownerNode;
		}

		const RE::NiPoint3 targetPosition = a_billboardGeometry->world.translate;
		EmissiveTintCandidate bestAnyTint{};
		EmissiveTintCandidate bestNonWhiteTint{};
		CollectNearbyLightingTint(searchRoot, a_billboardGeometry, 2u, targetPosition, bestAnyTint, bestNonWhiteTint);
		if (!bestAnyTint.valid) {
			return false;
		}

		// Prefer non-white sibling emissive tint when available; fall back to closest emissive tint otherwise.
		a_outTint = bestNonWhiteTint.valid ? bestNonWhiteTint.tint : bestAnyTint.tint;
		return true;
	}

	void ApplyEffectShaderEmittance(RE::NiColorA& a_color, const RE::BSEffectShaderProperty* a_shaderProperty);

	RE::NiColorA BuildEffectMaterialEmissiveTint(RE::BSEffectShaderMaterial* a_material, RE::BSEffectShaderProperty* a_shaderProperty)
	{
		RE::NiColorA materialEmissiveTint{
			a_material->baseColor.red * a_material->baseColorScale,
			a_material->baseColor.green * a_material->baseColorScale,
			a_material->baseColor.blue * a_material->baseColorScale,
			1.0f
		};
		ApplyEffectShaderEmittance(materialEmissiveTint, a_shaderProperty);
		return materialEmissiveTint;
	}

	void ApplyEffectShaderEmittance(RE::NiColorA& a_color, const RE::BSEffectShaderProperty* a_shaderProperty)
	{
		if (!a_shaderProperty) {
			return;
		}

		// CommonLib v4.18 used the legacy alias `unk88`; v4.19+ exposes `emittanceColor`.
		if (const auto* emittance = a_shaderProperty->emittanceColor) {
			a_color.red *= emittance->red;
			a_color.green *= emittance->green;
			a_color.blue *= emittance->blue;
		}
	}

	float GetEmissiveTintLuma(const RE::NiColorA& a_tint)
	{
		return std::max(a_tint.red, 0.0f) +
		       std::max(a_tint.green, 0.0f) +
		       std::max(a_tint.blue, 0.0f);
	}

	void SetPointLightTypeFlags(LightLimitFix::LightData& a_light, RE::BSLight* a_bsLight)
	{
		PointLightFlags::SetPointLightTypeFlags(a_light.lightFlags, a_bsLight);
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LightLimitFix::Settings,
	EnableParticleLights,
	EnableParticleLightsCulling,
	EnableParticleLightsDetection,
	ParticleLightsSaturation,
	EnableParticleLightsOptimization,
	ParticleBrightness,
	ParticleRadius,
	BillboardBrightness,
	BillboardRadius,
	UseParticleLights087LegacyMode,
	ParticleClusterThreshold,  // NEW
	MaxParticlesPerEmitter,    // NEW
	MaxParticleDistance,       // NEW
	JsonPlacedLightIntensity,
	JsonPlacedLightsInteriorsOnly,
	JsonPlacedLightsPortalStrictOnly,
	EnableContactShadows,
	ContactShadowsInteriorsOnly,
	EnableParticleContactShadows,
	ContactShadowQuality,
	ContactShadowClusterBudget,
	ParticleContactShadowBudget,
	StrictContactShadowBudget,
	EnableLightsVisualisation,
	LightsVisualisationMode)
void LightLimitFix::DrawSettings()
{
	DrawSettingsPanel(true);
}

void LightLimitFix::DrawPerformanceSettings(bool)
{
	DrawSettingsPanel(false);
}

void LightLimitFix::DrawSettingsPanel(bool a_showEmbeddedInfo)
{
	{
		ImGui::Text("ImageSpace Refraction");
		DrawHeatWarpStrengthSetting();

		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::TreeNodeEx("Particle Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Enable Particle Lights", &settings.EnableParticleLights);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Enables Particle Lights.");
			}

			ImGui::Separator();
			ImGui::TextWrapped("Particle Lights Performance");

			ImGui::Checkbox("Enable Culling", &settings.EnableParticleLightsCulling);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Significantly improves performance by not rendering empty textures. Only disable if you are encountering issues.");
			}

			ImGui::Checkbox("Enable Detection", &settings.EnableParticleLightsDetection);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Adds particle lights to the player light level, so that NPCs can detect them for stealth and gameplay.");
			}

			ImGui::Checkbox("Enable Optimization", &settings.EnableParticleLightsOptimization);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Merges vertices which are close enough to each other to improve performance.");
			}

			// NEW: clustering controls
			ImGui::SliderFloat("Cluster Threshold", &settings.ParticleClusterThreshold, kParticleClusterThresholdMin, kParticleClusterThresholdMax, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Distance+radius similarity threshold for merging particles into one light.\n"
					"Higher = more merging, better performance, blurrier lights.\n"
					"Lower = less merging, more precise, more expensive.");
			}

			ImGui::SliderInt("Max Particles per Emitter", &settings.MaxParticlesPerEmitter, kMaxParticlesPerEmitterMin, kMaxParticlesPerEmitterMax);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Maximum number of particles sampled per emitter per frame.\n"
					"Higher = closer to the real particle system but more CPU work.\n"
					"Lower = faster, especially for very dense effects.");
			}

			// NEW: distance cutoff for particle lights
			ImGui::SliderFloat("Max Particle Distance", &settings.MaxParticleDistance, 1000.0f, kMaxParticleDistanceMax, "%.0f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Particle lights beyond this distance from the camera are skipped entirely.\n"
					"Lower = better performance, but distant effects won't contribute light.\n"
					"Higher = more distant particle lighting, but more cost.");
			}

			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::TextWrapped("Particle Lights Customisation");
			ImGui::SliderFloat("Saturation", &settings.ParticleLightsSaturation, kParticleLightsSaturationMin, kParticleLightsSaturationMax, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Particle light saturation.");
			}
			ImGui::SliderFloat("Particle Brightness", &settings.ParticleBrightness, kParticleBrightnessMin, kParticleBrightnessMax, "%.2f");
			ImGui::SliderFloat("Particle Radius", &settings.ParticleRadius, kParticleRadiusMin, kParticleRadiusMax, "%.2f");
			ImGui::SliderFloat("Billboard Brightness", &settings.BillboardBrightness, kBillboardBrightnessMin, kBillboardBrightnessMax, "%.2f");
			ImGui::SliderFloat("Billboard Radius", &settings.BillboardRadius, kBillboardRadiusMin, kBillboardRadiusMax, "%.2f");
			ImGui::Checkbox("v0.8.7 Particle Lights Legacy", &settings.UseParticleLights087LegacyMode);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Restores the v0.8.7 particle-light alpha model.\n"
					"When enabled, brightness comes from material / shader / vertex alpha and RadiusMult affects radius only.\n"
					"When disabled, the current path uses RadiusMult for both intensity and radius.\n"
					"This is most noticeable on billboard-backed particle lights.");
			}

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Placed Lights (JSON)", ImGuiTreeNodeFlags_DefaultOpen)) {
			const bool jsonPlacedLightsSupported = globals::features::inverseSquareLighting.loaded;
			ImGui::BeginDisabled(!jsonPlacedLightsSupported);
			ImGui::SliderFloat("Intensity Scale", &settings.JsonPlacedLightIntensity, kJsonPlacedLightIntensityMin, kJsonPlacedLightIntensityMax, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Scales intensity for attached runtime lights generated from light records.\n"
					"This primarily targets Light Placer-style JSON lights.\n"
					"Requires Inverse Square Lighting runtime metadata to identify those lights.");
			}

			ImGui::Checkbox("Interiors Only", &settings.JsonPlacedLightsInteriorsOnly);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Only apply the intensity scale while in interiors.");
			}

			ImGui::Checkbox("Portal Strict Only", &settings.JsonPlacedLightsPortalStrictOnly);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Only apply the intensity scale to portal-strict lights.");
			}
			ImGui::EndDisabled();

			if (a_showEmbeddedInfo && !jsonPlacedLightsSupported) {
				ImGui::TextDisabled("Requires Inverse Square Lighting to identify JSON-placed runtime lights.");
			}

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Contact Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Enable Point Light Contact Shadows", &settings.EnableContactShadows);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Adds short screen-space contact shadows to LLF point lights.\n"
					"Uses a cached per-cluster candidate list to limit the number of ray marches.");
			}

			ImGui::Checkbox("Interiors Only", &settings.ContactShadowsInteriorsOnly);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Only run LLF contact shadows in interior cells.");
			}

			const char* qualityOptions[] = { "Low", "Medium", "High" };
			int contactShadowQuality = static_cast<int>(settings.ContactShadowQuality);
			if (ImGui::Combo("Quality", &contactShadowQuality, qualityOptions, kContactShadowQualityOptionCount)) {
				settings.ContactShadowQuality = static_cast<uint>(std::clamp(contactShadowQuality, 0, static_cast<int>(kContactShadowQualityMax)));
			}

			int contactShadowClusterBudget = static_cast<int>(settings.ContactShadowClusterBudget);
			if (ImGui::SliderInt("Cached Lights per Cluster", &contactShadowClusterBudget, static_cast<int>(kContactShadowClusterBudgetMin), static_cast<int>(kContactShadowClusterBudgetMax))) {
				settings.ContactShadowClusterBudget = static_cast<uint>(contactShadowClusterBudget);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Maximum cached point and particle lights per cluster that can cast contact shadows. Set to 0 to disable clustered contact shadows.");
			}

			int strictContactShadowBudget = static_cast<int>(settings.StrictContactShadowBudget);
			if (ImGui::SliderInt("Strict Light Budget", &strictContactShadowBudget, 0, static_cast<int>(kStrictContactShadowBudgetMax))) {
				settings.StrictContactShadowBudget = static_cast<uint>(strictContactShadowBudget);
			}

			ImGui::Checkbox("Enable Particle Contact Shadows", &settings.EnableParticleContactShadows);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Adds cheaper contact shadows for particle lights.\n"
					"Keep the particle budget low for fire, smoke, and magic-heavy scenes.");
			}

			int particleContactShadowBudget = static_cast<int>(settings.ParticleContactShadowBudget);
			if (ImGui::SliderInt("Particle Budget per Cluster", &particleContactShadowBudget, 0, static_cast<int>(kParticleContactShadowBudgetMax))) {
				settings.ParticleContactShadowBudget = static_cast<uint>(particleContactShadowBudget);
			}

			ImGui::Spacing();
			ImGui::TreePop();
		}
	}
	auto shaderCache = globals::shaderCache;

	if (a_showEmbeddedInfo) {
		if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text(std::format("Clustered Light Count : {}", lightCount).c_str());
			ImGui::Text(std::format(
				"Particle Lights Count : {}",
				particleLightDiagnostics.currentEmitters.load(std::memory_order_relaxed) +
					particleLightDiagnostics.currentBillboards.load(std::memory_order_relaxed))
					.c_str());
			ImGui::Text(std::format(
				"Particle Emitters / Billboards : {} / {}",
				particleLightDiagnostics.currentEmitters.load(std::memory_order_relaxed),
				particleLightDiagnostics.currentBillboards.load(std::memory_order_relaxed))
					.c_str());
			ImGui::Text(std::format(
				"Particle Cache Entries : {}",
				particleLightDiagnostics.cacheEntries.load(std::memory_order_relaxed))
					.c_str());

			ImGui::TreePop();
		}

		///////////////////////////////
		ImGui::SeparatorText("Debug");

		if (ImGui::TreeNode("Light Limit Visualization")) {
			ImGui::Checkbox("Enable Lights Visualisation", &settings.EnableLightsVisualisation);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Enables visualization of the light limit\n");
			}

			{
				static const char* comboOptions[] = { "Light Limit", "Strict Lights Count", "Clustered Lights Count", "Shadow Mask" };
				ImGui::Combo("Lights Visualisation Mode", (int*)&settings.LightsVisualisationMode, comboOptions, 4);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						" - Visualise the light limit. Red when the \"strict\" light limit is reached (portal-strict lights).\n"
						" - Visualise the number of strict lights.\n"
						" - Visualise the number of clustered lights.\n"
						" - Visualize the Shadow Mask.\n");
				}
			}
			currentEnableLightsVisualisation = settings.EnableLightsVisualisation;
			if (previousEnableLightsVisualisation != currentEnableLightsVisualisation) {
				globals::state->SetDefines(settings.EnableLightsVisualisation ? "LLFDEBUG" : "");
				shaderCache->Clear(RE::BSShader::Type::Lighting);
				previousEnableLightsVisualisation = currentEnableLightsVisualisation;
			}
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TreePop();
		}
	}
}

void LightLimitFix::DrawOverlay()
{
	if (!settings.EnableLightsVisualisation)
		return;
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale();
	ImGui::SetNextWindowPos(ImVec2(pos, pos), ImGuiCond_Always);
	ImGui::Begin("##LLFDebug", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	Util::Text::Error("DEBUG FEATURE - LIGHT LIMIT VISUALISATION ENABLED");

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Clustered Light Count : {}", lightCount).c_str());
		ImGui::Text(std::format(
			"Particle Lights Count : {}",
			particleLightDiagnostics.currentEmitters.load(std::memory_order_relaxed) +
				particleLightDiagnostics.currentBillboards.load(std::memory_order_relaxed))
				.c_str());
		ImGui::TreePop();
	}

	ImGui::End();
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
	perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
	perFrame.ContactShadowFlags = PackContactShadowFlags(settings);
	perFrame.ContactShadowParams = PackContactShadowParams(settings);
	std::copy(clusterSize, clusterSize + 3, perFrame.ClusterSize);
	return perFrame;
}

void LightLimitFix::SetupResources()
{
	const uint32_t screenWidth = globals::game::graphicsState ? globals::game::graphicsState->screenWidth : 1u;
	const uint32_t screenHeight = globals::game::graphicsState ? globals::game::graphicsState->screenHeight : 1u;
	clusterSize[0] = ((screenWidth + 63) / 64);
	clusterSize[1] = ((screenHeight + 63) / 64);
	clusterSize[2] = 32;
	uint clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];
	static ID3D11Device* shaderDevice = nullptr;
	if (shaderDevice != globals::d3d::device) {
		delete lightBuildingCB;
		lightBuildingCB = nullptr;
		delete lightCullingCB;
		lightCullingCB = nullptr;
		delete strictLightDataCB;
		strictLightDataCB = nullptr;
		if (clusterBuildingCS) {
			clusterBuildingCS->Release();
			clusterBuildingCS = nullptr;
		}
		if (clusterCullingCS) {
			clusterCullingCS->Release();
			clusterCullingCS = nullptr;
		}
		shaderDevice = globals::d3d::device;
	}

	{
		if (!clusterBuildingCS)
			clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl", {}, "cs_5_0");
		if (!clusterCullingCS)
			clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl", {}, "cs_5_0");

		if (!lightBuildingCB)
			lightBuildingCB = new ConstantBuffer(ConstantBufferDesc<LightBuildingCB>());
		if (!lightCullingCB)
			lightCullingCB = new ConstantBuffer(ConstantBufferDesc<LightCullingCB>());
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = clusterCount;

		sbDesc.StructureByteStride = sizeof(ClusterAABB);
		sbDesc.ByteWidth = sizeof(ClusterAABB) * numElements;
		clusters = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::Clusters");
		srvDesc.Buffer.NumElements = numElements;
		clusters->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		clusters->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexCounter = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightIndexCounter");
		srvDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateUAV(uavDesc);

		numElements = clusterCount * CLUSTER_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexList = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightIndexList");
		srvDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateUAV(uavDesc);

		numElements = clusterCount;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		lightGrid = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightGrid");
		srvDesc.Buffer.NumElements = numElements;
		lightGrid->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightGrid->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		contactShadowIndexCounter = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::ContactShadowIndexCounter");
		srvDesc.Buffer.NumElements = numElements;
		contactShadowIndexCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		contactShadowIndexCounter->CreateUAV(uavDesc);

		numElements = clusterCount * CONTACT_SHADOW_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		contactShadowIndexList = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::ContactShadowIndexList");
		srvDesc.Buffer.NumElements = numElements;
		contactShadowIndexList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		contactShadowIndexList->CreateUAV(uavDesc);

		numElements = clusterCount;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		contactShadowGrid = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::ContactShadowGrid");
		srvDesc.Buffer.NumElements = numElements;
		contactShadowGrid->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		contactShadowGrid->CreateUAV(uavDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(LightData);
		sbDesc.ByteWidth = sizeof(LightData) * MAX_LIGHTS;
		lights = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::Lights");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_LIGHTS;
		lights->CreateSRV(srvDesc);
	}

	{
		if (!strictLightDataCB)
			strictLightDataCB = new ConstantBuffer(ConstantBufferDesc<StrictLightDataCB>());
	}
}

void LightLimitFix::Reset()
{
	{
		std::lock_guard<std::mutex> currentLock{ currentParticleLightsMutex };

		// NiPointer releases the retained emitter geometry and particle data.
		currentParticleEmitters.clear();
		currentBillboardLights.clear();

		{
			std::lock_guard<std::mutex> queueLock{ particleLightsQueueMutex };
			std::swap(currentParticleEmitters, queuedParticleEmitters);
			std::swap(currentBillboardLights, queuedBillboardLights);
			queuedEmitterIndices.clear();
			queuedBillboardIndices.clear();
			nextParticleLightSequence = 0;

			particleLightDiagnostics.currentEmitters.store(currentParticleEmitters.size(), std::memory_order_relaxed);
			particleLightDiagnostics.currentBillboards.store(currentBillboardLights.size(), std::memory_order_relaxed);
		}
	}

	const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
	PruneParticleLightCache(frame);
	jsonPlacedLightCache.clear();
}

void LightLimitFix::PruneParticleLightCache(std::uint32_t a_frame)
{
	if (a_frame - lastParticleLightCacheSweepFrame < kParticleLightCacheSweepInterval) {
		return;
	}
	lastParticleLightCacheSweepFrame = a_frame;

	const auto configVersion = globals::features::llf::particleLights.configVersion;
	std::lock_guard<std::mutex> cacheLock{ particleLightsCacheMutex };
	for (auto it = particleLightsReferences.begin(); it != particleLightsReferences.end();) {
		const bool configChanged = it->second.reference.configVersion != configVersion;
		const bool entryExpired = a_frame - it->second.lastSeenFrame > kParticleLightCacheMaxIdleFrames;
		if (configChanged || entryExpired) {
			it = particleLightsReferences.erase(it);
		} else {
			++it;
		}
	}

	particleLightDiagnostics.cacheEntries.store(particleLightsReferences.size(), std::memory_order_relaxed);
}

void LightLimitFix::DrawEssentialSettings()
{
	ImGui::Checkbox("Enable Particle Lights", &settings.EnableParticleLights);
	ImGui::Checkbox("Enable Point Light Contact Shadows", &settings.EnableContactShadows);
	DrawHeatWarpStrengthSetting();
}

void LightLimitFix::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void LightLimitFix::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
	SanitizeSettings(settings);
}

RE::NiNode* GetParentRoomNode(RE::NiAVObject* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	static const auto* roomRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSMultiBoundRoom }.get();
	static const auto* portalRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSPortalSharedNode }.get();

	const auto* rtti = object->GetRTTI();
	if (rtti == roomRtti || rtti == portalRtti) {
		return static_cast<RE::NiNode*>(object);
	}

	return GetParentRoomNode(object->parent);
}

void LightLimitFix::BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	ClearStrictLightData(strictLightDataTemp, true);

	if (!a_pass || !a_pass->geometry) {
		return;
	}
	if (!roomNodes.empty()) {
		if (RE::NiNode* roomNode = GetParentRoomNode(a_pass->geometry)) {
			if (auto it = roomNodes.find(roomNode); it != roomNodes.cend()) {
				strictLightDataTemp.RoomIndex = it->second;
			}
		}
	}
}

void LightLimitFix::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->sceneLights) {
		ClearStrictLightData(strictLightDataTemp, false);
		return;
	}

	auto smState = globals::game::smState;
	if (!smState) {
		ClearStrictLightData(strictLightDataTemp, false);
		return;
	}

	auto& isl = globals::features::inverseSquareLighting;

	auto accumulator = *globals::game::currentAccumulator.get();
	if (!accumulator) {
		ClearStrictLightData(strictLightDataTemp, false);
		return;
	}

	bool inWorld = accumulator->GetRuntimeData().activeShadowSceneNode == smState->shadowSceneNode[0];
	const bool isInterior = LocationContext::Get().inInterior;

	constexpr uint32_t kStrictLightCapacity = 15;
	const uint32_t availableSceneLights = a_pass->numLights > 0 ? (a_pass->numLights - 1) : 0;
	const uint32_t requestedStrictLights = inWorld ? 0u : availableSceneLights;
	const uint32_t strictLightCount = std::min(requestedStrictLights, kStrictLightCapacity);
	const uint32_t shadowLightCount = std::min(static_cast<uint32_t>(a_pass->numShadowLights), availableSceneLights);
	RefreshJsonPlacedLightCacheFrame();

	ClearStrictLightData(strictLightDataTemp, false);

	uint32_t outIndex = 0;
#if defined(_MSC_VER)
	__try
#endif
	{
		for (uint32_t i = 0; i < strictLightCount; i++) {
			auto bsLight = a_pass->sceneLights[i + 1];
			if (!bsLight) {
				continue;
			}
			auto niLight = bsLight->light.get();
			if (!niLight) {
				continue;
			}

			auto& runtimeData = niLight->GetLightRuntimeData();

			LightData light{};
			light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
			light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

			if (isl.loaded) {
				isl.ProcessLight(light, bsLight, niLight);
			} else {
				light.radius = runtimeData.radius.x;
				// light.color *= runtimeData.fade;
				light.fade = runtimeData.fade;
			}

			SetPointLightTypeFlags(light, bsLight);
			light.fade *= bsLight->lodDimmer;
			const bool isPortalStrict = !IsGlobalLight(bsLight);
			ApplyJsonPlacedLightIntensityScale(light, bsLight, niLight, isPortalStrict, isInterior);

			SetLightPosition(light, niLight->world.translate, inWorld);

			if (i < shadowLightCount && bsLight->IsShadowLight()) {
				auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
				const auto maskIndex = shadowLight->GetRuntimeData().maskIndex;
				if (maskIndex < 32) {
					light.shadowMaskIndex = maskIndex;
					light.lightFlags.set(LightFlags::Shadow);
				}
			}

			strictLightDataTemp.StrictLights[outIndex++] = light;
		}
		strictLightDataTemp.NumStrictLights = outIndex;

		for (uint32_t i = 0; i < shadowLightCount; i++) {
			auto bsLight = a_pass->sceneLights[i + 1];
			if (!bsLight || !bsLight->IsShadowLight()) {
				continue;
			}
			auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
			const auto maskIndex = shadowLight->GetRuntimeData().maskIndex;
			if (maskIndex < 32) {
				strictLightDataTemp.ShadowBitMask |= (1u << maskIndex);
			}
		}
	}
#if defined(_MSC_VER)
	__except (1) {
		ClearStrictLightData(strictLightDataTemp, false);
	}
#endif
}

void LightLimitFix::BSLightingShader_SetupGeometry_After(RE::BSRenderPass*)
{
	auto shaderCache = globals::shaderCache;
	auto context = globals::d3d::context;
	auto smState = globals::game::smState;

	if (!shaderCache->IsEnabled())
		return;

	if (!smState || !strictLightDataCB) {
		return;
	}

	auto accumulator = *globals::game::currentAccumulator.get();
	if (!accumulator) {
		return;
	}

	auto shadowSceneNode = smState->shadowSceneNode[0];

	const auto isEmpty = strictLightDataTemp.NumStrictLights == 0;
	const bool isWorld = accumulator->GetRuntimeData().activeShadowSceneNode == shadowSceneNode;
	const auto roomIndex = strictLightDataTemp.RoomIndex;
	const auto shadowBitMask = strictLightDataTemp.ShadowBitMask;

	if (!isEmpty || (isEmpty && !wasEmpty) || isWorld != wasWorld || previousRoomIndex != roomIndex || shadowBitMask != previousShadowBitMask) {
		strictLightDataCB->Update(strictLightDataTemp);
		wasEmpty = isEmpty;
		wasWorld = isWorld;
		previousRoomIndex = roomIndex;
		previousShadowBitMask = shadowBitMask;
	}

	if (frameChecker.IsNewFrame()) {
		ID3D11Buffer* buffer = { strictLightDataCB->CB() };
		context->PSSetConstantBuffers(3, 1, &buffer);
	}
}

void LightLimitFix::SetLightPosition(LightLimitFix::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached)
{
	RE::NiPoint3 eyePosition;

	if (a_cached) {
		eyePosition = eyePositionCached;
	} else {
		eyePosition = Util::GetEyePosition();
	}

	auto worldPos = a_initialPosition - eyePosition;
	a_light.positionWS.data.x = worldPos.x;
	a_light.positionWS.data.y = worldPos.y;
	a_light.positionWS.data.z = worldPos.z;
}

void LightLimitFix::RefreshJsonPlacedLightCacheFrame()
{
	if (jsonPlacedLightCacheFrameChecker.IsNewFrame()) {
		jsonPlacedLightCache.clear();
	}
}

bool LightLimitFix::IsJsonPlacedLight(RE::BSLight* a_bsLight, RE::NiLight* a_niLight)
{
	if (!a_bsLight || !a_niLight || !a_bsLight->pointLight) {
		return false;
	}
	if (!globals::features::inverseSquareLighting.loaded) {
		return false;
	}
	if (const auto it = jsonPlacedLightCache.find(a_niLight); it != jsonPlacedLightCache.end()) {
		return it->second;
	}

	bool isJsonPlacedLight = false;
	const auto ownerRef = a_niLight->GetUserData();
	if (ownerRef) {
		if (const auto ownerBase = ownerRef->GetObjectReference(); ownerBase && ownerBase->GetFormType() != RE::FormType::Light) {
			const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(a_niLight);
			if (runtimeData && runtimeData->lighFormId != 0) {
				const auto lighForm = RE::TESForm::LookupByID(runtimeData->lighFormId);
				isJsonPlacedLight = lighForm && lighForm->GetFormType() == RE::FormType::Light;
			}
		}
	}

	jsonPlacedLightCache.insert_or_assign(a_niLight, isJsonPlacedLight);
	return isJsonPlacedLight;
}

void LightLimitFix::ApplyJsonPlacedLightIntensityScale(
	LightData& a_light,
	RE::BSLight* a_bsLight,
	RE::NiLight* a_niLight,
	bool a_isPortalStrict,
	bool a_isInterior)
{
	if (std::abs(settings.JsonPlacedLightIntensity - 1.0f) <= 1e-4f) {
		return;
	}
	if (!LocationContext::AllowsInteriorOnly(settings.JsonPlacedLightsInteriorsOnly, a_isInterior)) {
		return;
	}
	if (settings.JsonPlacedLightsPortalStrictOnly && !a_isPortalStrict) {
		return;
	}
	if (!IsJsonPlacedLight(a_bsLight, a_niLight)) {
		return;
	}

	a_light.fade *= settings.JsonPlacedLightIntensity;
}

float LightLimitFix::CalculateLuminance(CachedParticleLight& light, RE::NiPoint3& point)
{
	// See BSLight::CalculateLuminance_14131D3D0
	// Performs lighting on the CPU which is identical to GPU code

	auto lightDirection = light.position - point;
	float lightDist = lightDirection.Length();
	float intensityFactor = std::clamp(lightDist / light.radius, 0.0f, 1.0f);
	float intensityMultiplier = 1 - intensityFactor * intensityFactor;

	return light.grey * intensityMultiplier;
}

void LightLimitFix::AddParticleLightLuminance(RE::NiPoint3& targetPosition, int& numHits, float& lightLevel)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	std::shared_lock<std::shared_mutex> lk{ cachedParticleLightsMutex };
	int particleLightsDetectionHits = 0;
	if (settings.EnableParticleLightsDetection) {
		for (auto& light : cachedParticleLights) {
			auto luminance = CalculateLuminance(light, targetPosition);
			lightLevel += luminance;
			if (luminance > 0.0)
				particleLightsDetectionHits++;
		}
	}
	numHits += particleLightsDetectionHits;
}

void LightLimitFix::Prepass()
{
	auto context = globals::d3d::context;
	auto state = globals::state;
	if (!context || !state || !lights || !lightIndexList || !lightGrid ||
		!contactShadowIndexList || !contactShadowGrid ||
		!lights->srv || !lightIndexList->srv || !lightGrid->srv ||
		!contactShadowIndexList->srv || !contactShadowGrid->srv) {
		return;
	}

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "LightLimitFix Prepass");
	state->BeginPerfEvent("LightLimitFix Prepass");
	UpdateLights();

	ID3D11ShaderResourceView* views[5]{};
	views[0] = lights->srv.get();
	views[1] = lightIndexList->srv.get();
	views[2] = lightGrid->srv.get();
	views[3] = contactShadowIndexList->srv.get();
	views[4] = contactShadowGrid->srv.get();
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);

	state->EndPerfEvent();
}

bool LightLimitFix::IsValidLight(RE::BSLight* a_light)
{
	return a_light && a_light->light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool LightLimitFix::IsGlobalLight(RE::BSLight* a_light)
{
	return a_light && !(a_light->portalStrict || !a_light->portalGraph);
}

struct VertexColor
{
	std::uint8_t data[4];
};

struct VertexPosition
{
	std::uint8_t data[3];
};

bool TryGetMaxAlphaVertexColor(const std::uint8_t* a_rawVertexData, std::uint32_t a_vertexSize, std::uint32_t a_colorOffset, std::uint32_t a_vertexCount, VertexColor& a_outVertexColor)
{
	if (!a_rawVertexData || a_vertexSize < sizeof(VertexColor) || a_vertexCount == 0) {
		return false;
	}
	if (a_colorOffset > (a_vertexSize - sizeof(VertexColor))) {
		return false;
	}

	std::uint8_t maxAlpha = 0;
	bool found = false;
	VertexColor bestColor{};

#if defined(_MSC_VER)
	__try
#endif
	{
		for (std::uint32_t v = 0; v < a_vertexCount; ++v) {
			const std::size_t byteOffset = static_cast<std::size_t>(a_vertexSize) * static_cast<std::size_t>(v) + static_cast<std::size_t>(a_colorOffset);
			const auto* vertex = reinterpret_cast<const VertexColor*>(a_rawVertexData + byteOffset);
			const std::uint8_t alpha = vertex->data[3];
			if (alpha > maxAlpha) {
				maxAlpha = alpha;
				bestColor = *vertex;
				found = true;
			}
		}
	}
#if defined(_MSC_VER)
	__except (1) {
		return false;
	}
#endif

	if (found) {
		a_outVertexColor = bestColor;
	}
	return found;
}

std::string ExtractTextureStem(std::string_view a_path)
{
	if (a_path.empty())
		return {};

	auto lastSeparatorPos = a_path.find_last_of("\\/");
	std::string_view filename = (lastSeparatorPos == std::string::npos) ? a_path : a_path.substr(lastSeparatorPos + 1);
	if (filename.empty() || !EndsWithDdsInsensitive(filename)) {
		return {};
	}

	filename.remove_suffix(4);  // Remove ".dds"
	if (filename.empty()) {
		return {};
	}

	std::string textureName{};
	textureName.reserve(filename.size());
	for (char c : filename) {
		textureName.push_back(ToLowerAscii(c));
	}

	return textureName;
}

void ResolveBillboardTint(
	LightLimitFix::ParticleLightReference& a_reference,
	RE::BSGeometry* a_geometry,
	RE::BSEffectShaderMaterial* a_material,
	RE::BSEffectShaderProperty* a_shaderProperty)
{
	a_reference.applyEffectMaterialTint = true;
	a_reference.baseColor = { 1, 1, 1, 1 };

	bool hasVertexTint = false;
	if (auto rendererData = a_geometry->GetGeometryRuntimeData().rendererData) {
		if (auto triShape = a_geometry->AsTriShape()) {
			const std::uint32_t vertexSize = rendererData->vertexDesc.GetSize();
			if (rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_COLORS) && rendererData->rawVertexData && vertexSize > 0u) {
				const std::uint32_t offset = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_COLOR);
				const std::uint32_t vertexCount = static_cast<std::uint32_t>(triShape->GetTrishapeRuntimeData().vertexCount);

				VertexColor maxAlphaVertexColor{};
				if (TryGetMaxAlphaVertexColor(rendererData->rawVertexData, vertexSize, offset, vertexCount, maxAlphaVertexColor)) {
					a_reference.baseColor.red *= maxAlphaVertexColor.data[0] / 255.f;
					a_reference.baseColor.green *= maxAlphaVertexColor.data[1] / 255.f;
					a_reference.baseColor.blue *= maxAlphaVertexColor.data[2] / 255.f;
					hasVertexTint = true;
					if (a_shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha)) {
						a_reference.baseColor.alpha *= maxAlphaVertexColor.data[3] / 255.f;
					}
				}
			}
		}
	}

	RE::NiColorA siblingEmissiveTint{};
	bool hasSiblingEmissiveTint = false;
	const bool vertexTintLooksWhite = hasVertexTint && IsNearWhiteTint(a_reference.baseColor);
	if (!hasVertexTint || vertexTintLooksWhite) {
		hasSiblingEmissiveTint = TryGetBillboardSiblingEmissiveTint(a_geometry, siblingEmissiveTint);
		const bool siblingTintIsNonWhite = hasSiblingEmissiveTint && !IsNearWhiteTint(siblingEmissiveTint);

		const RE::NiColorA materialEmissiveTint = BuildEffectMaterialEmissiveTint(a_material, a_shaderProperty);
		const float materialEmissiveLuma = GetEmissiveTintLuma(materialEmissiveTint);
		const bool hasMaterialEmissiveTint = materialEmissiveLuma > 1e-4f;
		const bool materialTintIsNonWhite = hasMaterialEmissiveTint && !IsNearWhiteTint(materialEmissiveTint);

		// Resolve fallback from a single source to avoid mixing color from one source
		// with emissive energy from another. Prefer the billboard's own effect material
		// when it already provides a non-white tint, then fall back to sibling tint.
		if (materialTintIsNonWhite) {
			a_reference.baseColor = materialEmissiveTint;
			a_reference.applyEffectMaterialTint = false;
		} else if (siblingTintIsNonWhite) {
			a_reference.baseColor = siblingEmissiveTint;
			a_reference.applyEffectMaterialTint = false;
		} else if (hasMaterialEmissiveTint) {
			a_reference.baseColor = materialEmissiveTint;
			a_reference.applyEffectMaterialTint = false;
		} else if (hasSiblingEmissiveTint) {
			a_reference.baseColor = siblingEmissiveTint;
			a_reference.applyEffectMaterialTint = false;
		} else {
			a_reference.baseColor = BuildBillboardFallbackTint(
				a_reference.config,
				a_reference.hasGradientConfig,
				a_reference.gradientConfig);
			a_reference.applyEffectMaterialTint = true;
		}
	}
}

LightLimitFix::ParticleLightReference LightLimitFix::GetParticleLightConfigs(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return {};
	}

	if (!settings.EnableParticleLights) {
		return {};
	}

	auto& particleLights = globals::features::llf::particleLights;
	auto shaderProperty = a_pass->shaderProperty->GetRTTI() == globals::rtti::BSEffectShaderPropertyRTTI.get() ?
	                          static_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty) :
	                          nullptr;
	if (!shaderProperty || shaderProperty->lightData) {
		return {};
	}

	auto material = shaderProperty->GetMaterial();
	if (!material) {
		return {};
	}

	const bool billboard = a_pass->geometry->GetRTTI() != globals::rtti::NiParticleSystemRTTI.get();
	if (billboard) {
		auto parent = a_pass->geometry->parent;
		if (!parent || parent->GetRTTI() != globals::rtti::NiBillboardNodeRTTI.get()) {
			return {};
		}
	}

	auto* node = a_pass->geometry;
	const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
	const ParticleLightKey key{ node, shaderProperty };

	ParticleLightCacheSignature signature{};
	signature.material = material;
	signature.billboard = billboard;
	if (billboard) {
		signature.rendererData = node->GetGeometryRuntimeData().rendererData;
		if (signature.rendererData) {
			signature.rawVertexData = signature.rendererData->rawVertexData;
		}
	}

	std::optional<ParticleLightReference> cachedReference;
	{
		std::lock_guard<std::mutex> cacheLock{ particleLightsCacheMutex };
		auto it = particleLightsReferences.find(key);
		if (it != particleLightsReferences.end() &&
			it->second.reference.configVersion == particleLights.configVersion &&
			it->second.signature == signature &&
			it->second.sourceTexturePath == material->sourceTexturePath &&
			it->second.gradientTexturePath == material->greyscaleTexturePath) {
			it->second.lastSeenFrame = frame;
			if (!it->second.reference.valid || !billboard || it->second.tintResolvedFrame == frame) {
				return it->second.reference;
			}
			cachedReference = it->second.reference;
		} else if (it != particleLightsReferences.end()) {
			particleLightsReferences.erase(it);
		}
	}

	auto cacheReference = [&](ParticleLightReference a_reference) {
		ParticleLightCacheEntry entry{};
		entry.reference = a_reference;
		entry.signature = signature;
		// Retain the interned texture names so their identity cannot be recycled
		// while a stale geometry key remains in the bounded cache.
		entry.sourceTexturePath = material->sourceTexturePath;
		entry.gradientTexturePath = material->greyscaleTexturePath;
		entry.lastSeenFrame = frame;
		entry.tintResolvedFrame = billboard ? frame : 0;

		std::lock_guard<std::mutex> cacheLock{ particleLightsCacheMutex };
		if (auto it = particleLightsReferences.find(key); it != particleLightsReferences.end()) {
			it->second = std::move(entry);
		} else if (particleLightsReferences.size() < kParticleLightCacheMaxEntries) {
			particleLightsReferences.emplace(key, std::move(entry));
			particleLightDiagnostics.cacheEntries.store(particleLightsReferences.size(), std::memory_order_relaxed);
		}
		return a_reference;
	};

	if (cachedReference) {
		auto reference = *cachedReference;
		ResolveBillboardTint(reference, node, material, shaderProperty);
		return cacheReference(reference);
	}

	auto cacheInvalidReference = [&]() {
		ParticleLightReference invalidReference{};
		invalidReference.configVersion = particleLights.configVersion;
		return cacheReference(invalidReference);
	};

	// See https://www.nexusmods.com/skyrimspecialedition/articles/1391.
	if (material->sourceTexturePath.empty()) {
		return cacheInvalidReference();
	}

	auto textureName = ExtractTextureStem(material->sourceTexturePath.c_str());
	if (textureName.empty()) {
		return cacheInvalidReference();
	}

	auto configIt = particleLights.particleLightConfigs.find(textureName);
	if (configIt == particleLights.particleLightConfigs.end()) {
		return cacheInvalidReference();
	}

	ParticleLightReference reference{};
	reference.valid = true;
	reference.billboard = billboard;
	reference.config = configIt->second;
	reference.configVersion = particleLights.configVersion;

	if (!material->greyscaleTexturePath.empty()) {
		const auto gradientName = ExtractTextureStem(material->greyscaleTexturePath.c_str());
		if (gradientName.empty()) {
			return cacheInvalidReference();
		}

		auto gradientIt = particleLights.particleLightGradientConfigs.find(gradientName);
		if (gradientIt == particleLights.particleLightGradientConfigs.end()) {
			return cacheInvalidReference();
		}
		reference.hasGradientConfig = true;
		reference.gradientConfig = gradientIt->second;
	}

	if (billboard) {
		ResolveBillboardTint(reference, node, material, shaderProperty);
	}
	return cacheReference(reference);
}

bool LightLimitFix::CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return true;
	}

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return true;

	auto reference = GetParticleLightConfigs(a_pass);
	if (reference.valid) {
		if (AddParticleLight(a_pass, reference)) {
			return !(settings.EnableParticleLightsCulling && reference.config.cull);
		}
	}
	return true;
}

bool LightLimitFix::AddParticleLight(RE::BSRenderPass* a_pass, const ParticleLightReference& a_reference)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return false;
	}

	auto shaderProperty = a_pass->shaderProperty->GetRTTI() == globals::rtti::BSEffectShaderPropertyRTTI.get() ?
	                          static_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty) :
	                          nullptr;
	if (!shaderProperty) {
		return false;
	}

	auto material = shaderProperty->GetMaterial();
	if (!material) {
		return false;
	}
	const auto& config = a_reference.config;
	const ParticleLightKey key{ a_pass->geometry, shaderProperty };

	RE::NiColorA color = a_reference.baseColor;
	if (a_reference.applyEffectMaterialTint) {
		color.red *= material->baseColor.red * material->baseColorScale;
		color.green *= material->baseColor.green * material->baseColorScale;
		color.blue *= material->baseColor.blue * material->baseColorScale;

		ApplyEffectShaderEmittance(color, shaderProperty);
	}

	if (a_reference.hasGradientConfig) {
		auto grey = float3(config.colorMult.red, config.colorMult.green, config.colorMult.blue).Dot(float3(0.3f, 0.59f, 0.11f));
		color.red *= grey * a_reference.gradientConfig.color.red;
		color.green *= grey * a_reference.gradientConfig.color.green;
		color.blue *= grey * a_reference.gradientConfig.color.blue;
	} else {
		color.red *= config.colorMult.red;
		color.green *= config.colorMult.green;
		color.blue *= config.colorMult.blue;
	}

	const float sourceAlpha = std::max(color.alpha * material->baseColor.alpha * shaderProperty->alpha, 0.0f);
	if (settings.UseParticleLights087LegacyMode) {
		color.alpha = sourceAlpha;
	} else {
		// Keep particle light energy stable and config-driven (1.4.6-style behavior).
		color.alpha = std::max(config.radiusMult, 0.0f);
	}

	if (a_reference.billboard) {
		ResolvedBillboardLight resolved{};
		resolved.position = a_pass->geometry->world.translate;
		resolved.radius = a_pass->geometry->worldBound.radius * config.radiusMult * settings.BillboardRadius * 0.5f;
		resolved.color = Saturation(
			float3{ color.red, color.green, color.blue },
			ResolveParticleSaturation(settings.ParticleLightsSaturation, config.saturationMult));
		resolved.color *= color.alpha * settings.BillboardBrightness;
		resolved.flickerSeed = static_cast<std::uint32_t>(std::hash<void*>{}(a_pass->geometry));
		resolved.flicker = config.flicker;
		resolved.flickerSpeed = config.flickerSpeed;
		resolved.flickerIntensity = config.flickerIntensity;
		resolved.flickerMovement = config.flickerMovement;

		std::lock_guard<std::mutex> queueLock{ particleLightsQueueMutex };
		if (auto it = queuedBillboardIndices.find(key); it != queuedBillboardIndices.end()) {
			if (it->second < queuedBillboardLights.size()) {
				resolved.sequence = queuedBillboardLights[it->second].sequence;
				queuedBillboardLights[it->second] = resolved;
				return true;
			}
			queuedBillboardIndices.erase(it);
		}

		if (queuedParticleEmitters.size() + queuedBillboardLights.size() >= kMaxQueuedParticleLights) {
			return false;
		}

		resolved.sequence = nextParticleLightSequence++;
		const std::size_t billboardIndex = queuedBillboardLights.size();
		queuedBillboardLights.push_back(resolved);
		queuedBillboardIndices[key] = billboardIndex;
		return true;
	}

	auto* particleSystem = static_cast<RE::NiParticleSystem*>(a_pass->geometry);
	auto* particleData = particleSystem->GetParticlesRuntimeData().particleData.get();
	auto updateEmitter = [&](ParticleEmitterLight& a_emitter) {
		a_emitter.particleData.reset(particleData);
		a_emitter.color = color;
		a_emitter.radiusMult = config.radiusMult;
		a_emitter.saturationMult = config.saturationMult;
	};

	std::lock_guard<std::mutex> queueLock{ particleLightsQueueMutex };
	if (auto it = queuedEmitterIndices.find(key); it != queuedEmitterIndices.end()) {
		if (it->second < queuedParticleEmitters.size()) {
			auto& emitter = queuedParticleEmitters[it->second];
			updateEmitter(emitter);
			return true;
		}
		queuedEmitterIndices.erase(it);
	}

	if (queuedParticleEmitters.size() + queuedBillboardLights.size() >= kMaxQueuedParticleLights) {
		return false;
	}

	const std::size_t emitterIndex = queuedParticleEmitters.size();
	auto& emitter = queuedParticleEmitters.emplace_back();
	emitter.node.reset(particleSystem);
	updateEmitter(emitter);
	emitter.sequence = nextParticleLightSequence++;
	queuedEmitterIndices[key] = emitterIndex;
	return true;
}

void LightLimitFix::PostPostLoad()
{
	globals::features::llf::particleLights.GetConfigs();
	particleLightsReferences.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 4u);
	queuedParticleEmitters.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	currentParticleEmitters.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	queuedBillboardLights.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	currentBillboardLights.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	queuedEmitterIndices.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	queuedBillboardIndices.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	Hooks::Install();
}

void LightLimitFix::DataLoaded()
{
	if (auto gameSettings = globals::game::gameSettingCollection) {
		if (auto iMagicLightMaxCount = gameSettings->GetSetting("iMagicLightMaxCount")) {
			iMagicLightMaxCount->data.i = MAXINT32;
			logger::info("[LLF] Unlocked magic light limit");
		}
	}
}

void LightLimitFix::ClearShaderCache()
{
	if (clusterBuildingCS) {
		clusterBuildingCS->Release();
		clusterBuildingCS = nullptr;
	}
	if (clusterCullingCS) {
		clusterCullingCS->Release();
		clusterCullingCS = nullptr;
	}
	clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl", {}, "cs_5_0");
	clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl", {}, "cs_5_0");
}

float LightLimitFix::CalculateLightDistance(float3 a_lightPosition, float a_radius)
{
	return (a_lightPosition.x * a_lightPosition.x) + (a_lightPosition.y * a_lightPosition.y) + (a_lightPosition.z * a_lightPosition.z) - (a_radius * a_radius);
}

void LightLimitFix::AddCachedParticleLights(
	eastl::vector<LightData>& lightsData,
	LightLimitFix::LightData& light,
	const ResolvedBillboardLight* a_billboardLight)
{
	if (lightsData.size() >= MAX_LIGHTS) {
		return;
	}

	static float& lightFadeStart = *reinterpret_cast<float*>(REL::RelocationID(527668, 414582).address());
	static float& lightFadeEnd = *reinterpret_cast<float*>(REL::RelocationID(527669, 414583).address());
	const float3 luminanceWeights = float3(0.3f, 0.59f, 0.11f);

	// NEW: hard distance cutoff for particle lights
	if (settings.MaxParticleDistance > 0.0f) {
		float maxDist = settings.MaxParticleDistance;
		float maxDistSq = maxDist * maxDist;

		const auto& pos = light.positionWS.data;  // camera-relative
		float distSq = (pos.x * pos.x) + (pos.y * pos.y) + (pos.z * pos.z);

		if (distSq > maxDistSq) {
			// Too far away: don't add this particle light at all
			return;
		}
	}

	float distance = CalculateLightDistance(light.positionWS.data, light.radius);

	float dimmer = 0.0f;

	if (distance < lightFadeStart || lightFadeEnd == 0.0f || lightFadeEnd <= lightFadeStart) {
		dimmer = 1.0f;
	} else if (distance <= lightFadeEnd) {
		dimmer = 1.0f - ((distance - lightFadeStart) / (lightFadeEnd - lightFadeStart));
	} else {
		dimmer = 0.0f;
	}

	light.fade *= dimmer;
	const float luminanceScale = light.fade;
	if ((light.color.x + light.color.y + light.color.z) * luminanceScale > 1e-4 && light.radius > 1e-4) {
		if (a_billboardLight) {
			ApplyLegacyParticleLightFlicker(light, *a_billboardLight);
		}

		light.invRadius = 1.f / light.radius;
		lightsData.push_back(light);

		if (cachedParticleLights.size() < MAX_LIGHTS) {
			CachedParticleLight cachedParticleLight{};
			cachedParticleLight.grey = float3(light.color.x, light.color.y, light.color.z).Dot(luminanceWeights) * luminanceScale;
			cachedParticleLight.radius = light.radius;
			cachedParticleLight.position = { light.positionWS.data.x + eyePositionCached.x, light.positionWS.data.y + eyePositionCached.y, light.positionWS.data.z + eyePositionCached.z };

			cachedParticleLights.push_back(cachedParticleLight);
		}
	}
}

float3 LightLimitFix::Saturation(float3 color, float saturation)
{
	float grey = color.Dot(float3(0.3f, 0.59f, 0.11f));
	color.x = std::max(std::lerp(grey, color.x, saturation), 0.0f);
	color.y = std::max(std::lerp(grey, color.y, saturation), 0.0f);
	color.z = std::max(std::lerp(grey, color.z, saturation), 0.0f);
	return color;
}

void LightLimitFix::UpdateLights()
{
	auto clearCachedParticleLights = [&]() {
		std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
		cachedParticleLights.clear();
	};

	auto context = globals::d3d::context;
	if (!context || !lights || !lights->resource) {
		clearCachedParticleLights();
		return;
	}

	auto smState = globals::game::smState;
	auto& isl = globals::features::inverseSquareLighting;
	auto clearAndUpdate = [&]() {
		lightCount = 0;
		clearCachedParticleLights();
		UpdateStructure();
	};

	if (!smState) {
		clearAndUpdate();
		return;
	}

	auto shadowSceneNode = smState->shadowSceneNode[0];
	if (!shadowSceneNode) {
		clearAndUpdate();
		return;
	}

	// Cache data since cameraData can become invalid in first-person.
	auto eyePosition = globals::game::frameBufferCached.GetCameraPosAdjust();
	eyePositionCached = { eyePosition.x, eyePosition.y, eyePosition.z };

	eastl::vector<LightData> lightsData{};
	lightsData.reserve(MAX_LIGHTS);
	const bool isInterior = LocationContext::Get().inInterior;
	RefreshJsonPlacedLightCacheFrame();

	// Process point lights

	roomNodes.clear();

	auto addRoom = [&](RE::NiNode* node, LightData& light) {
		if (!node) {
			return;
		}

		constexpr std::size_t kMaxRoomFlags = 128;
		uint8_t roomIndex = 0;
		if (auto it = roomNodes.find(node); it == roomNodes.cend()) {
			if (roomNodes.size() >= kMaxRoomFlags) {
				return;
			}
			roomIndex = static_cast<uint8_t>(roomNodes.size());
			roomNodes.insert_or_assign(node, roomIndex);
		} else {
			roomIndex = it->second;
		}
		light.roomFlags.SetBit(roomIndex, 1);
	};

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) {
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightData light{};
					light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
					light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

					if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;
						// light.color *= runtimeData.fade;
						light.fade = runtimeData.fade;
					}

					SetPointLightTypeFlags(light, bsLight);
					light.fade *= bsLight->lodDimmer;
					const bool isPortalStrict = !IsGlobalLight(bsLight);

					if (isPortalStrict) {
						// List of BSMultiBoundRooms affected by a light
						for (const auto& roomPtr : bsLight->rooms) {
							if (roomPtr) {
								addRoom(static_cast<RE::NiNode*>(roomPtr), light);
							}
						}
						// List of BSPortals affected by a light
						for (const auto& portalPtr : bsLight->portals) {
							if (portalPtr && portalPtr->portalSharedNode) {
								addRoom(static_cast<RE::NiNode*>(portalPtr->portalSharedNode.get()), light);
							}
						}
						light.lightFlags.set(LightFlags::PortalStrict);
					}
					ApplyJsonPlacedLightIntensityScale(light, bsLight, niLight, isPortalStrict, isInterior);

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						const auto maskIndex = shadowLight->GetRuntimeData().maskIndex;
						light.shadowMaskIndex = maskIndex;
						light.lightFlags.set(LightFlags::Shadow);
					}

					// Check for inactive shadow light
					if (light.shadowMaskIndex != 255) {
						SetLightPosition(light, niLight->world.translate);

						if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4 &&
							lightsData.size() < MAX_LIGHTS) {
							lightsData.push_back(light);
						}
					}
				}
			}
		}
	};

	for (auto& e : shadowSceneNode->GetRuntimeData().activeLights) {
		addLight(e);
	}
	for (auto& e : shadowSceneNode->GetRuntimeData().activeShadowLights) {
		addLight(e);
	}

	{
		std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
		cachedParticleLights.clear();

		LightData clusteredLight{};
		uint32_t clusteredLights = 0;

		auto flushClusteredLight = [&]() {
			if (!clusteredLights) {
				return;
			}

			const float clusterCount = static_cast<float>(clusteredLights);
			clusteredLight.radius /= clusterCount;
			clusteredLight.positionWS.data /= clusterCount;

			clusteredLight.lightFlags.set(LightFlags::Simple);
			clusteredLight.lightFlags.set(LightFlags::Particle);
			AddCachedParticleLights(lightsData, clusteredLight);

			clusteredLights = 0;
			clusteredLight = {};
		};

		auto processParticleEmitter = [&](const ParticleEmitterLight& a_particleEmitter) {
			if (!a_particleEmitter.node ||
				IsParticleEmitterBeyondDistance(a_particleEmitter.node.get(), eyePositionCached, settings.MaxParticleDistance)) {
				return;
			}

			auto* particleSystem = a_particleEmitter.node.get();
			auto* particleData = a_particleEmitter.particleData.get();
			if (!particleSystem || !particleData) {
				return;
			}

			auto& particleSystemRuntimeData = particleSystem->GetParticleSystemRuntimeData();
			auto& particleRuntimeData = particleData->GetParticlesRuntimeData();
			if (!particleRuntimeData.radii || !particleRuntimeData.sizes || !particleRuntimeData.positions) {
				return;
			}

			std::uint32_t numVertices = static_cast<std::uint32_t>(particleData->GetActiveVertexCount());
			const std::uint32_t runtimeMaxVertices = static_cast<std::uint32_t>(particleRuntimeData.maxNumVertices);
			const std::uint32_t runtimeNumVertices = static_cast<std::uint32_t>(particleRuntimeData.numVertices);
			if (runtimeMaxVertices == 0) {
				return;
			}
			numVertices = std::min(numVertices, runtimeMaxVertices);
			if (runtimeNumVertices > 0) {
				numVertices = std::min(numVertices, runtimeNumVertices);
			}

			const std::uint32_t maxPerEmitter = static_cast<std::uint32_t>(std::max(1, settings.MaxParticlesPerEmitter));
			numVertices = std::min(numVertices, maxPerEmitter);
			const float saturation = ResolveParticleSaturation(settings.ParticleLightsSaturation, a_particleEmitter.saturationMult);

			for (std::uint32_t p = 0; p < numVertices; p++) {
				if (lightsData.size() >= MAX_LIGHTS) {
					break;
				}

				const float radius = particleRuntimeData.radii[p] * particleRuntimeData.sizes[p];

				auto initialPosition = particleRuntimeData.positions[p];
				if (!particleSystemRuntimeData.isWorldspace) {
					// Detect first-person meshes.
					if ((particleSystem->GetModelData().modelBound.radius * particleSystem->world.scale) != particleSystem->worldBound.radius) {
						const auto& center = particleSystem->worldBound.center;
						initialPosition = { initialPosition.x + center.x, initialPosition.y + center.y, initialPosition.z + center.z };
					} else {
						const auto& translate = particleSystem->world.translate;
						initialPosition = { initialPosition.x + translate.x, initialPosition.y + translate.y, initialPosition.z + translate.z };
					}
				}

				RE::NiPoint3 positionWS{
					initialPosition.x - eyePositionCached.x,
					initialPosition.y - eyePositionCached.y,
					initialPosition.z - eyePositionCached.z
				};

				if (clusteredLights) {
					const auto averageRadius = clusteredLight.radius / static_cast<float>(clusteredLights);
					const float radiusDiff = std::abs(averageRadius - radius);

					const auto averagePosition = clusteredLight.positionWS.data / static_cast<float>(clusteredLights);
					const float positionDiff = positionWS.GetDistance({ averagePosition.x, averagePosition.y, averagePosition.z });
					if ((radiusDiff + positionDiff) > settings.ParticleClusterThreshold ||
						!settings.EnableParticleLightsOptimization) {
						flushClusteredLight();
					}
					if (lightsData.size() >= MAX_LIGHTS) {
						break;
					}
				}

				float alpha = a_particleEmitter.color.alpha;
				float3 color{
					a_particleEmitter.color.red,
					a_particleEmitter.color.green,
					a_particleEmitter.color.blue
				};
				if (particleRuntimeData.color) {
					alpha *= particleRuntimeData.color[p].alpha;
					color.x *= particleRuntimeData.color[p].red;
					color.y *= particleRuntimeData.color[p].green;
					color.z *= particleRuntimeData.color[p].blue;
				}
				clusteredLight.color += Saturation(color, saturation) * alpha * settings.ParticleBrightness;
				clusteredLight.radius += radius * a_particleEmitter.radiusMult * settings.ParticleRadius;
				clusteredLight.positionWS.data.x += positionWS.x;
				clusteredLight.positionWS.data.y += positionWS.y;
				clusteredLight.positionWS.data.z += positionWS.z;
				clusteredLights++;
			}
		};

		auto processBillboard = [&](const ResolvedBillboardLight& a_billboardLight) {
			// Preserve submission order and budget priority: an emitter cluster
			// sequenced before this billboard must be emitted first.
			flushClusteredLight();
			if (lightsData.size() >= MAX_LIGHTS) {
				return;
			}

			LightData light{};
			light.color = a_billboardLight.color;
			light.radius = a_billboardLight.radius;
			SetLightPosition(light, a_billboardLight.position);
			light.lightFlags.set(LightFlags::Simple);
			light.lightFlags.set(LightFlags::Particle);
			AddCachedParticleLights(lightsData, light, &a_billboardLight);
		};

		std::lock_guard<std::mutex> currentLock{ currentParticleLightsMutex };
		std::size_t emitterIndex = 0;
		std::size_t billboardIndex = 0;
		while (emitterIndex < currentParticleEmitters.size() || billboardIndex < currentBillboardLights.size()) {
			if (lightsData.size() >= MAX_LIGHTS) {
				break;
			}

			const bool processEmitter =
				billboardIndex >= currentBillboardLights.size() ||
				(emitterIndex < currentParticleEmitters.size() &&
					currentParticleEmitters[emitterIndex].sequence <= currentBillboardLights[billboardIndex].sequence);
			if (processEmitter) {
				processParticleEmitter(currentParticleEmitters[emitterIndex++]);
			} else {
				processBillboard(currentBillboardLights[billboardIndex++]);
			}
		}

		flushClusteredLight();
	}

	lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);

	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(lights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	size_t bytes = sizeof(LightData) * lightCount;
	if (bytes > 0) {
		memcpy_s(mapped.pData, bytes, lightsData.data(), bytes);
	}
	context->Unmap(lights->resource.get(), 0);

	UpdateStructure();
}

void LightLimitFix::UpdateStructure()
{
	auto context = globals::d3d::context;
	if (!context || !lightBuildingCB || !lightCullingCB || !clusters || !lights ||
		!lightIndexCounter || !lightIndexList || !lightGrid ||
		!contactShadowIndexCounter || !contactShadowIndexList || !contactShadowGrid ||
		!clusterBuildingCS || !clusterCullingCS) {
		return;
	}

	if (globals::game::cameraNear) {
		lightsNear = *globals::game::cameraNear;
	}
	if (globals::game::cameraFar) {
		lightsFar = *globals::game::cameraFar;
	}

	const auto screenWidth = std::max(1u, globals::game::graphicsState ? globals::game::graphicsState->screenWidth : 1u);
	const auto screenHeight = std::max(1u, globals::game::graphicsState ? globals::game::graphicsState->screenHeight : 1u);
	const uint32_t renderWidth = screenWidth;
	const uint32_t renderHeight = screenHeight;
	const float2 renderSize = Util::ConvertToDynamic(float2{ static_cast<float>(renderWidth), static_cast<float>(renderHeight) });
	clusterSize[0] = ((uint)renderSize.x + 63) / 64;
	clusterSize[1] = ((uint)renderSize.y + 63) / 64;
	clusterSize[2] = 32;

	{
		LightBuildingCB updateData{};
		updateData.LightsNear = lightsNear;
		updateData.LightsFar = lightsFar;
		std::copy(clusterSize, clusterSize + 3, updateData.ClusterSize);

		lightBuildingCB->Update(updateData);

		ID3D11Buffer* buffer = lightBuildingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11UnorderedAccessView* clusters_uav = clusters->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &clusters_uav, nullptr);

		context->CSSetShader(clusterBuildingCS, nullptr, 0);
		globals::profiler->BeginPass("LightLimitFix::ClusterBuild");
		context->Dispatch((clusterSize[0] + 15) / 16, (clusterSize[1] + 15) / 16, (clusterSize[2] + 3) / 4);
		globals::profiler->EndPass();

		ID3D11UnorderedAccessView* null_uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
	}

	{
		LightCullingCB updateData{};
		updateData.LightCount = lightCount;
		updateData.ContactShadowFlags = PackContactShadowFlags(settings);
		updateData.ContactShadowParams = PackContactShadowParams(settings);
		std::copy(clusterSize, clusterSize + 3, updateData.ClusterSize);

		lightCullingCB->Update(updateData);

		UINT counterReset[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(lightIndexCounter->uav.get(), counterReset);
		context->ClearUnorderedAccessViewUint(contactShadowIndexCounter->uav.get(), counterReset);

		ID3D11Buffer* buffer = lightCullingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11ShaderResourceView* srvs[] = { clusters->srv.get(), lights->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = {
			lightIndexCounter->uav.get(),
			lightIndexList->uav.get(),
			lightGrid->uav.get(),
			contactShadowIndexCounter->uav.get(),
			contactShadowIndexList->uav.get(),
			contactShadowGrid->uav.get()
		};
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(clusterCullingCS, nullptr, 0);
		globals::profiler->BeginPass("LightLimitFix::ClusterCull");
		context->Dispatch((clusterSize[0] + 15) / 16, (clusterSize[1] + 15) / 16, (clusterSize[2] + 3) / 4);
		globals::profiler->EndPass();
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11ShaderResourceView* null_srvs[2] = { nullptr };
	context->CSSetShaderResources(0, 2, null_srvs);

	ID3D11UnorderedAccessView* null_uavs[6] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 6, null_uavs, nullptr);
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	uint32_t numLights = 0;
	RE::BSLight* directionalLight = nullptr;
	RE::NiLight* directionalNiLight = nullptr;
	// BSLightingShader dereferences sceneLights[0]->light without validation.
	// Invalid UI 3D scene directional slots are skipped to avoid a CTD.
	const bool directionalSlotSafe = IsDirectionalSceneLightSafe(Pass, numLights, directionalLight, directionalNiLight);
	if (!directionalSlotSafe) {
		static bool everLogged = false;
		static std::uintptr_t lastLoggedNiLight = 0;
		static int distinctLogged = 0;
		const auto niLightValue = reinterpret_cast<std::uintptr_t>(directionalNiLight);
		if ((!everLogged || niLightValue != lastLoggedNiLight) && distinctLogged++ < 20) {
			everLogged = true;
			lastLoggedNiLight = niLightValue;
			logger::warn(
				"[LLF] BSLightingShader_SetupGeometry: directional sceneLights[0] unsafe "
				"(numLights={} BSLight=0x{:x} NiLight=0x{:x}); skipping engine SetupGeometry",
				numLights,
				reinterpret_cast<std::uintptr_t>(directionalLight),
				niLightValue);
		}
	}

	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	if (directionalSlotSafe) {
		func(This, Pass, RenderFlags);
	}
	singleton.BSLightingShader_SetupGeometry_After(Pass);
}

void LightLimitFix::Hooks::BSEffectShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	ExternalEmittance::UpdatePermutation(Pass);
	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
};

void LightLimitFix::Hooks::BSWaterShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	LightEditor::ObserveWaterPass(Pass);
	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
};

float LightLimitFix::Hooks::AIProcess_CalculateLightValue_GetLuminance::thunk(
	RE::ShadowSceneNode* shadowSceneNode,
	RE::NiPoint3& targetPosition,
	int& numHits,
	float& sunLightLevel,
	float& lightLevel,
	RE::NiLight& refLight,
	int32_t shadowBitMask)
{
	auto ret = func(shadowSceneNode, targetPosition, numHits, sunLightLevel, lightLevel, refLight, shadowBitMask);
	globals::features::lightLimitFix.AddParticleLightLuminance(targetPosition, numHits, ret);
	return ret;
}
