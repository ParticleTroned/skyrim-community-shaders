#include "Features/TerrainVariationPolicy.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace RE
{
	struct BSFixedString
	{
		std::shared_ptr<const std::string> value;
		BSFixedString() = default;
		BSFixedString(const char* text) : value(text ? std::make_shared<const std::string>(text) : nullptr) {}
		const char* c_str() const { return value ? value->c_str() : nullptr; }
	};
	struct BSTextureSet
	{
		enum class Texture
		{
			kDiffuse
		};
	};
	struct BGSTextureSet
	{
		const char* diffuse = nullptr;
		BGSTextureSet* seasonal = nullptr;
		const char* GetTexturePath(BSTextureSet::Texture) const { return diffuse; }
	};
	struct TESLandTexture
	{
		BGSTextureSet* textureSet = nullptr;
	};
	struct TESDataHandler
	{
		static inline TESDataHandler* singleton = nullptr;
		std::vector<TESLandTexture*> records;
		static TESDataHandler* GetSingleton() { return singleton; }
		template <class T>
		auto& GetFormArray()
		{
			return records;
		}
	};
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
		BSFixedString name = "textures/landscape/dirt.dds";
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
namespace Util
{
	RE::BGSTextureSet* GetSeasonalSwap(RE::BGSTextureSet* textureSet)
	{
		return textureSet && textureSet->seasonal ? textureSet->seasonal : textureSet;
	}
}
namespace logger
{
	template <class... Args>
	void info(const char*, Args&&...)
	{}
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
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
	struct CachedTexture
	{
		RE::BSFixedString name;
		bool landscape = false;
	};
	std::shared_mutex meshTextureMutex;
	std::unordered_set<std::string> landscapeDiffusePaths;
	bool landscapeDiffusePathsAvailable = false;
	std::unordered_map<const char*, CachedTexture> meshTextureCache;
	bool IsMeshSupportEnabled() const { return loaded && settings.enableMeshSupport != 0; }
	bool IsLandscapeDiffuseTexture(const RE::BSFixedString&);
	void DataLoaded();
	void UpdateMeshPermutation(RE::BSRenderPass*);
	void SetMeshSupportEnabled(bool);
};

#include "terrain_variation_mesh_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void TestPathPolicy()
{
	using namespace TerrainVariationPolicy;
	const std::unordered_set<std::string> registered{ "landscape/dirt.dds", "custom/ground.dds", "seasonal/snow.dds", "landscape/trees/bark.dds", "../custom/ground.dds" };
	Require(CanonicaliseTexturePath("Data\\Textures\\LANDSCAPE\\Dirt.DDS") == "landscape/dirt.dds", "case and separators must normalize");
	Require(IsLandscapeDiffusePath(CanonicaliseTexturePath("Textures/Custom/Ground.dds"), registered, true), "registered custom terrain path must match");
	for (const auto* path : { "landscape/dirt.dds", "custom/ground.dds", "seasonal/snow.dds" })
		Require(IsLandscapeDiffusePath(path, registered, true), "registered landscape path rejected");
	for (const auto* path : { "landscape/dirtcliffs/dirtcliffsroots01.dds", "landscape/trees_extra/rock.dds" }) {
		Require(!IsLandscapeDiffusePath(path, registered, true), "directory alone must not override loaded records");
		Require(!IsLandscapeDiffusePath(path, {}, true), "an available empty record set must not use directory fallback");
		Require(IsLandscapeDiffusePath(path, {}, false), "unavailable records must retain directory fallback");
	}
	Require(!IsLandscapeDiffusePath("custom/ground.dds", {}, false), "directory fallback must not admit unrelated paths");
	for (const auto* path : { "", "landscape/", "landscapes/dirt.dds", "architecture/rock.dds", "landscape/trees/bark.dds",
			 "landscape/../trees/bark.dds", "../custom/ground.dds", "/landscape/dirt.dds", "c:/landscape/dirt.dds", "landscape//dirt.dds", "landscape/./dirt.dds" }) {
		Require(!IsLandscapeDiffusePath(path, registered, true), "unsafe or unrelated path selected from records");
		Require(!IsLandscapeDiffusePath(path, {}, false), "unsafe or unrelated path selected by directory fallback");
	}
}

void TestRecordLoadingAndCache()
{
	TerrainVariation feature;
	RE::TESDataHandler handler;
	const RE::BSFixedString roots = "Textures\\Landscape\\DirtCliffs\\DirtCliffsRoots01.dds";
	const RE::BSFixedString ground = "textures/custom/ground.dds";
	const RE::BSFixedString snow = "textures/seasonal/snow.dds";
	feature.DataLoaded();
	Require(feature.IsLandscapeDiffuseTexture(roots), "missing data handler must permit directory fallback");
	Require(!feature.IsLandscapeDiffuseTexture(ground), "missing records must reject unrelated directories");
	Require(feature.IsLandscapeDiffuseTexture(roots), "cached directory fallback must remain eligible");
	Require(!feature.IsLandscapeDiffuseTexture(ground), "cached rejection must remain ineligible");
	Require(!feature.IsLandscapeDiffuseTexture({}), "null texture names must be rejected");
	Require(!feature.IsLandscapeDiffuseTexture(""), "empty texture names must be rejected");

	RE::TESDataHandler::singleton = &handler;
	feature.DataLoaded();
	Require(!feature.IsLandscapeDiffuseTexture(roots), "available empty records must invalidate cached directory fallback");
	Require(!feature.IsLandscapeDiffuseTexture(ground), "available empty records must reject custom textures");

	RE::BGSTextureSet seasonal{ "DATA\\Textures\\Seasonal\\Snow.DDS" };
	RE::BGSTextureSet dirt{ "DATA\\Textures\\LANDSCAPE\\Dirt.DDS", &seasonal };
	RE::BGSTextureSet custom{ "Textures/Custom/Ground.dds" };
	RE::BGSTextureSet empty{ "" };
	RE::BGSTextureSet nullPath;
	RE::BGSTextureSet malformed{ "landscape/../trees/bark.dds" };
	RE::BGSTextureSet tree{ "landscape/trees/bark.dds" };
	RE::TESLandTexture dirtRecord{ &dirt }, customRecord{ &custom }, emptyRecord{ &empty }, nullPathRecord{ &nullPath },
		nullSetRecord, malformedRecord{ &malformed }, treeRecord{ &tree };
	handler.records = { nullptr, &nullSetRecord, &nullPathRecord, &emptyRecord, &dirtRecord, &customRecord, &malformedRecord, &treeRecord };
	feature.DataLoaded();
	Require(!feature.IsLandscapeDiffuseTexture(roots), "loaded records must reject unregistered cliff roots");
	Require(feature.IsLandscapeDiffuseTexture(ground), "loading registered custom texture must invalidate cached rejection");
	Require(feature.IsLandscapeDiffuseTexture("textures/landscape/dirt.dds"), "base landscape texture paths must normalize");
	Require(feature.IsLandscapeDiffuseTexture(snow), "seasonal texture paths must normalize");
	Require(!feature.IsLandscapeDiffuseTexture(malformed.diffuse), "registered malformed texture must be rejected");
	Require(!feature.IsLandscapeDiffuseTexture(tree.diffuse), "registered tree texture must be rejected");

	RE::BGSTextureSet rootTexture{ roots.c_str() };
	RE::TESLandTexture rootRecord{ &rootTexture };
	handler.records = { &rootRecord };
	feature.DataLoaded();
	Require(feature.IsLandscapeDiffuseTexture(roots), "reloading records must invalidate cached cliff-root rejection");
	Require(!feature.IsLandscapeDiffuseTexture(ground), "reloading records must invalidate cached custom eligibility");
	Require(!feature.IsLandscapeDiffuseTexture(snow), "reloading records must remove previous seasonal membership");

	RE::TESDataHandler::singleton = nullptr;
	feature.DataLoaded();
	Require(feature.IsLandscapeDiffuseTexture(roots), "unavailable records after reload must restore directory fallback");
	Require(!feature.IsLandscapeDiffuseTexture(ground), "unavailable records must not retain old custom membership");
}

void TestDrawEligibility()
{
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
	RE::TESDataHandler handler;
	RE::TESDataHandler::singleton = &handler;
	feature.DataLoaded();
	check(&pass, false);
	RE::BGSTextureSet dirt{ "landscape/dirt.dds" }, ground{ "custom/ground.dds" };
	RE::TESLandTexture dirtRecord{ &dirt }, groundRecord{ &ground };
	handler.records = { &dirtRecord, &groundRecord };
	feature.DataLoaded();
	check(&pass, true);
	texture.name = "Textures\\Landscape\\DirtCliffs\\DirtCliffsRoots01.dds";
	check(&pass, false);
	RE::TESDataHandler::singleton = nullptr;
	feature.DataLoaded();
	check(&pass, true);
	RE::TESDataHandler::singleton = &handler;
	feature.DataLoaded();
	check(&pass, false);
	texture.name = "textures/custom/ground.dds";
	check(&pass, true);
	texture.name = "textures/landscape/dirt.dds";
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
	RE::TESDataHandler::singleton = nullptr;
}

int main()
try {
	TestPathPolicy();
	TestRecordLoadingAndCache();
	TestDrawEligibility();
	std::puts("Terrain variation record loading, cache, mesh eligibility and disable checks passed");
	return 0;
} catch (const std::exception& error) {
	std::fprintf(stderr, "%s\n", error.what());
	return 1;
}
