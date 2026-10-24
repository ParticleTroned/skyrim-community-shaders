#pragma once

#include "MeshBlendingPolicy.h"

#include "../Feature.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * Softens eligible static NIF overlays without adding a render pass.
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
		std::uint32_t NifEnabled = 1u;
		float BlendStrength = 1.0f;
		float BlendWidth = 12.0f;
		float DepthBias = 0.25f;
		float MaximumGap = 64.0f;
		std::uint32_t ProjectedSnowEnabled = 1u;
		float ProjectedSnowEdgeWidth = 2.0f;

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
		std::uint32_t Padding0 = 0u;
		float ProjectedSnowEdgeWidth = 2.0f;
		float Padding2 = 0.0f;
		float Padding3 = 0.0f;
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

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Inspect or update session controls on the main thread. */
	json ControlDevBench(const json& a_request);
#endif

	/** Returns the sanitized GPU settings uploaded once per frame. */
	PerFrame GetCommonBufferData() const;
	/** Returns whether the configured policy can currently select any draw. */
	bool IsRuntimeEnabled() const;

	/**
	 * Clears and conditionally sets the per-draw descriptor for a Lighting pass.
	 * This must run before the engine's original SetupGeometry call.
	 */
	void PrepareLightingDraw(RE::BSRenderPass* a_pass);

private:
	using Classification = CSX::MeshBlendingPolicy::CachedClassification;
	using CompiledRules = CSX::MeshBlendingPolicy::CompiledRules;
	static constexpr std::size_t kMaximumRootDepth = 64u;

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
		std::array<CSX::MeshBlendingPolicy::NodePathPart, kMaximumRootDepth> nodeParts{};
		std::size_t nodePathLength = 0u;

		[[nodiscard]] auto NodePath() const { return std::span(nodeParts.data(), nodePathLength); }
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

	struct ReceiverRoute
	{
		std::array<std::uint32_t, kMaximumRootDepth> indices{};
		std::size_t length = 0u;
		bool valid = false;
	};

	struct CacheEntry
	{
		Signature signature{};
		CSX::MeshBlendingPolicy::RuleIdentity ruleIdentity;
		ReceiverRoute receiverRoute;
		RE::BSGeometry* receiver = nullptr;
		Classification classification = Classification::kRejected;
		std::uint32_t classifiedFrame = 0u;
		std::uint32_t lastUsedFrame = 0u;
		bool occupied = false;
	};

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
		std::uint32_t currentFrame = 0u;
		std::uint32_t currentFrameActive = 0u;
		std::uint32_t lastFrameActive = 0u;
	};

	struct DiscoverySaveResult
	{
		bool success = false;
		std::size_t added = 0u;
		std::size_t alreadyAllowed = 0u;
		std::size_t denied = 0u;
		std::size_t capacityRejected = 0u;
		std::string error;
	};

	struct RuleFileState
	{
		std::vector<OverrideRule> allowList;
		std::vector<OverrideRule> detectedAllowList;
		std::vector<OverrideRule> denyList;
		json document = json::object();
		std::optional<std::string> sourceContents;
	};

	static constexpr std::size_t kCacheSetCount = 1024u;
	static constexpr std::size_t kCacheWays = 4u;
	static constexpr std::size_t kCacheCapacity = kCacheSetCount * kCacheWays;
	static constexpr std::uint32_t kCacheValidationFrames = 600u;
	static constexpr std::uint32_t kSourceCacheValidationFrames = 120u;
	static constexpr std::size_t kMaximumTraversalObjects = 256u;
	static constexpr std::size_t kMaximumRules = 1024u;
	static constexpr std::size_t kMaximumRuleLength = 512u;
	static constexpr std::uint32_t kMaximumClassificationLogs = 64u;
	static_assert(std::has_single_bit(kCacheSetCount));

	std::array<CacheEntry, kCacheCapacity> classificationCache{};
	std::vector<OverrideRule> allowList;
	std::vector<OverrideRule> detectedAllowList;
	std::vector<OverrideRule> denyList;
	CompiledRules compiledRules;
	json ruleFileDocument = json::object();
	std::uint32_t policyGeneration = 1u;
	std::uint32_t cachedEyeFrame = static_cast<std::uint32_t>(-1);
	RE::NiPoint3 cachedEyePosition{};
	std::uint32_t classificationLogs = 0u;
	Diagnostics diagnostics{};
	std::atomic_bool discoveryCaptureEnabled = false;
	mutable std::mutex discoveryMutex;
	std::set<std::pair<std::string, std::string>> discoveredRuleKeys;
	std::size_t discoveryDuplicateObservations = 0u;
	std::size_t discoveryDropped = 0u;
	std::string discoveryStatus;
	bool ruleFileLoadAttempted = false;
	std::atomic_bool ruleFileMutationAllowed = true;
	std::string ruleFileLoadError;

	void SanitizeSettings();
	static Settings Sanitize(Settings a_settings);
	bool IsOverlayPolicyEnabled() const;
	void DrawMechanismToggles();
	static CompiledRules CompileRules(const RuleFileState& a_state);
	void InvalidateClassificationCache();
	void BeginDiagnosticsFrame(std::uint32_t a_frame);
	/** Validates alpha overlays; each caller applies its narrower material/flag policy. */
	bool BuildOverlaySourceState(RE::BSRenderPass* a_pass, SourceState& a_source) const;
	bool BuildSourceState(RE::BSRenderPass* a_pass, SourceState& a_source) const;
	bool ResolveStaticOwner(SourceState& a_source) const;
	bool IsInsideDistanceBubble(const RE::NiBound& a_bound, std::uint32_t a_frame);
	Signature BuildSourceSignature(const SourceState& a_source) const;
	void CompleteOwnershipSignature(const SourceState& a_source, Signature& a_signature) const;
	CacheEntry* FindCachedClassification(
		const Signature& a_signature,
		std::uint32_t a_frame,
		bool a_sourceStateOnly);
	void StoreClassification(
		const SourceState& a_source,
		const Signature& a_signature,
		std::uint32_t a_frame,
		Classification a_classification,
		RE::BSGeometry* a_receiver = nullptr);
	Classification GetSourceClassification(
		SourceState& a_source,
		std::uint32_t a_frame,
		bool a_captureDiscovery,
		bool a_collectDiagnostics);
	Classification ClassifyOnCacheMiss(
		const SourceState& a_source,
		bool a_captureDiscovery,
		RE::BSGeometry*& a_receiver);
	bool HasOpaqueSibling(
		const SourceState& a_source,
		bool& a_hitTraversalLimit,
		RE::BSGeometry*& a_receiver);
	ReceiverRoute BuildReceiverRoute(const SourceState& a_source, RE::BSGeometry* a_receiver) const;
	bool ValidateCachedReceiver(const SourceState& a_source, const CacheEntry& a_entry) const;
	bool IsSafeReceiver(const SourceState& a_source, RE::BSGeometry* a_receiver) const;
	bool BoundsOverlap(const RE::NiBound& a_source, const RE::NiBound& a_receiver) const;
	std::string BuildModelPath(const SourceState& a_source) const;
	std::string BuildNodePath(const SourceState& a_source) const;
	void DrawDiagnostics() const;
	bool IsDiscoveryCaptureActive() const;
	bool IsDiscoveryCaptureSaturated() const;
	void SetDiscoveryCaptureEnabled(bool a_enabled);
	void CaptureDiscoveredRule(std::string_view a_modelPath, std::string_view a_nodePath);
	bool TryPrepareProjectedSnow(RE::BSRenderPass* a_pass);
	void EnsureRuleFileLoaded();
	bool LoadRuleFile();
	bool ReadRuleFileState(RuleFileState& a_state, bool& a_notFound, std::string& a_error) const;
	void ApplyRuleFileState(RuleFileState&& a_state, CompiledRules&& a_compiledRules);
	bool WriteRuleFile(const RuleFileState& a_state, std::string& a_error) const;
	DiscoverySaveResult SaveDiscoveredRules();
	bool ClearSavedRules(std::string& a_error);
	void ClearDiscoveredRules();
	void DrawDiscoverySettings();
};
