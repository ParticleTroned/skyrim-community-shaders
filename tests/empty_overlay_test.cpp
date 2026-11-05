#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RE
{
	struct BSShaderProperty
	{
		enum EShaderPropertyFlag
		{
			kSkinned = 1,
			kZBufferWrite = 2
		};
		struct Flags
		{
			unsigned value = kSkinned;
			bool any(EShaderPropertyFlag flag) const { return (value & flag) != 0; }
		} flags;
	};
	struct NiAlphaProperty
	{
		enum class AlphaFunction
		{
			kSrcAlpha,
			kInvSrcAlpha,
			kOne
		};
		bool blend = true;
		AlphaFunction source = AlphaFunction::kSrcAlpha, destination = AlphaFunction::kInvSrcAlpha;
		bool GetAlphaBlending() const { return blend; }
		AlphaFunction GetSrcBlendMode() const { return source; }
		AlphaFunction GetDestBlendMode() const { return destination; }
	};
	struct BSGeometry
	{
		struct Data
		{
			std::shared_ptr<NiAlphaProperty> alphaProperty = std::make_shared<NiAlphaProperty>();
		} data;
		Data& GetGeometryRuntimeData() { return data; }
	};
	struct BSShaderMaterial
	{
		enum class Type
		{
			kLighting,
			Other
		};
		enum class Feature
		{
			kDefault,
			Other
		};
		Type type = Type::kLighting;
		Feature feature = Feature::kDefault;
		Type GetType() const { return type; }
		Feature GetFeature() const { return feature; }
	};
	struct BSLightingShaderMaterialBase : BSShaderMaterial
	{
		struct Texture
		{
			std::string name = "verified.dds";
		};
		std::shared_ptr<Texture> diffuseTexture = std::make_shared<Texture>();
	};
	struct BSLightingShaderProperty : BSShaderProperty
	{
		BSLightingShaderMaterialBase* material = nullptr;
		BSShaderMaterial* GetBaseMaterial() { return material; }
	};
}
namespace Util
{
	bool IEquals(std::string_view a, std::string_view b) { return a == b; }
}
namespace
{
	std::string emptyTexture = "verified.dds";
#include "empty_overlay_under_test.h"
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}
int main()
{
	try {
		RE::BSLightingShaderMaterialBase material;
		RE::BSLightingShaderProperty property;
		RE::BSGeometry geometry;
		property.material = &material;
		Require(IsEmptyOverlay(&property, &geometry), "Verified inert overlay rejected");
		Require(!IsEmptyOverlay(nullptr, &geometry) && !IsEmptyOverlay(&property, nullptr), "Missing geometry must render normally");
		property.flags.value |= RE::BSShaderProperty::kZBufferWrite;
		Require(!IsEmptyOverlay(&property, &geometry), "Depth-writing overlay must survive");
		property.flags.value = 0;
		Require(!IsEmptyOverlay(&property, &geometry), "Unskinned geometry must survive");
		property.flags.value = RE::BSShaderProperty::kSkinned;
		auto& alpha = *geometry.data.alphaProperty;
		alpha.blend = false;
		Require(!IsEmptyOverlay(&property, &geometry), "Alpha testing alone must not cull");
		alpha.blend = true;
		alpha.source = RE::NiAlphaProperty::AlphaFunction::kOne;
		Require(!IsEmptyOverlay(&property, &geometry), "Additive source must survive");
		alpha.source = RE::NiAlphaProperty::AlphaFunction::kSrcAlpha;
		alpha.destination = RE::NiAlphaProperty::AlphaFunction::kOne;
		Require(!IsEmptyOverlay(&property, &geometry), "Different destination blend must survive");
		alpha.destination = RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha;
		material.feature = RE::BSShaderMaterial::Feature::Other;
		Require(!IsEmptyOverlay(&property, &geometry), "Specialized material must survive");
		material.feature = RE::BSShaderMaterial::Feature::kDefault;
		material.diffuseTexture->name = "assigned.dds";
		Require(!IsEmptyOverlay(&property, &geometry), "Assigned overlay must render immediately");
		material.diffuseTexture.reset();
		Require(!IsEmptyOverlay(&property, &geometry), "Missing texture must survive");
		std::cout << "Overlay admission checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
