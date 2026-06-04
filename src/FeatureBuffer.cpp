#include "FeatureBuffer.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <tuple>
#include <type_traits>

#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/IBL.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/LinearLighting.h"
#include "Features/Skylighting.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainShadows.h"
#include "Features/TerrainVariation.h"
#include "Features/WetnessEffects.h"
#include "Features/Wetterness.h"
#include "TruePBR.h"

namespace
{
	using GrassLightingSettingsCB = GrassLighting::Settings;
	using ExtendedMaterialsSettingsCB = ExtendedMaterials::Settings;
	using DynamicCubemapsSettingsCB = DynamicCubemaps::Settings;
	using TerrainShadowsSettingsCB = TerrainShadows::PerFrame;
	using LightLimitFixSettingsCB = LightLimitFix::PerFrame;
	using WetnessEffectsSettingsCB = WetnessEffects::PerFrame;
	using WetternessSettingsCB = Wetterness::PerFrame;
	using SkylightingSettingsCB = Skylighting::SkylightingCB;
	using CloudShadowsSettingsCB = CloudShadows::Settings;
	using LODBlendingSettingsCB = LODBlending::Settings;
	using HairSpecularSettingsCB = HairSpecular::Settings;
	using TerrainVariationSettingsCB = TerrainVariation::Settings;
	using IBLSettingsCB = IBL::CommonBufferData;
	using ExtendedTranslucencySettingsCB = ExtendedTranslucency::PerFrame;
	using LinearLightingSettingsCB = LinearLighting::PerFrameData;
	using TerrainBlendingSettingsCB = TerrainBlending::Settings;
	using ExponentialHeightFogSettingsCB = ExponentialHeightFog::Settings;
	using TruePBRSettingsCB = TruePBR::Settings;

	// Keep these in lock-step with package/Shaders/Common/SharedData.hlsli::FeatureData.
	struct FeatureDataLayout
	{
		GrassLightingSettingsCB grassLightingSettings;
		ExtendedMaterialsSettingsCB extendedMaterialSettings;
		DynamicCubemapsSettingsCB cubemapCreatorSettings;
		TerrainShadowsSettingsCB terraOccSettings;
		LightLimitFixSettingsCB lightLimitFixSettings;
		WetnessEffectsSettingsCB wetnessEffectsSettings;
		WetternessSettingsCB wetternessSettings;
		SkylightingSettingsCB skylightingSettings;
		CloudShadowsSettingsCB cloudShadowsSettings;
		LODBlendingSettingsCB lodBlendingSettings;
		HairSpecularSettingsCB hairSpecularSettings;
		TerrainVariationSettingsCB terrainVariationSettings;
		IBLSettingsCB iblSettings;
		ExtendedTranslucencySettingsCB extendedTranslucencySettings;
		LinearLightingSettingsCB linearLightingSettings;
		TerrainBlendingSettingsCB terrainBlendingSettings;
		ExponentialHeightFogSettingsCB exponentialHeightFogSettings;
		TruePBRSettingsCB truePBRSettings;
	};

	using FeatureDataTuple = std::tuple<
		GrassLightingSettingsCB,
		ExtendedMaterialsSettingsCB,
		DynamicCubemapsSettingsCB,
		TerrainShadowsSettingsCB,
		LightLimitFixSettingsCB,
		WetnessEffectsSettingsCB,
		WetternessSettingsCB,
		SkylightingSettingsCB,
		CloudShadowsSettingsCB,
		LODBlendingSettingsCB,
		HairSpecularSettingsCB,
		TerrainVariationSettingsCB,
		IBLSettingsCB,
		ExtendedTranslucencySettingsCB,
		LinearLightingSettingsCB,
		TerrainBlendingSettingsCB,
		ExponentialHeightFogSettingsCB,
		TruePBRSettingsCB>;

	static_assert(sizeof(GrassLightingSettingsCB) == 32);
	static_assert(sizeof(ExtendedMaterialsSettingsCB) == 32);
	static_assert(sizeof(DynamicCubemapsSettingsCB) == 32);
	static_assert(sizeof(TerrainShadowsSettingsCB) == 32);
	static_assert(sizeof(LightLimitFixSettingsCB) == 32);
	static_assert(sizeof(WetnessEffectsSettingsCB) == 192);
	static_assert(sizeof(WetternessSettingsCB) == 256);
	static_assert(sizeof(SkylightingSettingsCB) == 144);
	static_assert(sizeof(CloudShadowsSettingsCB) == 16);
	static_assert(sizeof(LODBlendingSettingsCB) == 32);
	static_assert(sizeof(HairSpecularSettingsCB) == 80);
	static_assert(sizeof(TerrainVariationSettingsCB) == 16);
	static_assert(sizeof(IBLSettingsCB) == 48);
	static_assert(sizeof(ExtendedTranslucencySettingsCB) == 16);
	static_assert(sizeof(LinearLightingSettingsCB) == 112);
	static_assert(sizeof(TerrainBlendingSettingsCB) == 16);
	static_assert(sizeof(ExponentialHeightFogSettingsCB) == 96);
	static_assert(sizeof(TruePBRSettingsCB) == 16);

	constexpr std::size_t kGrassLightingOffset = 0;
	constexpr std::size_t kExtendedMaterialsOffset = kGrassLightingOffset + sizeof(GrassLightingSettingsCB);
	constexpr std::size_t kDynamicCubemapsOffset = kExtendedMaterialsOffset + sizeof(ExtendedMaterialsSettingsCB);
	constexpr std::size_t kTerrainShadowsOffset = kDynamicCubemapsOffset + sizeof(DynamicCubemapsSettingsCB);
	constexpr std::size_t kLightLimitFixOffset = kTerrainShadowsOffset + sizeof(TerrainShadowsSettingsCB);
	constexpr std::size_t kWetnessEffectsOffset = kLightLimitFixOffset + sizeof(LightLimitFixSettingsCB);
	constexpr std::size_t kWetternessOffset = kWetnessEffectsOffset + sizeof(WetnessEffectsSettingsCB);
	constexpr std::size_t kSkylightingOffset = kWetternessOffset + sizeof(WetternessSettingsCB);
	constexpr std::size_t kCloudShadowsOffset = kSkylightingOffset + sizeof(SkylightingSettingsCB);
	constexpr std::size_t kLODBlendingOffset = kCloudShadowsOffset + sizeof(CloudShadowsSettingsCB);
	constexpr std::size_t kHairSpecularOffset = kLODBlendingOffset + sizeof(LODBlendingSettingsCB);
	constexpr std::size_t kTerrainVariationOffset = kHairSpecularOffset + sizeof(HairSpecularSettingsCB);
	constexpr std::size_t kIBLOffset = kTerrainVariationOffset + sizeof(TerrainVariationSettingsCB);
	constexpr std::size_t kExtendedTranslucencyOffset = kIBLOffset + sizeof(IBLSettingsCB);
	constexpr std::size_t kLinearLightingOffset = kExtendedTranslucencyOffset + sizeof(ExtendedTranslucencySettingsCB);
	constexpr std::size_t kTerrainBlendingOffset = kLinearLightingOffset + sizeof(LinearLightingSettingsCB);
	constexpr std::size_t kExponentialHeightFogOffset = kTerrainBlendingOffset + sizeof(TerrainBlendingSettingsCB);
	constexpr std::size_t kTruePBROffset = kExponentialHeightFogOffset + sizeof(ExponentialHeightFogSettingsCB);
	constexpr std::size_t kFeatureDataSize = kTruePBROffset + sizeof(TruePBRSettingsCB);

	static_assert(std::is_standard_layout_v<FeatureDataLayout>);
	static_assert(std::is_trivially_copyable_v<FeatureDataLayout>);
	static_assert(sizeof(FeatureDataLayout) % 16 == 0);
	static_assert(offsetof(FeatureDataLayout, grassLightingSettings) == kGrassLightingOffset);
	static_assert(offsetof(FeatureDataLayout, extendedMaterialSettings) == kExtendedMaterialsOffset);
	static_assert(offsetof(FeatureDataLayout, cubemapCreatorSettings) == kDynamicCubemapsOffset);
	static_assert(offsetof(FeatureDataLayout, terraOccSettings) == kTerrainShadowsOffset);
	static_assert(offsetof(FeatureDataLayout, lightLimitFixSettings) == kLightLimitFixOffset);
	static_assert(offsetof(FeatureDataLayout, wetnessEffectsSettings) == kWetnessEffectsOffset);
	static_assert(offsetof(FeatureDataLayout, wetternessSettings) == kWetternessOffset);
	static_assert(offsetof(FeatureDataLayout, skylightingSettings) == kSkylightingOffset);
	static_assert(offsetof(FeatureDataLayout, cloudShadowsSettings) == kCloudShadowsOffset);
	static_assert(offsetof(FeatureDataLayout, lodBlendingSettings) == kLODBlendingOffset);
	static_assert(offsetof(FeatureDataLayout, hairSpecularSettings) == kHairSpecularOffset);
	static_assert(offsetof(FeatureDataLayout, terrainVariationSettings) == kTerrainVariationOffset);
	static_assert(offsetof(FeatureDataLayout, iblSettings) == kIBLOffset);
	static_assert(offsetof(FeatureDataLayout, extendedTranslucencySettings) == kExtendedTranslucencyOffset);
	static_assert(offsetof(FeatureDataLayout, linearLightingSettings) == kLinearLightingOffset);
	static_assert(offsetof(FeatureDataLayout, terrainBlendingSettings) == kTerrainBlendingOffset);
	static_assert(offsetof(FeatureDataLayout, exponentialHeightFogSettings) == kExponentialHeightFogOffset);
	static_assert(offsetof(FeatureDataLayout, truePBRSettings) == kTruePBROffset);
	static_assert(sizeof(FeatureDataLayout) == kFeatureDataSize);

	template <class T>
	void PackField(unsigned char* a_dst, std::size_t& a_offset, const T& a_value)
	{
		static_assert(std::is_standard_layout_v<T>);
		static_assert(std::is_trivially_copyable_v<T>);
		std::memcpy(a_dst + a_offset, std::addressof(a_value), sizeof(T));
		a_offset += sizeof(T);
	}

	template <class... Ts>
	std::pair<const unsigned char*, std::size_t> BuildFeatureBufferData(const Ts&... a_fields)
	{
		using PackedTuple = std::tuple<std::remove_cv_t<std::remove_reference_t<Ts>>...>;
		static_assert(std::is_same_v<PackedTuple, FeatureDataTuple>, "FeatureData packing order/type mismatch");

		constexpr std::size_t totalSize = (sizeof(Ts) + ...);
		static_assert(totalSize % 16 == 0);
		static_assert(totalSize == sizeof(FeatureDataLayout));

		alignas(16) static thread_local std::array<unsigned char, totalSize> data;
		// Start from a deterministic payload; this avoids stale bytes in any untouched padding.
		std::memset(data.data(), 0, data.size());

		std::size_t offset = 0;
		(PackField(data.data(), offset, a_fields), ...);

		return std::make_pair(data.data(), data.size());
	}
}

std::pair<const unsigned char*, std::size_t> GetFeatureBufferData(bool a_inWorld)
{
	auto grassLightingSettings = globals::features::grassLighting.settings;
	const auto wetternessSettings = globals::features::wetterness.GetCommonBufferData();
	grassLightingSettings.Glossiness = globals::features::wetterness.GetEffectiveGrassGlossiness(grassLightingSettings.Glossiness, wetternessSettings);
	grassLightingSettings.SpecularStrength = globals::features::wetterness.GetEffectiveGrassSpecularStrength(grassLightingSettings.SpecularStrength, wetternessSettings);

	return BuildFeatureBufferData(
		grassLightingSettings,
		globals::features::extendedMaterials.settings,
		globals::features::dynamicCubemaps.settings,
		globals::features::terrainShadows.GetCommonBufferData(),
		globals::features::lightLimitFix.GetCommonBufferData(),
		globals::features::wetnessEffects.GetCommonBufferData(),
		wetternessSettings,
		globals::features::skylighting.GetCommonBufferData(a_inWorld),
		globals::features::cloudShadows.settings,
		globals::features::lodBlending.settings,
		globals::features::hairSpecular.settings,
		globals::features::terrainVariation.settings,
		globals::features::ibl.GetCommonBufferData(),
		globals::features::extendedTranslucency.GetCommonBufferData(),
		globals::features::linearLighting.GetCommonBufferData(),
		globals::features::terrainBlending.settings,
		globals::features::exponentialHeightFog.settings,
		globals::features::truePBR.settings);
}
