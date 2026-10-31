#include "Features/MeshBlendingLandscape.h"
#include "Features/MeshBlendingLandscapeSnapshot.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Only the engine boundary is simulated; capture, publication, draw validation,
// recording replay and draw admission are extracted without rewriting their logic.
namespace RE
{
	using BSFixedString = std::string;
	struct NiRTTI
	{};
	inline NiRTTI integerRTTI;
	inline NiRTTI lightingRTTI;
	struct Address
	{
		std::uintptr_t value;
		std::uintptr_t address() const { return value; }
	};
	template <class T>
	using NiPointer = std::shared_ptr<T>;
	struct NiExtraData : std::enable_shared_from_this<NiExtraData>
	{
		BSFixedString name;
		virtual ~NiExtraData() = default;
		virtual const NiRTTI* GetRTTI() const = 0;
	};
	struct NiIntegersExtraData : NiExtraData
	{
		static constexpr const NiRTTI* Ni_RTTI = &integerRTTI;
		std::int32_t* value = nullptr;
		std::uint32_t size = 0u;
		~NiIntegersExtraData() override { delete[] value; }
		const NiRTTI* GetRTTI() const override { return Ni_RTTI; }
	};
	inline bool failAllocation = false;
	template <class T>
	T* calloc(std::size_t)
	{
		return failAllocation ? nullptr : new T();
	}
	template <class T>
	void free(T* value)
	{
		delete value;
	}
	template <class T>
	T* NiAlloc(std::size_t size)
	{
		return new T[size]();
	}
	namespace stl
	{
		template <class T>
		bool emplace_vtable(T*)
		{
			return true;
		}
	}
	struct NiObjectNET
	{
		std::map<std::string, std::shared_ptr<NiExtraData>> extras;
		bool rejectExtraData = false;
		const NiExtraData* GetExtraData(const BSFixedString& name) const
		{
			const auto found = extras.find(name);
			return found == extras.end() ? nullptr : found->second.get();
		}
		void RemoveExtraData(const BSFixedString& name) { extras.erase(name); }
		bool AddExtraData(NiExtraData* extra)
		{
			if (rejectExtraData)
				return false;
			extras[extra->name] = extra->shared_from_this();
			return true;
		}
	};
	struct NiSourceTexture
	{
		BSFixedString name;
	};
	struct BSShaderMaterial
	{
		std::uintptr_t vtable = 0u;
	};
	struct BSLightingShaderMaterialLandscape : BSShaderMaterial
	{
		static constexpr Address VTABLE[]{ { 0x1234u } };
		std::uint32_t numLandscapeTextures = 2u;
		NiPointer<NiSourceTexture> diffuseTexture;
		std::array<NiPointer<NiSourceTexture>, 5u> landscapeDiffuseTexture;
		BSLightingShaderMaterialLandscape() { vtable = VTABLE[0].address(); }
	};
	struct BSShaderProperty
	{
		enum class EShaderPropertyFlag
		{
			kMultiTextureLandscape
		};
		struct Flags
		{
			bool landscape = true;
			bool any(EShaderPropertyFlag) const { return landscape; }
		} flags;
		virtual ~BSShaderProperty() = default;
		virtual const NiRTTI* GetRTTI() const { return nullptr; }
	};
	struct BSLightingShaderProperty : BSShaderProperty
	{
		BSShaderMaterial* material = nullptr;
		const NiRTTI* GetRTTI() const override { return &lightingRTTI; }
	};
	struct NiNode;
	struct BSGeometry;
	struct NiAVObject : NiObjectNET
	{
		NiNode* parent = nullptr;
		virtual ~NiAVObject() = default;
		virtual BSGeometry* AsGeometry() { return nullptr; }
	};
	struct BSGeometry : NiAVObject
	{
		struct RuntimeData
		{
			NiPointer<BSShaderProperty> shaderProperty;
		} data;
		RuntimeData& GetGeometryRuntimeData() { return data; }
		BSGeometry* AsGeometry() override { return this; }
	};
	struct NiNode : NiAVObject
	{
		std::vector<NiPointer<NiAVObject>> children;
		const auto& GetChildren() const { return children; }
	};
	struct BSTextureSet
	{
		enum class Textures
		{
			kDiffuse
		};
		std::string diffuse;
		const char* GetTexturePath(Textures) const { return diffuse.c_str(); }
	};
	struct BGSMaterialType
	{
		std::string key;
		std::string editorID;
		std::string materialName;
		BGSMaterialType* parentType = nullptr;
		const char* GetFormEditorID() const { return editorID.c_str(); }
	};
	struct TESLandTexture
	{
		std::string key;
		std::string editorID;
		std::uint32_t formID = 1u;
		BSTextureSet* textureSet = nullptr;
		BGSMaterialType* materialType = nullptr;
		std::uint32_t GetFormID() const { return formID; }
		const char* GetFormEditorID() const { return editorID.c_str(); }
	};
	class TESObjectLAND
	{
	public:
		struct LoadedData
		{
			NiNode* mesh[4]{};
			TESLandTexture* defQuadTextures[4]{};
			TESLandTexture* quadTextures[4][6]{};
		};
		std::unique_ptr<LoadedData> loadedData = std::make_unique<LoadedData>();
	};
	class BSRenderPass
	{
	public:
		BSGeometry* geometry = nullptr;
		BSShaderProperty* shaderProperty = nullptr;
	};
}

namespace REL
{
	template <class T>
	struct Relocation
	{
		T value;
		T get() const { return value; }
	};
}

struct BSLightingShaderMaterialPBRLandscape : RE::BSShaderMaterial
{
	static inline const RE::BSShaderMaterial* registered = nullptr;
	std::uint32_t numLandscapeTextures = 2u;
	std::array<RE::NiPointer<RE::NiSourceTexture>, 6u> landscapeBaseColorTextures;
	static const BSLightingShaderMaterialPBRLandscape* GetRegistered(const RE::BSShaderMaterial* material)
	{
		return material == registered ? static_cast<const BSLightingShaderMaterialPBRLandscape*>(material) : nullptr;
	}
};

namespace globals
{
	struct StateBoundary
	{
		bool developer = true;
		std::uint32_t modifiedPixelDescriptor = 0u;
		bool IsDeveloperMode() const { return developer; }
	};
	inline StateBoundary* state = nullptr;
	namespace rtti
	{
		inline REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI{ &RE::lightingRTTI };
	}
}
namespace Util
{
	inline RE::TESLandTexture* defaultTexture = nullptr;
	inline RE::BSTextureSet* GetSeasonalSwap(RE::BSTextureSet* value) { return value; }
	template <class T>
	std::string GetFormFileKey(const T* value)
	{
		return value->key;
	}
	inline RE::TESLandTexture* GetDefaultLandTexture() { return defaultTexture; }
}
namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{}
}
namespace SIE::ShaderCache
{
	enum class LightingShaderFlags : std::uint32_t
	{
		LandscapeSeams = 1u
	};
}

#include "mesh_blending_landscape_under_test.h"

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct Observation
	{
		std::string form;
		std::string diffuse;
		MeshBlendingLandscape::Kind kind;
	};
	struct Fixture
	{
		globals::StateBoundary state;
		MeshBlendingLandscape landscape;
		RE::TESObjectLAND land;
		RE::NiNode root;
		std::shared_ptr<RE::BSGeometry> geometry = std::make_shared<RE::BSGeometry>();
		std::shared_ptr<RE::BSLightingShaderProperty> property = std::make_shared<RE::BSLightingShaderProperty>();
		RE::BSLightingShaderMaterialLandscape material;
		RE::BSTextureSet snowSet{ "Landscape/Snow.dds" };
		RE::BSTextureSet stoneSet{ "Landscape/Stone.dds" };
		RE::BGSMaterialType snowType{ "0x1~Materials.esm", "Snow", "Snow", nullptr };
		RE::BGSMaterialType stoneType{ "0x2~Materials.esm", "Stone", "Stone", nullptr };
		RE::TESLandTexture snow{ "0x123~Terrain.esp", "SnowLayer", 1u, &snowSet, &snowType };
		RE::TESLandTexture stone{ "0x456~Terrain.esp", "StoneLayer", 2u, &stoneSet, &stoneType };
		RE::BSRenderPass pass;
		std::vector<Observation> observations;
		bool capture = false;
		bool throwObservation = false;
		struct Settings
		{
			std::uint32_t LandscapeLayerBlending = 1u;
			float LandscapeBlendStrength = 1.0f;
		} settings;
		struct DrawDiagnostics
		{
			std::uint64_t landscapeRegistryHits = 0u;
			std::uint64_t landscapeRegistryMisses = 0u;
			std::uint64_t activeDraws = 0u;
			std::uint64_t currentFrameActive = 0u;
		} diagnostics;
		std::uint32_t featureDescriptor = 0u;

		Fixture()
		{
			globals::state = &state;
			Util::defaultTexture = &snow;
			land.loadedData->mesh[0] = &root;
			land.loadedData->defQuadTextures[0] = &snow;
			land.loadedData->quadTextures[0][0] = &stone;
			root.children.push_back(geometry);
			geometry->parent = &root;
			geometry->data.shaderProperty = property;
			property->material = &material;
			material.diffuseTexture = std::make_shared<RE::NiSourceTexture>(RE::NiSourceTexture{ snowSet.diffuse });
			material.landscapeDiffuseTexture[0] = std::make_shared<RE::NiSourceTexture>(RE::NiSourceTexture{ stoneSet.diffuse });
			pass.geometry = geometry.get();
			pass.shaderProperty = property.get();
		}
		~Fixture()
		{
			globals::state = nullptr;
			Util::defaultTexture = nullptr;
			BSLightingShaderMaterialPBRLandscape::registered = nullptr;
			RE::failAllocation = false;
		}
		void StartRecording()
		{
			capture = true;
			landscape.BeginDiscoverySession();
		}
		MeshBlendingLandscape::DiscoverySink GetLandscapeDiscoverySink()
		{
			if (!capture)
				return {};
			return { this, [](void* context, const MeshBlendingLandscape::Texture& texture, MeshBlendingLandscape::Kind kind) {
						auto& fixture = *static_cast<Fixture*>(context);
						if (fixture.throwObservation)
							throw std::runtime_error("observation failed");
						fixture.observations.push_back({ std::string(texture.form), std::string(texture.diffuse), kind });
					},
				landscape.GetDiscoveryEpoch() };
		}
		void Capture(bool succeeded = true) { landscape.Capture(&land, succeeded, GetLandscapeDiscoverySink()); }
		bool IsFinite(float value) const { return std::isfinite(value); }
		void Draw(bool runtimeEnabled = true, bool seam = false, bool landscapePass = true, bool lodLandscapePass = false)
		{
			auto* a_pass = &pass;
			auto* state = &this->state;
			state->modifiedPixelDescriptor = seam ? 1u : 0u;
			const bool discoveryCaptureActive = capture;
			const bool collectDiagnostics = state->developer;
			constexpr auto landscapeClassMask = CSX::MeshBlendingLandscapePolicy::ClassMask;
			featureDescriptor = 0u;
#include "mesh_blending_landscape_draw_under_test.h"
		}
	};

	void TestResidentDiscoveryAndDrawGates()
	{
		Fixture fixture;
		fixture.Capture();
		Require(fixture.observations.empty(), "capture recorded while recording was disabled");
		fixture.StartRecording();
		fixture.settings.LandscapeLayerBlending = 0u;
		fixture.Draw();
		Require(fixture.observations.size() == 2u && fixture.featureDescriptor == 0u,
			"enabling recording after capture did not observe resident LAND with blending disabled");
		fixture.Draw();
		Require(fixture.observations.size() == 2u, "resident draws repeated the same publication observation");
		fixture.StartRecording();
		fixture.Draw(false);
		Require(fixture.observations.size() == 4u && fixture.featureDescriptor == 0u,
			"recording restart failed with the master feature disabled");
		fixture.landscape.PublishPolicy(CSX::MeshBlendingLandscapePolicy::CompilePolicy({}));
		fixture.Draw(false);
		Require(fixture.observations.size() == 6u, "policy revision did not refresh recorded material classes");
		fixture.settings.LandscapeLayerBlending = 1u;
		fixture.Draw(true, true);
		Require(fixture.featureDescriptor == 0u, "LAND seam permutation acquired blend classes");
		fixture.Draw();
		Require(fixture.featureDescriptor != 0u, "valid six-layer LAND failed draw admission");
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto diagnostics = fixture.landscape.GetDiagnostics();
		Require(diagnostics.captureSuccesses == 1u && diagnostics.seamBypasses == 1u && diagnostics.drawSuccesses == 6u,
			"capture, seam bypass or recording-only draw diagnostics were lost");
#endif
		fixture.StartRecording();
		fixture.Draw(true, false, false, true);
		Require(fixture.observations.size() == 6u, "LOD landscape was treated as supported LAND");
	}

	void TestUnsupportedCaptureStillObserves()
	{
		using Rejection = MeshBlendingLandscape::Rejection;
		for (const auto reason : { Rejection::MaterialSetup, Rejection::TextureIdentity, Rejection::MaterialLayout, Rejection::LayerCount }) {
			Fixture fixture;
			fixture.Capture();
			fixture.StartRecording();
			if (reason == Rejection::TextureIdentity)
				fixture.material.diffuseTexture->name = "Landscape/Wrong.dds";
			if (reason == Rejection::MaterialLayout)
				fixture.material.vtable = 0u;
			if (reason == Rejection::LayerCount)
				fixture.material.numLandscapeTextures = 8u;
			fixture.Capture(reason != Rejection::MaterialSetup);
			Require(fixture.observations.size() == 2u && fixture.observations[1].form == "0x456~terrain.esp",
				"a failed render capture hid readable LTEX identities");
			fixture.Draw();
			Require(fixture.featureDescriptor == 0u, "a failed recapture retained valid publication authority");
#ifdef DEVBENCH_BRIDGE_ENABLED
			const auto diagnostics = fixture.landscape.GetDiagnostics();
			Require(diagnostics.captureRejects[static_cast<std::size_t>(reason)] == 1u,
				"capture failed without its specific reason");
			if (reason == Rejection::TextureIdentity)
				Require(diagnostics.drawRejects[static_cast<std::size_t>(Rejection::StalePublication)] == 1u,
					"failed recapture did not invalidate the old draw publication");
#endif
		}
		Fixture fixture;
		fixture.StartRecording();
		fixture.land.loadedData->quadTextures[0][5] = &fixture.stone;
		fixture.Capture(false);
		Require(fixture.observations.size() == 3u, "recording hid the readable LTEX slot beyond the supported render layers");
	}

	void TestDrawIdentityAndFailureDetails()
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		using Rejection = MeshBlendingLandscape::Rejection;
#endif
		Fixture fixture;
		fixture.material.landscapeDiffuseTexture[0]->name = "Other.dds";
		fixture.Capture();
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto failed = fixture.landscape.GetDiagnostics().lastCaptureRejection;
		Require(failed.reason == Rejection::TextureIdentity && failed.quadrant == 0u && failed.layer == 1u &&
					failed.expectedTexture == "textures/landscape/stone.dds" && failed.boundTexture == "textures/other.dds",
			"failed binding identity omitted its bounded layer/path detail");
#endif
		fixture.material.landscapeDiffuseTexture[0]->name = fixture.stoneSet.diffuse;
		fixture.Capture();
		fixture.material.landscapeDiffuseTexture[0] = std::make_shared<RE::NiSourceTexture>(RE::NiSourceTexture{ fixture.stoneSet.diffuse });
		fixture.StartRecording();
		fixture.Draw();
		Require(fixture.observations.empty() && fixture.featureDescriptor == 0u,
			"a changed material binding reused stale snapshot observations or classes");
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(fixture.landscape.GetDiagnostics().lastDrawRejection.reason == Rejection::StalePublication,
			"stale draw identity was not diagnosed");
		Require(fixture.landscape.GetDiagnostics().lastCaptureRejection.boundTexture == "textures/other.dds",
			"draw failures erased the preceding capture failure detail");
		fixture.landscape.ResetDiagnostics();
		Require(fixture.landscape.GetDiagnostics().lastDrawRejection.reason == Rejection::None &&
					fixture.landscape.GetDiagnostics().drawAttempts == 0u,
			"diagnostic reset retained counters or rejection detail");
#endif
		fixture.Capture();
		fixture.StartRecording();
		fixture.throwObservation = true;
		fixture.Draw();
		Require(fixture.featureDescriptor == 0u, "failed observation did not fail closed");
		fixture.throwObservation = false;
		fixture.observations.clear();
		fixture.Draw();
		Require(fixture.observations.size() == 2u, "failed observer poisoned recording deduplication");
		RE::failAllocation = true;
		fixture.Capture();
		fixture.Draw();
		Require(fixture.featureDescriptor == 0u, "allocation failure retained a usable old publication");
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(fixture.landscape.GetDiagnostics().captureRejects[static_cast<std::size_t>(Rejection::MetadataAllocation)] == 1u,
			"allocation failure was not diagnosed");
#endif
	}

	void TestPolicyRejectionsAndPBR()
	{
		namespace Landscape = CSX::MeshBlendingLandscapePolicy;
		Fixture fixture;
		BSLightingShaderMaterialPBRLandscape pbr;
		pbr.landscapeBaseColorTextures[0] = fixture.material.diffuseTexture;
		pbr.landscapeBaseColorTextures[1] = fixture.material.landscapeDiffuseTexture[0];
		BSLightingShaderMaterialPBRLandscape::registered = &pbr;
		fixture.property->material = &pbr;
		fixture.Capture();
		fixture.Draw();
		Require(fixture.featureDescriptor != 0u, "registered PBR material capture failed");
		Landscape::Policy policy;
		policy.assignments = { { "Unknown", "0x123~terrain.esp", {}, {} } };
		fixture.landscape.PublishPolicy(Landscape::CompilePolicy(policy));
		fixture.Draw();
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(fixture.landscape.GetDiagnostics().unknownLayerDraws == 1u, "unknown present material was not reported");
#endif
		policy = {};
		policy.blendPairsExplicit = true;
		policy.blendPairs = { { "Snow", "Snow" } };
		fixture.landscape.PublishPolicy(Landscape::CompilePolicy(policy));
		fixture.Draw();
		Require(fixture.featureDescriptor == 0u, "disallowed material pair received blend classes");
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto diagnostics = fixture.landscape.GetDiagnostics();
		Require(diagnostics.rejectedPairDraws == 1u && diagnostics.zeroClassDraws == 1u,
			"policy rejection was not distinguishable from failed snapshot capture");
		fixture.state.developer = false;
		fixture.Draw(true, true);
		Require(fixture.landscape.GetDiagnostics().seamBypasses == 0u, "developer-only counters ran with developer mode off");
#endif
	}
}

int main()
{
	try {
		TestResidentDiscoveryAndDrawGates();
		TestUnsupportedCaptureStillObserves();
		TestDrawIdentityAndFailureDetails();
		TestPolicyRejectionsAndPBR();
		std::cout << "Production LAND capture, resident discovery, draw admission and rejection checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
