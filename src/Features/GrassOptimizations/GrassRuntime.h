#pragma once

#include <RE/B/BSGrassShaderProperty.h>
#include <REL/Relocation.h>

#include <algorithm>
#include <cstddef>
#include <span>

namespace GrassRuntime
{
	/** @brief Native grass fields follow the runtime-sized lighting property. */
	struct PropertyData
	{
		RE::BSTArray<float> fadeAlphas;
		std::uint64_t unknown18;
		RE::BSGraphics::TextureAddressMode clampMode;
		float wavePeriod;
		std::uint32_t windTimer;
		std::uint32_t unknown2C;
		RE::BSRenderPass* depthPass;
		RE::BSShaderPropertyLightData lightData;
		RE::BSShaderProperty::RenderPassArray shadowPasses;
	};
	static_assert(offsetof(PropertyData, wavePeriod) == 0x24);
	static_assert(offsetof(PropertyData, lightData) == 0x38);
	static_assert(sizeof(PropertyData) == 0x70);

	/** @brief Match persistent grouping state; animation comes from current native draw constants. */
	inline bool MatchesPersistentProperty(const PropertyData& live, float wavePeriod,
		std::uint32_t lightMask, std::span<RE::BSLight* const> lights)
	{
		return live.wavePeriod == wavePeriod && live.lightData.activeLightMask == lightMask &&
		       std::equal(lights.begin(), lights.end(), live.lightData.lights.begin(), live.lightData.lights.end());
	}

	/** @brief Read the native SE/AE or VR tail without assuming the universal C++ layout. */
	inline PropertyData& GetPropertyData(RE::BSGrassShaderProperty* property, bool vr)
	{
		return REL::RelocateMemberIf<PropertyData>(vr, property,
			RE::BSLightingShaderProperty::kSizeVR, RE::BSLightingShaderProperty::kSizeFlat);
	}
	inline const PropertyData& GetPropertyData(const RE::BSGrassShaderProperty* property, bool vr)
	{
		return GetPropertyData(const_cast<RE::BSGrassShaderProperty*>(property), vr);
	}
}
