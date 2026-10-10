#include "Features/TextureStreaming/Settings.h"
#include <array>
#include <atomic>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace RE
{
	struct NiSourceTexture
	{};
	struct BSShaderMaterial
	{
#include "streaming_features_under_test.h"
		Feature feature = Feature::kDefault;
		Type type = Type::kLighting;
		Feature GetFeature() const { return feature; }
		Type GetType() const { return type; }
	};
	struct BSLightingShaderMaterialBase : BSShaderMaterial
	{
		int diffuseRenderTargetSourceIndex = -1;
		float materialAlpha = 1, refractionPower = 0;
		void* rimSoftLightingTexture = nullptr;
		void* specularBackLightingTexture = nullptr;
		std::shared_ptr<NiSourceTexture> diffuseTexture, normalTexture;
	};
	struct BSLightingShaderMaterialGlowmap : BSLightingShaderMaterialBase
	{
		std::shared_ptr<NiSourceTexture> glowTexture;
	};
	struct BSShaderProperty
	{
		static constexpr std::uint64_t BIT64 = 1;
#include "streaming_flags_under_test.h"
		struct Flags
		{
			std::uint64_t value = 0;
			std::uint64_t underlying() const { return value; }
		} flags;
		BSShaderMaterial* material = nullptr;
		bool controlled = false;
		bool GetControllers() const { return controlled; }
	};
	struct NiAlphaProperty
	{
		bool controlled = false, blending = false, testing = true;
		bool GetControllers() const { return controlled; }
		bool GetAlphaBlending() const { return blending; }
		bool GetAlphaTesting() const { return testing; }
	};
	struct BSGeometry
	{
		enum class Type : std::uint8_t
		{
			kTriShape,
			kOther
		};
		struct Kind
		{
			Type value = Type::kTriShape;
			std::uint8_t underlying() const { return static_cast<std::uint8_t>(value); }
		} kind;
		struct Data
		{
			std::shared_ptr<BSShaderProperty> shaderProperty = std::make_shared<BSShaderProperty>();
			std::shared_ptr<NiAlphaProperty> alphaProperty;
			void* skinInstance = nullptr;
		} data;
		bool controlled = false;
		Kind GetType() const { return kind; }
		const Data& GetGeometryRuntimeData() const { return data; }
		bool GetControllers() const { return controlled; }
	};
}
namespace StreamingTextures
{
#include "streaming_material_under_test.h"
}
namespace Policy = TextureStreamingPolicy;
namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{}
}
std::atomic<bool> originHookReady{ true };
std::uint64_t GetTickCount64() { return 1000; }
namespace Util
{
	bool disabled = false;
	struct DisableGuard
	{
		bool previous;
		explicit DisableGuard(bool value) : previous(disabled) { disabled |= value; }
		~DisableGuard() { disabled = previous; }
	};
	namespace Text
	{
		void WrappedWarning(const char*) {}
	}
	namespace Widgets
	{
		std::map<std::string, bool> clicks;
		bool Checkbox(const char* name, bool* value)
		{
			const auto found = clicks.find(name);
			if (disabled || found == clicks.end())
				return false;
			*value = found->second;
			clicks.erase(found);
			return true;
		}
		bool SliderInt(const char*, int*, int, std::uint32_t) { return false; }
	}
}
namespace MenuUI
{
#include "streaming_sections_under_test.h"
	std::string selected = "categories";
	std::vector<Section> sections;
	struct SettingsPage
	{
		SettingsPage(const char*, std::initializer_list<Section> value) { sections = value; }
		bool Is(const char* id) const { return selected == id; }
	};
	struct DetailGrid
	{
		explicit DetailGrid(const char*) {}
		void Next() {}
	};
	void DetailNote(const char*) {}
	void DetailText(const char*) {}
	void SectionHeading(const char*) {}
}
struct TextureStreaming
{
	struct Inventory
	{
		unsigned clears = 0;
		void Clear() { ++clears; }
	};
	struct Record
	{
		Inventory consumers;
		std::array<std::uint64_t, 4> bytes{};
		unsigned drop = 0;
	};
	struct State
	{
		std::mutex mutex;
		StreamingTextures::Categories categories;
		bool enabled = false;
		unsigned maximumDrop = 2;
		std::uint64_t epoch = 7, completeScan = 12, nextScanMs = 10000;
		std::vector<int> frontier{ 1 }, visited{ 1 }, reducedRecords;
		std::map<int, Record> records{ { 1, {} } };
		std::string detail;
		void UpdatePressureDiagnostics(std::uint64_t) {}
	};
	std::unique_ptr<State> state = std::make_unique<State>();
	void Configure(bool enabled, std::uint32_t maximumDrop, std::optional<StreamingTextures::Categories> categories = std::nullopt);
	void LoadSettings(nlohmann::json&);
	void SaveSettings(nlohmann::json&);
	void RestoreDefaultSettings();
	void DrawSettingsEnabledControl();
	void DrawSettings();
};
#include "streaming_enabled_under_test.h"
#include "streaming_settings_under_test.h"
#include "streaming_ui_under_test.h"

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
int main()
{
	try {
		using namespace StreamingTextures;
		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		RE::BSLightingShaderMaterialGlowmap material;
		RE::BSGeometry geometry;
		auto& property = *geometry.data.shaderProperty;
		property.material = &material;
		std::uint32_t categories = UINT32_MAX;
		Require(StaticMaterial(&geometry, &categories) == &material && categories == 0, "Ordinary static material changed eligibility");
		property.flags.value = static_cast<std::uint64_t>(Flag::kOwnEmit);
		Require(StaticMaterial(&geometry, &categories) == &material && categories == EmissiveStatics, "Emissive coverage was not independently classified");
		geometry.data.alphaProperty = std::make_shared<RE::NiAlphaProperty>();
		property.flags.value |= static_cast<std::uint64_t>(Flag::kTwoSided);
		Require(StaticMaterial(&geometry, &categories) == &material && categories == (EmissiveStatics | AlphaTestedStatics), "Combined cutout/emissive material lost a required toggle");
		geometry.data.alphaProperty->blending = true;
		Require(!StaticMaterial(&geometry, &categories), "Blended transparency entered cutout streaming");
		geometry.data.alphaProperty->blending = false;
		geometry.data.alphaProperty->testing = false;
		Require(!StaticMaterial(&geometry, &categories), "Unknown alpha contract entered streaming");
		geometry.data.alphaProperty->testing = true;
		geometry.data.alphaProperty->controlled = true;
		Require(!StaticMaterial(&geometry, &categories), "Animated alpha contract entered streaming");
		geometry.data.alphaProperty.reset();
		property.flags.value = 0;
		for (const auto feature : { RE::BSShaderMaterial::Feature::kParallax, RE::BSShaderMaterial::Feature::kMultiTexLand,
				 RE::BSShaderMaterial::Feature::kLODLand, RE::BSShaderMaterial::Feature::kLODObjectsHD, RE::BSShaderMaterial::Feature::kFaceGen,
				 RE::BSShaderMaterial::Feature::kEnvironmentMap }) {
			material.feature = feature;
			Require(!StaticMaterial(&geometry, &categories), "Special, terrain, character or LOD material entered optional coverage");
		}
		material.feature = RE::BSShaderMaterial::Feature::kDefault;
		material.type = RE::BSShaderMaterial::Type::kWater;
		Require(!StaticMaterial(&geometry, &categories), "Water material entered static streaming");
		material.type = RE::BSShaderMaterial::Type::kLighting;
		for (const auto flag : { Flag::kLODLandscape, Flag::kLODObjects, Flag::kHDLODObjects, Flag::kBillboard, Flag::kParallax,
				 Flag::kMultiTextureLandscape, Flag::kTreeAnim, Flag::kSkinned, Flag::kVertexAlpha }) {
			property.flags.value = static_cast<std::uint64_t>(flag);
			Require(!StaticMaterial(&geometry, &categories), "A protected shader flag escaped optional coverage");
		}
		property.flags.value = 0;
		material.feature = RE::BSShaderMaterial::Feature::kGlowMap;
		material.glowTexture = std::make_shared<RE::NiSourceTexture>();
		Require(StaticMaterial(&geometry, &categories) && categories == EmissiveStatics, "Static glow material could not classify ordinary slots");
		material.diffuseTexture = material.glowTexture;
		Require(!StaticMaterial(&geometry, &categories), "Glow map aliased with a streamed diffuse texture");
		material.diffuseTexture.reset();
		geometry.data.skinInstance = &material;
		Require(!StaticMaterial(&geometry, &categories), "Skinned emissive material entered static streaming");
		geometry.data.skinInstance = nullptr;
		geometry.controlled = true;
		Require(!StaticMaterial(&geometry, &categories), "Animated geometry entered static streaming");

		TextureStreaming feature;
		Util::Widgets::clicks = { { "Landscape textures on static meshes", true }, { "Alpha-tested static surfaces", true }, { "Emissive static surfaces", true } };
		feature.DrawSettings();
		Require(feature.state->categories == Categories{ true, true, true } && Util::Widgets::clicks.empty(), "UI did not apply all independent category toggles");
		Require(feature.state->epoch == 8 && feature.state->frontier.empty() && feature.state->visited.empty() && !feature.state->completeScan && !feature.state->nextScanMs && feature.state->records[1].consumers.clears == 1, "Category change retained stale scans or pending work identity");
		Require(MenuUI::sections.size() == 3 && std::string(MenuUI::sections[1].id) == "categories", "Optional categories were absent from page navigation");
		feature.Configure(true, 3);
		Require(feature.state->categories == Categories{ true, true, true } && feature.state->epoch == 8, "Legacy enable/mip control reset categories or invalidated a stable scan");
		feature.Configure(true, 3, Categories{ true, true, true });
		Require(feature.state->epoch == 8, "Unchanged categories invalidated an inventory");
		Util::Widgets::clicks = { { "Emissive static surfaces", false } };
		feature.DrawSettings();
		Require(feature.state->categories == Categories{ true, true, false } && feature.state->epoch == 9, "Narrowing coverage did not invalidate an unfinished reduction");
		nlohmann::json saved;
		feature.SaveSettings(saved);
		TextureStreaming loaded;
		loaded.LoadSettings(saved);
		Require(loaded.state->enabled && loaded.state->maximumDrop == 3 && loaded.state->categories == feature.state->categories, "Saved optional selections were not restored");
		auto invalid = saved;
		invalid["Enabled"] = false;
		invalid["Categories"]["water"] = true;
		loaded.LoadSettings(invalid);
		Require(loaded.state->enabled && loaded.state->categories == feature.state->categories, "Invalid settings partially changed streaming configuration");
		originHookReady = false;
		Util::Widgets::clicks = { { "Emissive static surfaces", true } };
		loaded.DrawSettings();
		Require(!loaded.state->categories.emissiveStatics && !Util::disabled, "Unavailable UI changed coverage or leaked ImGui disable state");
		loaded.RestoreDefaultSettings();
		Require(!loaded.state->enabled && loaded.state->maximumDrop == 2 && loaded.state->categories == Categories{}, "Defaults retained optional coverage");
		originHookReady = true;
		saved.erase("Categories");
		feature.LoadSettings(saved);
		Require(feature.state->categories == Categories{}, "An older preset enabled optional categories");
		std::cout << "PASS: static material contracts, water/LOD protection, category UI, persistence and pending-work invalidation\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
