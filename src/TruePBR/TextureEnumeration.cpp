#include "TextureEnumeration.h"
#include "BSLightingShaderMaterialPBRLandscape.h"

#include <array>

std::int32_t PBRTextures::ForEach(RE::BSShaderProperty& property, RE::BSShaderProperty::ForEachVisitor& visitor)
{
	if (!property.material)
		return 1;
	if (const auto* landscape = BSLightingShaderMaterialPBRLandscape::GetRegistered(property.material)) {
		// PBR terrain can exceed the engine visitor's twenty-pointer scratch buffer.
		std::array<RE::NiSourceTexture*, BSLightingShaderMaterialPBRLandscape::TextureCapacity> textures{};
		const auto count = landscape->CollectTextures(textures);
		std::int32_t result = 1;
		for (std::uint32_t i = 0; i < count; ++i) {
			result = static_cast<std::int32_t>(visitor.Accept(textures[i]));
			if (result <= 0)
				break;
		}
		return result;
	}
	return property.ForEachTexture(visitor);
}
