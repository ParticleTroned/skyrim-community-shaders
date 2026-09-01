#pragma once

#include "../Feature.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * Softens selected intersections between static NIF shapes and rebalances
 * compatible LAND/LTEX material weights without adding a render pass.
 */
struct MeshBlending final : Feature
{
	/** Controls how safe source shapes are selected. */
	enum class DetectionMode : std::uint32_t
	{
		kDisabled = 0,
		kAllowListOnly = 1,
		kStrictAutomatic = 2
	};

	/** Stable model/node selector used by allow and deny policy. */
	struct OverrideRule
	{
		std::string Model;
		std::string NodePath;
	};

	/** User-facing settings. CPU-only controls are intentionally not uploaded. */
	struct Settings
	{
		std::uint32_t Enabled = 1u;
		float BlendStrength = 1.0f;
		float BlendWidth = 12.0f;
		float DepthBias = 0.25f;
		float MaximumGap = 64.0f;
		float ProjectedSnowEdgeWidth = 2.0f;
		std::uint32_t LandscapeLayerBlending = 1u;

		std::uint32_t DetectionMode = static_cast<std::uint32_t>(MeshBlending::DetectionMode::kStrictAutomatic);
		float MaximumDistance = 8192.0f;
		float BoundsExpansion = 32.0f;
		std::uint32_t RequireOverlappingBounds = 1u;
		std::uint32_t DeveloperLogging = 0u;
	};

	/** Exact 32-byte GPU payload appended to the shared feature buffer. */
	struct alignas(16) PerFrame
	{
		float BlendStrength = 1.0f;
		float BlendWidth = 12.0f;
		float DepthBias = 0.25f;
		float MaximumGap = 64.0f;
		std::uint32_t EnableLandscapeBlending = 1u;
		float ProjectedSnowEdgeWidth = 2.0f;
		float Padding0 = 0.0f;
		float Padding1 = 0.0f;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);
	static_assert(sizeof(PerFrame) == 32);

	Settings settings;

	std::string GetName() override { return "Mesh Blending"; }
	std::string GetShortName() override { return "MeshBlending"; }
	std::string_view GetShaderDefineName() override { return "MESH_BLENDING"; }
	std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }
	bool HasShaderDefine(RE::BSShader::Type a_shaderType) override { return a_shaderType == RE::BSShader::Type::Lighting; }
	bool SupportsVR() override { return true; }

	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	void DrawSettings() override;
	bool HasEssentialSettings() const override { return true; }
	void DrawEssentialSettings() override;
	bool HasPerformanceSettings() const override { return true; }
	void DrawPerformanceSettings(bool a_advanced) override;
	json CapturePerformanceSettingsState() const override;
	bool SupportsPerformanceCostMeasurement() const override { return true; }
	bool IsPerformanceCostMeasurementEnabled() const override { return IsRuntimeEnabled() && !IsDiscoveryCaptureActive(); }
	void SetPerformanceCostMeasurementEnabled(bool a_enabled) override;
	json CapturePerformanceCostMeasurementState() const override;
	void RestorePerformanceCostMeasurementState(const json& a_state) override;
	void LoadSettings(json& a_json) override;
	void SaveSettings(json& a_json) override;
	void RestoreDefaultSettings() override;

	/** Returns the sanitized GPU settings uploaded once per frame. */
	PerFrame GetCommonBufferData() const;
	/** Returns whether the configured policy can currently select any draw. */
	bool IsRuntimeEnabled() const;

	/**
	 * Clears and conditionally sets the per-draw descriptor for a Lighting pass.
	 * This must run before the engine's original SetupGeometry call.
	 */
	void PrepareLightingDraw(RE::BSRenderPass* a_pass);

	/** Captures the final vanilla/True PBR LTEX identities for four LAND quadrants. */
	void CaptureLandscapeMaterials(RE::TESObjectLAND* a_land);

private:
	enum class Classification : std::uint8_t
	{
		kRejected,
		kAllowedByRule,
		kAutomatic
	};

	struct SourceState
	{
		RE::BSGeometry* geometry = nullptr;
		RE::BSLightingShaderProperty* shaderProperty = nullptr;
		RE::NiAlphaProperty* alphaProperty = nullptr;
		RE::BSShaderMaterial* material = nullptr;
		RE::TESObjectREFR* owner = nullptr;
		RE::NiAVObject* root = nullptr;
		const RE::TESModel* model = nullptr;
		const char* modelPath = nullptr;
		std::uint64_t shaderFlags = 0u;
	};

	struct Signature
	{
		std::uintptr_t geometry = 0u;
		std::uintptr_t parent = 0u;
		std::uintptr_t shaderProperty = 0u;
		std::uintptr_t alphaProperty = 0u;
		std::uintptr_t material = 0u;
		std::uintptr_t rendererData = 0u;
		std::uintptr_t skinInstance = 0u;
		std::uintptr_t owner = 0u;
		std::uintptr_t root = 0u;
		std::uintptr_t model = 0u;
		std::uintptr_t modelPath = 0u;
		std::uint64_t shaderFlags = 0u;
		std::uint32_t ownerFormID = 0u;
		std::uint32_t materialFeature = 0u;
		std::uint32_t parentIndex = 0u;
		std::uint32_t boundXBits = 0u;
		std::uint32_t boundYBits = 0u;
		std::uint32_t boundZBits = 0u;
		std::uint32_t boundRadiusBits = 0u;
		std::uint32_t policyGeneration = 0u;
		std::uint16_t alphaFlags = 0u;

		bool operator==(const Signature&) const = default;

		bool HasSameSourceState(const Signature& a_other) const
		{
			return geometry == a_other.geometry &&
			       parent == a_other.parent &&
			       owner == a_other.owner &&
			       shaderProperty == a_other.shaderProperty &&
			       alphaProperty == a_other.alphaProperty &&
			       material == a_other.material &&
			       rendererData == a_other.rendererData &&
			       skinInstance == a_other.skinInstance &&
			       shaderFlags == a_other.shaderFlags &&
			       materialFeature == a_other.materialFeature &&
			       parentIndex == a_other.parentIndex &&
			       boundXBits == a_other.boundXBits &&
			       boundYBits == a_other.boundYBits &&
			       boundZBits == a_other.boundZBits &&
			       boundRadiusBits == a_other.boundRadiusBits &&
			       policyGeneration == a_other.policyGeneration &&
			       alphaFlags == a_other.alphaFlags;
		}
	};

	struct CacheEntry
	{
		Signature signature{};
		Classification classification = Classification::kRejected;
		std::uint32_t classifiedFrame = 0u;
		std::uint32_t lastUsedFrame = 0u;
		bool occupied = false;
	};

	struct CompiledRule
	{
		std::string model;
		std::string nodePath;
		bool modelHasWildcard = false;
		bool nodeHasWildcard = false;
	};

	struct ExactRuleLess
	{
		using is_transparent = void;

		template <class Left, class Right>
		bool operator()(const Left& a_left, const Right& a_right) const
		{
			if (a_left.first != a_right.first) {
				return a_left.first < a_right.first;
			}
			return a_left.second < a_right.second;
		}
	};

	using ExactRuleSet = std::set<std::pair<std::string, std::string>, ExactRuleLess>;

	struct Diagnostics
	{
		std::uint64_t drawCalls = 0u;
		std::uint64_t scopeRejects = 0u;
		std::uint64_t sourceRejects = 0u;
		std::uint64_t distanceRejects = 0u;
		std::uint64_t cacheHits = 0u;
		std::uint64_t cacheMisses = 0u;
		std::uint64_t cacheEvictions = 0u;
		std::uint64_t preOwnerCacheHits = 0u;
		std::uint64_t fullSignatureCacheHits = 0u;
		std::uint64_t ownerResolutionAttempts = 0u;
		std::uint64_t rootTraversals = 0u;
		std::uint64_t nodesVisited = 0u;
		std::uint64_t traversalLimitRejects = 0u;
		std::uint64_t ruleAccepts = 0u;
		std::uint64_t automaticAccepts = 0u;
		std::uint64_t activeDraws = 0u;
		std::uint64_t landscapeRegistryHits = 0u;
		std::uint64_t landscapeRegistryMisses = 0u;
		std::uint32_t currentFrame = 0u;
		std::uint32_t currentFrameActive = 0u;
		std::uint32_t lastFrameActive = 0u;
	};

	enum class LandscapeMaterialKind : std::uint8_t
	{
		kUnknown,
		kSoft,
		kSnow,
		kDirt,
		kAsh,
		kGravel,
		kMud,
		kSand,
		kGrass,
		kGroundCover,
		kHard,
		kIce,
		kStone,
		kBoulder,
		kWood,
		kGlass,
		kMetal
	};

	enum class LandscapeMaterialClass : std::uint8_t
	{
		kUnknown = 0u,
		kHard = 1u,
		kSoft = 2u,
		kReserved = 3u
	};

	struct LandscapeAssignment
	{
		std::string Kind;
		std::string Form;
		std::string EditorID;
		std::string Diffuse;

		bool operator==(const LandscapeAssignment&) const = default;
	};

	struct SurfaceMaterialRule
	{
		std::string Material;
		std::string Kind;

		bool operator==(const SurfaceMaterialRule&) const = default;
	};

	struct BlendPairRule
	{
		std::string Source;
		std::string Receiver;

		bool operator==(const BlendPairRule&) const = default;
	};

	struct LandscapeDiagnostic
	{
		std::string Kind;
		std::string Form;
		std::string EditorID;
		std::string Diffuse;
		std::string Material;
	};

	struct LandscapeRegistryEntry
	{
		std::atomic_uint32_t sequence = 0u;
		std::atomic_uintptr_t geometry = 0u;
		std::atomic_uintptr_t shaderProperty = 0u;
		std::atomic_uintptr_t material = 0u;
		std::atomic_uint32_t packedClasses = 0u;
		std::atomic_uint32_t policy = 0u;
		std::atomic_uint64_t writeSerial = 0u;
	};

	struct DiscoverySaveResult
	{
		bool success = false;
		std::size_t added = 0u;
		std::size_t alreadyAllowed = 0u;
		std::size_t denied = 0u;
		std::size_t capacityRejected = 0u;
		std::size_t landscapeAdded = 0u;
		std::size_t landscapeRefreshed = 0u;
		std::size_t landscapeCapacityRejected = 0u;
		std::string error;
	};

	struct RuleFileState
	{
		std::vector<OverrideRule> allowList;
		std::vector<OverrideRule> detectedAllowList;
		std::vector<OverrideRule> denyList;
		std::vector<LandscapeAssignment> landscapeAssignments;
		std::vector<SurfaceMaterialRule> surfaceMaterialRules;
		std::vector<BlendPairRule> blendPairRules;
		std::vector<LandscapeDiagnostic> discoveryDiagnostics;
		bool blendPairsExplicit = false;
		json document = json::object();
	};

	static constexpr std::size_t kCacheSetCount = 1024u;
	static constexpr std::size_t kCacheWays = 4u;
	static constexpr std::size_t kCacheCapacity = kCacheSetCount * kCacheWays;
	static constexpr std::uint32_t kCacheValidationFrames = 600u;
	static constexpr std::uint32_t kSourceCacheValidationFrames = 120u;
	static constexpr std::size_t kMaximumTraversalObjects = 256u;
	static constexpr std::size_t kMaximumRootDepth = 64u;
	static constexpr std::size_t kMaximumRules = 1024u;
	static constexpr std::size_t kMaximumLandscapeIdentities = 4096u;
	static constexpr std::size_t kLandscapeRegistrySetCount = 1024u;
	static constexpr std::size_t kLandscapeRegistryWays = 4u;
	static constexpr std::size_t kLandscapeRegistryCapacity = kLandscapeRegistrySetCount * kLandscapeRegistryWays;
	static constexpr std::size_t kMaximumMaterialParentDepth = 8u;
	static constexpr std::size_t kMaximumRuleLength = 512u;
	static constexpr std::uint32_t kMaximumClassificationLogs = 64u;
	static_assert(std::has_single_bit(kCacheSetCount));
	static_assert(std::has_single_bit(kLandscapeRegistrySetCount));

	std::array<CacheEntry, kCacheCapacity> classificationCache{};
	std::vector<OverrideRule> allowList;
	std::vector<OverrideRule> detectedAllowList;
	std::vector<OverrideRule> denyList;
	std::vector<CompiledRule> compiledAllowList;
	std::vector<CompiledRule> compiledDenyList;
	ExactRuleSet compiledExactAllowRules;
	ExactRuleSet compiledExactDenyRules;
	std::set<std::string, std::less<>> compiledExactRuleModels;
	std::vector<LandscapeAssignment> landscapeAssignments;
	std::vector<SurfaceMaterialRule> surfaceMaterialRules;
	std::vector<BlendPairRule> blendPairRules;
	bool blendPairsExplicit = false;
	bool landscapeRulesInitialized = false;
	std::map<std::uint32_t, LandscapeMaterialKind> compiledLandscapeForms;
	std::map<std::string, LandscapeMaterialKind, std::less<>> compiledLandscapeEditorIDs;
	std::map<std::string, LandscapeMaterialKind, std::less<>> compiledLandscapeDiffusePaths;
	std::map<std::string, LandscapeMaterialKind, std::less<>> compiledSurfaceMaterials;
	std::set<std::pair<LandscapeMaterialKind, LandscapeMaterialKind>> compiledBlendPairs;
	mutable std::shared_mutex landscapeRulesMutex;
	json ruleFileDocument = json::object();
	bool flexibleRulesNeedNodePath = false;
	std::uint32_t policyGeneration = 1u;
	std::uint32_t cachedEyeFrame = static_cast<std::uint32_t>(-1);
	RE::NiPoint3 cachedEyePosition{};
	std::uint32_t classificationLogs = 0u;
	Diagnostics diagnostics{};
	std::atomic_bool discoveryCaptureEnabled = false;
	mutable std::mutex discoveryMutex;
	std::set<std::pair<std::string, std::string>> discoveredRuleKeys;
	std::map<std::string, LandscapeDiagnostic, std::less<>> discoveredLandscapeIdentities;
	std::vector<LandscapeDiagnostic> savedDiscoveryDiagnostics;
	std::size_t discoveryDuplicateObservations = 0u;
	std::size_t discoveryDropped = 0u;
	std::size_t landscapeDiscoveryDuplicateObservations = 0u;
	std::size_t landscapeDiscoveryDropped = 0u;
	std::string discoveryStatus;
	bool ruleFileLoadAttempted = false;
	std::atomic_bool ruleFileMutationAllowed = true;
	std::string ruleFileLoadError;
	std::array<LandscapeRegistryEntry, kLandscapeRegistryCapacity> landscapeRegistry{};
	mutable std::mutex landscapeRegistryMutex;
	std::atomic_uint64_t landscapeRegistryWriteSerial = 1u;
	std::atomic_uint32_t landscapePolicyGeneration = 1u;

	void SanitizeSettings();
	void RebuildRules();
	void RebuildLandscapeRules();
	void InvalidateClassificationCache();
	void InvalidateLandscapeRegistry();
	void BeginDiagnosticsFrame(std::uint32_t a_frame);
	bool BuildSourceState(RE::BSRenderPass* a_pass, SourceState& a_source) const;
	bool ResolveStaticOwner(SourceState& a_source) const;
	bool IsInsideDistanceBubble(const RE::NiBound& a_bound, std::uint32_t a_frame);
	Signature BuildSourceSignature(const SourceState& a_source) const;
	void CompleteOwnershipSignature(const SourceState& a_source, Signature& a_signature) const;
	bool TryGetCachedClassification(
		const Signature& a_signature,
		std::uint32_t a_frame,
		bool a_sourceStateOnly,
		Classification& a_classification);
	void StoreClassification(const Signature& a_signature, std::uint32_t a_frame, Classification a_classification);
	Classification GetSourceClassification(
		SourceState& a_source,
		std::uint32_t a_frame,
		bool a_captureDiscovery,
		bool a_collectDiagnostics);
	Classification ClassifyOnCacheMiss(const SourceState& a_source, bool a_captureDiscovery);
	bool HasOpaqueSibling(const SourceState& a_source, bool& a_hitTraversalLimit);
	bool IsSafeReceiver(const SourceState& a_source, RE::BSGeometry* a_receiver) const;
	bool BoundsOverlap(const RE::NiBound& a_source, const RE::NiBound& a_receiver) const;
	bool MatchesRules(
		const std::vector<CompiledRule>& a_rules,
		const ExactRuleSet& a_exactRules,
		std::string_view a_model,
		std::string_view a_nodePath) const;
	std::string BuildModelPath(const SourceState& a_source) const;
	std::string BuildNodePath(const SourceState& a_source) const;
	void DrawDiagnostics() const;
	bool IsDiscoveryCaptureActive() const;
	bool IsDiscoveryCaptureSaturated() const;
	void SetDiscoveryCaptureEnabled(bool a_enabled);
	void CaptureDiscoveredRule(std::string_view a_modelPath, std::string_view a_nodePath);
	void CaptureDiscoveredLandscape(RE::TESLandTexture* a_texture, LandscapeMaterialKind a_kind);
	static std::optional<LandscapeMaterialKind> ParseLandscapeMaterialKind(std::string_view a_kind);
	static const char* CanonicalLandscapeMaterialKind(LandscapeMaterialKind a_kind);
	static LandscapeMaterialClass GetLandscapeMaterialClass(LandscapeMaterialKind a_kind);
	LandscapeMaterialKind ClassifyLandscapeTexture(RE::TESLandTexture* a_texture) const;
	LandscapeMaterialKind ClassifySurfaceMaterial(const RE::BGSMaterialType* a_material) const;
	void StoreLandscapeClasses(
		RE::BSGeometry* a_geometry,
		RE::BSLightingShaderProperty* a_shaderProperty,
		RE::BSShaderMaterial* a_material,
		std::uint32_t a_packedClasses);
	bool TryGetLandscapeClasses(RE::BSRenderPass* a_pass, std::uint32_t& a_packedClasses);
	bool TryPrepareProjectedSnow(RE::BSRenderPass* a_pass);
	void EnsureRuleFileLoaded();
	bool LoadRuleFile();
	bool ReadRuleFileState(RuleFileState& a_state, bool& a_notFound, std::string& a_error) const;
	void ApplyRuleFileState(RuleFileState&& a_state);
	bool WriteRuleFile(const RuleFileState& a_state, std::string& a_error) const;
	DiscoverySaveResult SaveDiscoveredRules();
	bool ClearSavedRules(std::string& a_error);
	void ClearDiscoveredRules();
	void DrawDiscoverySettings();
};
