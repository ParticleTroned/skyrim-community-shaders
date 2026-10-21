#pragma once

#include <RE/B/BSGrassShaderProperty.h>
#include <RE/B/BSMultiStreamInstanceTriShape.h>
#include <RE/B/BSRenderPass.h>
#include <REL/Relocation.h>
#include <SKSE/Impl/PCH.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace GrassRuntime
{
	using SetupGeometry = void (*)(RE::BSShader*, RE::BSRenderPass*, uint32_t);
	/** @brief Detect structural attachment changes cheaply; in-place edits still require generation hooks. */
	struct GroupLayout
	{
		const void* storage = nullptr;
		uint32_t slots = 0, active = 0;
		bool operator==(const GroupLayout&) const = default;
	};
	/** @brief Capture native array metadata without scanning groups or trusting cached-file instance totals. */
	inline GroupLayout CaptureGroupLayout(const RE::BSMultiStreamInstanceTriShape::MULTISTREAM_TRISHAPE_RUNTIME_DATA& data)
	{
		return { data.instanceGroups.data(), data.instanceGroups.size(), data.activeGroupCount };
	}
	/** @brief Native instance headers initialize only the low 32 bits of their CPU byte width. */
	inline bool HasInstanceData(const RE::BSGraphics::VertexBuffer& buffer, uint64_t requiredBytes)
	{
		return buffer.m_data && requiredBytes <= uint32_t(buffer.byteWidth);
	}
	/** @brief Validate native group records; cached shapes need not initialize generation-only instanceSize. */
	inline bool MatchesInstanceBytes(const RE::BSGraphics::VertexBuffer& buffer, uint64_t requiredBytes)
	{
		return requiredBytes && requiredBytes == uint32_t(buffer.byteWidth);
	}
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
	static_assert(offsetof(PropertyData, windTimer) == 0x28);
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

	/** @brief Preserve native wind history; an optional raw phase restores the original draw's previous-wind constant. */
	inline void PrepareGeometryWithoutAdvancingWind(RE::BSShader* shader, RE::BSRenderPass* pass,
		uint32_t flags, SetupGeometry setup, bool vr, std::optional<uint32_t> previousWind = std::nullopt)
	{
		auto& data = GetPropertyData(static_cast<RE::BSGrassShaderProperty*>(pass->shaderProperty), vr);
		const auto saved = data.windTimer;
		const SKSE::stl::scope_exit restore([&]() noexcept { data.windTimer = saved; });
		if (previousWind)
			data.windTimer = *previousWind;
		setup(shader, pass, flags);
	}
}
