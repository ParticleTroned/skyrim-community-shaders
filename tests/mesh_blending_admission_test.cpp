#include "Features/MeshBlendingPolicy.h"
#include "Features/MeshBlendingSnowPolicy.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <type_traits>

// Engine objects expose only the state read by the extracted production code.
// Shader flags and material/alpha enums come from the pinned CommonLib headers.
namespace RE
{
	template <class T>
	struct Pointer
	{
		T* value = nullptr;
		T* get() const { return value; }
		explicit operator bool() const { return value != nullptr; }
	};
	struct NiBound
	{
		struct Point
		{
			float x = 0.0f, y = 0.0f, z = 0.0f;
		} center;
		float radius = 1.0f;
	};
	struct NiAlphaProperty
	{
#include "mesh_blending_alpha_enums.h"
		const void* rtti = this;
		bool blend = true, test = false;
		AlphaFunction source = AlphaFunction::kSrcAlpha, destination = AlphaFunction::kInvSrcAlpha;
		const void* GetRTTI() const { return rtti; }
		bool GetAlphaBlending() const { return blend; }
		bool GetAlphaTesting() const { return test; }
		AlphaFunction GetSrcBlendMode() const { return source; }
		AlphaFunction GetDestBlendMode() const { return destination; }
	};
	struct BSShaderMaterial
	{
#include "mesh_blending_material_enums.h"
		Type type = Type::kLighting;
		Feature feature = Feature::kDefault;
		Type GetType() const { return type; }
		Feature GetFeature() const { return feature; }
	};
	struct BSShaderProperty
	{
		static constexpr auto BIT64 = std::uint64_t{ 1 };
#include "mesh_blending_shader_enums.h"
		const void* rtti = this;
		const void* GetRTTI() const { return rtti; }
		struct Flags
		{
			std::uint64_t value = static_cast<std::uint64_t>(EShaderPropertyFlag::kZBufferTest);
			std::uint64_t underlying() const { return value; }
		} flags;
	};
	struct BSLightingShaderProperty : BSShaderProperty
	{
		BSShaderMaterial* material = nullptr;
		float alpha = 1.0f;
	};
	struct TESObjectREFR;
	struct NiNode;
	struct NiAVObject
	{
		NiNode* parent = nullptr;
		std::uint32_t parentIndex = 0;
		std::string name = "root";
		TESObjectREFR* owner = nullptr;
		bool animated = false;
		bool HasAnimation() const { return animated; }
	};
	struct NiNode : NiAVObject
	{
		std::vector<Pointer<NiAVObject>> children;
		auto& GetChildren() { return children; }
	};
	struct BSGeometry : NiAVObject
	{
		struct GeometryRuntime
		{
			Pointer<NiAlphaProperty> alphaProperty;
			Pointer<BSShaderProperty> shaderProperty;
			void* rendererData = this;
			void* skinInstance = nullptr;
		} runtime;
		NiBound worldBound;
		auto& GetGeometryRuntimeData() { return runtime; }
	};
	enum class FormType
	{
		Static,
		MovableStatic
	};
	struct TESModel
	{
		std::string path = "architecture/test.nif";
		const char* GetModel() const { return path.c_str(); }
	};
	struct TESBoundObject : TESModel
	{
		FormType type = FormType::Static;
		FormType GetFormType() const { return type; }
		template <class T>
		T* As()
		{
			return static_cast<T*>(this);
		}
	};
	struct ExtraModelSwap
	{
		TESModel* modelSwap = nullptr;
	};
	struct TESObjectREFR
	{
		TESBoundObject* base = nullptr;
		NiAVObject* root = nullptr;
		struct ExtraList
		{
			ExtraModelSwap* modelSwap = nullptr;
			template <class T>
			T* GetByType() const
			{
				return modelSwap;
			}
		} extraList;
		TESBoundObject* GetBaseObject() const { return base; }
		NiAVObject* Get3D() const { return root; }
	};
	struct BSRenderPass
	{
		BSGeometry* geometry = nullptr;
		BSShaderProperty* shaderProperty = nullptr;
	};
}

struct State
{
	enum class ExtraShaderDescriptors : std::uint32_t
	{
		MeshBlending = CSX::MeshBlendingPolicy::EnableMask
	};
};

namespace globals
{
	namespace rtti
	{
		RE::Pointer<const void> NiAlphaPropertyRTTI;
		RE::Pointer<const void> BSLightingShaderPropertyRTTI;
	}
	struct StateBoundary
	{
		std::uint32_t frameCount = 1u;
		struct
		{
			std::uint32_t ExtraShaderDescriptor = 0u;
		} permutationData;
		bool IsDeveloperMode() const { return true; }
	};
	StateBoundary stateBoundary;
	StateBoundary* state = &stateBoundary;
}
namespace Util
{
	RE::TESObjectREFR* GetOwnUserData(const RE::NiAVObject* object) { return object->owner; }
}
namespace logger
{
	template <class... Args>
	void debug(std::string_view, Args&&...)
	{}
}

using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;
using MaterialFeature = RE::BSShaderMaterial::Feature;
using CSX::MeshBlendingPolicy::NormalizePath;
using CSX::MeshBlendingPolicy::RejectSource;
using RejectionReason = CSX::MeshBlendingPolicy::RejectionReason;

struct MeshBlending
{
	using Classification = CSX::MeshBlendingPolicy::CachedClassification;
	static constexpr std::size_t kMaximumRootDepth = 64u, kMaximumRules = 1024u;
	static constexpr std::uint32_t kMaximumClassificationLogs = 64u;
#include "mesh_blending_admission_members.h"
	Settings settings;
	Diagnostics diagnostics;
	CSX::MeshBlendingPolicy::CompiledRules compiledRules;
	std::uint32_t classificationLogs = 0u;
	std::mutex discoveryMutex;
	std::set<std::pair<std::string, std::string>> discoveredRuleKeys;
	std::map<std::string, int> discoveredLandscapeIdentities;
	std::atomic_bool discoveryCaptureEnabled = true;
	bool receiverFound = true, traversalCapped = false;
	bool runtimeEnabled = true, overlayPolicyEnabled = true, insideDistance = true;
	Classification classificationResult = Classification::kAutomatic;
	unsigned classificationCalls = 0u;
	RE::BSGeometry receiver;
	bool BuildOverlaySourceState(RE::BSRenderPass*, SourceState&, RejectionReason*) const;
	bool BuildSourceState(RE::BSRenderPass*, SourceState&, RejectionReason*) const;
	bool ResolveStaticOwner(SourceState&, RejectionReason*) const;
	std::string BuildModelPath(const SourceState&) const;
	std::string BuildNodePath(const SourceState&) const;
	Classification ClassifyOnCacheMiss(const SourceState&, bool, RE::BSGeometry*&, RejectionReason*);
	bool TryPrepareProjectedSnow(RE::BSRenderPass*);
#ifdef DEVBENCH_BRIDGE_ENABLED
	void RecordRejection(RejectionReason);
#endif
	bool IsRuntimeEnabled() const { return runtimeEnabled; }
	bool IsOverlayPolicyEnabled() const { return overlayPolicyEnabled; }
	bool IsDiscoveryCaptureActive() const { return discoveryCaptureEnabled.load(); }
	bool IsInsideDistanceBubble(const RE::NiBound&, std::uint32_t) const { return insideDistance; }
	Classification GetSourceClassification(SourceState&, std::uint32_t, bool, bool)
	{
		++classificationCalls;
		return classificationResult;
	}
	bool HasOpaqueSibling(const SourceState&, bool& capped, RE::BSGeometry*& result)
	{
		capped = traversalCapped;
		result = receiverFound ? &receiver : nullptr;
		return receiverFound;
	}
	void CaptureDiscoveredRule(std::string_view model, std::string_view node) { discoveredRuleKeys.emplace(model, node); }
};

#include "mesh_blending_admission_under_test.h"

template <class T>
concept HasProjectedSnowDiagnostics = requires(T& value) {
	value.projectedSnowCandidates;
	value.projectedSnowActiveDraws;
};
#ifdef DEVBENCH_BRIDGE_ENABLED
static_assert(HasProjectedSnowDiagnostics<MeshBlending::Diagnostics>);
#else
static_assert(!HasProjectedSnowDiagnostics<MeshBlending::Diagnostics>);
#endif

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void RequireReason(RejectionReason actual, RejectionReason expected)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(actual == expected, "incorrect admission rejection reason");
#else
		(void)expected;
		Require(actual == RejectionReason::kNone, "ordinary build wrote developer diagnostic state");
#endif
	}
	struct Fixture
	{
		MeshBlending feature;
		RE::NiAlphaProperty alpha;
		RE::BSShaderMaterial material;
		RE::BSLightingShaderProperty shader;
		RE::BSGeometry geometry;
		RE::NiNode root;
		RE::TESBoundObject base;
		RE::TESObjectREFR owner;
		RE::BSRenderPass pass;
		Fixture()
		{
			globals::stateBoundary = {};
			globals::rtti::NiAlphaPropertyRTTI.value = alpha.rtti;
			globals::rtti::BSLightingShaderPropertyRTTI.value = shader.rtti;
			shader.material = &material;
			geometry.runtime.alphaProperty.value = &alpha;
			geometry.runtime.shaderProperty.value = &shader;
			geometry.parent = &root;
			geometry.name = "overlay";
			root.children.push_back({ &geometry });
			root.owner = &owner;
			owner.base = &base;
			owner.root = &root;
			pass = { &geometry, &shader };
		}
		MeshBlending::SourceState Source()
		{
			MeshBlending::SourceState source;
			RejectionReason reason = RejectionReason::kNone;
			Require(feature.BuildSourceState(&pass, source, &reason), "safe overlay rejected");
			return source;
		}
		MeshBlending::SourceState OwnedSource()
		{
			auto source = Source();
			RejectionReason reason = RejectionReason::kNone;
			Require(feature.ResolveStaticOwner(source, &reason), "safe static owner rejected");
			return source;
		}
	};
	void TestSourceAdmission()
	{
		const auto reject = [](auto mutate, RejectionReason expected) {
			Fixture fixture;
			mutate(fixture);
			MeshBlending::SourceState source;
			RejectionReason reason = RejectionReason::kNone;
			Require(!fixture.feature.BuildSourceState(&fixture.pass, source, &reason), "unsafe source accepted");
			RequireReason(reason, expected);
		};
		Fixture valid;
		Require(valid.Source().owner == &valid.owner, "bounded owner lookup failed");
		for (const auto feature : { MaterialFeature::kDefault, MaterialFeature::kParallax, MaterialFeature::kParallaxOcc }) {
			valid.material.feature = feature;
			(void)valid.Source();
		}
		reject([](auto& f) { f.pass.geometry = nullptr; }, RejectionReason::kMissingGeometry);
		reject([](auto& f) { f.geometry.runtime.alphaProperty.value = nullptr; }, RejectionReason::kAlphaProperty);
		reject([](auto& f) { f.alpha.rtti = nullptr; }, RejectionReason::kAlphaProperty);
		reject([](auto& f) { f.alpha.blend = false; }, RejectionReason::kAlphaBlending);
		reject([](auto& f) { f.alpha.test = true; }, RejectionReason::kAlphaTesting);
		reject([](auto& f) { f.alpha.source = RE::NiAlphaProperty::AlphaFunction::kOne; }, RejectionReason::kBlendMode);
		reject([](auto& f) { f.alpha.destination = RE::NiAlphaProperty::AlphaFunction::kOne; }, RejectionReason::kBlendMode);
		reject([](auto& f) { f.geometry.runtime.rendererData = nullptr; }, RejectionReason::kRendererData);
		reject([](auto& f) { f.geometry.runtime.skinInstance = &f; }, RejectionReason::kSkinned);
		reject([](auto& f) { f.geometry.runtime.shaderProperty.value = nullptr; }, RejectionReason::kShaderProperty);
		reject([](auto& f) { f.shader.rtti = nullptr; }, RejectionReason::kShaderProperty);
		reject([](auto& f) { f.shader.flags.value = 0u; }, RejectionReason::kDepthTest);
		reject([](auto& f) { f.shader.flags.value |= ToMask(ShaderFlag::kZBufferWrite); }, RejectionReason::kDepthWrite);
		reject([](auto& f) { f.shader.material = nullptr; }, RejectionReason::kMaterial);
		reject([](auto& f) { f.material.type = RE::BSShaderMaterial::Type::kEffect; }, RejectionReason::kMaterial);
		reject([](auto& f) { f.shader.alpha = std::numeric_limits<float>::quiet_NaN(); }, RejectionReason::kAlpha);
		reject([](auto& f) { f.shader.alpha = 0.0f; }, RejectionReason::kAlpha);
		reject([](auto& f) { f.geometry.worldBound.radius = 0.0f; }, RejectionReason::kBounds);
		reject([](auto& f) { f.geometry.worldBound.center.x = std::numeric_limits<float>::infinity(); }, RejectionReason::kBounds);
		reject([](auto& f) { f.shader.flags.value |= ToMask(ShaderFlag::kEnvMap); }, RejectionReason::kShaderFlags);
		reject([](auto& f) { f.material.feature = MaterialFeature::kMultilayerParallax; }, RejectionReason::kMaterialFeature);
		reject([](auto& f) { f.material.feature = MaterialFeature::kEnvironmentMap; }, RejectionReason::kMaterialFeature);
	}
	void TestStaticOwnership()
	{
		const auto reject = [](auto mutate, RejectionReason expected) {
			Fixture fixture;
			auto source = fixture.Source();
			mutate(fixture, source);
			RejectionReason reason = RejectionReason::kNone;
			Require(!fixture.feature.ResolveStaticOwner(source, &reason), "unsafe ownership accepted");
			RequireReason(reason, expected);
		};
		reject([](auto&, auto& source) { source.owner = nullptr; }, RejectionReason::kOwner);
		reject([](auto& f, auto&) { f.base.type = RE::FormType::MovableStatic; }, RejectionReason::kNonStaticOwner);
		reject([](auto& f, auto&) { f.owner.root = nullptr; }, RejectionReason::kRoot);
		reject([](auto& f, auto&) { f.root.children[0].value = nullptr; }, RejectionReason::kAncestry);
		reject([](auto& f, auto&) { f.geometry.parentIndex = 9u; }, RejectionReason::kAncestry);
		Fixture fixture;
		auto source = fixture.OwnedSource();
		Require(source.nodePathLength == 2u && source.nodeParts[0].name == "root" && source.nodeParts[1].name == "overlay",
			"canonical ancestry was not captured from live reciprocal links");
		RE::TESModel replacement;
		replacement.path = "replacement.nif";
		RE::ExtraModelSwap swap{ &replacement };
		fixture.owner.extraList.modelSwap = &swap;
		Require(fixture.feature.BuildModelPath(fixture.OwnedSource()) == "meshes/replacement.nif", "extra model swap identity ignored");
	}
	void TestClassificationDecisions()
	{
		using Classification = MeshBlending::Classification;
		const auto classify = [](Fixture& fixture, bool capture, RejectionReason expected, Classification accepted = Classification::kRejected) {
			auto source = fixture.OwnedSource();
			RE::BSGeometry* receiver = nullptr;
			RejectionReason reason = RejectionReason::kNone;
			Require(fixture.feature.ClassifyOnCacheMiss(source, capture, receiver, &reason) == accepted, "incorrect production classification decision");
			RequireReason(reason, expected);
		};
		Fixture fixture;
		classify(fixture, false, RejectionReason::kNone, Classification::kAutomatic);
		Require(fixture.feature.discoveredRuleKeys.empty(), "Automatic implicitly recorded an identity");
		classify(fixture, true, RejectionReason::kNone, Classification::kAutomatic);
		Require(fixture.feature.discoveredRuleKeys.size() == 1u, "enabled recording did not capture an accepted overlay");
		fixture.root.animated = true;
		classify(fixture, false, RejectionReason::kAnimatedRoot);
		fixture.root.animated = false;
		fixture.feature.receiverFound = false;
		classify(fixture, false, RejectionReason::kNoReceiver);
		fixture.feature.traversalCapped = true;
		classify(fixture, false, RejectionReason::kTraversalLimit);
		fixture.feature.receiverFound = true;
		fixture.base.path.clear();
		classify(fixture, false, RejectionReason::kModelIdentity);
		fixture.base.path = "architecture/test.nif";
		fixture.feature.settings.DetectionMode = static_cast<std::uint32_t>(MeshBlending::DetectionMode::kAllowListOnly);
		classify(fixture, false, RejectionReason::kPolicy);
		classify(fixture, true, RejectionReason::kNone, Classification::kAutomatic);
		fixture.feature.compiledRules.authored.Add("architecture/test.nif", "");
		fixture.feature.compiledRules.denied.Add("architecture/test.nif", "");
		classify(fixture, true, RejectionReason::kDenied);
		fixture.feature.compiledRules.denied = {};
		classify(fixture, false, RejectionReason::kNone, Classification::kAllowedByRule);
		fixture.feature.compiledRules.authored = {};
		fixture.feature.compiledRules.AddDetected("architecture/test.nif", "root/overlay[0]");
		fixture.feature.compiledRules.BuildNodeIndex();
		classify(fixture, false, RejectionReason::kNone, Classification::kDetectedByRule);
		fixture.geometry.name = "unsafe*selector";
		classify(fixture, false, RejectionReason::kNodeIdentity);
	}
	void TestProjectedSnowAdmission()
	{
		const auto snow = [](Fixture& f) {
			f.shader.flags.value |= CSX::MeshBlendingSnowPolicy::RequiredFlags<ShaderFlag>() | ToMask(ShaderFlag::kSnow);
			f.material.feature = MaterialFeature::kMultiIndexTriShapeSnow;
			f.feature.discoveryCaptureEnabled = false;
			f.feature.settings.DetectionMode = static_cast<std::uint32_t>(MeshBlending::DetectionMode::kStrictAutomatic);
			f.feature.settings.NifEnabled = 0u;
			f.feature.settings.ProjectedSnowEnabled = 1u;
			f.feature.settings.ProjectedSnowEdgeWidth = 2.0f;
		};
		Fixture accepted;
		snow(accepted);
		globals::state->permutationData.ExtraShaderDescriptor = 8u;
		Require(accepted.feature.TryPrepareProjectedSnow(&accepted.pass), "engine Snow flag rejected a safe multi-index overlay");
		Require(globals::state->permutationData.ExtraShaderDescriptor == (8u | CSX::MeshBlendingPolicy::EnableMask) &&
					accepted.feature.diagnostics.currentFrameActive == 1u && accepted.feature.classificationCalls == 1u,
			"snow activation did not preserve descriptors or classify exactly once");
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(accepted.feature.diagnostics.projectedSnowCandidates == 1u &&
					accepted.feature.diagnostics.projectedSnowActiveDraws == 1u && accepted.feature.diagnostics.eligibleSources == 1u,
			"snow admission and active counters were conflated");
#endif
		const auto reject = [&](auto mutate, RejectionReason expected) {
			Fixture f;
			snow(f);
			mutate(f);
			Require(!f.feature.TryPrepareProjectedSnow(&f.pass), "unsafe or unsupported snow draw accepted");
			Require(globals::state->permutationData.ExtraShaderDescriptor == 0u && f.feature.classificationCalls == 0u,
				"snow source rejection reached classification or enabled the descriptor");
#ifdef DEVBENCH_BRIDGE_ENABLED
			Require(f.feature.diagnostics.rejections[static_cast<std::size_t>(expected)] == 1u,
				"snow rejection omitted its first failed gate");
			Require(std::count_if(f.feature.diagnostics.rejections.begin(), f.feature.diagnostics.rejections.end(),
						[](auto value) { return value != 0u; }) == 1,
				"snow rejection counted more than its first failed gate");
#else
			(void)expected;
#endif
		};
		reject([](auto& f) { f.geometry.runtime.alphaProperty.value = nullptr; }, RejectionReason::kAlphaProperty);
		reject([](auto& f) { f.alpha.test = true; }, RejectionReason::kAlphaTesting);
		reject([](auto& f) { f.shader.flags.value |= ToMask(ShaderFlag::kEnvMap); }, RejectionReason::kShaderFlags);
		reject([](auto& f) { f.material.feature = MaterialFeature::kDefault; }, RejectionReason::kSnowMaterialFamily);
		reject([](auto& f) {
			f.shader.flags.value &= ~ToMask(ShaderFlag::kMultiIndexSnow);
			f.material.feature = MaterialFeature::kDefault;
			f.geometry.runtime.alphaProperty.value = nullptr;
		},
			RejectionReason::kSnowMaterialFamily);
		reject([](auto& f) { f.feature.insideDistance = false; }, RejectionReason::kDistance);
		Fixture rejected;
		snow(rejected);
		rejected.feature.classificationResult = MeshBlending::Classification::kRejected;
		Require(!rejected.feature.TryPrepareProjectedSnow(&rejected.pass) && rejected.feature.classificationCalls == 1u &&
					globals::state->permutationData.ExtraShaderDescriptor == 0u,
			"rejected static classification enabled snow coverage");
		Fixture disabled;
		snow(disabled);
		disabled.feature.settings.ProjectedSnowEdgeWidth = 0.0f;
		Require(!disabled.feature.TryPrepareProjectedSnow(&disabled.pass) && disabled.feature.classificationCalls == 0u,
			"zero snow width performed classification work");
		disabled.feature.settings.ProjectedSnowEdgeWidth = 2.0f;
		disabled.feature.runtimeEnabled = false;
		disabled.feature.discoveryCaptureEnabled = true;
		Require(!disabled.feature.TryPrepareProjectedSnow(&disabled.pass) && disabled.feature.classificationCalls == 1u &&
					globals::state->permutationData.ExtraShaderDescriptor == 0u,
			"recording-only snow selection activated blending");
	}
}

int main()
{
	try {
		TestSourceAdmission();
		TestStaticOwnership();
		TestClassificationDecisions();
		TestProjectedSnowAdmission();
		std::cout << "Production overlay source, static ownership, classification and snow admission passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
