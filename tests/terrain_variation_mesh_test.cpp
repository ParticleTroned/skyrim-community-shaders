#include "Features/TerrainVariationPolicy.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace RE
{
	struct TESObjectTREE
	{};
	struct Object
	{
		bool tree = false;
		template <class T>
		T* As()
		{
			return tree ? reinterpret_cast<T*>(this) : nullptr;
		}
	};
	struct Reference
	{
		Object* object = nullptr;
		Object* GetBaseObject() { return object; }
	};
	struct NiAlphaProperty
	{
		bool tested = false, blended = false;
		bool GetAlphaTesting() const { return tested; }
		bool GetAlphaBlending() const { return blended; }
	};
	struct BSShaderMaterial
	{
		enum class Type
		{
			kBase,
			kLighting
		};
		Type type = Type::kLighting;
		Type GetType() const { return type; }
	};
	struct BSLightingShaderMaterialBase : BSShaderMaterial
	{
		int32_t textureClampMode = 3;
	};
	struct Texture
	{
		std::string name = "textures/landscape/dirt.dds";
	};
	struct BSShaderProperty
	{
		enum class EShaderPropertyFlag : uint64_t
		{
			kMultiTextureLandscape = 1ull << 14,
			kLODLandscape = 1ull << 33,
			kDecal = 1ull << 26,
			kDynamicDecal = 1ull << 27,
			kTreeAnim = 1ull << 61,
			kSkinned = 1ull << 1,
			kFace = 1ull << 10,
			kFaceGenRGBTint = 1ull << 21,
			kHairTint = 1ull << 18,
			kEyeReflect = 1ull << 17,
			kLODObjects = 1ull << 34,
			kHDLODObjects = 1ull << 63,
			kProjectedUV = 1ull << 23,
			kMultiLayerParallax = 1ull << 56
		};
		struct Flags
		{
			uint64_t bits = 0;
			template <class... T>
			bool any(T... values) const
			{
				return (bits & (uint64_t(values) | ...)) != 0;
			}
		} flags;
		const void* rtti = this;
		BSShaderMaterial* material = nullptr;
		Texture* texture = nullptr;
		const void* GetRTTI() const { return rtti; }
		BSShaderMaterial* GetBaseMaterial() { return material; }
		Texture* GetBaseTexture() { return texture; }
	};
	struct Geometry
	{
		struct Data
		{
			NiAlphaProperty* alphaProperty = nullptr;
		} data;
		Reference* reference = nullptr;
		const Data& GetGeometryRuntimeData() const { return data; }
		Reference* GetUserData() { return reference; }
	};
	struct BSRenderPass
	{
		Geometry* geometry = nullptr;
		BSShaderProperty* shaderProperty = nullptr;
	};
	namespace BSGraphics
	{
		enum class TextureAddressMode
		{
			kWrapSWrapT = 3
		};
	}
}
struct State
{
	enum class ExtraFeatureDescriptors : uint32_t
	{
		TVMeshVariation = 1u << 10
	};
	struct
	{
		uint32_t ExtraFeatureDescriptor = 0;
	} permutationData;
};
namespace globals
{
	inline State* state = nullptr;
	namespace rtti
	{
		struct
		{
			const void* value = nullptr;
			const void* get() const { return value; }
		} BSLightingShaderPropertyRTTI;
	}
}
struct TerrainVariation
{
	struct
	{
		uint32_t enableMeshSupport = 1;
	} settings;
	bool loaded = true;
	std::unordered_set<std::string> paths;
	bool IsMeshSupportEnabled() const { return loaded && settings.enableMeshSupport != 0; }
	bool IsLandscapeDiffuseTexture(const std::string& name)
	{
		return TerrainVariationPolicy::IsLandscapeDiffusePath(TerrainVariationPolicy::CanonicaliseTexturePath(name), paths);
	}
	void UpdateMeshPermutation(RE::BSRenderPass*);
	void SetMeshSupportEnabled(bool);
};

#include "terrain_variation_mesh_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
try {
	using namespace TerrainVariationPolicy;
	const std::unordered_set<std::string> registered{ "custom/ground.dds", "landscape/trees/bark.dds", "../custom/ground.dds" };
	Require(CanonicaliseTexturePath("Data\\Textures\\LANDSCAPE\\Dirt.DDS") == "landscape/dirt.dds", "case and separators must normalize");
	Require(IsLandscapeDiffusePath(CanonicaliseTexturePath("Textures/Custom/Ground.dds"), registered), "registered custom terrain path must match");
	for (const auto* path : { "landscape/dirt.dds", "landscape/trees_extra/rock.dds", "custom/ground.dds" })
		Require(IsLandscapeDiffusePath(path, registered), "eligible landscape path rejected");
	for (const auto* path : { "", "landscape/", "landscapes/dirt.dds", "architecture/rock.dds", "landscape/trees/bark.dds",
			 "landscape/../trees/bark.dds", "../custom/ground.dds", "/landscape/dirt.dds", "c:/landscape/dirt.dds", "landscape//dirt.dds", "landscape/./dirt.dds" })
		Require(!IsLandscapeDiffusePath(path, registered), "unsafe or unrelated path selected");

	State state;
	globals::state = &state;
	TerrainVariation feature;
	RE::BSLightingShaderMaterialBase material;
	RE::Texture texture;
	RE::BSShaderProperty property;
	property.material = &material;
	property.texture = &texture;
	globals::rtti::BSLightingShaderPropertyRTTI.value = property.rtti;
	RE::Geometry geometry;
	RE::BSRenderPass pass{ &geometry, &property };
	constexpr uint32_t bit = 1u << 10;
	constexpr uint32_t preserved = (1u << 9) | 0x25u;
	auto check = [&](RE::BSRenderPass* candidate, bool expected) {
		state.permutationData.ExtraFeatureDescriptor = preserved | bit;
		feature.UpdateMeshPermutation(candidate);
		Require(state.permutationData.ExtraFeatureDescriptor == (preserved | (expected ? bit : 0u)), "eligibility must replace only its own descriptor bit");
	};
	check(&pass, true);
	check(nullptr, false);
	pass.geometry = nullptr;
	check(&pass, false);
	pass.geometry = &geometry;
	pass.shaderProperty = nullptr;
	check(&pass, false);
	pass.shaderProperty = &property;
	property.rtti = nullptr;
	check(&pass, false);
	property.rtti = globals::rtti::BSLightingShaderPropertyRTTI.value;
	property.material = nullptr;
	check(&pass, false);
	property.material = &material;
	material.type = RE::BSShaderMaterial::Type::kBase;
	check(&pass, false);
	material.type = RE::BSShaderMaterial::Type::kLighting;
	material.textureClampMode = 0;
	check(&pass, false);
	material.textureClampMode = 3;
	property.texture = nullptr;
	check(&pass, false);
	property.texture = &texture;
	texture.name = "textures/landscape/trees/bark.dds";
	check(&pass, false);
	texture.name = "textures/landscape/dirt.dds";
	RE::NiAlphaProperty alpha;
	geometry.data.alphaProperty = &alpha;
	alpha.tested = true;
	check(&pass, false);
	alpha.tested = false;
	alpha.blended = true;
	check(&pass, false);
	alpha.blended = false;
	check(&pass, true);
	using enum RE::BSShaderProperty::EShaderPropertyFlag;
	for (const auto flag : { kMultiTextureLandscape, kLODLandscape, kDecal, kDynamicDecal, kTreeAnim,
			 kSkinned, kFace, kFaceGenRGBTint, kHairTint, kEyeReflect, kLODObjects, kHDLODObjects, kProjectedUV, kMultiLayerParallax }) {
		property.flags.bits = uint64_t(flag);
		check(&pass, false);
	}
	property.flags.bits = 0;
	RE::Object object{ true };
	RE::Reference reference{ &object };
	geometry.reference = &reference;
	check(&pass, false);
	object.tree = false;
	check(&pass, true);
	feature.loaded = false;
	check(&pass, false);
	feature.loaded = true;
	feature.SetMeshSupportEnabled(false);
	Require(state.permutationData.ExtraFeatureDescriptor == preserved, "disable must immediately clear stale eligibility");
	check(&pass, false);
	feature.SetMeshSupportEnabled(true);
	check(&pass, true);
	globals::state = nullptr;
	feature.UpdateMeshPermutation(&pass);
	feature.SetMeshSupportEnabled(false);
	std::puts("Terrain variation mesh path, exclusion, per-draw reset and disable checks passed");
	return 0;
} catch (const std::exception& error) {
	std::fprintf(stderr, "%s\n", error.what());
	return 1;
}
