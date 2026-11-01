#include "TruePBR/MaterialRegistry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace RE
{
	struct NiSourceTexture
	{};
	template <class T>
	struct NiPointer
	{
		T* value = nullptr;
		T* get() const { return value; }
		explicit operator bool() const { return value != nullptr; }
		bool operator!=(std::nullptr_t) const { return value != nullptr; }
		bool operator==(std::nullptr_t) const { return value == nullptr; }
	};
	struct BSShaderMaterial
	{
		enum class Type
		{
			kLighting,
			kEffect
		};
		enum class Feature
		{
			kDefault,
			kMultiTexLandLODBlend
		};
		virtual ~BSShaderMaterial() = default;
		virtual Type GetType() const { return Type::kLighting; }
		virtual Feature GetFeature() const { return Feature::kDefault; }
	};
	struct BSLightingShaderMaterialBase : BSShaderMaterial
	{
		NiPointer<NiSourceTexture> rimSoftLightingTexture, specularBackLightingTexture;
		virtual uint32_t GetTextures(NiSourceTexture**) = 0;
		void ReceiveValuesFromRootMaterial(bool, bool, bool, bool, bool) {}
	};
	struct BSShaderProperty
	{
		struct ForEachVisitor
		{
			virtual ~ForEachVisitor() = default;
			virtual std::uint32_t Accept(NiSourceTexture*) = 0;
		};
		BSShaderMaterial* material = nullptr;
		unsigned nativeCalls = 0;
		std::int32_t ForEachTexture(ForEachVisitor&)
		{
			++nativeCalls;
			return 7;
		}
	};
}

class BSLightingShaderMaterialPBRLandscape : public RE::BSLightingShaderMaterialBase
{
public:
#include "pbr_texture_constants_under_test.h"
	inline static PBRMaterialRegistry<BSLightingShaderMaterialPBRLandscape, std::array<int*, 6>> All;
	std::uint32_t numLandscapeTextures = 0;
	std::array<RE::NiPointer<RE::NiSourceTexture>, NumTiles> landscapeBaseColorTextures, landscapeNormalTextures,
		landscapeDisplacementTextures, landscapeRMAOSTextures;
	RE::NiPointer<RE::NiSourceTexture> terrainOverlayTexture, terrainNoiseTexture;
	struct Glint
	{
		bool enabled = false;
	};
	std::array<Glint, NumTiles> glintParameters;
	Feature GetFeature() const override { return Feature::kMultiTexLandLODBlend; }
	uint32_t GetTextures(RE::NiSourceTexture** textures) override;
	uint32_t CollectTextures(std::span<RE::NiSourceTexture*> textures) const;
	void ReceiveValuesFromRootMaterial(bool, bool, bool, bool, bool);
	static const BSLightingShaderMaterialPBRLandscape* GetRegistered(const RE::BSShaderMaterial*);
	bool HasGlint() const;
};
namespace globals::game
{
	struct GraphicsState
	{
		std::array<RE::NiSourceTexture, 4> textures;
		RE::NiPointer<RE::NiSourceTexture> defaultTextureBlack{ &textures[0] }, defaultTextureNormalMap{ &textures[1] },
			defaultTextureWhite{ &textures[2] };
		GraphicsState& GetRuntimeData() { return *this; }
	} graphics;
	auto* graphicsState = &graphics;
}
#include "pbr_texture_collection_under_test.h"
namespace PBRTextures
{
	std::int32_t ForEach(RE::BSShaderProperty&, RE::BSShaderProperty::ForEachVisitor&);
}
#include "pbr_texture_visitor_under_test.h"

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	struct Fixture
	{
		std::array<RE::NiSourceTexture, 28> textures;
		std::array<RE::NiSourceTexture*, 28> Expected()
		{
			constexpr std::array<unsigned, 28> indices{ 0, 1, 2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27,
				4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25 };
			std::array<RE::NiSourceTexture*, 28> result{};
			for (unsigned i = 0; i < indices.size(); ++i)
				result[i] = &textures[indices[i]];
			return result;
		}
		BSLightingShaderMaterialPBRLandscape material;
		Fixture()
		{
			material.numLandscapeTextures = 6;
			material.rimSoftLightingTexture.value = &textures[0];
			material.specularBackLightingTexture.value = &textures[1];
			for (unsigned i = 0; i < 6; ++i) {
				material.landscapeBaseColorTextures[i].value = &textures[2 + i * 4];
				material.landscapeNormalTextures[i].value = &textures[3 + i * 4];
				material.landscapeDisplacementTextures[i].value = &textures[4 + i * 4];
				material.landscapeRMAOSTextures[i].value = &textures[5 + i * 4];
			}
			material.terrainOverlayTexture.value = &textures[26];
			material.terrainNoiseTexture.value = &textures[27];
			material.All.Register(&material);
		}
		~Fixture() { material.All.Unregister(&material); }
	};

	struct Visitor : RE::BSShaderProperty::ForEachVisitor
	{
		std::vector<RE::NiSourceTexture*> visited;
		std::size_t stopAfter = 100;
		std::int32_t stopResult = 0;
		std::uint32_t Accept(RE::NiSourceTexture* texture) override
		{
			visited.push_back(texture);
			return static_cast<std::uint32_t>(visited.size() == stopAfter ? stopResult : 2);
		}
	};

	void NativeBufferIsBounded()
	{
		Fixture fixture;
		RE::NiSourceTexture guard;
		struct Buffer
		{
			RE::NiSourceTexture* before;
			RE::NiSourceTexture* slots[20];
			RE::NiSourceTexture* after[8];
		} buffer{};
		buffer.before = &guard;
		std::fill(std::begin(buffer.after), std::end(buffer.after), &guard);
		RE::BSLightingShaderMaterialBase& native = fixture.material;
		Require(native.GetTextures(buffer.slots) == 20, "Native count exceeds the twenty-entry buffer");
		Require(buffer.before == &guard, "Native write before buffer");
		for (auto* value : buffer.after)
			Require(value == &guard, "Native write overwrote adjacent saved state");
		for (unsigned i = 0; i < 20; ++i)
			Require(buffer.slots[i] == fixture.Expected()[i], "Native texture order changed");
		Require(native.GetTextures(nullptr) == 0, "Null native destination must be empty");
	}

	void EveryDestinationSizeIsBounded()
	{
		Fixture fixture;
		RE::NiSourceTexture guard;
		for (std::size_t capacity = 0; capacity <= 32; ++capacity) {
			std::vector<RE::NiSourceTexture*> buffer(capacity + 2, &guard);
			const auto count = fixture.material.CollectTextures({ buffer.data() + 1, capacity });
			Require(count == std::min(capacity, std::size_t{ 28 }), "Incorrect bounded texture count");
			Require(buffer.front() == &guard && buffer.back() == &guard, "Collection crossed the destination boundary");
			for (std::size_t i = 0; i < count; ++i)
				Require(buffer[i + 1] == fixture.Expected()[i], "Full texture order changed");
			for (std::size_t i = count; i < capacity; ++i)
				Require(buffer[i + 1] == &guard, "Collection wrote beyond the returned count");
		}
		std::array<RE::NiSourceTexture*, 28> buffer{};
		fixture.material.numLandscapeTextures = std::numeric_limits<std::uint32_t>::max();
		Require(fixture.material.CollectTextures(buffer) == 28, "Invalid tile count must stop at six layers");
		fixture.material.numLandscapeTextures = 0;
		Require(fixture.material.CollectTextures(buffer) == 4, "Zero tiles must retain auxiliary slots");
		Require(buffer[2] == &fixture.textures[26] && buffer[3] == &fixture.textures[27], "Overlay or noise missing");
		fixture.material.rimSoftLightingTexture.value = nullptr;
		fixture.material.specularBackLightingTexture.value = nullptr;
		fixture.material.terrainOverlayTexture.value = nullptr;
		fixture.material.terrainNoiseTexture.value = nullptr;
		Require(fixture.material.CollectTextures(buffer) == 0, "Empty material must produce no textures");
		Visitor visitor;
		RE::BSShaderProperty property{ &fixture.material };
		Require(PBRTextures::ForEach(property, visitor) == 1 && visitor.visited.empty(), "Empty visitor must report success without callbacks");
	}

	void StreamingRetainsEveryConsumer()
	{
		Fixture fixture;
		RE::BSShaderProperty property{ &fixture.material };
		Visitor visitor;
		Require(PBRTextures::ForEach(property, visitor) == 2, "Visitor must preserve the last positive result");
		Require(property.nativeCalls == 0, "PBR terrain must bypass the native scratch buffer");
		Require(visitor.visited.size() == 28, "Streaming/preview protection lost PBR textures");
		for (unsigned i = 0; i < 28; ++i)
			Require(visitor.visited[i] == fixture.Expected()[i], "Streaming texture order changed");
		fixture.material.rimSoftLightingTexture.value = nullptr;
		fixture.material.specularBackLightingTexture.value = nullptr;
		visitor.visited.clear();
		Require(PBRTextures::ForEach(property, visitor) == 2 && visitor.visited.size() == 26, "Reported 26-texture landscape lost consumers");
		Require(std::find(visitor.visited.begin(), visitor.visited.end(), &fixture.textures[27]) != visitor.visited.end(), "Noise texture must remain protected");
		fixture.material.landscapeNormalTextures[1].value = nullptr;
		fixture.material.landscapeRMAOSTextures[5].value = &fixture.textures[2];
		visitor.visited.clear();
		PBRTextures::ForEach(property, visitor);
		Require(visitor.visited.size() == 25, "Null slots must be skipped");
		Require(std::count(visitor.visited.begin(), visitor.visited.end(), &fixture.textures[2]) == 2, "Shared textures must retain both slot visits");
	}

	void NativeRetainsAllTerrainLayers()
	{
		Fixture fixture;
		std::array<RE::NiSourceTexture*, 20> native{};
		fixture.material.GetTextures(native.data());
		const auto retained = [&](RE::NiSourceTexture* texture) {
			return std::find(native.begin(), native.end(), texture) != native.end();
		};
		for (unsigned i = 0; i < 6; ++i) {
			Require(retained(&fixture.textures[2 + i * 4]), "Native enumeration omitted a diffuse terrain layer");
			Require(retained(&fixture.textures[3 + i * 4]), "Native enumeration omitted a normal terrain layer");
		}
		for (auto i : { 0u, 1u, 26u, 27u })
			Require(retained(&fixture.textures[i]), "Native enumeration omitted a shared terrain texture");
	}

	void InvalidLayerCountsStayWithinMaterialStorage()
	{
		Fixture fixture;
		fixture.material.numLandscapeTextures = std::numeric_limits<std::uint32_t>::max();
		Require(!fixture.material.HasGlint(), "Invalid count read outside the glint storage");
		fixture.material.glintParameters[5].enabled = true;
		Require(fixture.material.HasGlint(), "The sixth terrain layer must remain visible to glint detection");
		for (unsigned i = 0; i < 6; ++i) {
			fixture.material.landscapeBaseColorTextures[i].value = nullptr;
			fixture.material.landscapeNormalTextures[i].value = nullptr;
			fixture.material.landscapeDisplacementTextures[i].value = nullptr;
			fixture.material.landscapeRMAOSTextures[i].value = nullptr;
		}
		fixture.material.ReceiveValuesFromRootMaterial(false, false, false, false, false);
		for (unsigned i = 0; i < 6; ++i) {
			Require(fixture.material.landscapeBaseColorTextures[i].value == globals::game::graphics.defaultTextureBlack.value, "Missing base colour fallback");
			Require(fixture.material.landscapeNormalTextures[i].value == globals::game::graphics.defaultTextureNormalMap.value, "Missing normal fallback");
			Require(fixture.material.landscapeDisplacementTextures[i].value == globals::game::graphics.defaultTextureBlack.value, "Missing displacement fallback");
			Require(fixture.material.landscapeRMAOSTextures[i].value == globals::game::graphics.defaultTextureWhite.value, "Missing RMAOS fallback");
		}
	}

	void VisitorStopsAndFallbacksMatchNative()
	{
		Fixture fixture;
		RE::BSShaderProperty property{ &fixture.material };
		for (const auto result : { 0, -1, -7 }) {
			Visitor visitor;
			visitor.stopAfter = 3;
			visitor.stopResult = result;
			Require(PBRTextures::ForEach(property, visitor) == result, "Stop result must propagate unchanged");
			Require(visitor.visited.size() == 3, "Nonpositive visitor results must stop iteration");
		}
		Visitor visitor;
		Require(!BSLightingShaderMaterialPBRLandscape::GetRegistered(reinterpret_cast<RE::BSShaderMaterial*>(std::uintptr_t{ 1 })), "Registry lookup must not dereference an unknown material address");
		fixture.material.All.Unregister(&fixture.material);
		Require(PBRTextures::ForEach(property, visitor) == 7 && property.nativeCalls == 1, "Unregistered vanilla terrain must use its native visitor");
		RE::BSShaderMaterial ordinary;
		property.material = &ordinary;
		Require(PBRTextures::ForEach(property, visitor) == 7 && property.nativeCalls == 2, "Other materials must use their native visitor");
		struct VanillaTerrain : RE::BSLightingShaderMaterialBase
		{
			Feature GetFeature() const override { return Feature::kMultiTexLandLODBlend; }
			uint32_t GetTextures(RE::NiSourceTexture**) override { return 0; }
		} vanilla;
		property.material = &vanilla;
		Require(!BSLightingShaderMaterialPBRLandscape::GetRegistered(&vanilla), "Vanilla terrain must not be downcast to PBR");
		Require(PBRTextures::ForEach(property, visitor) == 7 && property.nativeCalls == 3, "Vanilla terrain must retain native behavior");
		struct EffectTerrain : VanillaTerrain
		{
			Type GetType() const override { return Type::kEffect; }
		} effect;
		property.material = &effect;
		Require(!BSLightingShaderMaterialPBRLandscape::GetRegistered(&effect), "Effect materials cannot use the PBR terrain layout");
		Require(PBRTextures::ForEach(property, visitor) == 7 && property.nativeCalls == 4, "Effect materials must retain native behavior");
		property.material = nullptr;
		Require(PBRTextures::ForEach(property, visitor) == 1 && property.nativeCalls == 4, "Missing materials must avoid native null dereferences");
	}
}

int main()
{
	NativeRetainsAllTerrainLayers();
	NativeBufferIsBounded();
	InvalidLayerCountsStayWithinMaterialStorage();
	EveryDestinationSizeIsBounded();
	StreamingRetainsEveryConsumer();
	VisitorStopsAndFallbacksMatchNative();
	std::cout << "PBR texture enumeration checks passed\n";
}
