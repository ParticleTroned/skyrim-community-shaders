#include "RenderMap/Collector.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Windows.h>
#endif

namespace CSX::RenderMap
{
	const char* EventKindName(EventKind a_kind) noexcept
	{
		static constexpr std::array names{
			"capture-marker", "gap", "frame-begin", "frame-end",
			"scene-accumulation-begin", "scene-accumulation-end", "eye-begin", "eye-end",
			"object-observed", "geometry-observed", "material-observed", "render-pass-created",
			"render-pass-enter", "render-pass-exit", "technique-begin", "technique-end",
			"geometry-setup-begin", "geometry-setup-end", "pipeline-object-created", "pipeline-bind",
			"render-target-bind", "depth-source-ready", "visibility-candidate", "visibility-result-ready",
			"visibility-consumed", "cull-decision", "draw", "dispatch", "finish-command-list",
			"execute-command-list", "shader-observed", "stage-shader-observed", "technique-resolved",
			"device-context-observed", "target-view-observed", "resource-observed",
			"resource-view-bind", "resource-view-state-observed", "resource-flow", "resource-cpu-access", "resource-version-observed", "eye-submitted",
		};
		const auto index = static_cast<std::size_t>(a_kind);
		return index < names.size() ? names[index] : "gap";
	}

	EventKindMask ResolveEventKindDependencies(EventKindMask a_requested) noexcept
	{
		a_requested &= kAllEventKindsMask;
		auto resolved = a_requested;
		const auto add = [&](EventKind a_kind) { resolved |= EventKindBit(a_kind); };
		const auto has = [&](EventKind a_kind) { return (resolved & EventKindBit(a_kind)) != 0; };
		do {
			const auto before = resolved;
			const auto pair = [&](EventKind a_begin, EventKind a_end) {
				if (has(a_begin) || has(a_end)) {
					add(a_begin);
					add(a_end);
				}
			};
			pair(EventKind::kFrameBegin, EventKind::kFrameEnd);
			pair(EventKind::kSceneAccumulationBegin, EventKind::kSceneAccumulationEnd);
			pair(EventKind::kEyeBegin, EventKind::kEyeEnd);
			pair(EventKind::kRenderPassEnter, EventKind::kRenderPassExit);
			pair(EventKind::kTechniqueBegin, EventKind::kTechniqueEnd);
			pair(EventKind::kGeometrySetupBegin, EventKind::kGeometrySetupEnd);
			if (has(EventKind::kGeometrySetupBegin)) {
				add(EventKind::kObjectObserved);
				add(EventKind::kGeometryObserved);
				add(EventKind::kMaterialObserved);
			}

			if (has(EventKind::kRenderPassEnter)) add(EventKind::kRenderPassCreated);
			if (has(EventKind::kTechniqueBegin)) add(EventKind::kShaderObserved);
			if (has(EventKind::kPipelineBind)) add(EventKind::kPipelineObjectCreated);
			if (has(EventKind::kStageShaderObserved)) add(EventKind::kShaderObserved);
			if (has(EventKind::kTechniqueResolved)) {
				add(EventKind::kShaderObserved);
				add(EventKind::kStageShaderObserved);
			}
			if (has(EventKind::kMaterialObserved)) add(EventKind::kResourceObserved);

			if (has(EventKind::kRenderTargetBind) || has(EventKind::kDepthSourceReady) ||
				has(EventKind::kResourceViewBind) || has(EventKind::kResourceViewStateObserved) ||
				has(EventKind::kVisibilityResultReady) ||
				has(EventKind::kVisibilityConsumed)) {
				add(EventKind::kTargetViewObserved);
				add(EventKind::kResourceObserved);
			}
			if (has(EventKind::kResourceFlow) || has(EventKind::kResourceCpuAccess) ||
				has(EventKind::kResourceVersionObserved) ||
				has(EventKind::kEyeSubmitted)) {
				add(EventKind::kResourceObserved);
			}
			if (has(EventKind::kVisibilityResultReady)) add(EventKind::kResourceVersionObserved);

			if (has(EventKind::kRenderTargetBind) || has(EventKind::kResourceViewBind) ||
				has(EventKind::kResourceViewStateObserved) ||
				has(EventKind::kResourceFlow) || has(EventKind::kResourceCpuAccess) ||
				has(EventKind::kResourceVersionObserved) ||
				has(EventKind::kVisibilityResultReady) || has(EventKind::kVisibilityConsumed) ||
				has(EventKind::kDraw) || has(EventKind::kDispatch) ||
				has(EventKind::kFinishCommandList) || has(EventKind::kExecuteCommandList)) {
				add(EventKind::kDeviceContextObserved);
			}

			if (has(EventKind::kDraw) || has(EventKind::kDispatch)) {
				add(EventKind::kStageShaderObserved);
				add(EventKind::kRenderTargetBind);
				add(EventKind::kResourceViewBind);
				add(EventKind::kResourceViewStateObserved);
			}
			if (resolved == before)
				break;
		} while (true);
		return resolved;
	}

	void NormalizeEventKindSelection(CollectorConfig& a_config) noexcept
	{
		a_config.requestedEventKindMask &= kAllEventKindsMask;
		a_config.eventKindMask = ResolveEventKindDependencies(a_config.requestedEventKindMask);
	}

	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct ScopeFrame
		{
			std::uint64_t token{ 0 };
			std::uint64_t observationId{ 0 };
		};

		struct ThreadState
		{
			const Collector* owner{ nullptr };
			std::uint64_t generation{ 0 };
			FrameContext frame;
			std::array<std::array<ScopeFrame, kMaximumScopeDepth>, static_cast<std::size_t>(ScopeKind::kCount)> scopes{};
			std::array<std::uint8_t, static_cast<std::size_t>(ScopeKind::kCount)> depths{};
		};

		thread_local ThreadState threadState;
		std::atomic_uint64_t nextSessionGeneration{ 1 };

		std::uint64_t ReadClockTicks() noexcept
		{
#ifdef _WIN32
			LARGE_INTEGER value{};
			return ::QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0;
#else
			return static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
#endif
		}

		std::uint64_t CurrentThreadId() noexcept
		{
#ifdef _WIN32
			return static_cast<std::uint64_t>(::GetCurrentThreadId());
#else
			return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
		}

		std::uint64_t DurationToTicks(std::chrono::nanoseconds a_duration) noexcept
		{
#ifdef _WIN32
			const auto frequency = Collector::ClockFrequencyHz();
			const long double seconds = static_cast<long double>(a_duration.count()) / 1'000'000'000.0L;
			const auto ticks = seconds * static_cast<long double>(frequency);
			return ticks > 0.0L ? static_cast<std::uint64_t>(ticks) : 0;
#else
			const auto ticks = std::chrono::duration_cast<Clock::duration>(a_duration).count();
			return ticks > 0 ? static_cast<std::uint64_t>(ticks) : 0;
#endif
		}

		std::size_t ScopeIndex(ScopeKind a_kind) noexcept
		{
			return static_cast<std::size_t>(a_kind);
		}

		ThreadState& SynchronizeThreadState(const Collector* a_owner, std::uint64_t a_generation) noexcept
		{
			if (threadState.owner != a_owner || threadState.generation != a_generation) {
				threadState = {};
				threadState.owner = a_owner;
				threadState.generation = a_generation;
			}
			return threadState;
		}

		ScopeSnapshot SnapshotScopes(const ThreadState& a_state) noexcept
		{
			const auto read = [&](ScopeKind a_kind) {
				const auto index = ScopeIndex(a_kind);
				if (a_state.depths[index] == 0)
					return ScopeBinding{};
				const auto& frame = a_state.scopes[index][a_state.depths[index] - 1];
				return ScopeBinding{ frame.token, frame.observationId };
			};

			return {
				.renderPass = read(ScopeKind::kRenderPass),
				.technique = read(ScopeKind::kTechnique),
				.geometry = read(ScopeKind::kGeometry),
				.commandList = read(ScopeKind::kCommandList),
			};
		}

		StopReason LimitToStopReason(RecordResult a_result, StopReason a_fallback) noexcept
		{
			switch (a_result) {
			case RecordResult::kFrameLimit:
				return StopReason::kFrameLimit;
			case RecordResult::kTimeLimit:
				return StopReason::kTimeLimit;
			case RecordResult::kEventLimit:
				return StopReason::kEventLimit;
			case RecordResult::kByteLimit:
				return StopReason::kByteLimit;
			default:
				return a_fallback;
			}
		}

		void LatchLimit(
			std::atomic<RecordResult>& a_firstLimit,
			std::atomic_bool& a_accepting,
			RecordResult a_limit) noexcept
		{
			auto expected = RecordResult::kRecorded;
			if (a_firstLimit.compare_exchange_strong(
					expected, a_limit, std::memory_order_acq_rel, std::memory_order_acquire)) {
				a_accepting.store(false, std::memory_order_release);
			}
		}

		template <std::size_t N>
		bool CopyBounded(std::string_view a_source, std::array<char, N>& a_target) noexcept
		{
			static_assert(N > 0);
			const auto count = std::min(a_source.size(), N - 1);
			std::copy_n(a_source.data(), count, a_target.data());
			a_target[count] = '\0';
			return a_source.size() > count;
		}

		template <std::size_t N>
		std::string_view StoredString(const std::array<char, N>& a_value) noexcept
		{
			return { a_value.data(), std::char_traits<char>::length(a_value.data()) };
		}

		void AppendIdentityBytes(std::uint64_t& a_hash, const void* a_data, std::size_t a_size) noexcept
		{
			constexpr std::uint64_t prime = 1099511628211ull;
			const auto* bytes = static_cast<const unsigned char*>(a_data);
			for (std::size_t index = 0; index < a_size; ++index) {
				a_hash ^= bytes[index];
				a_hash *= prime;
			}
		}

		template <class T>
		void AppendIdentity(std::uint64_t& a_hash, const T& a_value) noexcept
		{
			AppendIdentityBytes(a_hash, std::addressof(a_value), sizeof(a_value));
		}

		void AppendIdentity(std::uint64_t& a_hash, std::string_view a_value) noexcept
		{
			AppendIdentityBytes(a_hash, a_value.data(), a_value.size());
			constexpr unsigned char separator = 0xFF;
			AppendIdentityBytes(a_hash, std::addressof(separator), sizeof(separator));
		}

		std::uint64_t HashSceneObjectIdentity(const SceneObjectObservationInput& a_input) noexcept
		{
			std::uint64_t hash = 14695981039346656037ull;
			AppendIdentity(hash, a_input.reference);
			AppendIdentity(hash, a_input.referenceFormId);
			AppendIdentity(hash, a_input.baseFormId);
			AppendIdentity(hash, a_input.referenceName);
			AppendIdentity(hash, a_input.baseFormName);
			AppendIdentity(hash, a_input.referenceFormDynamic);
			AppendIdentity(hash, a_input.baseFormDynamic);
			return hash;
		}

		bool SameSceneObjectIdentity(
			const SceneObjectObservationRecord& a_record,
			const SceneObjectObservationInput& a_input) noexcept
		{
			return !a_record.referenceNameTruncated && !a_record.baseFormNameTruncated &&
				a_input.referenceName.size() <= kMaximumSceneObjectNameLength &&
				a_input.baseFormName.size() <= kMaximumSceneObjectNameLength &&
				a_record.pointerEvidence == a_input.reference &&
				a_record.referenceFormId == a_input.referenceFormId &&
				a_record.baseFormId == a_input.baseFormId &&
				StoredString(a_record.referenceName) == a_input.referenceName &&
				StoredString(a_record.baseFormName) == a_input.baseFormName &&
				a_record.referenceFormDynamic == a_input.referenceFormDynamic &&
				a_record.baseFormDynamic == a_input.baseFormDynamic;
		}

		std::uint64_t HashGeometryIdentity(const GeometryObservationInput& a_input) noexcept
		{
			std::uint64_t hash = 14695981039346656037ull;
			AppendIdentity(hash, a_input.geometry);
			AppendIdentity(hash, a_input.runtimeTypeName);
			AppendIdentity(hash, a_input.name);
			AppendIdentity(hash, a_input.geometryType);
			AppendIdentity(hash, a_input.vertexDescriptor);
			AppendIdentity(hash, a_input.worldTransform);
			AppendIdentity(hash, a_input.worldBound);
			AppendIdentity(hash, a_input.sceneObjectObservationId);
			AppendIdentity(hash, a_input.worldTransformAvailable);
			AppendIdentity(hash, a_input.worldBoundAvailable);
			return hash;
		}

		bool SameGeometryIdentity(
			const GeometryObservationRecord& a_record,
			const GeometryObservationInput& a_input) noexcept
		{
			return !a_record.runtimeTypeNameTruncated && !a_record.nameTruncated &&
				a_input.runtimeTypeName.size() <= kMaximumRuntimeTypeNameLength &&
				a_input.name.size() <= kMaximumGeometryNameLength &&
				a_record.pointerEvidence == a_input.geometry &&
				StoredString(a_record.runtimeTypeName) == a_input.runtimeTypeName &&
				StoredString(a_record.name) == a_input.name &&
				a_record.geometryType == a_input.geometryType &&
				a_record.vertexDescriptor == a_input.vertexDescriptor &&
				a_record.worldTransform == a_input.worldTransform &&
				a_record.worldBound == a_input.worldBound &&
				a_record.sceneObjectObservationId == a_input.sceneObjectObservationId &&
				a_record.worldTransformAvailable == a_input.worldTransformAvailable &&
				a_record.worldBoundAvailable == a_input.worldBoundAvailable;
		}

		std::uint64_t HashMaterialStateIdentity(const MaterialStateObservationInput& a_input) noexcept
		{
			std::uint64_t hash = 14695981039346656037ull;
			AppendIdentity(hash, a_input.shaderProperty);
			AppendIdentity(hash, a_input.shaderPropertyRuntimeTypeName);
			AppendIdentity(hash, a_input.shaderPropertyFlags);
			AppendIdentity(hash, a_input.alpha);
			AppendIdentity(hash, a_input.engineMaterialType);
			AppendIdentity(hash, a_input.material);
			AppendIdentity(hash, a_input.materialType);
			AppendIdentity(hash, a_input.feature);
			AppendIdentity(hash, a_input.hashKey);
			AppendIdentity(hash, a_input.textureBindingCount);
			for (std::size_t index = 0; index < a_input.textureBindingCount; ++index) {
				const auto& binding = a_input.textureBindings[index];
				AppendIdentity(hash, binding.role);
				AppendIdentity(hash, binding.bindingIndex);
				AppendIdentity(hash, binding.niSourceTexture);
				AppendIdentity(hash, binding.path);
				AppendIdentity(hash, binding.resource.d3dObject);
				AppendIdentity(hash, binding.resourceObservationId);
				AppendIdentity(hash, binding.path.size() > kMaximumTexturePathLength);
			}
			AppendIdentity(hash, a_input.shaderPropertyAvailable);
			AppendIdentity(hash, a_input.materialAvailable);
			AppendIdentity(hash, a_input.textureBindingsTruncated);
			return hash;
		}

		bool SameMaterialStateIdentity(
			const MaterialStateObservationRecord& a_record,
			const MaterialStateObservationInput& a_input,
			std::uint64_t a_fingerprint) noexcept
		{
			const auto baseMatches = !a_record.shaderPropertyRuntimeTypeNameTruncated &&
				a_input.shaderPropertyRuntimeTypeName.size() <= kMaximumRuntimeTypeNameLength &&
				a_record.fingerprint == a_fingerprint &&
				a_record.shaderPropertyEvidence == a_input.shaderProperty &&
				StoredString(a_record.shaderPropertyRuntimeTypeName) == a_input.shaderPropertyRuntimeTypeName &&
				a_record.shaderPropertyFlags == a_input.shaderPropertyFlags &&
				a_record.alpha == a_input.alpha &&
				a_record.engineMaterialType == a_input.engineMaterialType &&
				a_record.materialEvidence == a_input.material &&
				a_record.materialType == a_input.materialType &&
				a_record.feature == a_input.feature &&
				a_record.hashKey == a_input.hashKey &&
				a_record.textureBindingCount == a_input.textureBindingCount &&
				a_record.shaderPropertyAvailable == a_input.shaderPropertyAvailable &&
				a_record.materialAvailable == a_input.materialAvailable &&
				a_record.textureBindingsTruncated == a_input.textureBindingsTruncated;
			if (!baseMatches)
				return false;
			for (std::size_t index = 0; index < a_input.textureBindingCount; ++index) {
				const auto& stored = a_record.textureBindings[index];
				const auto& observed = a_input.textureBindings[index];
				if (stored.pathTruncated || observed.path.size() > kMaximumTexturePathLength ||
					stored.role != observed.role ||
					stored.bindingIndex != observed.bindingIndex ||
					stored.niSourceTextureEvidence != observed.niSourceTexture ||
					StoredString(stored.path) != observed.path ||
					stored.resourceObservationId != observed.resourceObservationId) {
					return false;
				}
			}
			return true;
		}

		std::uint64_t HashShaderIdentity(const ShaderObservationInput& a_input) noexcept
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			const auto append = [&](const auto* a_data, std::size_t a_size) {
				const auto* bytes = reinterpret_cast<const unsigned char*>(a_data);
				for (std::size_t index = 0; index < a_size; ++index) {
					hash ^= bytes[index];
					hash *= prime;
				}
			};
			append(&a_input.shader, sizeof(a_input.shader));
			append(&a_input.shaderType, sizeof(a_input.shaderType));
			const auto appendString = [&](std::string_view a_value) {
				append(a_value.data(), a_value.size());
				const unsigned char separator = 0xFF;
				append(&separator, sizeof(separator));
			};
			appendString(a_input.fxpFilename);
			appendString(a_input.imageSpaceName);
			appendString(a_input.compileSourceName);
			appendString(a_input.definesSuffix);
			return hash;
		}

		bool SameShaderIdentity(const ShaderObservationRecord& a_record, const ShaderObservationInput& a_input) noexcept
		{
			return !a_record.fxpFilenameTruncated && !a_record.imageSpaceNameTruncated &&
				!a_record.compileSourceNameTruncated &&
				!a_record.definesSuffixTruncated &&
				a_input.fxpFilename.size() <= kMaximumShaderNameLength &&
				a_input.imageSpaceName.size() <= kMaximumShaderNameLength &&
				a_input.compileSourceName.size() <= kMaximumShaderNameLength &&
				a_input.definesSuffix.size() <= kMaximumShaderDefinesSuffixLength &&
				a_record.pointerEvidence == a_input.shader &&
				a_record.shaderType == a_input.shaderType &&
				StoredString(a_record.fxpFilename) == a_input.fxpFilename.substr(0, kMaximumShaderNameLength) &&
				StoredString(a_record.imageSpaceName) == a_input.imageSpaceName.substr(0, kMaximumShaderNameLength) &&
				StoredString(a_record.compileSourceName) == a_input.compileSourceName.substr(0, kMaximumShaderNameLength) &&
				StoredString(a_record.definesSuffix) == a_input.definesSuffix.substr(0, kMaximumShaderDefinesSuffixLength);
		}

		std::uint64_t HashStageShaderIdentity(const StageShaderObservationInput& a_input) noexcept
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			const auto append = [&](const auto* a_data, std::size_t a_size) {
				const auto* bytes = reinterpret_cast<const unsigned char*>(a_data);
				for (std::size_t index = 0; index < a_size; ++index) {
					hash ^= bytes[index];
					hash *= prime;
				}
			};
			append(&a_input.stage, sizeof(a_input.stage));
			append(&a_input.wrapper, sizeof(a_input.wrapper));
			append(&a_input.d3dObject, sizeof(a_input.d3dObject));
			append(&a_input.wrapperDescriptor, sizeof(a_input.wrapperDescriptor));
			append(&a_input.bytecodeSize, sizeof(a_input.bytecodeSize));
			append(a_input.bytecodeSha256.data(), a_input.bytecodeSha256.size());
			append(a_input.cachePath.data(), a_input.cachePath.size());
			append(&a_input.engineAliasTotalCount, sizeof(a_input.engineAliasTotalCount));
			for (std::uint32_t index = 0; index < a_input.engineAliasCount; ++index) {
				append(a_input.engineAliases[index].loaderType.data(), a_input.engineAliases[index].loaderType.size());
				append(a_input.engineAliases[index].compileSourceName.data(), a_input.engineAliases[index].compileSourceName.size());
				append(&a_input.engineAliases[index].descriptor, sizeof(a_input.engineAliases[index].descriptor));
			}
			return hash;
		}

		bool SameStageShaderIdentity(
			const StageShaderObservationRecord& a_record,
			const StageShaderObservationInput& a_input) noexcept
		{
			if (a_record.bytecodeSha256Truncated || a_record.cachePathTruncated ||
				a_record.engineAliasesTruncated || a_input.engineAliasCount > kMaximumEngineShaderAliasesPerStage ||
				a_input.engineAliasCount != a_record.engineAliasCount ||
				a_input.engineAliasTotalCount != a_record.engineAliasTotalCount) {
				return false;
			}
			for (std::uint32_t index = 0; index < a_input.engineAliasCount; ++index) {
				const auto& stored = a_record.engineAliases[index];
				const auto& supplied = a_input.engineAliases[index];
				if (stored.loaderTypeTruncated || stored.compileSourceNameTruncated ||
					supplied.loaderType.size() > kMaximumShaderNameLength ||
					supplied.compileSourceName.size() > kMaximumShaderNameLength ||
					StoredString(stored.loaderType) != supplied.loaderType ||
					StoredString(stored.compileSourceName) != supplied.compileSourceName ||
					stored.descriptor != supplied.descriptor) {
					return false;
				}
			}
			return
				a_input.bytecodeSha256.size() <= kSha256HexLength &&
				a_input.cachePath.size() <= kMaximumShaderCachePathLength &&
				a_record.stage == a_input.stage && a_record.wrapperEvidence == a_input.wrapper &&
				a_record.pointerEvidence == a_input.d3dObject &&
				a_record.wrapperDescriptor == a_input.wrapperDescriptor &&
				a_record.bytecodeSize == a_input.bytecodeSize &&
				StoredString(a_record.bytecodeSha256) == a_input.bytecodeSha256 &&
				StoredString(a_record.cachePath) == a_input.cachePath;
		}

		void CopyEngineShaderAliases(
			StageShaderObservationRecord& a_record,
			const StageShaderObservationInput& a_input) noexcept
		{
			a_record.engineAliasCount = std::min<std::uint32_t>(
				a_input.engineAliasCount, static_cast<std::uint32_t>(kMaximumEngineShaderAliasesPerStage));
			a_record.engineAliasTotalCount = std::max(a_input.engineAliasTotalCount, a_input.engineAliasCount);
			a_record.engineAliasesTruncated = a_record.engineAliasTotalCount > a_record.engineAliasCount;
			for (std::uint32_t index = 0; index < a_record.engineAliasCount; ++index) {
				auto& stored = a_record.engineAliases[index];
				stored.descriptor = a_input.engineAliases[index].descriptor;
				stored.loaderTypeTruncated = CopyBounded(a_input.engineAliases[index].loaderType, stored.loaderType);
				stored.compileSourceNameTruncated = CopyBounded(
					a_input.engineAliases[index].compileSourceName, stored.compileSourceName);
				a_record.engineAliasesTruncated = a_record.engineAliasesTruncated ||
					stored.loaderTypeTruncated || stored.compileSourceNameTruncated;
			}
		}

		bool ContainsEngineShaderAlias(
			const StageShaderObservationRecord& a_record,
			const StageShaderObservationInput::EngineAlias& a_alias) noexcept
		{
			for (std::uint32_t index = 0; index < a_record.engineAliasCount; ++index) {
				const auto& stored = a_record.engineAliases[index];
				if (!stored.loaderTypeTruncated && !stored.compileSourceNameTruncated &&
					StoredString(stored.loaderType) == a_alias.loaderType &&
					StoredString(stored.compileSourceName) == a_alias.compileSourceName &&
					stored.descriptor == a_alias.descriptor) {
					return true;
				}
			}
			return false;
		}

		bool CompatibleStageShaderEvidence(
			const StageShaderObservationRecord& a_record,
			const StageShaderObservationInput& a_input) noexcept
		{
			if (a_record.stage != a_input.stage || a_record.pointerEvidence != a_input.d3dObject ||
				a_input.bytecodeSha256.size() > kSha256HexLength ||
				a_input.cachePath.size() > kMaximumShaderCachePathLength) {
				return false;
			}
			if (a_record.wrapperEvidence != 0 && a_input.wrapper != 0 &&
				a_record.wrapperEvidence != a_input.wrapper) {
				return false;
			}
			if (a_record.wrapperDescriptor != 0 && a_input.wrapperDescriptor != 0 &&
				a_record.wrapperDescriptor != a_input.wrapperDescriptor) {
				return false;
			}
			if (a_record.bytecodeSize != 0 && a_input.bytecodeSize != 0 &&
				a_record.bytecodeSize != a_input.bytecodeSize) {
				return false;
			}
			if (a_record.bytecodeSha256[0] != '\0' && !a_input.bytecodeSha256.empty() &&
				StoredString(a_record.bytecodeSha256) != a_input.bytecodeSha256) {
				return false;
			}
			if (a_record.cachePath[0] != '\0' && !a_input.cachePath.empty() &&
				StoredString(a_record.cachePath) != a_input.cachePath) {
				return false;
			}
			for (std::uint32_t index = 0; index < a_input.engineAliasCount; ++index) {
				if (a_input.engineAliases == nullptr ||
					a_input.engineAliases[index].loaderType.size() > kMaximumShaderNameLength ||
					a_input.engineAliases[index].compileSourceName.size() > kMaximumShaderNameLength) {
					return false;
				}
			}
			return true;
		}

		bool AddsStageShaderEvidence(
			const StageShaderObservationRecord& a_record,
			const StageShaderObservationInput& a_input) noexcept
		{
			if ((a_record.wrapperEvidence == 0 && a_input.wrapper != 0) ||
				(a_record.wrapperDescriptor == 0 && a_input.wrapperDescriptor != 0) ||
				(a_record.bytecodeSize == 0 && a_input.bytecodeSize != 0) ||
				(a_record.bytecodeSha256[0] == '\0' && !a_input.bytecodeSha256.empty()) ||
				(a_record.cachePath[0] == '\0' && !a_input.cachePath.empty()) ||
				a_input.engineAliasTotalCount > a_record.engineAliasTotalCount) {
				return true;
			}
			for (std::uint32_t index = 0; index < a_input.engineAliasCount; ++index) {
				if (!ContainsEngineShaderAlias(a_record, a_input.engineAliases[index]))
					return true;
			}
			return false;
		}

		void MergeStageShaderEvidence(
			StageShaderObservationRecord& a_record,
			const StageShaderObservationInput& a_input) noexcept
		{
			if (a_record.wrapperEvidence == 0)
				a_record.wrapperEvidence = a_input.wrapper;
			if (a_record.wrapperDescriptor == 0)
				a_record.wrapperDescriptor = a_input.wrapperDescriptor;
			if (a_record.bytecodeSize == 0)
				a_record.bytecodeSize = a_input.bytecodeSize;
			if (a_record.bytecodeSha256[0] == '\0' && !a_input.bytecodeSha256.empty())
				a_record.bytecodeSha256Truncated = CopyBounded(a_input.bytecodeSha256, a_record.bytecodeSha256);
			if (a_record.cachePath[0] == '\0' && !a_input.cachePath.empty())
				a_record.cachePathTruncated = CopyBounded(a_input.cachePath, a_record.cachePath);

			for (std::uint32_t index = 0; index < a_input.engineAliasCount; ++index) {
				const auto& supplied = a_input.engineAliases[index];
				if (ContainsEngineShaderAlias(a_record, supplied))
					continue;
				if (a_record.engineAliasCount >= kMaximumEngineShaderAliasesPerStage) {
					a_record.engineAliasesTruncated = true;
					continue;
				}
				auto& stored = a_record.engineAliases[a_record.engineAliasCount++];
				stored.descriptor = supplied.descriptor;
				stored.loaderTypeTruncated = CopyBounded(supplied.loaderType, stored.loaderType);
				stored.compileSourceNameTruncated = CopyBounded(
					supplied.compileSourceName, stored.compileSourceName);
				a_record.engineAliasesTruncated = a_record.engineAliasesTruncated ||
					stored.loaderTypeTruncated || stored.compileSourceNameTruncated;
			}
			a_record.engineAliasTotalCount = std::max({
				a_record.engineAliasTotalCount,
				a_input.engineAliasTotalCount,
				a_input.engineAliasCount,
				a_record.engineAliasCount,
			});
			a_record.engineAliasesTruncated = a_record.engineAliasesTruncated ||
				a_record.engineAliasTotalCount > a_record.engineAliasCount;
		}

		std::uint64_t HashTargetViewIdentity(const TargetViewObservationInput& a_input) noexcept
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			const auto append = [&](const auto& a_value) {
				const auto* bytes = reinterpret_cast<const unsigned char*>(std::addressof(a_value));
				for (std::size_t index = 0; index < sizeof(a_value); ++index) {
					hash ^= bytes[index];
					hash *= prime;
				}
			};
			append(a_input.kind);
			append(a_input.d3dObject);
			append(a_input.resourceObservationId);
			append(a_input.format);
			append(a_input.dimension);
			append(a_input.mipSlice);
			append(a_input.firstArraySlice);
			append(a_input.arraySize);
			append(a_input.firstElement);
			append(a_input.elementCount);
			append(a_input.flags);
			return hash;
		}

		std::uint64_t HashResourceIdentity(const ResourceObservationInput& a_input) noexcept
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			const auto append = [&](const auto& a_value) {
				const auto* bytes = reinterpret_cast<const unsigned char*>(std::addressof(a_value));
				for (std::size_t index = 0; index < sizeof(a_value); ++index) {
					hash ^= bytes[index];
					hash *= prime;
				}
			};
			append(a_input.d3dObject);
			append(a_input.dimension);
			append(a_input.widthOrBytes);
			append(a_input.height);
			append(a_input.depthOrArraySize);
			append(a_input.mipLevels);
			append(a_input.format);
			append(a_input.sampleCount);
			append(a_input.sampleQuality);
			append(a_input.usage);
			append(a_input.bindFlags);
			append(a_input.cpuAccessFlags);
			append(a_input.miscFlags);
			append(a_input.structureByteStride);
			return hash;
		}

		bool SameResourceIdentity(
			const ResourceObservationRecord& a_record,
			const ResourceObservationInput& a_input) noexcept
		{
			return a_record.d3dObject == a_input.d3dObject && a_record.dimension == a_input.dimension &&
				a_record.widthOrBytes == a_input.widthOrBytes && a_record.height == a_input.height &&
				a_record.depthOrArraySize == a_input.depthOrArraySize && a_record.mipLevels == a_input.mipLevels &&
				a_record.format == a_input.format && a_record.sampleCount == a_input.sampleCount &&
				a_record.sampleQuality == a_input.sampleQuality && a_record.usage == a_input.usage &&
				a_record.bindFlags == a_input.bindFlags && a_record.cpuAccessFlags == a_input.cpuAccessFlags &&
				a_record.miscFlags == a_input.miscFlags &&
				a_record.structureByteStride == a_input.structureByteStride;
		}

		bool SameTargetViewIdentity(
			const TargetViewObservationRecord& a_record,
			const TargetViewObservationInput& a_input) noexcept
		{
			return a_record.kind == a_input.kind && a_record.pointerEvidence == a_input.d3dObject &&
				a_record.resourceObservationId == a_input.resourceObservationId && a_record.format == a_input.format &&
				a_record.dimension == a_input.dimension && a_record.mipSlice == a_input.mipSlice &&
				a_record.firstArraySlice == a_input.firstArraySlice && a_record.arraySize == a_input.arraySize &&
				a_record.firstElement == a_input.firstElement && a_record.elementCount == a_input.elementCount &&
				a_record.flags == a_input.flags;
		}

		std::uint64_t HashTargetBindingIdentity(const TargetBindingObservationInput& a_input) noexcept
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			const auto appendBytes = [&](const void* a_data, std::size_t a_size) {
				const auto* bytes = static_cast<const unsigned char*>(a_data);
				for (std::size_t index = 0; index < a_size; ++index) {
					hash ^= bytes[index];
					hash *= prime;
				}
			};
			appendBytes(a_input.renderTargetObservationIds.data(), sizeof(a_input.renderTargetObservationIds));
			appendBytes(&a_input.depthTargetObservationId, sizeof(a_input.depthTargetObservationId));
			appendBytes(&a_input.renderTargetCount, sizeof(a_input.renderTargetCount));
			return hash;
		}

		bool SameTargetBindingIdentity(
			const TargetBindingObservationRecord& a_record,
			const TargetBindingObservationInput& a_input) noexcept
		{
			return a_record.renderTargetCount == a_input.renderTargetCount &&
				a_record.depthTargetObservationId == a_input.depthTargetObservationId &&
				a_record.renderTargetObservationIds == a_input.renderTargetObservationIds;
		}

		struct StagePointerIdentity
		{
			ShaderStage stage{ ShaderStage::kVertex };
			std::uintptr_t pointer{ 0 };

			bool operator==(const StagePointerIdentity&) const noexcept = default;
		};

		struct StagePointerIdentityHash
		{
			std::size_t operator()(const StagePointerIdentity& a_value) const noexcept
			{
				const auto pointerHash = std::hash<std::uintptr_t>{}(a_value.pointer);
				return pointerHash ^ (static_cast<std::size_t>(a_value.stage) * 0x9E3779B97F4A7C15ull);
			}
		};
	}

	struct Collector::Session
	{
		struct Slot
		{
			std::atomic_bool published{ false };
			EventRecord record;
		};

		CollectorConfig config;
		std::uint64_t generation{ 0 };
		std::uint64_t capacity{ 0 };
		RecordResult capacityLimit{ RecordResult::kEventLimit };
		std::uint64_t maxDurationTicks{ 0 };
		std::uint64_t startTimestampTicks{ 0 };
		std::unique_ptr<Slot[]> slots;
		std::unique_ptr<ShaderObservationRecord[]> shaderObservations;
		std::unique_ptr<bool[]> shaderObservationRetired;
		std::mutex shaderObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> shaderObservationLookup;
		std::uint32_t shaderObservationCount{ 0 };
		std::unique_ptr<StageShaderObservationRecord[]> stageShaderObservations;
		std::shared_mutex stageShaderObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> stageShaderObservationLookup;
		std::unordered_map<StagePointerIdentity, std::uint32_t, StagePointerIdentityHash> stageShaderByPointer;
		std::uint32_t stageShaderObservationCount{ 0 };
		std::unique_ptr<ResourceObservationRecord[]> resourceObservations;
		std::mutex resourceObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> resourceObservationLookup;
		std::uint32_t resourceObservationCount{ 0 };
		std::unique_ptr<TargetViewObservationRecord[]> targetViewObservations;
		std::mutex targetViewObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> targetViewObservationLookup;
		std::uint32_t targetViewObservationCount{ 0 };
		std::unique_ptr<TargetBindingObservationRecord[]> targetBindingObservations;
		std::mutex targetBindingObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> targetBindingObservationLookup;
		std::uint32_t targetBindingObservationCount{ 0 };
		std::unique_ptr<SceneObjectObservationRecord[]> sceneObjectObservations;
		std::mutex sceneObjectObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> sceneObjectObservationLookup;
		std::uint32_t sceneObjectObservationCount{ 0 };
		std::unique_ptr<GeometryObservationRecord[]> geometryObservations;
		std::mutex geometryObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> geometryObservationLookup;
		std::uint32_t geometryObservationCount{ 0 };
		std::unique_ptr<MaterialStateObservationRecord[]> materialStateObservations;
		std::mutex materialStateObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> materialStateObservationLookup;
		std::uint32_t materialStateObservationCount{ 0 };

		std::atomic_bool accepting{ true };
		std::atomic_uint64_t inFlight{ 0 };
		std::atomic_uint64_t nextIndex{ 0 };
		std::atomic_uint64_t nextObservationId{ 1 };
		std::atomic_uint64_t nextScopeToken{ 1 };
		std::atomic_uint64_t firstFrame{ kUnknownFrame };
		std::atomic<RecordResult> firstLimit{ RecordResult::kRecorded };

		std::atomic_uint64_t attempted{ 0 };
		std::atomic_uint64_t recorded{ 0 };
		std::atomic_uint64_t filtered{ 0 };
		std::atomic_uint64_t droppedStopped{ 0 };
		std::atomic_uint64_t droppedEventLimit{ 0 };
		std::atomic_uint64_t droppedByteLimit{ 0 };
		std::atomic_uint64_t droppedFrameLimit{ 0 };
		std::atomic_uint64_t droppedTimeLimit{ 0 };
		std::atomic_uint64_t scopeOverflow{ 0 };
		std::atomic_uint64_t scopeMismatch{ 0 };
		std::atomic_uint64_t droppedShaderObservations{ 0 };
		std::atomic_uint64_t droppedStageShaderObservations{ 0 };
		std::atomic_uint64_t droppedResourceObservations{ 0 };
		std::atomic_uint64_t droppedTargetViewObservations{ 0 };
		std::atomic_uint64_t droppedTargetBindingObservations{ 0 };
		std::atomic_uint64_t droppedSceneObjectObservations{ 0 };
		std::atomic_uint64_t droppedGeometryObservations{ 0 };
		std::atomic_uint64_t droppedMaterialStateObservations{ 0 };
	};

	Collector::ScopeGuard::ScopeGuard(
		Collector* a_owner,
		std::uint64_t a_generation,
		ScopeKind a_kind,
		std::uint64_t a_token,
		EventKind a_endKind,
		EventPayload a_endPayload) noexcept :
		owner(a_owner),
		generation(a_generation),
		kind(a_kind),
		token(a_token),
		endKind(a_endKind),
		endPayload(a_endPayload)
	{}

	Collector::ScopeGuard::~ScopeGuard()
	{
		Reset();
	}

	Collector::ScopeGuard::ScopeGuard(ScopeGuard&& a_other) noexcept :
		owner(a_other.owner),
		generation(a_other.generation),
		kind(a_other.kind),
		token(a_other.token),
		endKind(a_other.endKind),
		endPayload(a_other.endPayload)
	{
		a_other.owner = nullptr;
		a_other.token = 0;
	}

	Collector::ScopeGuard& Collector::ScopeGuard::operator=(ScopeGuard&& a_other) noexcept
	{
		if (this != std::addressof(a_other)) {
			Reset();
			owner = a_other.owner;
			generation = a_other.generation;
			kind = a_other.kind;
			token = a_other.token;
			endKind = a_other.endKind;
			endPayload = a_other.endPayload;
			a_other.owner = nullptr;
			a_other.token = 0;
		}
		return *this;
	}

	bool Collector::ScopeGuard::IsActive() const noexcept
	{
		return owner != nullptr;
	}

	std::uint64_t Collector::ScopeGuard::Token() const noexcept
	{
		return token;
	}

	void Collector::ScopeGuard::Reset() noexcept
	{
		if (owner)
			owner->ExitScope(generation, kind, token, endKind, endPayload);
		owner = nullptr;
		token = 0;
	}

	Collector::Collector() = default;

	Collector::~Collector()
	{
		Stop(StopReason::kShutdown);
	}

	StartResult Collector::Start(const CollectorConfig& a_config)
	{
		std::lock_guard stopLock(stopMutex);
		auto normalizedConfig = a_config;
		NormalizeEventKindSelection(normalizedConfig);
		if (a_config.maxFrames == 0 || a_config.maxEvents == 0 ||
			a_config.maxBytes < sizeof(EventRecord) || a_config.maxDuration.count() <= 0 ||
			a_config.maxScopeDepth == 0 || a_config.maxScopeDepth > kMaximumScopeDepth ||
			a_config.maxShaderObservations == 0 || a_config.maxStageShaderObservations == 0 ||
			a_config.maxResourceObservations == 0 ||
			a_config.maxTargetViewObservations == 0 || a_config.maxTargetBindingObservations == 0 ||
			a_config.maxSceneObjectObservations == 0 || a_config.maxGeometryObservations == 0 ||
			a_config.maxMaterialStateObservations == 0 || a_config.geometryShaderTypeMask == 0 ||
			normalizedConfig.requestedEventKindMask == 0) {
			return StartResult::kInvalidBounds;
		}

		if (drainingSession || activeSession.load(std::memory_order_acquire))
			return StartResult::kAlreadyCapturing;

		auto catalogueConfig = a_config;
		catalogueConfig.maxEvents = 0;
		const auto catalogueBytes = RequiredStorageBytes(catalogueConfig);
		if (a_config.maxBytes <= catalogueBytes)
			return StartResult::kInvalidBounds;
		const auto capacityByBytes = (a_config.maxBytes - catalogueBytes) /
			(sizeof(Session::Slot) + sizeof(EventRecord));
		const auto capacity = std::min(a_config.maxEvents, static_cast<std::uint64_t>(capacityByBytes));
		if (capacity == 0 || capacity > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
			return StartResult::kInvalidBounds;

		auto session = std::shared_ptr<Session>(new (std::nothrow) Session{});
		if (!session)
			return StartResult::kAllocationFailed;

		session->slots.reset(new (std::nothrow) Session::Slot[static_cast<std::size_t>(capacity)]);
		if (!session->slots)
			return StartResult::kAllocationFailed;
		session->shaderObservations.reset(new (std::nothrow) ShaderObservationRecord[a_config.maxShaderObservations]);
		session->shaderObservationRetired.reset(new (std::nothrow) bool[a_config.maxShaderObservations]{});
		session->stageShaderObservations.reset(
			new (std::nothrow) StageShaderObservationRecord[a_config.maxStageShaderObservations]);
		session->resourceObservations.reset(
			new (std::nothrow) ResourceObservationRecord[a_config.maxResourceObservations]);
		session->targetViewObservations.reset(
			new (std::nothrow) TargetViewObservationRecord[a_config.maxTargetViewObservations]);
		session->targetBindingObservations.reset(
			new (std::nothrow) TargetBindingObservationRecord[a_config.maxTargetBindingObservations]);
		session->sceneObjectObservations.reset(
			new (std::nothrow) SceneObjectObservationRecord[a_config.maxSceneObjectObservations]);
		session->geometryObservations.reset(
			new (std::nothrow) GeometryObservationRecord[a_config.maxGeometryObservations]);
		session->materialStateObservations.reset(
			new (std::nothrow) MaterialStateObservationRecord[a_config.maxMaterialStateObservations]);
		if (!session->shaderObservations || !session->shaderObservationRetired || !session->stageShaderObservations ||
			!session->resourceObservations ||
			!session->targetViewObservations || !session->targetBindingObservations ||
			!session->sceneObjectObservations || !session->geometryObservations || !session->materialStateObservations)
			return StartResult::kAllocationFailed;
		try {
			session->shaderObservationLookup.reserve(a_config.maxShaderObservations);
			session->stageShaderObservationLookup.reserve(a_config.maxStageShaderObservations);
			session->stageShaderByPointer.reserve(a_config.maxStageShaderObservations);
			session->resourceObservationLookup.reserve(a_config.maxResourceObservations);
			session->targetViewObservationLookup.reserve(a_config.maxTargetViewObservations);
			session->targetBindingObservationLookup.reserve(a_config.maxTargetBindingObservations);
			session->sceneObjectObservationLookup.reserve(a_config.maxSceneObjectObservations);
			session->geometryObservationLookup.reserve(a_config.maxGeometryObservations);
			session->materialStateObservationLookup.reserve(a_config.maxMaterialStateObservations);
		} catch (...) {
			return StartResult::kAllocationFailed;
		}

		session->config = normalizedConfig;
		session->generation = nextSessionGeneration.fetch_add(1, std::memory_order_relaxed);
		session->capacity = capacity;
		session->capacityLimit = a_config.maxEvents <= capacityByBytes ?
			RecordResult::kEventLimit : RecordResult::kByteLimit;
		session->maxDurationTicks = DurationToTicks(a_config.maxDuration);
		if (session->maxDurationTicks == 0)
			return StartResult::kInvalidBounds;
		session->startTimestampTicks = ReadClockTicks();

		std::shared_ptr<Session> expected;
		if (!activeSession.compare_exchange_strong(
				expected, session, std::memory_order_release, std::memory_order_acquire)) {
			return StartResult::kAlreadyCapturing;
		}

		return StartResult::kStarted;
	}

	std::uint64_t Collector::RequiredStorageBytes(const CollectorConfig& a_config) noexcept
	{
		constexpr std::uint64_t kHashEntryBudget = 64;
		auto saturatedAdd = [](std::uint64_t& a_total, std::uint64_t a_count, std::uint64_t a_size) {
			if (a_count != 0 && a_size > (std::numeric_limits<std::uint64_t>::max)() / a_count) {
				a_total = (std::numeric_limits<std::uint64_t>::max)();
				return;
			}
			const auto bytes = a_count * a_size;
			if (bytes > (std::numeric_limits<std::uint64_t>::max)() - a_total)
				a_total = (std::numeric_limits<std::uint64_t>::max)();
			else
				a_total += bytes;
		};

		std::uint64_t total = sizeof(Session) + sizeof(CaptureSnapshot);
		// Admission covers both the live fixed-capacity catalogue and the peak
		// snapshot copy retained while the drained session is still alive.
		saturatedAdd(total, a_config.maxEvents, sizeof(Session::Slot) + sizeof(EventRecord));
		saturatedAdd(total, a_config.maxShaderObservations,
			2 * sizeof(ShaderObservationRecord) + sizeof(bool) + kHashEntryBudget);
		saturatedAdd(total, a_config.maxStageShaderObservations,
			2 * sizeof(StageShaderObservationRecord) + 2 * kHashEntryBudget);
		saturatedAdd(total, a_config.maxResourceObservations,
			2 * sizeof(ResourceObservationRecord) + kHashEntryBudget);
		saturatedAdd(total, a_config.maxTargetViewObservations,
			2 * sizeof(TargetViewObservationRecord) + kHashEntryBudget);
		saturatedAdd(total, a_config.maxTargetBindingObservations,
			2 * sizeof(TargetBindingObservationRecord) + kHashEntryBudget);
		saturatedAdd(total, a_config.maxSceneObjectObservations,
			2 * sizeof(SceneObjectObservationRecord) + kHashEntryBudget);
		saturatedAdd(total, a_config.maxGeometryObservations,
			2 * sizeof(GeometryObservationRecord) + kHashEntryBudget);
		saturatedAdd(total, a_config.maxMaterialStateObservations,
			2 * sizeof(MaterialStateObservationRecord) + kHashEntryBudget);
		return total;
	}

	std::optional<CaptureSnapshot> Collector::Stop(StopReason a_reason, std::chrono::milliseconds a_drainTimeout)
	{
		std::lock_guard stopLock(stopMutex);
		auto session = drainingSession;
		if (!session) {
			session = activeSession.load(std::memory_order_acquire);
			if (!session)
				return std::nullopt;

			session->accepting.store(false, std::memory_order_release);
			if (!activeSession.compare_exchange_strong(
					session, std::shared_ptr<Session>{}, std::memory_order_acq_rel, std::memory_order_acquire)) {
				return std::nullopt;
			}
			drainingSession = session;
			draining.store(true, std::memory_order_release);
		}

		const auto deadline = Clock::now() + std::max(a_drainTimeout, std::chrono::milliseconds::zero());
		while (session->inFlight.load(std::memory_order_acquire) != 0 && Clock::now() < deadline)
			std::this_thread::yield();
		if (session->inFlight.load(std::memory_order_acquire) != 0)
			return std::nullopt;

		CaptureSnapshot snapshot;
		snapshot.config = session->config;
		snapshot.sessionGeneration = session->generation;
		snapshot.clockFrequencyHz = ClockFrequencyHz();
		snapshot.startTimestampTicks = session->startTimestampTicks;
		snapshot.endTimestampTicks = ReadClockTicks();
		snapshot.stopReason = LimitToStopReason(session->firstLimit.load(std::memory_order_acquire), a_reason);
		snapshot.statistics = {
			.attempted = session->attempted.load(std::memory_order_relaxed),
			.recorded = session->recorded.load(std::memory_order_relaxed),
			.filtered = session->filtered.load(std::memory_order_relaxed),
			.droppedStopped = session->droppedStopped.load(std::memory_order_relaxed),
			.droppedEventLimit = session->droppedEventLimit.load(std::memory_order_relaxed),
			.droppedByteLimit = session->droppedByteLimit.load(std::memory_order_relaxed),
			.droppedFrameLimit = session->droppedFrameLimit.load(std::memory_order_relaxed),
			.droppedTimeLimit = session->droppedTimeLimit.load(std::memory_order_relaxed),
			.scopeOverflow = session->scopeOverflow.load(std::memory_order_relaxed),
			.scopeMismatch = session->scopeMismatch.load(std::memory_order_relaxed),
			.droppedShaderObservations = session->droppedShaderObservations.load(std::memory_order_relaxed),
			.droppedStageShaderObservations = session->droppedStageShaderObservations.load(std::memory_order_relaxed),
			.droppedResourceObservations = session->droppedResourceObservations.load(std::memory_order_relaxed),
			.droppedTargetViewObservations = session->droppedTargetViewObservations.load(std::memory_order_relaxed),
			.droppedTargetBindingObservations = session->droppedTargetBindingObservations.load(std::memory_order_relaxed),
			.droppedSceneObjectObservations = session->droppedSceneObjectObservations.load(std::memory_order_relaxed),
			.droppedGeometryObservations = session->droppedGeometryObservations.load(std::memory_order_relaxed),
			.droppedMaterialStateObservations = session->droppedMaterialStateObservations.load(std::memory_order_relaxed),
		};

		const auto reserved = std::min(session->nextIndex.load(std::memory_order_acquire), session->capacity);
		snapshot.events.reserve(static_cast<std::size_t>(reserved));
		for (std::uint64_t index = 0; index < reserved; ++index) {
			const auto& slot = session->slots[static_cast<std::size_t>(index)];
			if (slot.published.load(std::memory_order_acquire))
				snapshot.events.push_back(slot.record);
		}

		snapshot.shaderObservations.reserve(session->shaderObservationCount);
		for (std::uint32_t index = 0; index < session->shaderObservationCount; ++index)
			snapshot.shaderObservations.push_back(session->shaderObservations[index]);
		snapshot.stageShaderObservations.reserve(session->stageShaderObservationCount);
		for (std::uint32_t index = 0; index < session->stageShaderObservationCount; ++index)
			snapshot.stageShaderObservations.push_back(session->stageShaderObservations[index]);
		snapshot.resourceObservations.reserve(session->resourceObservationCount);
		for (std::uint32_t index = 0; index < session->resourceObservationCount; ++index)
			snapshot.resourceObservations.push_back(session->resourceObservations[index]);
		snapshot.targetViewObservations.reserve(session->targetViewObservationCount);
		for (std::uint32_t index = 0; index < session->targetViewObservationCount; ++index)
			snapshot.targetViewObservations.push_back(session->targetViewObservations[index]);
		snapshot.targetBindingObservations.reserve(session->targetBindingObservationCount);
		for (std::uint32_t index = 0; index < session->targetBindingObservationCount; ++index)
			snapshot.targetBindingObservations.push_back(session->targetBindingObservations[index]);
		snapshot.sceneObjectObservations.reserve(session->sceneObjectObservationCount);
		for (std::uint32_t index = 0; index < session->sceneObjectObservationCount; ++index)
			snapshot.sceneObjectObservations.push_back(session->sceneObjectObservations[index]);
		snapshot.geometryObservations.reserve(session->geometryObservationCount);
		for (std::uint32_t index = 0; index < session->geometryObservationCount; ++index)
			snapshot.geometryObservations.push_back(session->geometryObservations[index]);
		snapshot.materialStateObservations.reserve(session->materialStateObservationCount);
		for (std::uint32_t index = 0; index < session->materialStateObservationCount; ++index)
			snapshot.materialStateObservations.push_back(session->materialStateObservations[index]);

		drainingSession.reset();
		draining.store(false, std::memory_order_release);
		return snapshot;
	}

	bool Collector::IsCapturing() const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		return session && session->accepting.load(std::memory_order_acquire);
	}

	bool Collector::IsDraining() const noexcept
	{
		return draining.load(std::memory_order_acquire);
	}

	std::uint64_t Collector::ActiveGeneration() const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		return session ? session->generation : 0;
	}

	bool Collector::IsGeometryShaderTypeSelected(std::uint32_t a_shaderType) const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		return session && session->accepting.load(std::memory_order_acquire) && a_shaderType < 64 &&
			(session->config.geometryShaderTypeMask & (std::uint64_t{ 1 } << a_shaderType)) != 0;
	}

	bool Collector::IsExecutionAllowedByGeometryScope(
		std::uint64_t a_preparedGeometrySetupObservationId) const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire))
			return false;
		if (!session->config.executionWithinSelectedGeometry)
			return true;
		const auto& state = SynchronizeThreadState(this, session->generation);
		return state.depths[ScopeIndex(ScopeKind::kGeometry)] != 0 ||
			a_preparedGeometrySetupObservationId != 0;
	}

	void Collector::CountFiltered(std::uint64_t a_count) noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (session && session->accepting.load(std::memory_order_acquire))
			session->filtered.fetch_add(a_count, std::memory_order_relaxed);
	}

	RecordResult Collector::Record(
		EventKind a_kind,
		const EventPayload& a_payload,
		std::uint64_t a_deviceContextObservationId,
		std::uint64_t a_commandStreamSequence,
		std::uint64_t a_targetBindingObservationId,
		std::uint64_t a_submissionObservationId,
		std::uint64_t a_preparedGeometrySetupObservationId) noexcept
	{
		return RecordForGeneration(
			a_kind, a_payload, a_deviceContextObservationId, 0, a_commandStreamSequence,
			a_targetBindingObservationId, a_submissionObservationId,
			a_preparedGeometrySetupObservationId);
	}

	RecordResult Collector::RecordForGeneration(
		EventKind a_kind,
		const EventPayload& a_payload,
		std::uint64_t a_deviceContextObservationId,
		std::uint64_t a_expectedGeneration,
		std::uint64_t a_commandStreamSequence,
		std::uint64_t a_targetBindingObservationId,
		std::uint64_t a_submissionObservationId,
		std::uint64_t a_preparedGeometrySetupObservationId) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session)
			return RecordResult::kInactive;
		if (a_expectedGeneration != 0 && session->generation != a_expectedGeneration)
			return RecordResult::kStopped;

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] {
			session->inFlight.fetch_sub(1, std::memory_order_release);
		};

		if (!session->accepting.load(std::memory_order_acquire) ||
			activeSession.load(std::memory_order_acquire) != session) {
			session->droppedStopped.fetch_add(1, std::memory_order_relaxed);
			releaseFlight();
			return RecordResult::kStopped;
		}

		session->attempted.fetch_add(1, std::memory_order_relaxed);
		if ((session->config.eventKindMask & EventKindBit(a_kind)) == 0) {
			session->filtered.fetch_add(1, std::memory_order_relaxed);
			releaseFlight();
			return RecordResult::kFiltered;
		}
		const auto timestamp = ReadClockTicks();
		if (timestamp - session->startTimestampTicks >= session->maxDurationTicks) {
			session->droppedTimeLimit.fetch_add(1, std::memory_order_relaxed);
			LatchLimit(session->firstLimit, session->accepting, RecordResult::kTimeLimit);
			releaseFlight();
			return RecordResult::kTimeLimit;
		}

		auto& state = SynchronizeThreadState(this, session->generation);
		if (state.frame.cpuFrame != kUnknownFrame) {
			auto firstFrame = session->firstFrame.load(std::memory_order_acquire);
			if (firstFrame == kUnknownFrame) {
				session->firstFrame.compare_exchange_strong(
					firstFrame, state.frame.cpuFrame, std::memory_order_acq_rel, std::memory_order_acquire);
				firstFrame = session->firstFrame.load(std::memory_order_acquire);
			}
			if (state.frame.cpuFrame >= firstFrame && state.frame.cpuFrame - firstFrame >= session->config.maxFrames) {
				session->droppedFrameLimit.fetch_add(1, std::memory_order_relaxed);
				LatchLimit(session->firstLimit, session->accepting, RecordResult::kFrameLimit);
				releaseFlight();
				return RecordResult::kFrameLimit;
			}
		}

		const auto index = session->nextIndex.fetch_add(1, std::memory_order_acq_rel);
		if (index >= session->capacity) {
			if (session->capacityLimit == RecordResult::kEventLimit)
				session->droppedEventLimit.fetch_add(1, std::memory_order_relaxed);
			else
				session->droppedByteLimit.fetch_add(1, std::memory_order_relaxed);
			LatchLimit(session->firstLimit, session->accepting, session->capacityLimit);
			releaseFlight();
			return session->capacityLimit;
		}

		EventRecord record;
		record.kind = a_kind;
		record.captureNumericId = session->config.captureNumericId;
		record.sessionGeneration = session->generation;
		record.sequence = index;
		record.timestampTicks = timestamp;
		record.threadId = CurrentThreadId();
		record.deviceContextObservationId = a_deviceContextObservationId;
		record.commandStreamSequence = a_commandStreamSequence;
		record.targetBindingObservationId = a_targetBindingObservationId;
		record.submissionObservationId = a_submissionObservationId;
		record.preparedGeometrySetupObservationId = a_preparedGeometrySetupObservationId;
		record.frame = state.frame;
		record.scopes = SnapshotScopes(state);
		record.payload = a_payload;

		auto& slot = session->slots[static_cast<std::size_t>(index)];
		slot.record = record;
		slot.published.store(true, std::memory_order_release);
		session->recorded.fetch_add(1, std::memory_order_relaxed);
		releaseFlight();
		return RecordResult::kRecorded;
	}

	Collector::ScopeGuard Collector::EnterScope(
		ScopeKind a_kind,
		std::uint64_t a_observationId,
		EventKind a_beginKind,
		EventKind a_endKind,
		const EventPayload& a_beginPayload,
		const EventPayload& a_endPayload,
		std::uint64_t a_expectedGeneration) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_kind == ScopeKind::kCount ||
			(a_expectedGeneration != 0 && session->generation != a_expectedGeneration))
			return {};

		auto& state = SynchronizeThreadState(this, session->generation);
		const auto index = ScopeIndex(a_kind);
		if (state.depths[index] >= session->config.maxScopeDepth) {
			session->scopeOverflow.fetch_add(1, std::memory_order_relaxed);
			return {};
		}

		const auto token = session->nextScopeToken.fetch_add(1, std::memory_order_relaxed);
		state.scopes[index][state.depths[index]++] = { token, a_observationId };
		if (RecordForGeneration(a_beginKind, a_beginPayload, 0, session->generation) != RecordResult::kRecorded) {
			--state.depths[index];
			return {};
		}

		return ScopeGuard(this, session->generation, a_kind, token, a_endKind, a_endPayload);
	}

	std::uint64_t Collector::AllocateObservationId(std::uint64_t a_expectedGeneration) noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) ||
			(a_expectedGeneration != 0 && session->generation != a_expectedGeneration))
			return 0;
		const auto observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
		return activeSession.load(std::memory_order_acquire) == session &&
				session->accepting.load(std::memory_order_acquire) ?
			observationId : 0;
	}

	ShaderObservationResult Collector::ObserveShader(const ShaderObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_input.shader == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) ||
			activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashShaderIdentity(a_input);
		ShaderObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->shaderObservationMutex);
			const auto [begin, end] = session->shaderObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto index = found->second;
				if (!session->shaderObservationRetired[index] &&
					SameShaderIdentity(session->shaderObservations[index], a_input)) {
					const auto& record = session->shaderObservations[index];
					result = { record.observationId, session->generation, record.pointerGeneration, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->shaderObservationCount >= session->config.maxShaderObservations) {
					session->droppedShaderObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t pointerGeneration = 1;
					for (std::uint32_t index = 0; index < session->shaderObservationCount; ++index) {
						if (session->shaderObservations[index].pointerEvidence == a_input.shader)
							pointerGeneration = std::max(pointerGeneration, session->shaderObservations[index].pointerGeneration + 1);
					}

					const auto index = session->shaderObservationCount++;
					auto& record = session->shaderObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.pointerEvidence = a_input.shader;
					record.pointerGeneration = pointerGeneration;
					record.shaderType = a_input.shaderType;
					record.fxpFilenameTruncated = CopyBounded(a_input.fxpFilename, record.fxpFilename);
					record.imageSpaceNameTruncated = CopyBounded(a_input.imageSpaceName, record.imageSpaceName);
					record.compileSourceNameTruncated = CopyBounded(
						a_input.compileSourceName, record.compileSourceName);
					record.definesSuffixTruncated = CopyBounded(a_input.definesSuffix, record.definesSuffix);
					try {
						session->shaderObservationLookup.emplace(identityHash, index);
						result = { record.observationId, session->generation, record.pointerGeneration, true };
					} catch (...) {
						--session->shaderObservationCount;
						record = {};
						session->droppedShaderObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	StageShaderObservationResult Collector::ObserveStageShader(const StageShaderObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_input.d3dObject == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashStageShaderIdentity(a_input);
		StageShaderObservationResult result{ .sessionGeneration = session->generation };
		{
			std::unique_lock lock(session->stageShaderObservationMutex);
			const auto [begin, end] = session->stageShaderObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto& record = session->stageShaderObservations[found->second];
				if (SameStageShaderIdentity(record, a_input)) {
					result = { record.observationId, session->generation, record.pointerGeneration, false };
					break;
				}
			}

			if (result.observationId == 0) {
				const StagePointerIdentity pointerIdentity{ a_input.stage, a_input.d3dObject };
				const auto previous = session->stageShaderByPointer.find(pointerIdentity);
				if (previous != session->stageShaderByPointer.end()) {
					auto& record = session->stageShaderObservations[previous->second];
					if (CompatibleStageShaderEvidence(record, a_input)) {
						try {
							session->stageShaderObservationLookup.emplace(identityHash, previous->second);
							if (AddsStageShaderEvidence(record, a_input))
								MergeStageShaderEvidence(record, a_input);
							result = { record.observationId, session->generation, record.pointerGeneration, false };
						} catch (...) {
							session->droppedStageShaderObservations.fetch_add(1, std::memory_order_relaxed);
						}
					}
				}
			}

			if (result.observationId == 0) {
				if (session->stageShaderObservationCount >= session->config.maxStageShaderObservations) {
					session->droppedStageShaderObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t pointerGeneration = 1;
					const StagePointerIdentity pointerIdentity{ a_input.stage, a_input.d3dObject };
					if (const auto previous = session->stageShaderByPointer.find(pointerIdentity);
						previous != session->stageShaderByPointer.end()) {
						pointerGeneration = session->stageShaderObservations[previous->second].pointerGeneration + 1;
					}

					const auto index = session->stageShaderObservationCount++;
					auto& record = session->stageShaderObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.stage = a_input.stage;
					record.wrapperEvidence = a_input.wrapper;
					record.pointerEvidence = a_input.d3dObject;
					record.pointerGeneration = pointerGeneration;
					record.wrapperDescriptor = a_input.wrapperDescriptor;
					record.bytecodeSize = a_input.bytecodeSize;
					record.bytecodeSha256Truncated = CopyBounded(a_input.bytecodeSha256, record.bytecodeSha256);
					record.cachePathTruncated = CopyBounded(a_input.cachePath, record.cachePath);
					CopyEngineShaderAliases(record, a_input);
					try {
						const auto identityEntry = session->stageShaderObservationLookup.emplace(identityHash, index);
						try {
							session->stageShaderByPointer.insert_or_assign(pointerIdentity, index);
						} catch (...) {
							session->stageShaderObservationLookup.erase(identityEntry);
							throw;
						}
						result = { record.observationId, session->generation, record.pointerGeneration, true };
					} catch (...) {
						--session->stageShaderObservationCount;
						record = {};
						session->droppedStageShaderObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	StageShaderObservationResult Collector::FindStageShader(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_d3dObject == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		StageShaderObservationResult result{ .sessionGeneration = session->generation };
		{
			std::shared_lock lock(session->stageShaderObservationMutex);
			const auto found = session->stageShaderByPointer.find({ a_stage, a_d3dObject });
			if (found != session->stageShaderByPointer.end()) {
				const auto& record = session->stageShaderObservations[found->second];
				result = {
					.observationId = record.observationId,
					.sessionGeneration = session->generation,
					.pointerGeneration = record.pointerGeneration,
					.firstSeen = false,
				};
			}
		}
		releaseFlight();
		return result;
	}

	ResourceObservationResult Collector::ObserveResource(
		const ResourceObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_input.d3dObject == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashResourceIdentity(a_input);
		ResourceObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->resourceObservationMutex);
			const auto [begin, end] = session->resourceObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto& record = session->resourceObservations[found->second];
				if (SameResourceIdentity(record, a_input)) {
					result = { record.observationId, session->generation, record.pointerGeneration, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->resourceObservationCount >= session->config.maxResourceObservations) {
					session->droppedResourceObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t pointerGeneration = 1;
					for (std::uint32_t index = 0; index < session->resourceObservationCount; ++index) {
						if (session->resourceObservations[index].d3dObject == a_input.d3dObject)
							pointerGeneration = std::max(
								pointerGeneration, session->resourceObservations[index].pointerGeneration + 1);
					}
					const auto index = session->resourceObservationCount++;
					auto& record = session->resourceObservations[index];
					static_cast<ResourceObservationInput&>(record) = a_input;
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.pointerGeneration = pointerGeneration;
					try {
						session->resourceObservationLookup.emplace(identityHash, index);
						result = { record.observationId, session->generation, pointerGeneration, true };
					} catch (...) {
						--session->resourceObservationCount;
						record = {};
						session->droppedResourceObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	TargetViewObservationResult Collector::ObserveTargetView(
		const TargetViewObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_input.d3dObject == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashTargetViewIdentity(a_input);
		TargetViewObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->targetViewObservationMutex);
			const auto [begin, end] = session->targetViewObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto& record = session->targetViewObservations[found->second];
				if (SameTargetViewIdentity(record, a_input)) {
					result = { record.observationId, session->generation, record.pointerGeneration, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->targetViewObservationCount >= session->config.maxTargetViewObservations) {
					session->droppedTargetViewObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t pointerGeneration = 1;
					for (std::uint32_t index = 0; index < session->targetViewObservationCount; ++index) {
						if (session->targetViewObservations[index].pointerEvidence == a_input.d3dObject)
							pointerGeneration = std::max(
								pointerGeneration, session->targetViewObservations[index].pointerGeneration + 1);
					}

					const auto index = session->targetViewObservationCount++;
					auto& record = session->targetViewObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.kind = a_input.kind;
					record.pointerEvidence = a_input.d3dObject;
					record.pointerGeneration = pointerGeneration;
					record.resourceObservationId = a_input.resourceObservationId;
					record.format = a_input.format;
					record.dimension = a_input.dimension;
					record.mipSlice = a_input.mipSlice;
					record.firstArraySlice = a_input.firstArraySlice;
					record.arraySize = a_input.arraySize;
					record.firstElement = a_input.firstElement;
					record.elementCount = a_input.elementCount;
					record.flags = a_input.flags;
					try {
						session->targetViewObservationLookup.emplace(identityHash, index);
						result = { record.observationId, session->generation, pointerGeneration, true };
					} catch (...) {
						--session->targetViewObservationCount;
						record = {};
						session->droppedTargetViewObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	TargetBindingObservationResult Collector::ObserveTargetBinding(
		const TargetBindingObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) ||
			a_input.renderTargetCount > kMaximumRenderTargets) {
			return {};
		}

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashTargetBindingIdentity(a_input);
		TargetBindingObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->targetBindingObservationMutex);
			const auto [begin, end] = session->targetBindingObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto& record = session->targetBindingObservations[found->second];
				if (SameTargetBindingIdentity(record, a_input)) {
					result = { record.observationId, session->generation, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->targetBindingObservationCount >= session->config.maxTargetBindingObservations) {
					session->droppedTargetBindingObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					const auto index = session->targetBindingObservationCount++;
					auto& record = session->targetBindingObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.renderTargetObservationIds = a_input.renderTargetObservationIds;
					record.depthTargetObservationId = a_input.depthTargetObservationId;
					record.renderTargetCount = a_input.renderTargetCount;
					try {
						session->targetBindingObservationLookup.emplace(identityHash, index);
						result = { record.observationId, session->generation, true };
					} catch (...) {
						--session->targetBindingObservationCount;
						record = {};
						session->droppedTargetBindingObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	SceneObjectObservationResult Collector::ObserveSceneObject(
		const SceneObjectObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_input.reference == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashSceneObjectIdentity(a_input);
		SceneObjectObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->sceneObjectObservationMutex);
			const auto [begin, end] = session->sceneObjectObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto& record = session->sceneObjectObservations[found->second];
				if (SameSceneObjectIdentity(record, a_input)) {
					result = { record.observationId, session->generation, record.pointerGeneration, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->sceneObjectObservationCount >= session->config.maxSceneObjectObservations) {
					session->droppedSceneObjectObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t pointerGeneration = 1;
					for (std::uint32_t index = 0; index < session->sceneObjectObservationCount; ++index) {
						if (session->sceneObjectObservations[index].pointerEvidence == a_input.reference)
							pointerGeneration = std::max(
								pointerGeneration, session->sceneObjectObservations[index].pointerGeneration + 1);
					}
					const auto index = session->sceneObjectObservationCount++;
					auto& record = session->sceneObjectObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.pointerEvidence = a_input.reference;
					record.pointerGeneration = pointerGeneration;
					record.referenceFormId = a_input.referenceFormId;
					record.baseFormId = a_input.baseFormId;
					record.referenceNameTruncated = CopyBounded(a_input.referenceName, record.referenceName);
					record.baseFormNameTruncated = CopyBounded(a_input.baseFormName, record.baseFormName);
					record.referenceFormDynamic = a_input.referenceFormDynamic;
					record.baseFormDynamic = a_input.baseFormDynamic;
					try {
						session->sceneObjectObservationLookup.emplace(identityHash, index);
						result = { record.observationId, session->generation, pointerGeneration, true };
					} catch (...) {
						--session->sceneObjectObservationCount;
						record = {};
						session->droppedSceneObjectObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	GeometryObservationResult Collector::ObserveGeometry(
		const GeometryObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_input.geometry == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashGeometryIdentity(a_input);
		GeometryObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->geometryObservationMutex);
			const auto [begin, end] = session->geometryObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto& record = session->geometryObservations[found->second];
				if (SameGeometryIdentity(record, a_input)) {
					result = { record.observationId, session->generation, record.pointerGeneration, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->geometryObservationCount >= session->config.maxGeometryObservations) {
					session->droppedGeometryObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t pointerGeneration = 1;
					for (std::uint32_t index = 0; index < session->geometryObservationCount; ++index) {
						if (session->geometryObservations[index].pointerEvidence == a_input.geometry)
							pointerGeneration = std::max(
								pointerGeneration, session->geometryObservations[index].pointerGeneration + 1);
					}
					const auto index = session->geometryObservationCount++;
					auto& record = session->geometryObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.pointerEvidence = a_input.geometry;
					record.pointerGeneration = pointerGeneration;
					record.runtimeTypeNameTruncated = CopyBounded(a_input.runtimeTypeName, record.runtimeTypeName);
					record.nameTruncated = CopyBounded(a_input.name, record.name);
					record.geometryType = a_input.geometryType;
					record.vertexDescriptor = a_input.vertexDescriptor;
					record.worldTransform = a_input.worldTransform;
					record.worldBound = a_input.worldBound;
					record.sceneObjectObservationId = a_input.sceneObjectObservationId;
					record.worldTransformAvailable = a_input.worldTransformAvailable;
					record.worldBoundAvailable = a_input.worldBoundAvailable;
					try {
						session->geometryObservationLookup.emplace(identityHash, index);
						result = { record.observationId, session->generation, pointerGeneration, true };
					} catch (...) {
						--session->geometryObservationCount;
						record = {};
						session->droppedGeometryObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	MaterialStateObservationResult Collector::ObserveMaterialState(
		const MaterialStateObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) ||
			(a_input.shaderProperty == 0 && a_input.material == 0)) {
			return {};
		}

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto fingerprint = HashMaterialStateIdentity(a_input);
		MaterialStateObservationResult result{
			.sessionGeneration = session->generation,
			.fingerprint = fingerprint,
		};
		{
			std::lock_guard lock(session->materialStateObservationMutex);
			const auto [begin, end] = session->materialStateObservationLookup.equal_range(fingerprint);
			for (auto found = begin; found != end; ++found) {
				const auto& record = session->materialStateObservations[found->second];
				if (SameMaterialStateIdentity(record, a_input, fingerprint)) {
					result = { record.observationId, session->generation, record.stateRevision, fingerprint, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->materialStateObservationCount >= session->config.maxMaterialStateObservations) {
					session->droppedMaterialStateObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t stateRevision = 1;
					for (std::uint32_t index = 0; index < session->materialStateObservationCount; ++index) {
						const auto& previous = session->materialStateObservations[index];
						if (previous.shaderPropertyEvidence == a_input.shaderProperty &&
							previous.materialEvidence == a_input.material) {
							stateRevision = std::max(stateRevision, previous.stateRevision + 1);
						}
					}
					const auto index = session->materialStateObservationCount++;
					auto& record = session->materialStateObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.stateRevision = stateRevision;
					record.fingerprint = fingerprint;
					record.shaderPropertyEvidence = a_input.shaderProperty;
					record.shaderPropertyRuntimeTypeNameTruncated = CopyBounded(
						a_input.shaderPropertyRuntimeTypeName, record.shaderPropertyRuntimeTypeName);
					record.shaderPropertyFlags = a_input.shaderPropertyFlags;
					record.alpha = a_input.alpha;
					record.engineMaterialType = a_input.engineMaterialType;
					record.materialEvidence = a_input.material;
					record.materialType = a_input.materialType;
					record.feature = a_input.feature;
					record.hashKey = a_input.hashKey;
					record.textureBindingCount = a_input.textureBindingCount;
					for (std::size_t bindingIndex = 0; bindingIndex < a_input.textureBindingCount; ++bindingIndex) {
						const auto& observed = a_input.textureBindings[bindingIndex];
						auto& stored = record.textureBindings[bindingIndex];
						stored.role = observed.role;
						stored.bindingIndex = observed.bindingIndex;
						stored.niSourceTextureEvidence = observed.niSourceTexture;
						stored.pathTruncated = CopyBounded(observed.path, stored.path);
						stored.resourceObservationId = observed.resourceObservationId;
					}
					record.shaderPropertyAvailable = a_input.shaderPropertyAvailable;
					record.materialAvailable = a_input.materialAvailable;
					record.textureBindingsTruncated = a_input.textureBindingsTruncated;
					try {
						session->materialStateObservationLookup.emplace(fingerprint, index);
						result = { record.observationId, session->generation, stateRevision, fingerprint, true };
					} catch (...) {
						--session->materialStateObservationCount;
						record = {};
						session->droppedMaterialStateObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	void Collector::RetireShaderObservation(std::uintptr_t a_shader) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_shader == 0)
			return;
		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		if (!session->accepting.load(std::memory_order_acquire) ||
			activeSession.load(std::memory_order_acquire) != session) {
			session->inFlight.fetch_sub(1, std::memory_order_release);
			return;
		}
		{
			std::lock_guard lock(session->shaderObservationMutex);
			for (std::uint32_t index = 0; index < session->shaderObservationCount; ++index) {
				if (session->shaderObservations[index].pointerEvidence == a_shader)
					session->shaderObservationRetired[index] = true;
			}
		}
		session->inFlight.fetch_sub(1, std::memory_order_release);
	}

	void Collector::SetThreadFrameContext(const FrameContext& a_context) noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session)
			return;
		SynchronizeThreadState(this, session->generation).frame = a_context;
	}

	FrameContext Collector::GetThreadFrameContext() const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session || threadState.owner != this || threadState.generation != session->generation)
			return {};
		return threadState.frame;
	}

	ScopeSnapshot Collector::GetThreadScopes() const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session || threadState.owner != this || threadState.generation != session->generation)
			return {};
		return SnapshotScopes(threadState);
	}

	std::uint64_t Collector::ClockFrequencyHz() noexcept
	{
#ifdef _WIN32
		LARGE_INTEGER frequency{};
		return ::QueryPerformanceFrequency(&frequency) ? static_cast<std::uint64_t>(frequency.QuadPart) : 0;
#else
		return static_cast<std::uint64_t>(Clock::period::den / Clock::period::num);
#endif
	}

	void Collector::ExitScope(
		std::uint64_t a_generation,
		ScopeKind a_kind,
		std::uint64_t a_token,
		EventKind a_endKind,
		const EventPayload& a_endPayload) noexcept
	{
		if (threadState.owner != this || threadState.generation != a_generation || a_kind == ScopeKind::kCount)
			return;

		const auto index = ScopeIndex(a_kind);
		if (threadState.depths[index] == 0)
			return;

		auto& depth = threadState.depths[index];
		if (threadState.scopes[index][depth - 1].token != a_token) {
			const auto session = activeSession.load(std::memory_order_acquire);
			if (session && session->generation == a_generation)
				session->scopeMismatch.fetch_add(1, std::memory_order_relaxed);

			const auto begin = threadState.scopes[index].begin();
			const auto end = begin + depth;
			const auto found = std::find_if(begin, end, [&](const ScopeFrame& a_frame) {
				return a_frame.token == a_token;
			});
			if (found != end) {
				std::move(found + 1, end, found);
				--depth;
			}
			return;
		}

		const auto session = activeSession.load(std::memory_order_acquire);
		if (session && session->generation == a_generation && session->accepting.load(std::memory_order_acquire))
			RecordForGeneration(a_endKind, a_endPayload, 0, a_generation);

		--depth;
	}
}
