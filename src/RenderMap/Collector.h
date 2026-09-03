#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace CSX::RenderMap
{
	inline constexpr std::uint64_t kUnknownFrame = std::numeric_limits<std::uint64_t>::max();
	inline constexpr std::size_t kMaximumScopeDepth = 32;
	inline constexpr std::size_t kMaximumShaderNameLength = 127;
	inline constexpr std::size_t kMaximumShaderDefinesSuffixLength = 95;
	inline constexpr std::size_t kMaximumShaderCachePathLength = 383;
	inline constexpr std::size_t kMaximumEngineShaderAliasesPerStage = 8;
	inline constexpr std::size_t kSha256HexLength = 64;
	inline constexpr std::size_t kMaximumRenderTargets = 8;
	inline constexpr std::size_t kMaximumShaderResourceSlots = 128;
	inline constexpr std::size_t kMaximumUnorderedAccessSlots = 64;
	inline constexpr std::size_t kMaximumSceneObjectNameLength = 127;
	inline constexpr std::size_t kMaximumGeometryNameLength = 127;
	inline constexpr std::size_t kMaximumRuntimeTypeNameLength = 95;
	inline constexpr std::size_t kMaximumMaterialTextureBindings = 32;
	inline constexpr std::size_t kMaximumTexturePathLength = 383;

	enum class EventKind : std::uint16_t
	{
		kCaptureMarker,
		kGap,
		kFrameBegin,
		kFrameEnd,
		kSceneAccumulationBegin,
		kSceneAccumulationEnd,
		kEyeBegin,
		kEyeEnd,
		kObjectObserved,
		kGeometryObserved,
		kMaterialObserved,
		kRenderPassCreated,
		kRenderPassEnter,
		kRenderPassExit,
		kTechniqueBegin,
		kTechniqueEnd,
		kGeometrySetupBegin,
		kGeometrySetupEnd,
		kPipelineObjectCreated,
		kPipelineBind,
		kRenderTargetBind,
		kDepthSourceReady,
		kVisibilityCandidate,
		kVisibilityResultReady,
		kVisibilityConsumed,
		kCullDecision,
		kDraw,
		kDispatch,
		kCommandRecordingObserved,
		kCommandListObserved,
		kFinishCommandList,
		kExecuteCommandList,
		kShaderObserved,
		kStageShaderObserved,
		kTechniqueResolved,
		kDeviceContextObserved,
		kTargetViewObserved,
		kResourceObserved,
		kResourceViewBind,
		kResourceViewStateObserved,
		kResourceFlow,
		kResourceCpuAccess,
		kResourceVersionObserved,
		kEyeSubmitted,
		kCount,
	};

	using EventKindMask = std::uint64_t;
	static_assert(static_cast<std::size_t>(EventKind::kCount) <= 64);

	inline constexpr EventKindMask EventKindBit(EventKind a_kind) noexcept
	{
		return a_kind < EventKind::kCount ?
		           EventKindMask{ 1 } << static_cast<std::uint16_t>(a_kind) :
		           0;
	}

	inline constexpr EventKindMask kAllEventKindsMask =
		(EventKindMask{ 1 } << static_cast<std::uint16_t>(EventKind::kCount)) - 1;
	inline constexpr std::uint16_t kEventFlagDeferredContext = 1u << 0;

	const char* EventKindName(EventKind a_kind) noexcept;
	EventKindMask ResolveEventKindDependencies(EventKindMask a_requested) noexcept;
	void NormalizeEventKindSelection(struct CollectorConfig& a_config) noexcept;

	enum class Eye : std::uint8_t
	{
		kUnknown,
		kLeft,
		kRight,
		kBoth,
		kMono,
	};

	enum class ScopeKind : std::uint8_t
	{
		kRenderPass,
		kTechnique,
		kGeometry,
		kCommandList,
		kCount,
	};

	enum class StartResult : std::uint8_t
	{
		kStarted,
		kAlreadyCapturing,
		kInvalidBounds,
		kAllocationFailed,
	};

	enum class RecordResult : std::uint8_t
	{
		kRecorded,
		kFiltered,
		kInactive,
		kStopped,
		kEventLimit,
		kByteLimit,
		kFrameLimit,
		kTimeLimit,
	};

	enum class StopReason : std::uint8_t
	{
		kRequested,
		kFrameLimit,
		kTimeLimit,
		kEventLimit,
		kByteLimit,
		kShutdown,
		kFailure,
	};

	struct CollectorConfig
	{
		std::uint64_t captureNumericId{ 0 };
		std::uint64_t maxFrames{ 1 };
		std::uint64_t maxEvents{ 1 };
		std::uint64_t maxBytes{ 1 };
		std::chrono::nanoseconds maxDuration{ std::chrono::seconds(1) };
		std::uint8_t maxScopeDepth{ 8 };
		std::uint32_t maxShaderObservations{ 1024 };
		std::uint32_t maxStageShaderObservations{ 4096 };
		std::uint32_t maxResourceObservations{ 4096 };
		std::uint32_t maxTargetViewObservations{ 4096 };
		std::uint32_t maxTargetBindingObservations{ 4096 };
		std::uint32_t maxSceneObjectObservations{ 2048 };
		std::uint32_t maxGeometryObservations{ 4096 };
		std::uint32_t maxMaterialStateObservations{ 4096 };
		std::uint64_t geometryShaderTypeMask{ (std::numeric_limits<std::uint64_t>::max)() };
		bool executionWithinSelectedGeometry{ false };
		EventKindMask requestedEventKindMask{ kAllEventKindsMask };
		EventKindMask eventKindMask{ kAllEventKindsMask };
	};

	struct FrameContext
	{
		std::uint64_t cpuFrame{ kUnknownFrame };
		std::uint64_t sceneEpoch{ kUnknownFrame };
		std::uint64_t submissionEpoch{ kUnknownFrame };
		Eye eye{ Eye::kUnknown };
		std::uint8_t eyeMask{ 0 };
	};

	struct ScopeBinding
	{
		std::uint64_t token{ 0 };
		std::uint64_t observationId{ 0 };
	};

	struct ScopeSnapshot
	{
		ScopeBinding renderPass;
		ScopeBinding technique;
		ScopeBinding geometry;
		ScopeBinding commandList;
	};

	struct EventPayload
	{
		std::uint16_t schema{ 0 };
		std::array<std::uint64_t, 8> words{};
	};

	struct EventRecord
	{
		std::uint16_t schemaMajor{ 1 };
		std::uint16_t schemaMinor{ 17 };
		EventKind kind{ EventKind::kCaptureMarker };
		std::uint16_t reserved{ 0 };
		std::uint64_t captureNumericId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint64_t sequence{ 0 };
		std::uint64_t timestampTicks{ 0 };
		std::uint64_t threadId{ 0 };
		std::uint64_t deviceContextObservationId{ 0 };
		std::uint64_t commandStreamSequence{ 0 };
		std::uint64_t targetBindingObservationId{ 0 };
		std::uint64_t submissionObservationId{ 0 };
		std::uint64_t preparedGeometrySetupObservationId{ 0 };
		FrameContext frame;
		ScopeSnapshot scopes;
		EventPayload payload;
	};

	static_assert(std::is_trivially_copyable_v<EventRecord>);
	static_assert(sizeof(EventRecord) <= 256);

	struct CaptureStatistics
	{
		std::uint64_t attempted{ 0 };
		std::uint64_t recorded{ 0 };
		std::uint64_t filtered{ 0 };
		std::uint64_t droppedStopped{ 0 };
		std::uint64_t droppedEventLimit{ 0 };
		std::uint64_t droppedByteLimit{ 0 };
		std::uint64_t droppedFrameLimit{ 0 };
		std::uint64_t droppedTimeLimit{ 0 };
		std::uint64_t scopeOverflow{ 0 };
		std::uint64_t scopeMismatch{ 0 };
		std::uint64_t droppedShaderObservations{ 0 };
		std::uint64_t droppedStageShaderObservations{ 0 };
		std::uint64_t droppedResourceObservations{ 0 };
		std::uint64_t droppedTargetViewObservations{ 0 };
		std::uint64_t droppedTargetBindingObservations{ 0 };
		std::uint64_t droppedSceneObjectObservations{ 0 };
		std::uint64_t droppedGeometryObservations{ 0 };
		std::uint64_t droppedMaterialStateObservations{ 0 };
	};

	enum class ShaderStage : std::uint8_t
	{
		kVertex = 1,
		kPixel = 2,
		kCompute = 3,
	};

	struct ShaderObservationInput
	{
		std::uintptr_t shader{ 0 };
		std::uint32_t shaderType{ 0 };
		std::string_view fxpFilename;
		std::string_view imageSpaceName;
		std::string_view compileSourceName;
		std::string_view definesSuffix;
	};

	struct ShaderObservationRecord
	{
		std::uint64_t observationId{ 0 };
		std::uintptr_t pointerEvidence{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		std::uint32_t shaderType{ 0 };
		std::array<char, kMaximumShaderNameLength + 1> fxpFilename{};
		std::array<char, kMaximumShaderNameLength + 1> imageSpaceName{};
		std::array<char, kMaximumShaderNameLength + 1> compileSourceName{};
		std::array<char, kMaximumShaderDefinesSuffixLength + 1> definesSuffix{};
		bool fxpFilenameTruncated{ false };
		bool imageSpaceNameTruncated{ false };
		bool compileSourceNameTruncated{ false };
		bool definesSuffixTruncated{ false };
	};

	struct ShaderObservationResult
	{
		std::uint64_t observationId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		bool firstSeen{ false };
	};

	struct StageShaderObservationInput
	{
		struct EngineAlias
		{
			std::string_view loaderType;
			std::string_view compileSourceName;
			std::uint32_t descriptor{ 0 };
		};

		ShaderStage stage{ ShaderStage::kVertex };
		std::uintptr_t wrapper{ 0 };
		std::uintptr_t d3dObject{ 0 };
		std::uint32_t wrapperDescriptor{ 0 };
		std::uint64_t bytecodeSize{ 0 };
		std::string_view bytecodeSha256;
		std::string_view cachePath;
		const EngineAlias* engineAliases{ nullptr };
		std::uint32_t engineAliasCount{ 0 };
		std::uint32_t engineAliasTotalCount{ 0 };
	};

	struct EngineShaderAliasRecord
	{
		std::array<char, kMaximumShaderNameLength + 1> loaderType{};
		std::array<char, kMaximumShaderNameLength + 1> compileSourceName{};
		std::uint32_t descriptor{ 0 };
		bool loaderTypeTruncated{ false };
		bool compileSourceNameTruncated{ false };
	};

	struct StageShaderObservationRecord
	{
		std::uint64_t observationId{ 0 };
		ShaderStage stage{ ShaderStage::kVertex };
		std::uintptr_t wrapperEvidence{ 0 };
		std::uintptr_t pointerEvidence{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		std::uint32_t wrapperDescriptor{ 0 };
		std::uint64_t bytecodeSize{ 0 };
		std::array<char, kSha256HexLength + 1> bytecodeSha256{};
		std::array<char, kMaximumShaderCachePathLength + 1> cachePath{};
		std::array<EngineShaderAliasRecord, kMaximumEngineShaderAliasesPerStage> engineAliases{};
		std::uint32_t engineAliasCount{ 0 };
		std::uint32_t engineAliasTotalCount{ 0 };
		bool bytecodeSha256Truncated{ false };
		bool cachePathTruncated{ false };
		bool engineAliasesTruncated{ false };
	};

	struct StageShaderObservationResult
	{
		std::uint64_t observationId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		bool firstSeen{ false };
	};

	enum class TargetViewKind : std::uint8_t
	{
		kRenderTarget = 1,
		kDepthTarget = 2,
		kShaderResource = 3,
		kUnorderedAccess = 4,
	};

	enum class ResourceDimension : std::uint8_t
	{
		kUnknown = 0,
		kBuffer = 1,
		kTexture1D = 2,
		kTexture2D = 3,
		kTexture3D = 4,
	};

	enum class ResourceStage : std::uint8_t
	{
		kVertex = 1,
		kHull = 2,
		kDomain = 3,
		kGeometry = 4,
		kPixel = 5,
		kCompute = 6,
		kOutputMerger = 7,
	};

	enum class ResourceBindingKind : std::uint8_t
	{
		kShaderResource = 1,
		kUnorderedAccess = 2,
	};

	struct ResourceObservationInput
	{
		std::uintptr_t d3dObject{ 0 };
		ResourceDimension dimension{ ResourceDimension::kUnknown };
		std::uint64_t widthOrBytes{ 0 };
		std::uint32_t height{ 0 };
		std::uint32_t depthOrArraySize{ 0 };
		std::uint32_t mipLevels{ 0 };
		std::uint32_t format{ 0 };
		std::uint32_t sampleCount{ 0 };
		std::uint32_t sampleQuality{ 0 };
		std::uint32_t usage{ 0 };
		std::uint32_t bindFlags{ 0 };
		std::uint32_t cpuAccessFlags{ 0 };
		std::uint32_t miscFlags{ 0 };
		std::uint32_t structureByteStride{ 0 };
	};

	struct ResourceObservationRecord : ResourceObservationInput
	{
		std::uint64_t observationId{ 0 };
		std::uint32_t pointerGeneration{ 0 };
	};

	struct ResourceObservationResult
	{
		std::uint64_t observationId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		bool firstSeen{ false };
	};

	struct TargetViewObservationInput
	{
		TargetViewKind kind{ TargetViewKind::kRenderTarget };
		std::uintptr_t d3dObject{ 0 };
		std::uint64_t resourceObservationId{ 0 };
		std::uint32_t format{ 0 };
		std::uint32_t dimension{ 0 };
		std::uint32_t mipSlice{ 0 };
		std::uint32_t firstArraySlice{ 0 };
		std::uint32_t arraySize{ 0 };
		std::uint32_t firstElement{ 0 };
		std::uint32_t elementCount{ 0 };
		std::uint32_t flags{ 0 };
	};

	struct TargetViewObservationRecord
	{
		std::uint64_t observationId{ 0 };
		TargetViewKind kind{ TargetViewKind::kRenderTarget };
		std::uintptr_t pointerEvidence{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		std::uint64_t resourceObservationId{ 0 };
		std::uint32_t format{ 0 };
		std::uint32_t dimension{ 0 };
		std::uint32_t mipSlice{ 0 };
		std::uint32_t firstArraySlice{ 0 };
		std::uint32_t arraySize{ 0 };
		std::uint32_t firstElement{ 0 };
		std::uint32_t elementCount{ 0 };
		std::uint32_t flags{ 0 };
	};

	struct TargetViewObservationResult
	{
		std::uint64_t observationId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		bool firstSeen{ false };
	};

	struct TargetBindingObservationInput
	{
		std::array<std::uint64_t, kMaximumRenderTargets> renderTargetObservationIds{};
		std::uint64_t depthTargetObservationId{ 0 };
		std::uint8_t renderTargetCount{ 0 };
	};

	struct TargetBindingObservationRecord
	{
		std::uint64_t observationId{ 0 };
		std::array<std::uint64_t, kMaximumRenderTargets> renderTargetObservationIds{};
		std::uint64_t depthTargetObservationId{ 0 };
		std::uint8_t renderTargetCount{ 0 };
	};

	struct TargetBindingObservationResult
	{
		std::uint64_t observationId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		bool firstSeen{ false };
	};

	struct SceneObjectObservationInput
	{
		std::uintptr_t reference{ 0 };
		std::uint32_t referenceFormId{ 0 };
		std::uint32_t baseFormId{ 0 };
		std::string_view referenceName;
		std::string_view baseFormName;
		bool referenceFormDynamic{ false };
		bool baseFormDynamic{ false };
	};

	struct SceneObjectObservationRecord
	{
		std::uint64_t observationId{ 0 };
		std::uintptr_t pointerEvidence{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		std::uint32_t referenceFormId{ 0 };
		std::uint32_t baseFormId{ 0 };
		std::array<char, kMaximumSceneObjectNameLength + 1> referenceName{};
		std::array<char, kMaximumSceneObjectNameLength + 1> baseFormName{};
		bool referenceFormDynamic{ false };
		bool baseFormDynamic{ false };
		bool referenceNameTruncated{ false };
		bool baseFormNameTruncated{ false };
	};

	struct SceneObjectObservationResult
	{
		std::uint64_t observationId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		bool firstSeen{ false };
	};

	struct GeometryObservationInput
	{
		std::uintptr_t geometry{ 0 };
		std::string_view runtimeTypeName;
		std::string_view name;
		std::uint32_t geometryType{ 0 };
		std::uint64_t vertexDescriptor{ 0 };
		std::array<float, 13> worldTransform{};
		std::array<float, 4> worldBound{};
		std::uint64_t sceneObjectObservationId{ 0 };
		bool worldTransformAvailable{ false };
		bool worldBoundAvailable{ false };
	};

	struct GeometryObservationRecord
	{
		std::uint64_t observationId{ 0 };
		std::uintptr_t pointerEvidence{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		std::array<char, kMaximumRuntimeTypeNameLength + 1> runtimeTypeName{};
		std::array<char, kMaximumGeometryNameLength + 1> name{};
		std::uint32_t geometryType{ 0 };
		std::uint64_t vertexDescriptor{ 0 };
		std::array<float, 13> worldTransform{};
		std::array<float, 4> worldBound{};
		std::uint64_t sceneObjectObservationId{ 0 };
		bool worldTransformAvailable{ false };
		bool worldBoundAvailable{ false };
		bool runtimeTypeNameTruncated{ false };
		bool nameTruncated{ false };
	};

	struct GeometryObservationResult
	{
		std::uint64_t observationId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint32_t pointerGeneration{ 0 };
		bool firstSeen{ false };
	};

	enum class MaterialTextureRole : std::uint8_t
	{
		kUnknown,
		kRuntimeMaterialList,
		kEffectSource,
		kEffectGreyscale,
		kWaterStaticReflection,
		kWaterNormal1,
		kWaterNormal2,
		kWaterNormal3,
		kWaterNormal4,
	};

	struct MaterialTextureBindingInput
	{
		MaterialTextureRole role{ MaterialTextureRole::kUnknown };
		std::uint32_t bindingIndex{ 0 };
		std::uintptr_t niSourceTexture{ 0 };
		std::string_view path;
		ResourceObservationInput resource;
		std::uint64_t resourceObservationId{ 0 };
	};

	struct MaterialTextureBindingRecord
	{
		MaterialTextureRole role{ MaterialTextureRole::kUnknown };
		std::uint32_t bindingIndex{ 0 };
		std::uintptr_t niSourceTextureEvidence{ 0 };
		std::array<char, kMaximumTexturePathLength + 1> path{};
		std::uint64_t resourceObservationId{ 0 };
		bool pathTruncated{ false };
	};

	struct MaterialStateObservationInput
	{
		std::uintptr_t shaderProperty{ 0 };
		std::string_view shaderPropertyRuntimeTypeName;
		std::uint64_t shaderPropertyFlags{ 0 };
		float alpha{ 0.0f };
		std::uint32_t engineMaterialType{ 0 };
		std::uintptr_t material{ 0 };
		std::uint32_t materialType{ 0 };
		std::uint32_t feature{ 0 };
		std::uint32_t hashKey{ 0 };
		std::array<MaterialTextureBindingInput, kMaximumMaterialTextureBindings> textureBindings{};
		std::uint8_t textureBindingCount{ 0 };
		bool shaderPropertyAvailable{ false };
		bool materialAvailable{ false };
		bool textureBindingsTruncated{ false };
	};

	struct MaterialStateObservationRecord
	{
		std::uint64_t observationId{ 0 };
		std::uint32_t stateRevision{ 0 };
		std::uint64_t fingerprint{ 0 };
		std::uintptr_t shaderPropertyEvidence{ 0 };
		std::array<char, kMaximumRuntimeTypeNameLength + 1> shaderPropertyRuntimeTypeName{};
		std::uint64_t shaderPropertyFlags{ 0 };
		float alpha{ 0.0f };
		std::uint32_t engineMaterialType{ 0 };
		std::uintptr_t materialEvidence{ 0 };
		std::uint32_t materialType{ 0 };
		std::uint32_t feature{ 0 };
		std::uint32_t hashKey{ 0 };
		std::array<MaterialTextureBindingRecord, kMaximumMaterialTextureBindings> textureBindings{};
		std::uint8_t textureBindingCount{ 0 };
		bool shaderPropertyAvailable{ false };
		bool materialAvailable{ false };
		bool textureBindingsTruncated{ false };
		bool shaderPropertyRuntimeTypeNameTruncated{ false };
	};

	struct MaterialStateObservationResult
	{
		std::uint64_t observationId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint32_t stateRevision{ 0 };
		std::uint64_t fingerprint{ 0 };
		bool firstSeen{ false };
	};

	struct CaptureSnapshot
	{
		CollectorConfig config;
		std::uint64_t sessionGeneration{ 0 };
		std::uint64_t clockFrequencyHz{ 0 };
		std::uint64_t startTimestampTicks{ 0 };
		std::uint64_t endTimestampTicks{ 0 };
		StopReason stopReason{ StopReason::kRequested };
		CaptureStatistics statistics;
		std::vector<EventRecord> events;
		std::vector<ShaderObservationRecord> shaderObservations;
		std::vector<StageShaderObservationRecord> stageShaderObservations;
		std::vector<ResourceObservationRecord> resourceObservations;
		std::vector<TargetViewObservationRecord> targetViewObservations;
		std::vector<TargetBindingObservationRecord> targetBindingObservations;
		std::vector<SceneObjectObservationRecord> sceneObjectObservations;
		std::vector<GeometryObservationRecord> geometryObservations;
		std::vector<MaterialStateObservationRecord> materialStateObservations;
	};

	class Collector
	{
	private:
		struct Session;

	public:
		class ScopeGuard
		{
		public:
			ScopeGuard() = default;
			~ScopeGuard();
			ScopeGuard(const ScopeGuard&) = delete;
			ScopeGuard& operator=(const ScopeGuard&) = delete;
			ScopeGuard(ScopeGuard&& a_other) noexcept;
			ScopeGuard& operator=(ScopeGuard&& a_other) noexcept;

			bool IsActive() const noexcept;
			std::uint64_t Token() const noexcept;

		private:
			friend class Collector;
			ScopeGuard(
				Collector* a_owner,
				std::uint64_t a_generation,
				ScopeKind a_kind,
				std::uint64_t a_token,
				EventKind a_endKind,
				EventPayload a_endPayload) noexcept;

			void Reset() noexcept;

			Collector* owner{ nullptr };
			std::uint64_t generation{ 0 };
			ScopeKind kind{ ScopeKind::kRenderPass };
			std::uint64_t token{ 0 };
			EventKind endKind{ EventKind::kCaptureMarker };
			EventPayload endPayload;
		};

		Collector();
		~Collector();
		Collector(const Collector&) = delete;
		Collector& operator=(const Collector&) = delete;

		StartResult Start(const CollectorConfig& a_config);
		std::optional<CaptureSnapshot> Stop(
			StopReason a_reason = StopReason::kRequested,
			std::chrono::milliseconds a_drainTimeout = std::chrono::milliseconds(100));
		bool IsCapturing() const noexcept;
		bool IsDraining() const noexcept;
		std::uint64_t ActiveGeneration() const noexcept;
		bool IsGeometryShaderTypeSelected(std::uint32_t a_shaderType) const noexcept;
		bool IsExecutionAllowedByGeometryScope(
			std::uint64_t a_preparedGeometrySetupObservationId = 0) const noexcept;
		void CountFiltered(std::uint64_t a_count = 1) noexcept;

		RecordResult Record(
			EventKind a_kind,
			const EventPayload& a_payload = {},
			std::uint64_t a_deviceContextObservationId = 0,
			std::uint64_t a_commandStreamSequence = 0,
			std::uint64_t a_targetBindingObservationId = 0,
			std::uint64_t a_submissionObservationId = 0,
			std::uint64_t a_preparedGeometrySetupObservationId = 0,
			std::uint64_t a_commandRecordingObservationId = 0,
			bool a_deferredContext = false) noexcept;
		RecordResult RecordForGeneration(
			EventKind a_kind,
			const EventPayload& a_payload,
			std::uint64_t a_deviceContextObservationId,
			std::uint64_t a_expectedGeneration,
			std::uint64_t a_commandStreamSequence = 0,
			std::uint64_t a_targetBindingObservationId = 0,
			std::uint64_t a_submissionObservationId = 0,
			std::uint64_t a_preparedGeometrySetupObservationId = 0,
			std::uint64_t a_commandRecordingObservationId = 0,
			bool a_deferredContext = false) noexcept;

		ScopeGuard EnterScope(
			ScopeKind a_kind,
			std::uint64_t a_observationId,
			EventKind a_beginKind,
			EventKind a_endKind,
			const EventPayload& a_beginPayload = {},
			const EventPayload& a_endPayload = {},
			std::uint64_t a_expectedGeneration = 0) noexcept;

		std::uint64_t AllocateObservationId(std::uint64_t a_expectedGeneration = 0) noexcept;
		ShaderObservationResult ObserveShader(const ShaderObservationInput& a_input) noexcept;
		StageShaderObservationResult ObserveStageShader(const StageShaderObservationInput& a_input) noexcept;
		StageShaderObservationResult FindStageShader(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject) noexcept;
		ResourceObservationResult ObserveResource(const ResourceObservationInput& a_input) noexcept;
		TargetViewObservationResult ObserveTargetView(const TargetViewObservationInput& a_input) noexcept;
		TargetBindingObservationResult ObserveTargetBinding(const TargetBindingObservationInput& a_input) noexcept;
		SceneObjectObservationResult ObserveSceneObject(const SceneObjectObservationInput& a_input) noexcept;
		GeometryObservationResult ObserveGeometry(const GeometryObservationInput& a_input) noexcept;
		MaterialStateObservationResult ObserveMaterialState(const MaterialStateObservationInput& a_input) noexcept;
		void RetireShaderObservation(std::uintptr_t a_shader) noexcept;
		void SetThreadFrameContext(const FrameContext& a_context) noexcept;
		FrameContext GetThreadFrameContext() const noexcept;
		ScopeSnapshot GetThreadScopes() const noexcept;

		static constexpr std::size_t EventRecordSize() noexcept { return sizeof(EventRecord); }
		static std::uint64_t RequiredStorageBytes(const CollectorConfig& a_config) noexcept;
		static std::uint64_t ClockFrequencyHz() noexcept;

	private:
		void ExitScope(
			std::uint64_t a_generation,
			ScopeKind a_kind,
			std::uint64_t a_token,
			EventKind a_endKind,
			const EventPayload& a_endPayload) noexcept;

		std::atomic<std::shared_ptr<Session>> activeSession;
		mutable std::mutex stopMutex;
		std::shared_ptr<Session> drainingSession;
		std::atomic_bool draining{ false };
	};
}
