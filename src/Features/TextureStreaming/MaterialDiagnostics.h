#pragma once
#include <array>
#include <cstdint>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <string>
#	include <string_view>
#endif

namespace StreamingTextures
{
	/** First failed invariant in static material classification. */
	enum class MaterialRejection : std::uint8_t
	{
		None,
		GeometryType,
		MissingProperty,
		MissingMaterial,
		Skin,
		GeometryController,
		AlphaController,
		AlphaBlending,
		AlphaTesting,
		ShaderFlags,
		PropertyController,
		MaterialType,
		MaterialFeature,
		RenderTarget,
		MaterialAlpha,
		Refraction,
		AuxiliaryTextures,
		GlowAlias,
		Count
	};
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Developer-only observations; shader values are valid only after the property check. */
	struct MaterialInspection
	{
		MaterialRejection reason = MaterialRejection::None;
		std::uint8_t geometryType = 0;
		std::uint32_t materialType = UINT32_MAX, materialFeature = UINT32_MAX;
		std::uint64_t flags = 0, disallowedFlags = 0;
	};
	/** Bounded cumulative counts and one representative per rejection, not unique meshes. */
	struct MaterialDiagnostics
	{
		static constexpr std::array names{
			std::string_view("eligible"), std::string_view("geometryType"), std::string_view("missingProperty"),
			std::string_view("missingMaterial"), std::string_view("skin"), std::string_view("geometryController"),
			std::string_view("alphaController"), std::string_view("alphaBlending"), std::string_view("alphaTesting"),
			std::string_view("shaderFlags"), std::string_view("propertyController"), std::string_view("materialType"),
			std::string_view("materialFeature"), std::string_view("renderTarget"), std::string_view("materialAlpha"),
			std::string_view("refraction"), std::string_view("auxiliaryTextures"), std::string_view("glowAlias")
		};
		static_assert(names.size() == static_cast<std::size_t>(MaterialRejection::Count));
		std::array<std::uint64_t, names.size()> reasons{};
		std::array<std::uint64_t, 256> geometryTypes{};
		std::array<std::uint64_t, 64> disallowedFlagBits{};
		std::array<MaterialInspection, names.size()> samples{};
		std::array<std::string, names.size()> geometryNames{};

		void Observe(const MaterialInspection& inspection, std::string_view geometryName)
		{
			const auto reason = static_cast<std::size_t>(inspection.reason);
			if (!reasons[reason]++) {
				samples[reason] = inspection;
				geometryNames[reason] = geometryName.substr(0, 128);
			}
			++geometryTypes[inspection.geometryType];
			for (unsigned bit = 0; bit < disallowedFlagBits.size(); ++bit)
				disallowedFlagBits[bit] += (inspection.disallowedFlags >> bit) & 1;
		}
	};
#endif
}
