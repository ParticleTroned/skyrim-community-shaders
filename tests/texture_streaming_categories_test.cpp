#include "Features/TextureStreaming/DemandDiagnostics.h"
#include "Features/TextureStreaming/GeometryDemand.h"
#include "Features/TextureStreaming/Settings.h"
#include "TruePBR/MaterialRegistry.h"
#include "asset_load_diagnostics_fixture.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace RE
{
	struct NiPoint3
	{
		float x = 0, y = 0, z = 0;
		float& operator[](unsigned i) { return (&x)[i]; }
		float operator[](unsigned i) const { return (&x)[i]; }
		NiPoint3 operator-(const NiPoint3& other) const { return { x - other.x, y - other.y, z - other.z }; }
	};
	class NiSourceTexture
	{
	public:
		struct Resource
		{
			void* texture = nullptr;
		};
		Resource* rendererTexture = nullptr;
	};
	struct BSShaderMaterial
	{
#include "streaming_features_under_test.h"
		Feature feature = Feature::kDefault;
		Type type = Type::kLighting;
		Feature GetFeature() const { return feature; }
		Type GetType() const { return type; }
	};
	class BSLightingShaderMaterialBase : public BSShaderMaterial
	{
	public:
		int diffuseRenderTargetSourceIndex = -1;
		float materialAlpha = 1, refractionPower = 0;
		struct UVScale
		{
			float x = 1, y = 1;
		};
		std::array<UVScale, 2> texCoordScale;
		std::uint32_t hashKey = 1;
		std::shared_ptr<NiSourceTexture> rimSoftLightingTexture, specularBackLightingTexture;
		std::shared_ptr<NiSourceTexture> diffuseTexture, normalTexture;
	};
	struct BSLightingShaderMaterialGlowmap : BSLightingShaderMaterialBase
	{
		std::shared_ptr<NiSourceTexture> glowTexture;
	};
	struct BSLightingShaderMaterialEnvmap : BSLightingShaderMaterialBase
	{
		std::shared_ptr<NiSourceTexture> envTexture, envMaskTexture;
	};
	struct BSLightingShaderMaterialParallax : BSLightingShaderMaterialBase
	{
		std::shared_ptr<NiSourceTexture> heightTexture;
	};
	struct BSLightingShaderMaterialParallaxOcc : BSLightingShaderMaterialParallax
	{};
	struct BSLightingShaderMaterialMultiLayerParallax : BSLightingShaderMaterialBase
	{
		std::shared_ptr<NiSourceTexture> layerTexture, envTexture, envMaskTexture;
	};
	struct BSLightingShaderMaterialLandscape : BSLightingShaderMaterialBase
	{
		std::uint32_t numLandscapeTextures = 6;
		std::array<std::shared_ptr<NiSourceTexture>, 5> landscapeDiffuseTexture, landscapeNormalTexture;
		std::shared_ptr<NiSourceTexture> terrainOverlayTexture, terrainNoiseTexture;
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
	class BSGeometry
	{
	public:
#include "streaming_geometry_types_under_test.h"
		struct Kind
		{
			Type value = Type::kTriShape;
			std::uint8_t underlying() const { return static_cast<std::uint8_t>(value); }
			bool operator==(Type other) const { return value == other; }
		} kind;
		struct Data
		{
			std::shared_ptr<BSShaderProperty> shaderProperty = std::make_shared<BSShaderProperty>();
			std::shared_ptr<NiAlphaProperty> alphaProperty;
			void* skinInstance = nullptr;
		} data;
		struct ShapeData
		{
			std::uint16_t triangleCount = 1, vertexCount = 3;
		} shapeData;
		const BSGeometry* AsTriShape() const { return kind.value == Type::kTriShape || kind.value == Type::kSubIndexTriShape ? this : nullptr; }
		const ShapeData& GetTrishapeRuntimeData() const { return shapeData; }
		struct Bound
		{
			NiPoint3 center;
			float radius = 100;
		} worldBound;
		struct World
		{
			float scale = 1;
		} world;
		bool controlled = false;
		Kind GetType() const { return kind; }
		const Data& GetGeometryRuntimeData() const { return data; }
		bool GetControllers() const { return controlled; }
	};
}
enum class PBRFlags
{
	InterlayerParallax = 8
};
struct BSLightingShaderMaterialPBR : RE::BSLightingShaderMaterialBase
{
	static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(32);
	struct Flags
	{
		bool interlayer = false;
		bool any(PBRFlags) const { return interlayer; }
	} pbrFlags;
	std::shared_ptr<RE::NiSourceTexture> rmaosTexture, emissiveTexture, displacementTexture, featuresTexture0, featuresTexture1;
};
struct BSLightingShaderMaterialPBRLandscape : RE::BSLightingShaderMaterialBase
{
	static constexpr std::uint32_t NumTiles = 6;
	static const BSLightingShaderMaterialPBRLandscape* GetRegistered(const RE::BSShaderMaterial* material);
	inline static PBRMaterialRegistry<BSLightingShaderMaterialPBRLandscape, bool> All;
	std::uint32_t numLandscapeTextures = 1;
	std::array<std::shared_ptr<RE::NiSourceTexture>, NumTiles> landscapeBaseColorTextures, landscapeNormalTextures;
	std::array<std::shared_ptr<RE::NiSourceTexture>, NumTiles> landscapeDisplacementTextures, landscapeRMAOSTextures;
	std::shared_ptr<RE::NiSourceTexture> terrainOverlayTexture, terrainNoiseTexture;
};
#include "streaming_landscape_lookup_under_test.h"
namespace globals
{
	namespace game
	{
		struct GraphicsState
		{
			std::shared_ptr<RE::NiSourceTexture> defaultTextureBlack = std::make_shared<RE::NiSourceTexture>();
			std::shared_ptr<RE::NiSourceTexture> defaultTextureNormalMap = std::make_shared<RE::NiSourceTexture>();
			GraphicsState& GetRuntimeData() { return *this; }
		} graphics;
		auto* graphicsState = &graphics;
	}
	namespace features
	{
		struct Extended
		{
			bool loaded = true;
			struct Settings
			{
				bool EnableComplexMaterial = true, EnableParallax = true, EnableTerrain = false;
				float ParallaxStrength = 1;
			} settings;
		} extendedMaterials;
		struct Helper
		{
			bool enabled = false;
			bool IsRuntimeEnabled() const { return enabled; }
			struct Slots
			{
				std::array<std::shared_ptr<RE::NiSourceTexture>, 6> parallax;
			};
			std::shared_mutex extendedSlotsMutex;
			std::map<std::uint32_t, Slots> extendedSlots;
		} terrainHelper;
	}
	struct RuntimeState
	{
		bool developerMode = true;
		std::uint32_t frameCount = 20;
		bool IsDeveloperMode() const { return developerMode; }
	} runtimeState;
	auto* state = &runtimeState;
}
std::uint64_t GetTickCount64();
namespace StreamingTextures
{
#include "streaming_demand_under_test.h"
#include "streaming_material_under_test.h"
}
namespace Policy = TextureStreamingPolicy;
#include "streaming_conflict_under_test.h"
constexpr int NO_ERROR = 0;
constexpr int ImGuiSliderFlags_AlwaysClamp = 1, ImGuiSliderFlags_NoInput = 2;
constexpr int ImGuiHoveredFlags_DelayNormal = 1, ImGuiHoveredFlags_AllowWhenDisabled = 2;
namespace ImGui
{
	void Spacing() {}
}
bool simulatedSPSLoaded = false;
unsigned hookInstalls = 0;
std::wstring queriedModule;
bool FixtureModuleHandle(const wchar_t* name)
{
	queriedModule = name;
	return simulatedSPSLoaded && queriedModule == L"SPS.dll";
}
int FixtureLoadDDS() { return 0; }
using DDSLoader = void (*)();
DDSLoader originalDDSLoader = nullptr;
namespace REL
{
	struct VariantID
	{
		VariantID(std::uint64_t, std::uint64_t, std::uint64_t) {}
	};
	template <class T>
	struct Relocation
	{
		explicit Relocation(VariantID) {}
		std::uintptr_t address() const { return 1; }
	};
}
namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{}
	template <class... T>
	void error(const char*, T&&...)
	{}
}
std::atomic<bool> originHookReady{ true };
std::uint64_t GetTickCount64() { return 1000; }
namespace Util
{
	struct HookResult
	{
		int error = 0;
	};
	HookResult AttachDetour(void**, void*)
	{
		++hookInstalls;
		return {};
	}
	bool disabled = false;
	void AddTooltip(const char*, int = 0) {}
	struct DisableGuard
	{
		bool previous;
		explicit DisableGuard(bool value) : previous(disabled) { disabled |= value; }
		~DisableGuard() { disabled = previous; }
	};
	namespace Text
	{
		std::string warning;
		void WrappedWarning(const char* text) { warning = text; }
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
		std::map<std::string, int> sliderEdits;
		bool SliderInt(const char* name, int* value, int, std::uint32_t, const char* = "%d", int = 0)
		{
			const auto found = sliderEdits.find(name);
			if (disabled || found == sliderEdits.end())
				return false;
			*value = found->second;
			sliderEdits.erase(found);
			return true;
		}
		bool Combo(const char*, int*, const char*) { return false; }
	}
}

namespace StreamingTextures::LoadHooks
{
	void Install() {}
	void Configure(bool, const Modes&, const Options&, const Categories&, std::uint32_t) {}
}
namespace StreamingTextures::Preview
{
	void Install() {}
}
namespace MenuUI
{
	struct ToggleChoice
	{
		const char* label;
		bool* value;
		const char* tooltip;
	};
	float ToggleColumnWidth(std::span<const ToggleChoice>) { return 0; }
	void ToggleGrid(const char*, std::span<const ToggleChoice> choices, float)
	{
		for (const auto& choice : choices)
			Util::Widgets::Checkbox(choice.label, choice.value);
	}
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
	bool loaded = true;
	std::string failedLoadedMessage;
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
		bool requestedEnabled = true, enabled = true, externalStreamingConflict = false;
		std::string inventoryFailure;
		StreamingTextures::Modes modes;
		StreamingTextures::Options options;
		Policy::Pressure pressure;
#ifdef DEVBENCH_BRIDGE_ENABLED
		bool debugForceStreaming = false;
#endif
		bool ReductionRequested() const noexcept;
		void UpdateConfigurationDetail();
		unsigned maximumDrop = 2;
		std::uint64_t epoch = 7, completeScan = 12, nextScanMs = 10000;
		std::vector<int> frontier{ 1 }, visited{ 1 }, reducedRecords;
		std::map<int, Record> records{ { 1, {} } };
		std::string detail;
		void UpdatePressureDiagnostics(std::uint64_t) {}
	};
	std::unique_ptr<State> state = std::make_unique<State>();
	void Configure(bool enabled, std::uint32_t maximumDrop, std::optional<StreamingTextures::Categories> categories = std::nullopt, std::optional<StreamingTextures::Modes> modes = std::nullopt, std::optional<StreamingTextures::Options> options = std::nullopt);
#ifdef DEVBENCH_BRIDGE_ENABLED
	void SetDebugForceStreaming(bool enabled);
#endif
	void LoadSettings(nlohmann::json&);
	void SaveSettings(nlohmann::json&);
	void RestoreDefaultSettings();
	void PostPostLoad();
	void DrawSettingsEnabledControl();
	void DrawSettings();
};
#define GetModuleHandleW FixtureModuleHandle
#define LoadDDS FixtureLoadDDS
#include "streaming_startup_under_test.h"
#undef LoadDDS
#undef GetModuleHandleW
#include "streaming_mode_under_test.h"
#include "streaming_settings_under_test.h"
#include "streaming_ui_under_test.h"

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

void CheckCameraIndependentDemand()
{
	using namespace StreamingTextures;
	RE::BSGeometry geometry;
	RE::BSLightingShaderMaterialBase material;
	geometry.data.shaderProperty->material = &material;
	geometry.worldBound.center = { 10000, 0, 0 };
	DemandContext demand;
	demand.eyeCount = 1;
	demand.frame = globals::state->frameCount;
	demand.sampledAtMs = GetTickCount64();
	demand.eyes[0] = { 2000, 2000, 2000, 2000, 1, 1, 0 };
	demand.eyes[1] = demand.eyes[0];
	demand.forward[0] = { 1, 0, 0 };
	const auto front = RequiredEdge(&geometry, demand);
	material.texCoordScale[0].x = 0.25f;
	Require(RequiredEdge(&geometry, demand) == 4 * front, "Material UV magnification discarded required detail");
	material.texCoordScale[0].x = 1;
	material.texCoordScale[1].y = -0.5f;
	Require(RequiredEdge(&geometry, demand) == 2 * front, "Second UV buffer or mirrored magnification was ignored");
	for (float invalid : { 0.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
		material.texCoordScale[1].y = invalid;
		Require(!std::isfinite(RequiredEdge(&geometry, demand)), "Invalid UV scale authorized a reduction");
	}
	material.texCoordScale[1].y = 1;
	Require(TextureStreamingPolicy::DesiredDrop(4096, 13, front, 2) == 2, "A distant static bound could not request reduced mips");
	geometry.world.scale = 8;
	Require(RequiredEdge(&geometry, demand) == front, "World bounds were incorrectly scaled a second time");
	geometry.worldBound.center.x = -10000;
	const auto behind = RequiredEdge(&geometry, demand);
	Require(std::isfinite(front) && front > 0 && front == behind,
		"Behind-camera geometry forced full detail instead of the same distance demand");
	demand.forward[0] = { -1, 0, 0 };
	Require(RequiredEdge(&geometry, demand) == behind, "Turning the camera changed the rotation-safe demand");
	demand.eyeCount = 2;
	demand.positions[1] = { -8000, 0, 0 };
	Require(RequiredEdge(&geometry, demand) > behind, "The nearer VR eye was ignored");
	demand.positions[1] = { -10000, 0, 0 };
	Require(!std::isfinite(RequiredEdge(&geometry, demand)), "An eye inside the movement horizon entered reduced detail");
	demand.eyeCount = 3;
	Require(!std::isfinite(RequiredEdge(&geometry, demand)), "An invalid eye count accessed the fixed stereo arrays");
	demand.eyeCount = 1;
	geometry.worldBound.radius = -1;
	Require(!std::isfinite(RequiredEdge(&geometry, demand)), "An invalid geometry bound entered reduced detail");
	geometry.worldBound.radius = 100;
	demand.frame -= 2;
	Require(!std::isfinite(RequiredEdge(&geometry, demand)), "A stale renderer frame authorized a detail reduction");
	demand.frame = globals::state->frameCount;

#ifdef DEVBENCH_BRIDGE_ENABLED
	DemandDiagnostics observations;
	DemandDiagnostics::Sample sample;
	sample.requiredEdge = front;
	sample.desiredDrop = 2;
	sample.context = demand;
	observations.Observe(sample, std::string(512, 't'), std::string(256, 'g'));
	observations.Observe(sample, "later", "later");
	Require(observations.reasons[2] == 2 && observations.texturePaths[2] == std::string(256, 't') &&
				observations.geometryNames[2] == std::string(128, 'g') && observations.samples[2].requiredEdge == front,
		"Demand diagnostics lost bounded representative values");
	sample.requiredEdge = std::numeric_limits<double>::infinity();
	observations.Observe(sample, "invalid", "invalid");
	Require(observations.reasons[0] == 1, "Invalid demand was confused with full detail");
	sample.requiredEdge = 8192;
	sample.desiredDrop = 0;
	observations.Observe(sample, "full", "full");
	Require(observations.reasons[1] == 1, "Valid full-detail demand was confused with an invalid renderer sample");
#endif
	demand.sampledAtMs -= 251;
	Require(!std::isfinite(RequiredEdge(&geometry, demand)), "Stale eye demand authorized a detail reduction");
}

void CheckSpecialSampling()
{
	using namespace StreamingTextures;
	RE::BSGeometry geometry;
	std::uint32_t categories = 0;
	RE::BSLightingShaderMaterialBase ordinary;
	geometry.data.shaderProperty->material = &ordinary;
	ordinary.diffuseTexture = ordinary.rimSoftLightingTexture = std::make_shared<RE::NiSourceTexture>();
	Require(!StaticMaterial(&geometry) && !IsStreamingSlot(&ordinary, ordinary.diffuseTexture.get()), "An auxiliary alias entered diffuse streaming");
	ordinary.rimSoftLightingTexture.reset();
	Require(StaticMaterial(&geometry), "Unaliased ordinary diffuse was blocked");
	ordinary.feature = static_cast<RE::BSShaderMaterial::Feature>(99);
	Require(!StaticMaterial(&geometry), "Unknown material sampling was assumed ordinary");
	RE::BSLightingShaderMaterialEnvmap env;
	env.feature = RE::BSShaderMaterial::Feature::kEnvironmentMap;
	geometry.data.shaderProperty->material = &env;
	env.envMaskTexture = std::make_shared<RE::NiSourceTexture>();
	Require(StaticMaterial(&geometry, &categories) && (categories & Parallax) && !Categories{ false, false, false, false, false }.Allows(categories), "Complex-material parallax bypassed disabled coverage");
	globals::features::extendedMaterials.settings.EnableComplexMaterial = false;
	Require(StaticMaterial(&geometry), "An ordinary environment material was blocked with complex sampling off");
	env.diffuseTexture = env.envMaskTexture;
	Require(!StaticMaterial(&geometry), "An environment mask alias entered diffuse streaming");
	globals::features::extendedMaterials.settings.EnableComplexMaterial = true;
	BSLightingShaderMaterialPBR pbr;
	pbr.feature = BSLightingShaderMaterialPBR::FEATURE;
	geometry.data.shaderProperty->material = &pbr;
	pbr.displacementTexture = globals::game::graphics.defaultTextureBlack;
	pbr.emissiveTexture = globals::game::graphics.defaultTextureBlack;
	Require(StaticMaterial(&geometry, &categories) && !categories, "PBR default placeholders were mistaken for active maps");
	pbr.emissiveTexture = std::make_shared<RE::NiSourceTexture>();
	Require(StaticMaterial(&geometry, &categories) && categories == EmissiveStatics, "PBR emission bypassed its coverage toggle");
	pbr.displacementTexture = std::make_shared<RE::NiSourceTexture>();
	Require(StaticMaterial(&geometry, &categories) && (categories & Parallax) && !Categories{ false, false, false, false, false }.Allows(categories), "PBR displacement bypassed disabled coverage");
	pbr.displacementTexture = globals::game::graphics.defaultTextureBlack;
	pbr.diffuseTexture = pbr.rmaosTexture = std::make_shared<RE::NiSourceTexture>();
	Require(!StaticMaterial(&geometry), "A PBR auxiliary alias entered streaming");
	RE::BSLightingShaderMaterialLandscape terrain;
	terrain.feature = RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
	geometry.data.shaderProperty->material = &terrain;
	globals::features::extendedMaterials.settings.EnableTerrain = true;
	Require(StaticMaterial(&geometry, &categories) && categories == (Terrain | Parallax) && !Categories{ false, false, false, true, false }.Allows(categories), "Native terrain parallax bypassed its separate consent");
	globals::features::extendedMaterials.settings.EnableTerrain = false;
	auto& helper = globals::features::terrainHelper;
	helper.enabled = true;
	helper.extendedSlots[terrain.hashKey].parallax[0] = globals::game::graphics.defaultTextureNormalMap;
	Require(StaticMaterial(&geometry), "A Terrain Helper placeholder blocked ordinary terrain");
	helper.extendedSlots[terrain.hashKey].parallax[0] = std::make_shared<RE::NiSourceTexture>();
	Require(StaticMaterial(&geometry, &categories) && categories == (Terrain | Parallax), "Terrain Helper parallax did not require both coverage choices");
	terrain.landscapeNormalTexture[0] = helper.extendedSlots[terrain.hashKey].parallax[0];
	Require(!StaticMaterial(&geometry) && !IsStreamingSlot(&terrain, terrain.landscapeNormalTexture[0].get()), "A terrain height alias entered color streaming");
	terrain.landscapeNormalTexture[0].reset();
	helper.enabled = false;
	helper.extendedSlots.clear();
	Require(StaticMaterial(&geometry), "Disabled Terrain Helper continued to block terrain");
}

void CheckParallaxCoverage()
{
	using namespace StreamingTextures;
	Categories allowed;
	Require(allowed.parallax && allowed.terrain && allowed.landscapeStatics && allowed.alphaTestedStatics && allowed.emissiveStatics,
		"All optional texture coverage should default on");
	allowed = ParseCategories(nlohmann::json::object());
	allowed.parallax = true;
	Require(allowed.Allows(Parallax) && !allowed.Allows(Parallax | Terrain), "Parallax implicitly enabled terrain");
	allowed.terrain = true;
	Require(allowed.Allows(Parallax | Terrain), "Combined terrain/parallax consent was rejected");
	Require(ParseCategories(SerializeCategories(allowed)) == allowed, "Parallax consent did not round-trip");
	Require(!ParseCategories({ { "terrain", true } }).parallax, "An older category selection enabled parallax");
	RE::BSGeometry geometry;
	for (const auto feature : { RE::BSShaderMaterial::Feature::kParallax, RE::BSShaderMaterial::Feature::kParallaxOcc }) {
		RE::BSLightingShaderMaterialParallaxOcc material;
		material.feature = feature;
		material.diffuseTexture = std::make_shared<RE::NiSourceTexture>();
		material.normalTexture = std::make_shared<RE::NiSourceTexture>();
		material.heightTexture = std::make_shared<RE::NiSourceTexture>();
		geometry.data.shaderProperty->material = &material;
		geometry.data.shaderProperty->flags.value = static_cast<std::uint64_t>(RE::BSShaderProperty::EShaderPropertyFlag::kParallax);
		std::uint32_t required = 0;
		Require(StaticMaterial(&geometry, &required) && required == Parallax, "Known parallax layout did not classify color slots");
		Require(IsStreamingSlot(&material, material.diffuseTexture.get()) && IsStreamingSlot(&material, material.normalTexture.get()) &&
					!IsStreamingSlot(&material, material.heightTexture.get()),
			"Parallax consent admitted height pixels");
		RE::NiSourceTexture::Resource shared{ &material };
		material.diffuseTexture->rendererTexture = material.heightTexture->rendererTexture = &shared;
		Require(!StaticMaterial(&geometry), "Distinct texture wrappers sharing height pixels escaped protection");
	}
	RE::BSLightingShaderMaterialMultiLayerParallax material;
	material.feature = RE::BSShaderMaterial::Feature::kMultilayerParallax;
	material.layerTexture = std::make_shared<RE::NiSourceTexture>();
	material.diffuseTexture = std::make_shared<RE::NiSourceTexture>();
	geometry.data.shaderProperty->material = &material;
	std::uint32_t required = 0;
	Require(StaticMaterial(&geometry, &required) && required == Parallax && !IsStreamingSlot(&material, material.layerTexture.get()), "Inner-layer texture escaped protection");
	material.diffuseTexture = material.layerTexture;
	Require(!StaticMaterial(&geometry), "An inner layer alias entered color streaming");
}

void CheckStreamingConflict()
{
	TextureStreaming blocked;
	originHookReady = false;
	simulatedSPSLoaded = true;
	blocked.PostPostLoad();
	Require(blocked.loaded && !blocked.state->enabled && blocked.state->externalStreamingConflict && !originHookReady && !hookInstalls,
		"SPS detection installed a competing hook or left native streaming active");
	Require(queriedModule == L"SPS.dll" && blocked.state->detail.find("Disable the SPS mod and restart") != std::string::npos,
		"Conflict warning did not identify the loaded plugin or give recovery guidance");
	blocked.Configure(true, 2, std::nullopt, StreamingTextures::Modes{ false, true });
	Require(!blocked.state->enabled && blocked.state->modes.continuous && blocked.state->detail == streamingConflictWarning,
		"Continuous mode bypassed the loaded-plugin conflict or erased its warning");
#ifdef DEVBENCH_BRIDGE_ENABLED
	blocked.SetDebugForceStreaming(false);
	Require(blocked.state->detail == streamingConflictWarning && !blocked.state->enabled && blocked.state->requestedEnabled,
		"Legacy trigger alias erased the SPS warning or bypassed inhibition");
	blocked.Configure(true, 2, std::nullopt, StreamingTextures::Modes{ false, true });
#endif

	Util::Widgets::clicks = { { "Enabled", false }, { "On pressure", true } };
	MenuUI::selected = "quality";
	blocked.DrawSettingsEnabledControl();
	blocked.DrawSettings();
	Require(!blocked.state->enabled && blocked.state->requestedEnabled && !blocked.state->modes.onPressure &&
				Util::Text::warning == streamingConflictWarning && !Util::disabled,
		"Blocked UI bypassed SPS, hid its warning or leaked disabled state");
	Util::Widgets::clicks.clear();
	blocked.RestoreDefaultSettings();
	Require(!blocked.state->enabled && blocked.state->modes == StreamingTextures::Modes{},
		"Restoring defaults bypassed the conflict or destroyed trigger preferences");
	simulatedSPSLoaded = false;
	TextureStreaming native;
	native.PostPostLoad();
	Require(native.loaded && native.state->enabled && !native.state->externalStreamingConflict && originHookReady && hookInstalls == 1,
		"Removing SPS and restarting did not permit native streaming");
	native.PostPostLoad();
#ifdef DEVBENCH_BRIDGE_ENABLED
	Require(CSX::Diagnostics::Stutters::AssetLoadHooks::ddsAvailable, "DDS hook availability must reach diagnostics");
#endif
	Require(hookInstalls == 1, "Repeated startup installed the DDS hook twice");

	TextureStreaming rejected;
	rejected.state->inventoryFailure = "Invalid world child inventory; texture streaming suspended until restart";
	rejected.Configure(true, 2, std::nullopt, StreamingTextures::Modes{ false, true });
	Require(!rejected.state->enabled && rejected.state->requestedEnabled && rejected.state->detail == rejected.state->inventoryFailure,
		"Configuration re-enabled a rejected inventory or discarded its failure reason");
#ifdef DEVBENCH_BRIDGE_ENABLED
	rejected.SetDebugForceStreaming(false);
	Require(!rejected.state->enabled, "The legacy streaming alias bypassed inventory suspension");
#endif
	Util::Widgets::clicks = { { "Enabled", false }, { "On pressure", true } };
	rejected.DrawSettingsEnabledControl();
	rejected.DrawSettings();
	Require(!rejected.state->enabled && rejected.state->requestedEnabled && Util::Text::warning == rejected.state->inventoryFailure && !Util::disabled,
		"Suspended inventory controls hid the failure or bypassed the session latch");
	Util::Widgets::clicks.clear();
	rejected.RestoreDefaultSettings();
	Require(!rejected.state->enabled && rejected.state->detail == rejected.state->inventoryFailure,
		"Restoring settings bypassed inventory suspension");
}

int main()
{
	try {
		CheckStreamingConflict();
		CheckSpecialSampling();
		CheckParallaxCoverage();
		CheckCameraIndependentDemand();
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
		Require(StaticMaterial(&geometry, &categories) && categories == EmissiveStatics, "An inactive alpha property blocked an opaque static");
		geometry.data.alphaProperty->testing = true;
		geometry.data.alphaProperty->controlled = true;
		Require(!StaticMaterial(&geometry, &categories), "Animated alpha contract entered streaming");
		geometry.data.alphaProperty.reset();
		property.flags.value = 0;
		for (const auto feature : { RE::BSShaderMaterial::Feature::kMultiTexLand,
				 RE::BSShaderMaterial::Feature::kLODLand, RE::BSShaderMaterial::Feature::kLODObjectsHD, RE::BSShaderMaterial::Feature::kFaceGen }) {
			material.feature = feature;
			Require(!StaticMaterial(&geometry, &categories), "Special, terrain, character or LOD material entered optional coverage");
		}
		material.feature = RE::BSShaderMaterial::Feature::kDefault;
		material.type = RE::BSShaderMaterial::Type::kWater;
		Require(!StaticMaterial(&geometry, &categories), "Water material entered static streaming");
		material.type = RE::BSShaderMaterial::Type::kLighting;
		for (const auto flag : { Flag::kLODLandscape, Flag::kLODObjects, Flag::kHDLODObjects, Flag::kBillboard, Flag::kParallax,
				 Flag::kMultiTextureLandscape, Flag::kTreeAnim, Flag::kSkinned, Flag::kProjectedUV }) {
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

		geometry.controlled = false;
		material.feature = RE::BSShaderMaterial::Feature::kDefault;
		geometry.kind.value = RE::BSGeometry::Type::kSubIndexTriShape;
		Require(StaticMaterial(&geometry, &categories) == &material, "Segmented static geometry was incorrectly rejected");
		geometry.kind.value = RE::BSGeometry::Type::kMultiIndexTriShape;
		RE::BSLightingShaderMaterialEnvmap environment;
		environment.feature = RE::BSShaderMaterial::Feature::kEnvironmentMap;
		property.material = &environment;
		Require(StaticMaterial(&geometry, &categories) == &environment, "A static multi-index environment material blocked ordinary diffuse and normal maps");
		property.material = &material;
		geometry.kind.value = RE::BSGeometry::Type::kTriShape;
		property.flags.value = static_cast<std::uint64_t>(Flag::kAssumeShadowmask);
		Require(StaticMaterial(&geometry, &categories) == &material, "A shadow-only flag blocked ordinary texture sampling");
		for (const auto flag : { Flag::kRemappableTextures, Flag::kEnvmapLightFade, Flag::kEnvMap, Flag::kVertexAlpha, Flag::kVertexLighting,
				 Flag::kDecal, Flag::kDynamicDecal, Flag::kTwoSided, Flag::kSoftLighting, Flag::kBackLighting }) {
			property.flags.value = static_cast<std::uint64_t>(flag);
			Require(StaticMaterial(&geometry, &categories) == &material, "Ordinary texture metadata was mistaken for unsupported sampling");
		}
		property.flags.value = static_cast<std::uint64_t>(Flag::kRemappableTextures) | static_cast<std::uint64_t>(Flag::kEnvmapLightFade);
		Require(StaticMaterial(&geometry, &categories) == &material, "Common remapping and environment fade metadata blocked a static material");
		property.flags.value |= static_cast<std::uint64_t>(Flag::kParallax);
		Require(!StaticMaterial(&geometry, &categories), "Permitting metadata flags bypassed parallax protection");
		property.flags.value = 0;
		for (const auto type : { RE::BSGeometry::Type::kDynamicTriShape, RE::BSGeometry::Type::kSubIndexLandTriShape,
				 RE::BSGeometry::Type::kMeshLODTriShape, RE::BSGeometry::Type::kLODMultiIndexTriShape }) {
			geometry.kind.value = type;
			Require(!StaticMaterial(&geometry, &categories), "Dynamic, terrain or LOD geometry entered streaming");
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		MaterialInspection inspection;
		geometry.kind.value = RE::BSGeometry::Type::kDynamicTriShape;
		Require(!StaticMaterial(&geometry, &categories, &inspection) && inspection.reason == MaterialRejection::GeometryType && inspection.geometryType == 4,
			"Geometry rejection did not preserve the actual runtime type");
		geometry.kind.value = RE::BSGeometry::Type::kTriShape;
		property.flags.value = static_cast<std::uint64_t>(Flag::kProjectedUV) | static_cast<std::uint64_t>(Flag::kSpecular);
		Require(!StaticMaterial(&geometry, &categories, &inspection) && inspection.reason == MaterialRejection::ShaderFlags &&
					inspection.disallowedFlags == static_cast<std::uint64_t>(Flag::kProjectedUV) && inspection.materialType == static_cast<std::uint32_t>(material.type),
			"Shader rejection lost the offending bits or material type");
		MaterialDiagnostics observations;
		observations.Observe(inspection, std::string(256, 'x'));
		observations.Observe(inspection, "later sample");
		const auto reason = static_cast<std::size_t>(MaterialRejection::ShaderFlags);
		Require(observations.reasons[reason] == 2 && observations.geometryTypes[3] == 2 && observations.disallowedFlagBits[23] == 2 &&
					observations.geometryNames[reason] == std::string(128, 'x'),
			"Diagnostics were not cumulative, bounded and representative");
		property.flags.value = 0;
		Require(StaticMaterial(&geometry, &categories, &inspection) && inspection.reason == MaterialRejection::None && inspection.disallowedFlags == 0,
			"Successful inspection retained a previous rejection");
		geometry.data.skinInstance = &material;
		Require(!StaticMaterial(&geometry, &categories, &inspection) && inspection.reason == MaterialRejection::Skin && inspection.materialType == UINT32_MAX,
			"Early rejection invented unavailable material metadata");
		geometry.data.skinInstance = nullptr;
#else
		geometry.kind.value = RE::BSGeometry::Type::kTriShape;
#endif

		RE::BSLightingShaderMaterialLandscape terrain;
		terrain.feature = RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
		geometry.kind.value = RE::BSGeometry::Type::kSubIndexLandTriShape;
		property.material = &terrain;
		terrain.landscapeDiffuseTexture[0] = std::make_shared<RE::NiSourceTexture>();
		terrain.landscapeNormalTexture[4] = std::make_shared<RE::NiSourceTexture>();
		terrain.terrainNoiseTexture = std::make_shared<RE::NiSourceTexture>();
		Require(StaticMaterial(&geometry, &categories) == &terrain && categories == Terrain, "Near terrain was not independently classified");
		Require(IsStreamingSlot(&terrain, terrain.landscapeDiffuseTexture[0].get()) &&
					IsStreamingSlot(&terrain, terrain.landscapeNormalTexture[4].get()) && !IsStreamingSlot(&terrain, terrain.terrainNoiseTexture.get()),
			"Terrain layer slots or auxiliary protection were incomplete");
		terrain.numLandscapeTextures = 7;
		Require(!StaticMaterial(&geometry), "An invalid layer count escaped the terrain boundary");
		terrain.numLandscapeTextures = 6;
		terrain.terrainOverlayTexture = terrain.landscapeDiffuseTexture[0];
		Require(!StaticMaterial(&geometry), "An overlay aliased a streamed terrain layer");
		terrain.terrainOverlayTexture.reset();
		{
			BSLightingShaderMaterialPBRLandscape pbrTerrain;
			pbrTerrain.feature = RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
			pbrTerrain.landscapeBaseColorTextures[0] = std::make_shared<RE::NiSourceTexture>();
			pbrTerrain.landscapeNormalTextures[0] = std::make_shared<RE::NiSourceTexture>();
			BSLightingShaderMaterialPBRLandscape::All.Register(&pbrTerrain);
			property.material = &pbrTerrain;
			property.flags.value = static_cast<std::uint64_t>(Flag::kVertexLighting) | static_cast<std::uint64_t>(Flag::kMultiTextureLandscape);
			Require(StaticMaterial(&geometry, &categories) == &pbrTerrain && categories == Terrain &&
						IsStreamingSlot(&pbrTerrain, pbrTerrain.landscapeBaseColorTextures[0].get()) &&
						IsStreamingSlot(&pbrTerrain, pbrTerrain.landscapeNormalTextures[0].get()),
				"PBR terrain's first layer was read using the vanilla texture layout");
			pbrTerrain.numLandscapeTextures = 6;
			for (std::uint32_t layer = 1; layer < 5; ++layer) {
				pbrTerrain.landscapeBaseColorTextures[layer] = globals::game::graphics.defaultTextureBlack;
				pbrTerrain.landscapeNormalTextures[layer] = globals::game::graphics.defaultTextureNormalMap;
				pbrTerrain.landscapeDisplacementTextures[layer] = globals::game::graphics.defaultTextureBlack;
			}
			pbrTerrain.landscapeBaseColorTextures[5] = std::make_shared<RE::NiSourceTexture>();
			pbrTerrain.landscapeNormalTextures[5] = std::make_shared<RE::NiSourceTexture>();
			pbrTerrain.landscapeDisplacementTextures[0] = std::make_shared<RE::NiSourceTexture>();
			pbrTerrain.landscapeRMAOSTextures[5] = std::make_shared<RE::NiSourceTexture>();
			Require(StaticMaterial(&geometry, &categories) == &pbrTerrain && categories == (Terrain | Parallax) &&
						IsStreamingSlot(&pbrTerrain, pbrTerrain.landscapeBaseColorTextures[5].get()) &&
						IsStreamingSlot(&pbrTerrain, pbrTerrain.landscapeNormalTextures[5].get()) &&
						!IsStreamingSlot(&pbrTerrain, pbrTerrain.landscapeDisplacementTextures[0].get()) &&
						!IsStreamingSlot(&pbrTerrain, pbrTerrain.landscapeRMAOSTextures[5].get()),
				"PBR terrain lost its final layer, parallax consent or auxiliary protection");
			pbrTerrain.landscapeRMAOSTextures[5] = pbrTerrain.landscapeBaseColorTextures[0];
			Require(!StaticMaterial(&geometry), "PBR terrain streamed an auxiliary alias");
			pbrTerrain.landscapeRMAOSTextures[5].reset();
			RE::NiSourceTexture::Resource sharedTerrainResource;
			int terrainResourceIdentity;
			sharedTerrainResource.texture = &terrainResourceIdentity;
			pbrTerrain.landscapeDisplacementTextures[0]->rendererTexture = &sharedTerrainResource;
			pbrTerrain.landscapeNormalTextures[5]->rendererTexture = &sharedTerrainResource;
			Require(!StaticMaterial(&geometry), "PBR terrain streamed a height-map resource alias");
			pbrTerrain.landscapeNormalTextures[5]->rendererTexture = nullptr;
			pbrTerrain.numLandscapeTextures = 7;
			Require(!StaticMaterial(&geometry), "PBR terrain accepted an out-of-range layer count");
			pbrTerrain.numLandscapeTextures = 6;
			BSLightingShaderMaterialPBRLandscape::All.Unregister(&pbrTerrain);
			Require(!StaticMaterial(&geometry), "An unregistered PBR landscape was cast as vanilla terrain");
			property.material = &terrain;
			property.flags.value = 0;
		}
		property.flags.value = static_cast<std::uint64_t>(Flag::kLODLandscape);
		Require(!StaticMaterial(&geometry), "Terrain opt-in admitted LOD");
		property.flags.value = 0;
		Require(PathCategories("textures\\landscape\\dirt.dds", Terrain) == 0 &&
					PathCategories("textures\\landscape\\dirt.dds") == LandscapeStatics,
			"Terrain wrongly required the unrelated landscape-mesh toggle");
		for (const auto* protectedPath : { "textures\\armor\\a.dds", "textures\\books\\a.dds", "textures\\fonts\\a.dds",
				 "textures\\interface\\a.dds", "textures\\terrain\\a.dds", "textures\\world\\maps\\a.dds" })
			Require(!PathCategories(protectedPath, Terrain), "Expanded coverage bypassed a protected path");
		MenuUI::selected = "categories";
		TextureStreaming terrainControl;
		Util::Widgets::clicks = { { "Terrain", false } };
		terrainControl.DrawSettings();
		Require(!terrainControl.state->categories.terrain && terrainControl.state->categories.parallax && terrainControl.state->categories.landscapeStatics, "Terrain UI was not independent");
		Util::Widgets::clicks = { { "Parallax", false } };
		terrainControl.DrawSettings();
		Require(!terrainControl.state->categories.parallax && !terrainControl.state->categories.terrain, "Parallax UI changed terrain consent");
		nlohmann::json parallaxSaved;
		terrainControl.SaveSettings(parallaxSaved);
		TextureStreaming parallaxLoaded;
		parallaxLoaded.LoadSettings(parallaxSaved);
		Require(!parallaxLoaded.state->categories.parallax && !parallaxLoaded.state->categories.terrain, "Saved disabled coverage was overwritten by defaults");
		TextureStreaming feature;
		feature.state->categories = { false, false, false, true, true };
		Util::Widgets::clicks = { { "Landscape", true }, { "Cutouts", true }, { "Emissive", true } };
		feature.DrawSettings();
		Require(feature.state->categories == Categories{ true, true, true, true, true } && Util::Widgets::clicks.empty(), "UI did not apply all independent category toggles");
		Require(feature.state->epoch == 8 && feature.state->frontier.empty() && feature.state->visited.empty() && !feature.state->completeScan && !feature.state->nextScanMs && feature.state->records[1].consumers.clears == 1, "Category change retained stale scans or pending work identity");
		Require(MenuUI::sections.size() == 3 && std::string(MenuUI::sections[1].id) == "categories", "Optional categories were absent from page navigation");
		feature.Configure(true, 3);
		Require(feature.state->categories == Categories{ true, true, true, true, true } && feature.state->epoch == 9, "Mip control reset categories or retained stale pending work");
		feature.Configure(true, 3, Categories{ true, true, true, true, true });
		Require(feature.state->epoch == 9, "Unchanged categories invalidated an inventory");
		Util::Widgets::clicks = { { "Emissive", false } };
		feature.DrawSettings();
		Require(feature.state->categories == Categories{ true, true, false, true, true } && feature.state->epoch == 10, "Narrowing coverage did not invalidate an unfinished reduction");
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
		Util::Widgets::clicks = { { "Emissive", true } };
		loaded.DrawSettings();
		Require(!loaded.state->categories.emissiveStatics && !Util::disabled, "Unavailable UI changed coverage or leaked ImGui disable state");
		loaded.RestoreDefaultSettings();
		Require(loaded.state->enabled && loaded.state->modes == Modes{} && loaded.state->maximumDrop == 2 && loaded.state->categories == Categories{}, "Reset did not restore default coverage");
		originHookReady = true;
		saved.erase("Categories");
		feature.LoadSettings(saved);
		Require(feature.state->categories == Categories{}, "Absent saved categories did not use default coverage");
		TextureStreaming master;
		master.Configure(true, 3, std::nullopt, Modes{ false, true }, Options{ 2048, false, 1024 });
		Util::Widgets::clicks = { { "Enabled", false } };
		master.DrawSettingsEnabledControl();
		master.SaveSettings(saved);
		Require(!master.state->enabled && !saved.at("Enabled").get<bool>() && master.state->modes.continuous,
			"Master toggle did not disable streaming while retaining continuous mode");
		TextureStreaming restoredMaster;
		restoredMaster.LoadSettings(saved);
		Require(!restoredMaster.state->enabled && restoredMaster.state->modes.continuous && restoredMaster.state->options.maximumSize == 1024,
			"Disabled master and tuning did not survive reload");
		Util::Widgets::clicks = { { "Enabled", true } };
		restoredMaster.DrawSettingsEnabledControl();
		Require(restoredMaster.state->enabled && restoredMaster.state->modes.continuous && restoredMaster.state->maximumDrop == 3,
			"Re-enabling lost detailed tuning");
		TextureStreaming mode;
		Require(mode.state->enabled && mode.state->modes == Modes{} && !mode.state->ReductionRequested(), "Default pressure policy activated at healthy memory");
		mode.state->pressure.conserving = true;
		Require(mode.state->ReductionRequested(), "Default pressure trigger did not activate reductions");
		const auto initialEpoch = mode.state->epoch;
		Util::Widgets::clicks = { { "On pressure", false }, { "Always stream", true } };
		globals::state->developerMode = false;
		MenuUI::selected = "quality";
		mode.DrawSettings();
		Require(mode.state->enabled && mode.state->modes == Modes{ false, true } && mode.state->epoch == initialEpoch + 1,
			"Info controls failed to select independent continuous mode or invalidate pending work");
		mode.state->pressure.conserving = false;
		Require(mode.state->ReductionRequested(), "Continuous mode depended on real pressure");
		MenuUI::selected = "status";
		mode.DrawSettings();
		Require(mode.state->ReductionRequested(), "Changing UI pages cleared continuous mode");
		mode.SaveSettings(saved);
		TextureStreaming continuous;
		continuous.LoadSettings(saved);
		Require(continuous.state->modes == Modes{ false, true } && continuous.state->ReductionRequested(), "Continuous mode did not survive saving/reloading");
		Util::Widgets::clicks = { { "Always stream", false } };
		MenuUI::selected = "quality";
		mode.DrawSettings();
		mode.state->pressure.conserving = true;
		Require(mode.state->enabled && !mode.state->ReductionRequested(), "Turning both triggers off retained distance reductions or disabled the independent cap");
		mode.Configure(true, 2);
		Require(mode.state->modes == Modes{ false, false } && !mode.state->ReductionRequested(), "Master enable changed trigger preferences");
		mode.Configure(false, 2);
		Require(!mode.state->requestedEnabled && mode.state->modes == Modes{ false, false }, "Master disable changed trigger preferences");
		mode.LoadSettings(saved = { { "Enabled", false } });
		Require(!mode.state->enabled, "An explicit disabled older preset was silently enabled");
		mode.LoadSettings(saved = nlohmann::json::object());
		Require(mode.state->modes == Modes{}, "Missing saved settings did not use the pressure-on default");
		mode.state->pressure.conserving = false;
		originHookReady = false;
		Util::Widgets::clicks = { { "Always stream", true } };
		MenuUI::selected = "quality";
		mode.DrawSettings();
		Require(!mode.state->modes.continuous && !Util::disabled, "Unavailable Info controls changed modes or leaked disabled state");
		originHookReady = true;
		mode.Configure(true, 2, std::nullopt, std::nullopt, Options{ 2048, false, 1024 });
		mode.SaveSettings(saved);
		Require(saved.at("Options") == SerializeOptions(Options{ 2048, false, 1024 }), "New options were not saved");
		continuous.LoadSettings(saved);
		Require(continuous.state->options == mode.state->options, "New options were not loaded");
		const auto revision = mode.state->epoch;
		auto invalidOptions = saved;
		invalidOptions["Options"]["maximumSize"] = 1536;
		mode.LoadSettings(invalidOptions);
		Require(mode.state->epoch == revision && mode.state->options.maximumSize == 1024, "Invalid cap partially mutated configuration");
		mode.RestoreDefaultSettings();
		Require(mode.state->options == Options{}, "Defaults retained reserve, remembered toggle or size cap");
		mode.SaveSettings(saved);
		auto badModes = saved;
		badModes["Modes"]["continuous"] = 1;
		mode.LoadSettings(badModes);
		Require(mode.state->modes == Modes{}, "Malformed saved modes partially mutated state");
		for (const auto& value : { nlohmann::json(nullptr), nlohmann::json::object(), nlohmann::json{ { "onPressure", true }, { "continuous", 1 } },
				 nlohmann::json{ { "onPressure", true }, { "continuous", false }, { "unknown", true } } }) {
			bool rejected = false;
			try {
				(void)ParseModes(value);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Require(rejected, "Malformed modes escaped boundary validation");
		}
		bool rejected = false;
		try {
			mode.Configure(false, 2, std::nullopt, Modes{});
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		Require(!rejected && !mode.state->enabled && mode.state->modes == Modes{}, "Disabled master failed to retain trigger preferences");
		mode.Configure(true, 2);
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(!ParseDebugForceStreaming({ { "action", "texture_streaming" } }), "Omitted legacy alias mutated modes");
		Require(ParseDebugForceStreaming({ { "action", "texture_streaming" }, { "debugForceStreaming", true } }) == true, "Legacy alias lost its boolean");
		mode.SetDebugForceStreaming(true);
		Require(mode.state->modes.continuous && mode.state->ReductionRequested(), "Legacy alias did not select continuous mode");
		mode.SaveSettings(saved);
		continuous.LoadSettings(saved);
		Require(continuous.state->modes.continuous, "Legacy alias diverged from normal saved continuous mode");
		mode.Configure(false, 2);
		rejected = false;
		try {
			mode.SetDebugForceStreaming(true);
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		Require(rejected && !mode.state->enabled, "Disabled streaming admitted a legacy continuous request");
#endif
		mode.Configure(true, 2);
		MenuUI::selected = "quality";
		Util::Widgets::sliderEdits = { { "Keep free", INT_MAX }, { "Maximum mip levels removed", INT_MAX } };
		mode.DrawSettings();
		Require(mode.state->options.reserveMiB == Util::TextureMemoryPolicy::MaximumReserveMiB && mode.state->maximumDrop == Policy::MaximumDrop,
			"Out-of-range menu input bypassed maximum bounds");
		Util::Widgets::sliderEdits = { { "Keep free", INT_MIN }, { "Maximum mip levels removed", INT_MIN } };
		mode.DrawSettings();
		Require(mode.state->options.reserveMiB == Util::TextureMemoryPolicy::ReserveStepMiB && mode.state->maximumDrop == 1,
			"Out-of-range menu input bypassed minimum bounds");
		std::cout << "PASS: static material protection, terrain slots, compact Info controls, independent triggers, persistence and pending-work invalidation\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
