#include "CharacterRendering.h"

#include "CharacterActorPolicy.h"
#include "CharacterCategoryFormat.h"
#include "CharacterComputeSubrect.h"
#include "CharacterMaskReadback.h"
#include "CharacterMaskRoi.h"
#include "CharacterMaskRoiAdmission.h"
#include "CharacterMaskWorkPolicy.h"

#include "Globals.h"
#include "Profiler.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <SimpleMath.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		constexpr std::uint32_t kReadbackLatency = 3;
		constexpr std::uint32_t kRegionQuantization = 4;
		constexpr float kVisibilityDepthThreshold = 0.001f;
		constexpr std::uint32_t kDiagnosticCounterCount = 9;

		// Camera-relative bounds must use the origin belonging to the cached
		// view-projection, not a live shadow-state pose that can already be newer.
		RE::NiPoint3 GetProjectionEyePosition(std::uint32_t a_eye) noexcept
		{
			const auto& position = globals::game::frameBufferCached.GetCameraPosAdjust(a_eye);
			return { position.x, position.y, position.z };
		}

		RE::NiPoint3 GetProjectionAverageEyePosition() noexcept
		{
			const auto left = GetProjectionEyePosition(0);
			if (!globals::game::isVR)
				return left;
			const auto right = GetProjectionEyePosition(1);
			return { left.x * 0.5f + right.x * 0.5f,
				left.y * 0.5f + right.y * 0.5f, left.z * 0.5f + right.z * 0.5f };
		}

		enum DiagnosticCounter : std::uint32_t
		{
			MaskPixels = 0,
			AuthoredFacePixels,
			AuthoredSkinPixels,
			AuthoredHairPixels,
			VisibleFacePixels,
			VisibleSkinPixels,
			VisibleHairPixels,
			VisibilityRejectedPixels,
			DistanceRejectedPixels,
		};

		bool IsSupportedDepthViewFormat(DXGI_FORMAT a_format) noexcept
		{
			return a_format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
			       a_format == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS ||
			       a_format == DXGI_FORMAT_R32_FLOAT ||
			       a_format == DXGI_FORMAT_R16_UNORM;
		}

		void Increment(std::uint64_t& a_value) noexcept
		{
			if (a_value != std::numeric_limits<std::uint64_t>::max())
				++a_value;
		}

		void Increment(std::uint32_t& a_value) noexcept
		{
			if (a_value != std::numeric_limits<std::uint32_t>::max())
				++a_value;
		}

		template <class T>
		void AtomicIncrement(std::atomic<T>& a_value) noexcept
		{
			auto current = a_value.load(std::memory_order_relaxed);
			while (current != std::numeric_limits<T>::max() &&
				   !a_value.compare_exchange_weak(
					   current, current + 1, std::memory_order_relaxed)) {
			}
		}

		bool SameIdentity(IUnknown* a_left, IUnknown* a_right) noexcept
		{
			if (!a_left || !a_right)
				return false;
			ComPtr<IUnknown> left;
			ComPtr<IUnknown> right;
			return SUCCEEDED(a_left->QueryInterface(IID_PPV_ARGS(&left))) &&
			       SUCCEEDED(a_right->QueryInterface(IID_PPV_ARGS(&right))) &&
			       left.Get() == right.Get();
		}

		std::uintptr_t GetIdentityToken(IUnknown* a_object) noexcept
		{
			if (!a_object)
				return 0;
			ComPtr<IUnknown> identity;
			return SUCCEEDED(a_object->QueryInterface(IID_PPV_ARGS(&identity))) ?
			           reinterpret_cast<std::uintptr_t>(identity.Get()) :
			           0;
		}

		std::uint64_t HashCombine(std::uint64_t a_hash, std::uint64_t a_value) noexcept
		{
			return (a_hash ^ a_value) * 1099511628211ull;
		}

		std::uint64_t BuildSettingsKey(const CharacterSettings& a_settings) noexcept
		{
			std::uint64_t hash = 1469598103934665603ull;
			auto add = [&](std::uint64_t a_value) { hash = HashCombine(hash, a_value); };
			auto addFloat = [&](float a_value) {
				std::uint32_t bits = 0;
				std::memcpy(&bits, &a_value, sizeof(bits));
				add(bits);
			};
			add(a_settings.enabled);
			add(a_settings.faces);
			add(a_settings.skin);
			add(a_settings.hair);
			addFloat(a_settings.faceStrength);
			addFloat(a_settings.skinStrength);
			addFloat(a_settings.hairStrength);
			addFloat(a_settings.maximumDistanceMeters);
			add(a_settings.adaptiveRoiSelection);
			add(a_settings.multiRoi);
			add(a_settings.minimumFacePixelSize);
			addFloat(a_settings.roiMargin);
			add(a_settings.roiHoldFrames);
			add(a_settings.depthAwareFeather);
			add(a_settings.visibilityDepthTest);
			add(a_settings.featherRadius);
			addFloat(a_settings.featherDepthThreshold);
			add(static_cast<std::uint32_t>(a_settings.maskTestMode));
			add(static_cast<std::uint32_t>(a_settings.debugView));
			return hash;
		}

		bool UsesAuthoredMask(CharacterMaskTestMode a_mode) noexcept
		{
			return a_mode == CharacterMaskTestMode::Authored ||
			       a_mode ==
			           CharacterMaskTestMode::AuthoredWithoutVisibilityDepth;
		}

		ComputeSubrect BuildFullComputeSubrect(
			std::uint32_t a_width,
			std::uint32_t a_height) noexcept
		{
			return {
				.baseX = 0,
				.baseY = 0,
				.width = a_width,
				.height = a_height,
			};
		}

		class ComputeStateGuard
		{
		public:
			explicit ComputeStateGuard(ID3D11DeviceContext* a_context) noexcept :
				context_(a_context)
			{
				if (!context_)
					return;
				classInstanceCount_ = static_cast<UINT>(classInstances_.size());
				context_->CSGetShader(&shader_, classInstances_.data(), &classInstanceCount_);
				context_->CSGetConstantBuffers(0, 1, &constantBuffer_);
				context_->CSGetShaderResources(0, static_cast<UINT>(shaderResources_.size()), shaderResources_.data());
				context_->CSGetUnorderedAccessViews(0, static_cast<UINT>(unorderedAccess_.size()), unorderedAccess_.data());
				captured_ = true;
			}

			ComputeStateGuard(const ComputeStateGuard&) = delete;
			ComputeStateGuard& operator=(const ComputeStateGuard&) = delete;

			~ComputeStateGuard() noexcept
			{
				if (captured_) {
					std::array<ID3D11ShaderResourceView*, 3> nullSrvs{};
					std::array<ID3D11UnorderedAccessView*, 2> nullUavs{};
					context_->CSSetShaderResources(0, static_cast<UINT>(nullSrvs.size()), nullSrvs.data());
					context_->CSSetUnorderedAccessViews(0, static_cast<UINT>(nullUavs.size()), nullUavs.data(), nullptr);
					context_->CSSetShader(shader_, classInstances_.data(), classInstanceCount_);
					context_->CSSetConstantBuffers(0, 1, &constantBuffer_);
					context_->CSSetShaderResources(0, static_cast<UINT>(shaderResources_.size()), shaderResources_.data());
					context_->CSSetUnorderedAccessViews(0, static_cast<UINT>(unorderedAccess_.size()), unorderedAccess_.data(), nullptr);
				}
				if (shader_)
					shader_->Release();
				for (UINT index = 0; index < classInstanceCount_; ++index) {
					if (classInstances_[index])
						classInstances_[index]->Release();
				}
				if (constantBuffer_)
					constantBuffer_->Release();
				for (auto* resource : shaderResources_) {
					if (resource)
						resource->Release();
				}
				for (auto* resource : unorderedAccess_) {
					if (resource)
						resource->Release();
				}
			}

			[[nodiscard]] bool Captured() const noexcept { return captured_; }

		private:
			ID3D11DeviceContext* context_ = nullptr;
			ID3D11ComputeShader* shader_ = nullptr;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classInstances_{};
			UINT classInstanceCount_ = 0;
			ID3D11Buffer* constantBuffer_ = nullptr;
			std::array<ID3D11ShaderResourceView*, 3> shaderResources_{};
			std::array<ID3D11UnorderedAccessView*, 2> unorderedAccess_{};
			bool captured_ = false;
		};

		class OutputMergerStateGuard
		{
		public:
			explicit OutputMergerStateGuard(ID3D11DeviceContext* a_context) noexcept :
				context_(a_context)
			{
				if (!context_)
					return;
				context_->OMGetRenderTargets(
					static_cast<UINT>(renderTargets_.size()),
					renderTargets_.data(), &depthStencil_);
				context_->OMSetRenderTargets(0, nullptr, nullptr);
				captured_ = true;
			}

			OutputMergerStateGuard(const OutputMergerStateGuard&) = delete;
			OutputMergerStateGuard& operator=(const OutputMergerStateGuard&) = delete;

			~OutputMergerStateGuard() noexcept
			{
				if (captured_) {
					context_->OMSetRenderTargets(
						static_cast<UINT>(renderTargets_.size()),
						renderTargets_.data(), depthStencil_);
				}
				for (auto* renderTarget : renderTargets_) {
					if (renderTarget)
						renderTarget->Release();
				}
				if (depthStencil_)
					depthStencil_->Release();
			}

			[[nodiscard]] bool Captured() const noexcept { return captured_; }

		private:
			ID3D11DeviceContext* context_ = nullptr;
			std::array<
				ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>
				renderTargets_{};
			ID3D11DepthStencilView* depthStencil_ = nullptr;
			bool captured_ = false;
		};
	}

	class CharacterRendering::State
	{
	public:
		struct Observation
		{
			std::uint32_t actorFormId = 0;
			std::uintptr_t geometryIdentity = 0;
			CharacterCategory category = CharacterCategory::None;
			float3 center{};
			float radius = 0.0f;
		};

		struct ActorAdmission
		{
			std::uintptr_t identity = 0;
			std::uint64_t policyKey = 0;
			std::uint32_t frame = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			float distanceMeters = std::numeric_limits<float>::max();
			std::uint32_t facePixelSize = 0;
			CharacterActorAdmissionState history{};
			bool hasFaceAnchor = false;
			bool admitted = false;
			bool valid = false;
		};

		struct ProjectedActor
		{
			std::uint32_t actorFormId = 0;
			std::array<CharacterRect, 2> selectedRects{};
			std::array<bool, 2> projectionUncertain{};
			std::array<std::uint32_t, 2> clippedGeometry{};
			std::array<CharacterProjectionReason, 2> fallbackReasons{};
			std::array<CharacterCategory, 2> fallbackCategories{};
			float nearestSelectedDistanceUnits =
				std::numeric_limits<float>::max();
		};

		struct ProjectionKey
		{
			std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint64_t settings = 0;

			bool operator==(const ProjectionKey&) const = default;
		};

		struct Readback
		{
			ComPtr<ID3D11Buffer> staging;
			ComPtr<ID3D11Query> ready;
			std::uint32_t frame = 0;
			std::uint32_t eyeIndex = 0;
			std::uint32_t featureSlot = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint64_t pixelCount = 0;
			std::uint64_t diagnosticKey = 0;
			std::uint64_t contentSerial = 0;
			std::uint64_t serial = 0;
			bool pending = false;
		};

		struct PrepareKey
		{
			std::uint32_t sourceWorldFrame = std::numeric_limits<std::uint32_t>::max();
			std::uint64_t generation = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint64_t settings = 0;
			UpscalingDLSS::ViewportCrop crop{};
			void* authoredMaskIdentity = nullptr;
			std::uintptr_t authoredDepthIdentity = 0;
			std::uintptr_t currentDepthIdentity = 0;
			float captureJitterX = 0.0f;
			float captureJitterY = 0.0f;

			bool operator==(const PrepareKey&) const = default;
		};

		struct Slot
		{
			ComPtr<ID3D11Texture2D> mask;
			ComPtr<ID3D11ShaderResourceView> maskSrv;
			ComPtr<ID3D11UnorderedAccessView> maskUav;
			ComPtr<ID3D11Buffer> coverageCounter;
			ComPtr<ID3D11UnorderedAccessView> coverageCounterUav;
			std::array<Readback, kReadbackLatency> readbacks{};
			std::uint32_t nextReadbackIndex = 0;
			ComPtr<ID3D11Buffer> maskBounds;
			ComPtr<ID3D11UnorderedAccessView> maskBoundsUav;
			ComPtr<ID3D11Buffer> maskBoundsStaging;
			ComPtr<ID3D11Query> maskBoundsReady;
			bool maskBoundsPending = false;
			bool maskBoundsResolvePending = false;
			std::uint64_t maskBoundsContentSerial = 0;
			std::uint64_t maskBoundsGeneration = 0;
			std::uint32_t maskBoundsFrame = 0;
			std::uint32_t maskBoundsSourceFrame = 0;
			std::vector<std::uint64_t> maskBoundsOwners;
			std::vector<CharacterMaskRoiTileBounds> maskBoundsTiles;
			StableCharacterMaskRoi stableMaskRoi{};
			CharacterMaskRoiAdmission maskRoiAdmission{};
			const char* maskRoiStatus = "disabled";
			bool maskRoiCurrentFrame = false;
			bool maskRoiGpuProvenEmpty = false;
			std::uint32_t maskRoiOccupiedTiles = 0;
			ComputeSubrect maskRoiRequiredSubrect{};
			double maskRoiReadbackWaitMs = 0.0;
			double maskRoiPlanningCpuMs = 0.0;
			std::uint64_t maskRoiReadbackFenceValue = 0;
			const char* maskRoiLastFailure = "";
			std::int32_t maskRoiLastFailureResult = 0;
			std::uint32_t maskRoiLastFailureFrame = std::numeric_limits<std::uint32_t>::max();
			double maskRoiLastFailureWaitMs = 0.0;
			std::uint64_t maskRoiReadbackAttempts = 0;
			std::uint64_t maskRoiReadbackSuccesses = 0;
			std::uint64_t maskRoiReadbackFallbacks = 0;
			PrepareKey prepareKey{};
			std::uint64_t contentSerial = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint32_t maskCoverageFrame =
				std::numeric_limits<std::uint32_t>::max();
			std::uint32_t maskCoverageFeatureSlot = 4;
			std::uint32_t maskCoverageWidth = 0;
			std::uint32_t maskCoverageHeight = 0;
			std::uint64_t maskPixels = 0;
			std::array<std::uint64_t, 3> authoredCategoryPixels{};
			std::array<std::uint64_t, 3> visibleCategoryPixels{};
			std::uint64_t visibilityRejectedPixels = 0;
			std::uint64_t distanceRejectedPixels = 0;
			std::uint64_t maskDiagnosticKey = 0;
			std::uint64_t maskCoverageContentSerial = 0;
			std::uint64_t maskCoverageSerial = 0;
			std::uint64_t lastCoverageRequestPolicyKey = 0;
			std::uint32_t lastCoverageRequestFrame =
				std::numeric_limits<std::uint32_t>::max();
			float maskCoveragePercent = 0.0f;
			bool maskCoverageReady = false;
			bool coverageRequestIssued = false;
			bool zeroCoverageBypassResolved = false;
			bool zeroCoverageBypassed = false;
			CharacterFeature18Disposition feature18Disposition =
				CharacterFeature18Disposition::Unresolved;
			bool zeroCoverageCpuProven = false;
			bool requiresEvaluation = true;
			ComputeSubrect computeSubrect{};
			CharacterComputeRegionPlan computeRegions{};
			StableCharacterMultiRoi stableMultiRoi{};
			CharacterMultiRoiReason multiRoiReason = CharacterMultiRoiReason::Disabled;
			std::uint64_t multiRoiPolicyKey = 0;
			ComputeSubrect maskWorkSubrect{};
			ComputeSubrect previousMaskWorkSubrect{};
			bool maskInitialized = false;
			bool maskUniform = false;
			float uniformMaskValue = 0.0f;
			StableCharacterComputeSubrect stableComputeSubrect{};
			std::uint64_t computeSubrectGeneration = 0;
			UpscalingDLSS::ViewportCrop computeSubrectCrop{};
			bool computeSubrectContractValid = false;
			bool prepared = false;
		};

		struct alignas(16) MaskConstants
		{
			std::uint32_t outputAndSourceSize[4]{};
			std::uint32_t sourceCrop[4]{};
			std::uint32_t options[4]{};
			float featherOptions[4]{};
			float visibilityOptions[4]{};
			float depthLinearization[4]{};
			Matrix cameraProjInverse{};
			float jitter[4]{};
			float categoryStrengths[4]{};
			float eligibilityRectangles
				[CharacterPolicy::kMaximumEligibilityRegions][4]{};
			std::uint32_t dispatchRegion[4]{};
			std::uint32_t authoredRegion[4]{};
		};
		static_assert(sizeof(MaskConstants) % 16 == 0);
		static_assert(offsetof(MaskConstants, cameraProjInverse) % 16 == 0);

		struct ProjectedPlan
		{
			std::vector<CharacterRect> regions;
			std::vector<CharacterMultiRoiActor> actorRegions;
			std::uint64_t eligibilitySignature = 0;
			std::uint32_t visibleFaces = 0;
			std::uint32_t visibleCharacters = 0;
			std::uint32_t selectedCharacters = 0;
			std::uint32_t adaptivelyCulledCharacters = 0;
			bool projectionUncertain = false;
			bool fullEyeEligibilityFallback = false;
			std::uint32_t projectionUncertainActors = 0;
			std::uint32_t projectionClippedGeometry = 0;
			std::uint32_t projectionFallbackActorFormId = 0;
			CharacterCategory projectionFallbackCategory = CharacterCategory::None;
			CharacterProjectionReason projectionFallbackReason = CharacterProjectionReason::ProjectedBounds;
		};

		State()
		{
			observationKeys_.reserve(
				CharacterPolicy::kMaximumObservationsPerFrame);
			actorAdmissions_.reserve(256);
		}

		[[nodiscard]] bool BeginObservationFrame(std::uint32_t a_frame)
		{
			if (observationFrame_ == a_frame)
				return true;
			if (observationFrame_ != std::numeric_limits<std::uint32_t>::max() &&
				static_cast<std::int32_t>(a_frame - observationFrame_) <= 0) {
				return false;
			}
			observationFrame_ = a_frame;
			observations_.clear();
			observationKeys_.clear();
			std::erase_if(actorAdmissions_, [a_frame](const auto& a_entry) {
				return static_cast<std::uint32_t>(a_frame - a_entry.second.frame) >
				       CharacterPolicy::kMaximumRoiHoldFrames + 1u;
			});
			unboundedCategoryMask_ = 0;
			InvalidateProjectionCache();
			snapshot_.observationFrame = a_frame;
			snapshot_.currentObservations = 0;
			snapshot_.currentCategoryObservations = {};
			return true;
		}

		void RecordClassificationRejection(
			std::uint32_t a_frame,
			std::size_t a_index)
		{
			if (a_index >= currentClassificationRejections_.size())
				return;
			std::scoped_lock lock(rejectionFrameMutex_);
			const auto currentFrame = rejectionFrame_.load(
				std::memory_order_relaxed);
			if (currentFrame != a_frame) {
				// A delayed render-worker report must never erase a newer frame.
				if (currentFrame != std::numeric_limits<std::uint32_t>::max() &&
					static_cast<std::int32_t>(a_frame - currentFrame) <= 0) {
					return;
				}
				for (auto& count : currentClassificationRejections_)
					count.store(0, std::memory_order_relaxed);
				rejectionFrame_.store(a_frame, std::memory_order_relaxed);
			}
			AtomicIncrement(currentClassificationRejections_[a_index]);
			AtomicIncrement(classificationRejections_[a_index]);
		}

		void ResetClassificationRejections()
		{
			std::scoped_lock lock(rejectionFrameMutex_);
			rejectionFrame_.store(
				std::numeric_limits<std::uint32_t>::max(),
				std::memory_order_release);
			for (auto& count : currentClassificationRejections_)
				count.store(0, std::memory_order_relaxed);
			for (auto& count : classificationRejections_)
				count.store(0, std::memory_order_relaxed);
		}

		void PublishClassificationRejections(CharacterSnapshot& a_snapshot) const
		{
			std::scoped_lock lock(rejectionFrameMutex_);
			const auto rejectionFrame = rejectionFrame_.load(std::memory_order_acquire);
			for (std::size_t index = 0;
				index < currentClassificationRejections_.size(); ++index) {
				a_snapshot.currentClassificationRejections[index] =
					rejectionFrame == a_snapshot.observationFrame ?
						currentClassificationRejections_[index].load(
							std::memory_order_relaxed) :
						0;
				a_snapshot.classificationRejections[index] =
					classificationRejections_[index].load(std::memory_order_relaxed);
			}
		}

		void InvalidateCaptureMetadata() noexcept
		{
			capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
			capturedCategoriesEmpty_ = false;
			capturedEyeWidth_ = 0;
			capturedHeight_ = 0;
			capturedEnabledCategoryMask_ = 0;
			capturedJitterX_ = 0.0f;
			capturedJitterY_ = 0.0f;
			capturedSourceRects_ = {};
			lastReadbackPollFrame_ =
				std::numeric_limits<std::uint32_t>::max();
			snapshot_.categoryCaptureFrame = capturedFrame_;
			snapshot_.categoryCaptureReady = false;
			snapshot_.categoryCaptureEmpty = false;
		}

		void InvalidateProjectionCache() noexcept
		{
			projectionKey_ = {};
			projectedActors_.clear();
			projectionCacheValid_ = false;
		}

		void InvalidatePreparedMasks(bool a_preserveMultiRoiHistory = false) noexcept
		{
			lastSlotForEye_ = { 4, 4 };
			snapshot_.eyes = {};
			for (auto& slot : slots_) {
				slot.prepared = false;
				// Keep an outstanding GPU copy alive for retirement, but it is no
				// longer eligible to finalize once its prepared content is invalid.
				slot.maskBoundsResolvePending = false;
				slot.computeRegions = {};
				if (!a_preserveMultiRoiHistory)
					slot.stableMultiRoi = {};
				if (!a_preserveMultiRoiHistory)
					slot.stableMaskRoi = {};
				if (!a_preserveMultiRoiHistory)
					slot.maskRoiAdmission = {};
				slot.prepareKey = {};
				slot.contentSerial = 0;
				slot.zeroCoverageBypassResolved = false;
				slot.zeroCoverageBypassed = false;
				slot.feature18Disposition =
					CharacterFeature18Disposition::Unresolved;
			}
		}

		void RecordPreparedFrame(
			std::uint32_t a_frame,
			std::uint32_t a_sourceWorldFrame,
			std::uint64_t a_generation,
			std::uint32_t a_featureSlot,
			std::uint64_t a_contentSerial,
			std::uint32_t a_width,
			std::uint32_t a_height,
			bool a_requiresEvaluation,
			std::uint32_t a_computeRegionCount) noexcept
		{
			CharacterPreparedFrameSnapshot* entry = nullptr;
			for (auto& candidate : snapshot_.preparedFrames) {
				if (candidate.frame == a_frame) {
					entry = &candidate;
					break;
				}
			}
			if (!entry) {
				entry = &snapshot_.preparedFrames[preparedFrameHistoryNext_];
				*entry = {};
				entry->frame = a_frame;
				preparedFrameHistoryNext_ =
					(preparedFrameHistoryNext_ + 1u) %
					static_cast<std::uint32_t>(snapshot_.preparedFrames.size());
			}
			if (a_featureSlot >= entry->widths.size() || a_contentSerial == 0)
				return;
			const auto slotBit = 1u << a_featureSlot;
			entry->resolutionRecordedSlotMask &= ~slotBit;
			entry->evaluatedSlotMask &= ~slotBit;
			entry->successfulSlotMask &= ~slotBit;
			entry->bypassedSlotMask &= ~slotBit;
			entry->abortedSlotMask &= ~slotBit;
			entry->preparedSlotMask |= slotBit;
			if (a_requiresEvaluation) {
				entry->evaluationRequiredSlotMask |= slotBit;
				entry->bypassRequestedSlotMask &= ~slotBit;
			} else {
				entry->evaluationRequiredSlotMask &= ~slotBit;
				entry->bypassRequestedSlotMask |= slotBit;
			}
			entry->sourceWorldFrames[a_featureSlot] = a_sourceWorldFrame;
			entry->generations[a_featureSlot] = a_generation;
			entry->contentSerials[a_featureSlot] = a_contentSerial;
			entry->widths[a_featureSlot] = a_width;
			entry->heights[a_featureSlot] = a_height;
			entry->computeRegionCounts[a_featureSlot] = a_requiresEvaluation ?
			                                                std::max(1u, a_computeRegionCount) :
			                                                0u;
		}

		void InvalidatePreparedSlot(
			std::uint32_t a_featureSlot,
			std::uint32_t a_eyeIndex,
			std::uint32_t a_frame) noexcept
		{
			if (a_featureSlot < slots_.size()) {
				auto& slot = slots_[a_featureSlot];
				slot.prepared = false;
				slot.requiresEvaluation = true;
				slot.computeSubrect = {};
				slot.computeRegions = {};
				slot.stableMultiRoi = {};
				slot.prepareKey = {};
				slot.contentSerial = 0;
			}
			if (a_eyeIndex < lastSlotForEye_.size()) {
				if (lastSlotForEye_[a_eyeIndex] == a_featureSlot)
					lastSlotForEye_[a_eyeIndex] = 4;
				snapshot_.eyes[a_eyeIndex] = {};
				snapshot_.eyes[a_eyeIndex].featureSlot = a_featureSlot;
			}
			if (a_featureSlot >= slots_.size())
				return;
			for (auto& preparedFrame : snapshot_.preparedFrames) {
				if (preparedFrame.frame != a_frame)
					continue;
				const auto slotBit = 1u << a_featureSlot;
				preparedFrame.preparedSlotMask &= ~slotBit;
				preparedFrame.evaluationRequiredSlotMask &= ~slotBit;
				preparedFrame.bypassRequestedSlotMask &= ~slotBit;
				preparedFrame.resolutionRecordedSlotMask &= ~slotBit;
				preparedFrame.evaluatedSlotMask &= ~slotBit;
				preparedFrame.successfulSlotMask &= ~slotBit;
				preparedFrame.bypassedSlotMask &= ~slotBit;
				preparedFrame.abortedSlotMask &= ~slotBit;
				preparedFrame.sourceWorldFrames[a_featureSlot] =
					std::numeric_limits<std::uint32_t>::max();
				preparedFrame.generations[a_featureSlot] = 0;
				preparedFrame.contentSerials[a_featureSlot] = 0;
				preparedFrame.widths[a_featureSlot] = 0;
				preparedFrame.heights[a_featureSlot] = 0;
				preparedFrame.computeRegionCounts[a_featureSlot] = 0;
				if (preparedFrame.preparedSlotMask == 0u)
					preparedFrame = {};
				break;
			}
		}

		/** The caller holds mutex_; every prepared-resource accessor uses this contract. */
		[[nodiscard]] const Slot* FindPreparedSlot(
			std::uint32_t a_featureSlot, std::uint32_t a_frameId,
			std::uint32_t a_sourceWorldFrame, std::uint64_t a_generation,
			std::uint32_t a_width, std::uint32_t a_height) const noexcept
		{
			if (a_featureSlot >= slots_.size())
				return nullptr;
			const auto& slot = slots_[a_featureSlot];
			const auto preparedFrame = std::ranges::find_if(snapshot_.preparedFrames,
				[a_frameId](const auto& prepared) { return prepared.frame == a_frameId; });
			const auto slotBit = 1u << a_featureSlot;
			return slot.prepared && preparedFrame != snapshot_.preparedFrames.end() &&
			               (preparedFrame->preparedSlotMask & slotBit) != 0 &&
			               preparedFrame->sourceWorldFrames[a_featureSlot] == a_sourceWorldFrame &&
			               preparedFrame->generations[a_featureSlot] == a_generation &&
			               preparedFrame->contentSerials[a_featureSlot] != 0 &&
			               preparedFrame->contentSerials[a_featureSlot] == slot.contentSerial &&
			               preparedFrame->widths[a_featureSlot] == a_width &&
			               preparedFrame->heights[a_featureSlot] == a_height &&
			               slot.prepareKey.sourceWorldFrame == a_sourceWorldFrame &&
			               slot.prepareKey.generation == a_generation &&
			               slot.width == a_width && slot.height == a_height ?
			           &slot :
			           nullptr;
		}

		void ClearMask(
			Slot& a_slot,
			ID3D11DeviceContext* a_context,
			std::uint32_t a_frame,
			std::uint32_t a_featureSlot,
			std::uint32_t a_width,
			std::uint32_t a_height,
			float a_value) noexcept
		{
			const std::array<float, 4> clear{ a_value, a_value, a_value, a_value };
			if (!a_slot.maskUniform || a_slot.uniformMaskValue != a_value) {
				a_context->ClearUnorderedAccessViewFloat(a_slot.maskUav.Get(), clear.data());
			}
			a_slot.maskInitialized = true;
			a_slot.maskUniform = true;
			a_slot.uniformMaskValue = a_value;
			a_slot.previousMaskWorkSubrect = a_value == 0.0f ? ComputeSubrect{} :
			                                                   BuildFullComputeSubrect(a_width, a_height);
			a_slot.maskCoverageFrame = a_frame;
			a_slot.maskCoverageFeatureSlot = a_featureSlot;
			a_slot.maskCoverageWidth = a_width;
			a_slot.maskCoverageHeight = a_height;
			const auto pixelCount = static_cast<std::uint64_t>(a_width) * a_height;
			a_slot.maskPixels = a_value > (0.5f / 255.0f) ? pixelCount : 0;
			a_slot.authoredCategoryPixels = {};
			a_slot.visibleCategoryPixels = {};
			a_slot.visibilityRejectedPixels = 0;
			a_slot.distanceRejectedPixels = 0;
			a_slot.maskDiagnosticKey = 0;
			a_slot.maskCoverageContentSerial = a_slot.contentSerial;
			a_slot.maskCoverageSerial = AllocateCoverageSerial();
			a_slot.lastCoverageRequestPolicyKey = 0;
			a_slot.lastCoverageRequestFrame =
				std::numeric_limits<std::uint32_t>::max();
			a_slot.maskCoveragePercent = a_slot.maskPixels ? 100.0f : 0.0f;
			a_slot.maskCoverageReady = true;
			a_slot.coverageRequestIssued = false;
			a_slot.zeroCoverageBypassResolved = false;
			a_slot.zeroCoverageBypassed = false;
			a_slot.feature18Disposition =
				CharacterFeature18Disposition::Unresolved;
			a_slot.zeroCoverageCpuProven = false;
		}

		std::uint64_t AllocateCoverageSerial() noexcept
		{
			const auto serial = nextCoverageRequestSerial_;
			if (nextCoverageRequestSerial_ !=
				std::numeric_limits<std::uint64_t>::max()) {
				++nextCoverageRequestSerial_;
			}
			return serial;
		}

		std::uint64_t AllocatePreparedContentSerial() noexcept
		{
			const auto serial = nextPreparedContentSerial_;
			++nextPreparedContentSerial_;
			if (nextPreparedContentSerial_ == 0)
				nextPreparedContentSerial_ = 1;
			return serial;
		}

		void AdoptDevice(ID3D11Device* a_device)
		{
			if (device_ && !SameIdentity(device_.Get(), a_device)) {
				slots_ = {};
				shader_.Reset();
				constants_.Reset();
				ResetMaskBoundsShader();
				capturedCategories_.Reset();
				capturedCategoriesSrv_.Reset();
				capturedCategoriesUav_.Reset();
				capturedDepth_.Reset();
				capturedDepthSrv_.Reset();
				capturedDepthUav_.Reset();
				captureShader_.Reset();
				captureConstants_.Reset();
				captureSourceCategoriesSrv_.Reset();
				captureShaderCompileFailed_ = false;
				InvalidateCaptureMetadata();
				shaderCompileFailed_ = false;
				actorAdmissions_.clear();
				InvalidateProjectionCache();
				InvalidatePreparedMasks();
				snapshot_.preparedFrames = {};
				preparedFrameHistoryNext_ = 0;
			}
			device_ = a_device;
		}

		bool EnsureCategoryCapture(
			ID3D11Device* a_device,
			const D3D11_TEXTURE2D_DESC& a_sourceDesc)
		{
			if (capturedCategories_) {
				D3D11_TEXTURE2D_DESC current{};
				capturedCategories_->GetDesc(&current);
				if (current.Width == a_sourceDesc.Width &&
					current.Height == a_sourceDesc.Height &&
					current.Format == a_sourceDesc.Format) {
					return true;
				}
			}

			capturedCategories_.Reset();
			capturedCategoriesSrv_.Reset();
			capturedCategoriesUav_.Reset();
			InvalidateCaptureMetadata();
			InvalidatePreparedMasks();
			D3D11_TEXTURE2D_DESC captureDesc = a_sourceDesc;
			captureDesc.Usage = D3D11_USAGE_DEFAULT;
			captureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			captureDesc.CPUAccessFlags = 0;
			captureDesc.MiscFlags = 0;
			if (FAILED(a_device->CreateTexture2D(
					&captureDesc, nullptr, &capturedCategories_)) ||
				FAILED(a_device->CreateShaderResourceView(
					capturedCategories_.Get(), nullptr,
					&capturedCategoriesSrv_)) ||
				FAILED(a_device->CreateUnorderedAccessView(
					capturedCategories_.Get(), nullptr, &capturedCategoriesUav_))) {
				capturedCategories_.Reset();
				capturedCategoriesSrv_.Reset();
				capturedCategoriesUav_.Reset();
				return false;
			}
			Util::SetResourceName(
				capturedCategories_.Get(),
				"DLSS5CharacterRendering::FrozenCategories");
			Util::SetResourceName(
				capturedCategoriesSrv_.Get(),
				"DLSS5CharacterRendering::FrozenCategories SRV");
			Util::SetResourceName(capturedCategoriesUav_.Get(), "DLSS5CharacterRendering::FrozenCategories UAV");
			return true;
		}

		bool EnsureDepthCapture(
			ID3D11Device* a_device,
			const D3D11_TEXTURE2D_DESC& a_sourceDesc)
		{
			if (capturedDepth_) {
				D3D11_TEXTURE2D_DESC current{};
				capturedDepth_->GetDesc(&current);
				if (current.Width == a_sourceDesc.Width &&
					current.Height == a_sourceDesc.Height &&
					current.Format == DXGI_FORMAT_R32_FLOAT) {
					return true;
				}
			}

			capturedDepth_.Reset();
			capturedDepthSrv_.Reset();
			capturedDepthUav_.Reset();
			InvalidateCaptureMetadata();
			InvalidatePreparedMasks();
			D3D11_TEXTURE2D_DESC captureDesc = a_sourceDesc;
			captureDesc.Format = DXGI_FORMAT_R32_FLOAT;
			captureDesc.Usage = D3D11_USAGE_DEFAULT;
			captureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			captureDesc.CPUAccessFlags = 0;
			captureDesc.MiscFlags = 0;
			if (FAILED(a_device->CreateTexture2D(
					&captureDesc, nullptr, &capturedDepth_)) ||
				FAILED(a_device->CreateShaderResourceView(
					capturedDepth_.Get(), nullptr, &capturedDepthSrv_)) ||
				FAILED(a_device->CreateUnorderedAccessView(
					capturedDepth_.Get(), nullptr, &capturedDepthUav_))) {
				capturedDepth_.Reset();
				capturedDepthSrv_.Reset();
				capturedDepthUav_.Reset();
				return false;
			}
			Util::SetResourceName(
				capturedDepth_.Get(),
				"DLSS5CharacterRendering::FrozenDepth");
			Util::SetResourceName(
				capturedDepthSrv_.Get(),
				"DLSS5CharacterRendering::FrozenDepth SRV");
			Util::SetResourceName(capturedDepthUav_.Get(), "DLSS5CharacterRendering::FrozenDepth UAV");
			return true;
		}

		bool EnsureCaptureShader(ID3D11Device* a_device, ID3D11Texture2D* a_source)
		{
			if (!captureShader_) {
				if (captureShaderCompileFailed_)
					return false;
				captureShader_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
					L"Data\\Shaders\\DLSS5CharacterCaptureCS.hlsl", {}, "cs_5_0")));
				if (!captureShader_) {
					captureShaderCompileFailed_ = true;
					return false;
				}
				Util::SetResourceName(captureShader_.Get(), "DLSS5CharacterRendering::CaptureCS");
			}
			if (!captureConstants_) {
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = 16;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				if (FAILED(a_device->CreateBuffer(&desc, nullptr, &captureConstants_)))
					return false;
				Util::SetResourceName(captureConstants_.Get(), "DLSS5CharacterRendering::CaptureConstants");
			}
			ComPtr<ID3D11Resource> previousSource;
			if (captureSourceCategoriesSrv_)
				captureSourceCategoriesSrv_->GetResource(&previousSource);
			if (!SameIdentity(previousSource.Get(), a_source)) {
				captureSourceCategoriesSrv_.Reset();
				if (FAILED(a_device->CreateShaderResourceView(a_source, nullptr, &captureSourceCategoriesSrv_)))
					return false;
				Util::SetResourceName(captureSourceCategoriesSrv_.Get(), "DLSS5CharacterRendering::SourceCategories SRV");
			}
			return true;
		}

		bool ValidateTexture(
			ID3D11ShaderResourceView* a_view,
			ID3D11Device* a_device,
			DXGI_FORMAT a_format,
			ComPtr<ID3D11Texture2D>& a_texture,
			D3D11_TEXTURE2D_DESC& a_desc,
			void*& a_identity,
			std::string& a_error) const
		{
			if (!a_view) {
				a_error = "required character-mask source view is null";
				return false;
			}
			ComPtr<ID3D11Resource> resource;
			a_view->GetResource(&resource);
			if (!resource || FAILED(resource.As(&a_texture)) || !a_texture) {
				a_error = "character-mask source is not a Texture2D";
				return false;
			}
			a_texture->GetDesc(&a_desc);
			if (a_desc.Format != a_format || a_desc.MipLevels != 1 ||
				a_desc.ArraySize != 1 || a_desc.SampleDesc.Count != 1) {
				a_error = std::format(
					"character-mask source has invalid format/layout (format={})",
					static_cast<std::uint32_t>(a_desc.Format));
				return false;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
			a_view->GetDesc(&viewDesc);
			if (viewDesc.Format != a_format ||
				viewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				viewDesc.Texture2D.MostDetailedMip != 0 ||
				viewDesc.Texture2D.MipLevels != 1) {
				a_error = "character-mask source view has invalid format or mip layout";
				return false;
			}
			ComPtr<ID3D11Device> resourceDevice;
			a_texture->GetDevice(&resourceDevice);
			if (!SameIdentity(a_device, resourceDevice.Get())) {
				a_error = "character-mask source belongs to a different D3D11 device";
				return false;
			}
			ComPtr<IUnknown> identity;
			if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&identity)))) {
				a_error = "character-mask source identity query failed";
				return false;
			}
			a_identity = identity.Get();
			return true;
		}

		bool ValidateDepthTexture(
			ID3D11ShaderResourceView* a_view,
			ID3D11Device* a_device,
			std::uint32_t a_expectedWidth,
			std::uint32_t a_expectedHeight,
			CharacterDepthExtentPolicy a_extentPolicy,
			ComPtr<ID3D11Texture2D>& a_texture,
			std::uintptr_t& a_identity,
			std::string& a_error) const
		{
			if (!a_view) {
				a_error = "character mask depth view is null";
				return false;
			}
			ComPtr<ID3D11Resource> resource;
			a_view->GetResource(&resource);
			if (!resource || FAILED(resource.As(&a_texture)) || !a_texture) {
				a_error = "character mask depth view is not a Texture2D";
				return false;
			}
			D3D11_TEXTURE2D_DESC textureDesc{};
			a_texture->GetDesc(&textureDesc);
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
			a_view->GetDesc(&viewDesc);
			if (!IsCharacterDepthExtentValid(textureDesc.Width, textureDesc.Height,
					a_expectedWidth, a_expectedHeight, a_extentPolicy) ||
				textureDesc.MipLevels != 1 || textureDesc.ArraySize != 1 ||
				textureDesc.SampleDesc.Count != 1 ||
				viewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				viewDesc.Texture2D.MostDetailedMip != 0 ||
				viewDesc.Texture2D.MipLevels != 1 ||
				!IsSupportedDepthViewFormat(viewDesc.Format)) {
				a_error = std::format(
					"character mask depth view has invalid layout (format={})",
					static_cast<std::uint32_t>(viewDesc.Format));
				return false;
			}
			ComPtr<ID3D11Device> resourceDevice;
			a_texture->GetDevice(&resourceDevice);
			if (!SameIdentity(a_device, resourceDevice.Get())) {
				a_error = "character mask depth view belongs to another D3D11 device";
				return false;
			}
			a_identity = GetIdentityToken(resource.Get());
			if (!a_identity) {
				a_error = "character mask depth-view identity could not be resolved";
				return false;
			}
			return true;
		}

		bool EnsureShader(ID3D11Device* a_device)
		{
			if (shader_)
				return true;
			if (shaderCompileFailed_)
				return false;

			auto* compiled = static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data\\Shaders\\DLSS5CharacterMaskCS.hlsl", {}, "cs_5_0"));
			if (!compiled) {
				shaderCompileFailed_ = true;
				return false;
			}
			shader_.Attach(compiled);
			Util::SetResourceName(shader_.Get(), "DLSS5CharacterRendering::MaskResolveCS");

			D3D11_BUFFER_DESC constantsDesc{};
			constantsDesc.ByteWidth = static_cast<UINT>(sizeof(MaskConstants));
			constantsDesc.Usage = D3D11_USAGE_DEFAULT;
			constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(a_device->CreateBuffer(&constantsDesc, nullptr, &constants_))) {
				shader_.Reset();
				shaderCompileFailed_ = true;
				return false;
			}
			Util::SetResourceName(constants_.Get(), "DLSS5CharacterRendering::MaskConstants");
			return true;
		}

		void ResetMaskBoundsShader() noexcept
		{
			maskBoundsShader_.Reset();
			maskBoundsConstants_.Reset();
			maskBoundsShaderFailed_ = false;
			maskBoundsFence_.Reset();
			maskBoundsFenceValue_ = 0;
			maskBoundsFenceUnsupported_ = false;
		}

		CharacterMaskReadbackCompletion SignalMaskBoundsCompletion(ID3D11DeviceContext* a_context)
		{
			if (maskBoundsFenceUnsupported_)
				return {};
			ComPtr<ID3D11DeviceContext4> context4;
			auto result = a_context->QueryInterface(IID_PPV_ARGS(&context4));
			if (result == E_NOINTERFACE) {
				maskBoundsFenceUnsupported_ = true;
				return {};
			}
			if (FAILED(result))
				return { nullptr, 0, result };
			if (!maskBoundsFence_) {
				ComPtr<ID3D11Device5> device5;
				result = device_.As(&device5);
				if (result == E_NOINTERFACE) {
					maskBoundsFenceUnsupported_ = true;
					return {};
				}
				if (FAILED(result))
					return { nullptr, 0, result };
				result = device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&maskBoundsFence_));
				if (FAILED(result))
					return { nullptr, 0, result };
				Util::SetResourceName(maskBoundsFence_.Get(), "DLSS5CharacterRendering::MaskReadbackFence");
			}
			if (maskBoundsFenceValue_ >= std::numeric_limits<std::uint64_t>::max() - 1u)
				return { nullptr, 0, HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW) };
			const auto value = ++maskBoundsFenceValue_;
			result = context4->Signal(maskBoundsFence_.Get(), value);
			return { maskBoundsFence_.Get(), value, result };
		}

		bool EnsureMaskBoundsResources(Slot& a_slot, ID3D11Device* a_device)
		{
			maskBoundsResourceResult_ = S_OK;
			if (!maskBoundsShader_) {
				if (maskBoundsShaderFailed_) {
					maskBoundsResourceResult_ = E_FAIL;
					return false;
				}
				auto* compiled = static_cast<ID3D11ComputeShader*>(Util::CompileShader(
					L"Data\\Shaders\\DLSS5CharacterMaskBoundsCS.hlsl", {}, "cs_5_0"));
				if (!compiled) {
					maskBoundsShaderFailed_ = true;
					maskBoundsResourceResult_ = E_FAIL;
					return false;
				}
				maskBoundsShader_.Attach(compiled);
				D3D11_BUFFER_DESC constantsDesc{};
				constantsDesc.ByteWidth = sizeof(std::uint32_t) * 4u;
				constantsDesc.Usage = D3D11_USAGE_DEFAULT;
				constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				if (FAILED(maskBoundsResourceResult_ = a_device->CreateBuffer(&constantsDesc, nullptr, &maskBoundsConstants_))) {
					maskBoundsShader_.Reset();
					maskBoundsShaderFailed_ = true;
					return false;
				}
				Util::SetResourceName(maskBoundsShader_.Get(), "DLSS5CharacterRendering::MaskBoundsCS");
			}
			if (a_slot.maskBounds && a_slot.maskBoundsUav &&
				a_slot.maskBoundsStaging && a_slot.maskBoundsReady)
				return true;
			const auto tilesX = (a_slot.width + kCharacterMaskRoiTileSize - 1u) / kCharacterMaskRoiTileSize;
			const auto tilesY = (a_slot.height + kCharacterMaskRoiTileSize - 1u) / kCharacterMaskRoiTileSize;
			const auto tileCount = static_cast<std::uint64_t>(tilesX) * tilesY;
			if (!tileCount || tileCount > std::numeric_limits<UINT>::max() / sizeof(CharacterMaskRoiTileBounds)) {
				maskBoundsResourceResult_ = E_INVALIDARG;
				return false;
			}
			// Allocate transactionally. A failed optional allocation must not damage
			// the already prepared mask or disable the normal CPU-bound NR path.
			ComPtr<ID3D11Buffer> bounds, staging;
			ComPtr<ID3D11UnorderedAccessView> uav;
			ComPtr<ID3D11Query> ready;
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = static_cast<UINT>(tileCount * sizeof(CharacterMaskRoiTileBounds));
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(CharacterMaskRoiTileBounds);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.NumElements = static_cast<UINT>(tileCount);
			if (FAILED(maskBoundsResourceResult_ = a_device->CreateBuffer(&desc, nullptr, &bounds)) ||
				FAILED(maskBoundsResourceResult_ = a_device->CreateUnorderedAccessView(bounds.Get(), &uavDesc, &uav)))
				return false;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.MiscFlags = 0;
			desc.StructureByteStride = 0;
			D3D11_QUERY_DESC queryDesc{ D3D11_QUERY_EVENT, 0 };
			if (FAILED(maskBoundsResourceResult_ = a_device->CreateBuffer(&desc, nullptr, &staging)) ||
				FAILED(maskBoundsResourceResult_ = a_device->CreateQuery(&queryDesc, &ready)))
				return false;
			a_slot.maskBoundsTiles.resize(static_cast<std::size_t>(tileCount));
			a_slot.maskBounds = std::move(bounds);
			a_slot.maskBoundsUav = std::move(uav);
			a_slot.maskBoundsStaging = std::move(staging);
			a_slot.maskBoundsReady = std::move(ready);
			Util::SetResourceName(a_slot.maskBounds.Get(), "DLSS5CharacterRendering::MaskTileBounds");
			Util::SetResourceName(a_slot.maskBoundsStaging.Get(), "DLSS5CharacterRendering::MaskTileBoundsReadback");
			return true;
		}

		bool RejectMaskBounds(const CharacterMaskPrepareArgs& a_args, Slot& a_slot,
			const char* a_reason, HRESULT a_result = S_OK, double a_waitMs = 0.0)
		{
			a_slot.maskRoiStatus = a_reason;
			a_slot.maskRoiLastFailure = a_reason;
			a_slot.maskRoiLastFailureResult = static_cast<std::int32_t>(a_result);
			a_slot.maskRoiLastFailureFrame = a_args.frameId;
			a_slot.maskRoiLastFailureWaitMs = a_waitMs;
			a_slot.maskBoundsResolvePending = false;
			a_slot.maskRoiAdmission.Reject(a_args.frameId);
			Increment(a_slot.maskRoiReadbackFallbacks);
			return false;
		}

		bool QueueCurrentMaskBounds(const CharacterMaskPrepareArgs& a_args, Slot& a_slot,
			const ProjectedPlan& a_plan, std::uint32_t a_sourceFrame)
		{
			if (!a_slot.maskRoiAdmission.CanAttempt(a_args.frameId)) {
				a_slot.maskRoiStatus = "readback_retry_backoff";
				return false;
			}
			Increment(a_slot.maskRoiReadbackAttempts);
			const auto fallback = [&](const char* a_reason, HRESULT a_result = S_OK) {
				return RejectMaskBounds(a_args, a_slot, a_reason, a_result);
			};
			if (a_args.context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return fallback("deferred_context");
			if (!EnsureMaskBoundsResources(a_slot, a_args.device))
				return fallback("bounds_resources_unavailable", maskBoundsResourceResult_);
			if (a_slot.maskBoundsPending) {
				// A timed-out copy is retired, NEVER consumed as current coverage.
				// Do not queue more copies behind a still-busy staging resource.
				BOOL ready = FALSE;
				const auto result = a_args.context->GetData(a_slot.maskBoundsReady.Get(),
					&ready, sizeof(ready), D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if (result != S_OK || !ready)
					return fallback(FAILED(result) ? "query_failed" : "previous_copy_pending", result);
				a_slot.maskBoundsPending = false;
			}
			a_slot.maskBoundsResolvePending = false;
			a_slot.maskBoundsOwners.clear();
			a_slot.maskBoundsOwners.reserve(a_plan.actorRegions.size());
			for (const auto& actor : a_plan.actorRegions)
				a_slot.maskBoundsOwners.push_back(actor.identity);
			{
				ComputeStateGuard stateGuard(a_args.context);
				if (!stateGuard.Captured())
					return fallback("compute_state_unavailable");
				const auto tilesX = (a_slot.width + kCharacterMaskRoiTileSize - 1u) / kCharacterMaskRoiTileSize;
				const auto tilesY = (a_slot.height + kCharacterMaskRoiTileSize - 1u) / kCharacterMaskRoiTileSize;
				const std::array<std::uint32_t, 4> sizes{ a_slot.width, a_slot.height, tilesX, 0 };
				a_args.context->UpdateSubresource(maskBoundsConstants_.Get(), 0, nullptr, sizes.data(), 0, 0);
				ID3D11Buffer* constants = maskBoundsConstants_.Get();
				ID3D11ShaderResourceView* source = a_slot.maskSrv.Get();
				ID3D11UnorderedAccessView* destination = a_slot.maskBoundsUav.Get();
				// Clear conflicting mask UAV bindings before exposing it as an SRV.
				std::array<ID3D11UnorderedAccessView*, 2> nullUavs{};
				a_args.context->CSSetUnorderedAccessViews(0, 2, nullUavs.data(), nullptr);
				a_args.context->CSSetShader(maskBoundsShader_.Get(), nullptr, 0);
				a_args.context->CSSetConstantBuffers(0, 1, &constants);
				a_args.context->CSSetShaderResources(0, 1, &source);
				a_args.context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
				{
					CS_PROFILE_SCOPE("Upscaling::DLSS5CharacterMaskBounds");
					a_args.context->Dispatch(tilesX, tilesY, 1);
				}
				a_args.context->CSSetUnorderedAccessViews(0, 2, nullUavs.data(), nullptr);
				a_args.context->CopyResource(a_slot.maskBoundsStaging.Get(), a_slot.maskBounds.Get());
				a_args.context->End(a_slot.maskBoundsReady.Get());
				a_slot.maskBoundsPending = true;
			}
			a_slot.maskBoundsResolvePending = true;
			a_slot.maskBoundsContentSerial = a_slot.contentSerial;
			a_slot.maskBoundsGeneration = a_args.generation;
			a_slot.maskBoundsFrame = a_args.frameId;
			a_slot.maskBoundsSourceFrame = a_sourceFrame;
			a_slot.maskRoiStatus = "current_bounds_queued";
			return true;
		}

		bool EnsureSlot(
			Slot& a_slot,
			ID3D11Device* a_device,
			std::uint32_t a_slotIndex,
			std::uint32_t a_width,
			std::uint32_t a_height)
		{
			if (a_slot.mask && a_slot.width == a_width && a_slot.height == a_height)
				return true;

			a_slot = {};
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = a_width;
			textureDesc.Height = a_height;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = DXGI_FORMAT_R8_UNORM;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			if (FAILED(a_device->CreateTexture2D(&textureDesc, nullptr, &a_slot.mask)) ||
				FAILED(a_device->CreateShaderResourceView(a_slot.mask.Get(), nullptr, &a_slot.maskSrv)) ||
				FAILED(a_device->CreateUnorderedAccessView(a_slot.mask.Get(), nullptr, &a_slot.maskUav))) {
				a_slot = {};
				return false;
			}
			const auto baseName = std::format(
				"DLSS5CharacterRendering::SelectionMaskSlot{}", a_slotIndex);
			Util::SetResourceName(a_slot.mask.Get(), "%s", baseName.c_str());
			Util::SetResourceName(a_slot.maskSrv.Get(), "%s SRV", baseName.c_str());
			Util::SetResourceName(a_slot.maskUav.Get(), "%s UAV", baseName.c_str());

			D3D11_BUFFER_DESC counterDesc{};
			counterDesc.ByteWidth =
				kDiagnosticCounterCount * sizeof(std::uint32_t);
			counterDesc.Usage = D3D11_USAGE_DEFAULT;
			counterDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			counterDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			if (FAILED(a_device->CreateBuffer(&counterDesc, nullptr, &a_slot.coverageCounter))) {
				a_slot = {};
				return false;
			}
			D3D11_UNORDERED_ACCESS_VIEW_DESC counterUavDesc{};
			counterUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			counterUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			counterUavDesc.Buffer.NumElements = kDiagnosticCounterCount;
			counterUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			if (FAILED(a_device->CreateUnorderedAccessView(
					a_slot.coverageCounter.Get(), &counterUavDesc,
					&a_slot.coverageCounterUav))) {
				a_slot = {};
				return false;
			}
			Util::SetResourceName(a_slot.coverageCounter.Get(), "%s Diagnostics", baseName.c_str());
			Util::SetResourceName(a_slot.coverageCounterUav.Get(), "%s Diagnostics UAV", baseName.c_str());

			D3D11_BUFFER_DESC stagingDesc = counterDesc;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.BindFlags = 0;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.MiscFlags = 0;
			D3D11_QUERY_DESC queryDesc{ D3D11_QUERY_EVENT, 0 };
			for (std::uint32_t index = 0; index < kReadbackLatency; ++index) {
				if (FAILED(a_device->CreateBuffer(
						&stagingDesc, nullptr, &a_slot.readbacks[index].staging)) ||
					FAILED(a_device->CreateQuery(
						&queryDesc, &a_slot.readbacks[index].ready))) {
					a_slot = {};
					return false;
				}
				Util::SetResourceName(
					a_slot.readbacks[index].staging.Get(),
					"%s Diagnostics Readback %u", baseName.c_str(), index);
				Util::SetResourceName(
					a_slot.readbacks[index].ready.Get(),
					"%s Diagnostics Ready %u", baseName.c_str(), index);
			}
			a_slot.width = a_width;
			a_slot.height = a_height;
			return true;
		}

		bool ReadCurrentMaskBounds(const CharacterMaskPrepareArgs& a_args, Slot& a_slot,
			std::chrono::steady_clock::time_point a_deadline,
			CharacterMaskReadbackCompletion a_completion)
		{
			try {
				if (!a_slot.maskBoundsResolvePending)
					return false;
				if (!a_slot.maskBoundsPending || a_slot.contentSerial == 0 ||
					a_slot.maskBoundsContentSerial != a_slot.contentSerial ||
					a_slot.maskBoundsFrame != a_args.frameId ||
					a_slot.maskBoundsSourceFrame != a_args.sourceWorldFrame ||
					a_slot.maskBoundsGeneration != a_args.generation) {
					RejectMaskBounds(a_args, a_slot, "queued_bounds_identity_mismatch", E_INVALIDARG);
					return false;
				}
				a_slot.maskRoiReadbackFenceValue = a_completion.value;
				const auto readback = ReadCharacterMaskBounds(a_args.context,
					a_slot.maskBoundsReady.Get(), a_slot.maskBoundsStaging.Get(),
					std::as_writable_bytes(std::span(a_slot.maskBoundsTiles)), a_deadline, a_completion);
				a_slot.maskRoiReadbackWaitMs = readback.waitMs;
				a_slot.maskBoundsResolvePending = false;
				if (!readback.Ready()) {
					RejectMaskBounds(a_args, a_slot, readback.Reason(), readback.result, readback.waitMs);
					return false;
				}
				a_slot.maskBoundsPending = false;
				Increment(a_slot.maskRoiReadbackSuccesses);
				return true;
			} catch (...) {
				return RejectMaskBounds(a_args, a_slot, "bounds_readback_failed", E_FAIL,
					a_slot.maskRoiReadbackWaitMs);
			}
		}

		void ResolveCurrentMaskBounds(const CharacterMaskPrepareArgs& a_args, Slot& a_slot)
		{
			try {
				const auto planningStart = std::chrono::steady_clock::now();
				const auto tight = ResolveCharacterMaskRoi(a_slot.maskBoundsTiles, a_slot.maskBoundsOwners,
					a_args.outputWidth, a_args.outputHeight, a_args.sourceWorldFrame, a_slot.stableMaskRoi);
				a_slot.maskRoiPlanningCpuMs = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - planningStart)
				                                  .count();
				if (!tight.valid) {
					RejectMaskBounds(a_args, a_slot, "invalid_current_bounds", E_INVALIDARG, a_slot.maskRoiReadbackWaitMs);
					return;
				}
				const bool admitted = a_slot.maskRoiAdmission.ObserveFresh(a_args.frameId);
				if (!admitted && !tight.empty) {
					a_slot.maskRoiStatus = "current_bounds_warmup";
					return;
				}
				// Coverage is proven by THIS resolved mask, not by broad geometry
				// eligibility or an earlier frame's debug counters. Keep maskWorkSubrect
				// unchanged: it defines authoring/dirty clearing, never inference cost.
				a_slot.computeSubrect = tight.computeSubrect;
				a_slot.computeRegions = tight.computeRegions;
				a_slot.multiRoiReason = tight.multiRoiReason;
				a_slot.maskRoiCurrentFrame = true;
				a_slot.maskRoiGpuProvenEmpty = tight.empty;
				a_slot.maskRoiOccupiedTiles = tight.occupiedTiles;
				a_slot.maskRoiRequiredSubrect = tight.requiredSubrect;
				a_slot.maskRoiStatus = tight.empty                     ? "current_mask_empty" :
				                       tight.computeRegions.count == 2 ? "current_mask_split" :
				                                                         "current_mask_single";
			} catch (...) {
				// Optional optimization failures retain the already valid CPU plan.
				a_slot.stableMaskRoi = {};
				RejectMaskBounds(a_args, a_slot, "bounds_optimization_failed", E_FAIL,
					a_slot.maskRoiReadbackWaitMs);
			}
		}

		static void PublishMaskRoiSnapshot(const Slot& a_slot, CharacterEyeSnapshot& a_eye)
		{
			a_eye.maskRoiStatus = a_slot.maskRoiStatus;
			a_eye.maskRoiCurrentFrame = a_slot.maskRoiCurrentFrame;
			a_eye.maskRoiGpuProvenEmpty = a_slot.maskRoiGpuProvenEmpty;
			a_eye.maskRoiOccupiedTiles = a_slot.maskRoiOccupiedTiles;
			a_eye.maskRoiRequiredSubrect = a_slot.maskRoiRequiredSubrect;
			a_eye.maskRoiReadbackWaitMs = a_slot.maskRoiReadbackWaitMs;
			a_eye.maskRoiPlanningCpuMs = a_slot.maskRoiPlanningCpuMs;
			a_eye.maskRoiReadbackFenceValue = a_slot.maskRoiReadbackFenceValue;
			a_eye.maskRoiLastFailure = a_slot.maskRoiLastFailure;
			a_eye.maskRoiLastFailureResult = a_slot.maskRoiLastFailureResult;
			a_eye.maskRoiLastFailureFrame = a_slot.maskRoiLastFailureFrame;
			a_eye.maskRoiLastFailureWaitMs = a_slot.maskRoiLastFailureWaitMs;
			a_eye.maskRoiReadbackAttempts = a_slot.maskRoiReadbackAttempts;
			a_eye.maskRoiReadbackSuccesses = a_slot.maskRoiReadbackSuccesses;
			a_eye.maskRoiReadbackFallbacks = a_slot.maskRoiReadbackFallbacks;
		}

		CharacterProjectionResult ProjectSphere(
			const Observation& a_observation,
			const RE::NiPoint3& a_eye,
			const float4x4& a_matrix,
			const float4x4& a_rasterMatrix,
			std::uint32_t a_width,
			std::uint32_t a_height,
			CharacterRect& a_rect,
			CharacterProjectionReason* a_reason = nullptr) const
		{
			const float3 relative{
				a_observation.center.x - a_eye.x,
				a_observation.center.y - a_eye.y,
				a_observation.center.z - a_eye.z,
			};
			std::array<CharacterClipPoint, 8> corners{};
			std::array<CharacterClipPoint, 8> rasterCorners{};
			std::size_t cornerIndex = 0;
			for (int z = -1; z <= 1; z += 2) {
				for (int y = -1; y <= 1; y += 2) {
					for (int x = -1; x <= 1; x += 2) {
						const float4 point{
							relative.x + x * a_observation.radius,
							relative.y + y * a_observation.radius,
							relative.z + z * a_observation.radius,
							1.0f,
						};
						const auto clip = DirectX::SimpleMath::Vector4::Transform(
							point, a_matrix);
						const auto rasterClip = DirectX::SimpleMath::Vector4::Transform(
							point, a_rasterMatrix);
						corners[cornerIndex] = { clip.x, clip.y, clip.w };
						rasterCorners[cornerIndex++] = { rasterClip.x, rasterClip.y, rasterClip.w };
					}
				}
			}
			// A bound outside the unjittered viewport can still author raster pixels.
			// Union both coherent projections before any offscreen/empty decision.
			return ResolveCharacterProjectionPair(corners, rasterCorners,
				a_width, a_height, a_rect, a_reason);
		}

		void RefreshProjectedActors(const CharacterMaskPrepareArgs& a_args)
		{
			const ProjectionKey key{
				.frame = a_args.frameId,
				.width = a_args.viewportCrop.fullOutput.width,
				.height = a_args.viewportCrop.fullOutput.height,
				.settings = BuildSettingsKey(a_args.settings),
			};
			if (projectionCacheValid_ && projectionKey_ == key)
				return;

			projectionKey_ = key;
			projectionCacheValid_ = true;
			projectedActors_.clear();
			if (observationFrame_ != a_args.frameId)
				return;

			std::unordered_map<std::uint32_t, ProjectedActor> actors;
			actors.reserve(observations_.size());
			const auto averageEye = GetProjectionAverageEyePosition();
			const std::array<RE::NiPoint3, 2> eyePositions{
				GetProjectionEyePosition(0),
				GetProjectionEyePosition(1),
			};
			const std::array<float4x4, 2> eyeMatrices{
				globals::game::frameBufferCached
					.GetCameraViewProjUnjittered(0)
					.Transpose(),
				globals::game::frameBufferCached
					.GetCameraViewProjUnjittered(1)
					.Transpose(),
			};
			const std::array<float4x4, 2> rasterMatrices{
				globals::game::frameBufferCached.GetCameraViewProj(0).Transpose(),
				globals::game::frameBufferCached.GetCameraViewProj(1).Transpose(),
			};
			for (const auto& observation : observations_) {
				if (!IsCharacterCategoryEnabled(observation.category, a_args.settings))
					continue;
				auto& actor = actors[observation.actorFormId];
				actor.actorFormId = observation.actorFormId;
				const float dx = observation.center.x - averageEye.x;
				const float dy = observation.center.y - averageEye.y;
				const float dz = observation.center.z - averageEye.z;
				const float surfaceDistance = std::max(
					0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - observation.radius);
				actor.nearestSelectedDistanceUnits = std::min(
					actor.nearestSelectedDistanceUnits, surfaceDistance);
				for (std::uint32_t eye = 0; eye < 2; ++eye) {
					CharacterRect projected{};
					CharacterProjectionReason reason{};
					const auto projection = ProjectSphere(
						observation, eyePositions[eye], eyeMatrices[eye], rasterMatrices[eye],
						key.width, key.height, projected, &reason);
					if (projection == CharacterProjectionResult::Offscreen) {
						continue;
					}
					if (reason == CharacterProjectionReason::ClippedBounds)
						++actor.clippedGeometry[eye];
					if (projection == CharacterProjectionResult::Uncertain && !actor.projectionUncertain[eye]) {
						actor.projectionUncertain[eye] = true;
						actor.fallbackReasons[eye] = reason;
						actor.fallbackCategories[eye] = observation.category;
					}
					actor.selectedRects[eye] = CharacterRegionPolicy::Union(
						actor.selectedRects[eye], projected);
				}
			}
			projectedActors_.reserve(actors.size());
			for (auto& [actorFormId, actor] : actors) {
				(void)actorFormId;
				projectedActors_.push_back(std::move(actor));
			}
			std::ranges::sort(
				projectedActors_, {}, &ProjectedActor::actorFormId);
		}

		ProjectedPlan BuildPlan(const CharacterMaskPrepareArgs& a_args)
		{
			CS_PROFILE_CPU_SCOPE("Upscaling::DLSS5CharacterRoiSetup");
			ProjectedPlan result;
			for (const auto category : {
					 CharacterCategory::Face,
					 CharacterCategory::Skin,
					 CharacterCategory::Hair }) {
				if (IsCharacterCategoryEnabled(category, a_args.settings)) {
					result.projectionUncertain = result.projectionUncertain ||
					                             (unboundedCategoryMask_ & CharacterPolicy::CategoryBit(category)) != 0;
				}
			}
			RefreshProjectedActors(a_args);
			const auto& crop = a_args.viewportCrop.output;
			std::vector<CharacterRegionCandidate> candidates;
			candidates.reserve(projectedActors_.size());
			for (const auto& [actorId, admission] : actorAdmissions_) {
				(void)actorId;
				if (admission.frame == a_args.frameId && admission.history.sizeEligible &&
					!admission.admitted && a_args.settings.adaptiveRoiSelection)
					++result.adaptivelyCulledCharacters;
			}
			for (const auto& actor : projectedActors_) {
				const auto admission = actorAdmissions_.find(actor.actorFormId);
				if (admission == actorAdmissions_.end() ||
					admission->second.frame != a_args.frameId || !admission->second.admitted)
					continue;
				const auto& actorRect = actor.selectedRects[a_args.eyeIndex];
				// Offscreen is a per-eye proof, not a reason to expand the other eye.
				if (!actorRect.IsValid())
					continue;
				const auto selectedDistanceMeters = Util::Units::GameUnitsToMeters(
					actor.nearestSelectedDistanceUnits);
				if (!CharacterRegionPolicy::IsWithinMaximumDistance(
						selectedDistanceMeters, a_args.settings.maximumDistanceMeters))
					continue;
				result.projectionUncertain = result.projectionUncertain ||
				                             actor.projectionUncertain[a_args.eyeIndex];
				result.projectionClippedGeometry += actor.clippedGeometry[a_args.eyeIndex];
				if (actor.projectionUncertain[a_args.eyeIndex]) {
					++result.projectionUncertainActors;
					if (!result.projectionFallbackActorFormId) {
						result.projectionFallbackActorFormId = actor.actorFormId;
						result.projectionFallbackReason = actor.fallbackReasons[a_args.eyeIndex];
						result.projectionFallbackCategory = actor.fallbackCategories[a_args.eyeIndex];
					}
				}
				const float marginX = (actorRect.maxX - actorRect.minX) * a_args.settings.roiMargin;
				const float marginY = (actorRect.maxY - actorRect.minY) * a_args.settings.roiMargin;
				const auto minX = static_cast<std::uint32_t>(
					std::max(0.0f, std::floor(actorRect.minX - marginX)));
				const auto minY = static_cast<std::uint32_t>(
					std::max(0.0f, std::floor(actorRect.minY - marginY)));
				const auto maxX = static_cast<std::uint32_t>(std::min(
					static_cast<float>(a_args.viewportCrop.fullOutput.width),
					std::ceil(actorRect.maxX + marginX)));
				const auto maxY = static_cast<std::uint32_t>(std::min(
					static_cast<float>(a_args.viewportCrop.fullOutput.height),
					std::ceil(actorRect.maxY + marginY)));
				if (maxX <= crop.left || maxY <= crop.top || minX >= crop.right || minY >= crop.bottom)
					continue;
				CharacterRect local{
					.minX = std::max(minX, crop.left) - crop.left,
					.minY = std::max(minY, crop.top) - crop.top,
					.maxX = std::min(maxX, crop.right) - crop.left,
					.maxY = std::min(maxY, crop.bottom) - crop.top,
				};
				local.minX = local.minX / kRegionQuantization * kRegionQuantization;
				local.minY = local.minY / kRegionQuantization * kRegionQuantization;
				local.maxX = std::min(a_args.outputWidth,
					(local.maxX + kRegionQuantization - 1u) / kRegionQuantization * kRegionQuantization);
				local.maxY = std::min(a_args.outputHeight,
					(local.maxY + kRegionQuantization - 1u) / kRegionQuantization * kRegionQuantization);
				if (!local.IsValid())
					continue;
				++result.visibleCharacters;
				if (admission->second.hasFaceAnchor)
					++result.visibleFaces;
				candidates.push_back({
					.rect = local,
					.distanceMeters = admission->second.distanceMeters,
					.facePixelSize = admission->second.facePixelSize,
					.stableId = actor.actorFormId,
				});
				// Preserve lifetime ownership before eligibility compaction can join
				// otherwise distant actors. Neither distance order nor geometry IDs
				// identify a temporal inference history.
				auto identity = HashCombine(
					HashCombine(1469598103934665603ull, actor.actorFormId), admission->second.identity);
				result.actorRegions.push_back({ identity ? identity : 1u, local });
			}
			// Only order here: actor admission was already applied to authored
			// categories. Re-testing thresholds now would leak rejected actors
			// through another actor's (or a compacted) eligibility rectangle.
			(void)CharacterRegionPolicy::SelectAdaptive(candidates, false);
			result.selectedCharacters = static_cast<std::uint32_t>(candidates.size());
			std::uint64_t eligibilityXor = 0;
			std::uint64_t eligibilitySum = 0;
			result.regions.reserve(candidates.size());
			for (const auto& candidate : candidates) {
				auto regionHash = HashCombine(1469598103934665603ull, candidate.stableId);
				regionHash = HashCombine(regionHash, candidate.rect.minX);
				regionHash = HashCombine(regionHash, candidate.rect.minY);
				regionHash = HashCombine(regionHash, candidate.rect.maxX);
				regionHash = HashCombine(regionHash, candidate.rect.maxY);
				eligibilityXor ^= regionHash;
				eligibilitySum += regionHash;
				result.regions.push_back(candidate.rect);
			}
			result.eligibilitySignature = HashCombine(
				HashCombine(eligibilityXor, eligibilitySum), candidates.size());
			CharacterRegionPolicy::CompactToCapacity(result.regions, CharacterPolicy::kMaximumEligibilityRegions);
			return result;
		}

		std::uint64_t BuildCoverageSamplingPolicyKey(
			const CharacterMaskPrepareArgs& a_args,
			std::uint32_t a_sourceEyeWidth,
			std::uint32_t a_sourceHeight) const noexcept
		{
			std::uint64_t hash = BuildSettingsKey(a_args.settings);
			auto add = [&](std::uint64_t a_value) {
				hash = HashCombine(hash, a_value);
			};
			add(a_args.eyeIndex);
			add(a_args.featureSlot);
			add(a_args.outputWidth);
			add(a_args.outputHeight);
			add(a_sourceEyeWidth);
			add(a_sourceHeight);
			add(a_args.viewportCrop.fullInput.width);
			add(a_args.viewportCrop.fullInput.height);
			add(a_args.viewportCrop.input.left);
			add(a_args.viewportCrop.input.top);
			add(a_args.viewportCrop.input.right);
			add(a_args.viewportCrop.input.bottom);
			add(a_args.viewportCrop.fullOutput.width);
			add(a_args.viewportCrop.fullOutput.height);
			add(a_args.viewportCrop.output.left);
			add(a_args.viewportCrop.output.top);
			add(a_args.viewportCrop.output.right);
			add(a_args.viewportCrop.output.bottom);
			return hash;
		}

		std::uint64_t BuildDiagnosticKey(
			const CharacterMaskPrepareArgs& a_args,
			const ProjectedPlan& a_plan,
			std::uint32_t a_sourceEyeWidth,
			std::uint32_t a_sourceHeight) const noexcept
		{
			std::uint64_t hash = BuildCoverageSamplingPolicyKey(
				a_args, a_sourceEyeWidth, a_sourceHeight);
			auto add = [&](std::uint64_t a_value) {
				hash = HashCombine(hash, a_value);
			};
			add(a_plan.eligibilitySignature);
			return hash;
		}

		void PollReadbacks(
			ID3D11DeviceContext* a_context,
			std::uint32_t a_frame)
		{
			if (lastReadbackPollFrame_ == a_frame)
				return;
			lastReadbackPollFrame_ = a_frame;
			for (std::uint32_t slotIndex = 0; slotIndex < slots_.size(); ++slotIndex) {
				auto& slot = slots_[slotIndex];
				for (auto& readback : slot.readbacks) {
					if (!readback.pending)
						continue;
					const auto queryResult = a_context->GetData(
						readback.ready.Get(), nullptr, 0,
						D3D11_ASYNC_GETDATA_DONOTFLUSH);
					if (queryResult == S_FALSE)
						continue;
					if (FAILED(queryResult)) {
						readback.pending = false;
						Increment(snapshot_.readbackDrops);
						continue;
					}
					D3D11_MAPPED_SUBRESOURCE mapped{};
					const auto mapResult = a_context->Map(
						readback.staging.Get(), 0, D3D11_MAP_READ,
						D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
					if (mapResult == DXGI_ERROR_WAS_STILL_DRAWING)
						continue;
					if (FAILED(mapResult)) {
						readback.pending = false;
						Increment(snapshot_.readbackDrops);
						continue;
					}
					std::array<std::uint32_t, kDiagnosticCounterCount> counters{};
					std::memcpy(counters.data(), mapped.pData, sizeof(counters));
					a_context->Unmap(readback.staging.Get(), 0);
					const bool sampleIsNewest =
						!slot.maskCoverageReady ||
						readback.serial >= slot.maskCoverageSerial;
					// World captures replace mask contents before delayed samples finish.
					// Keep their original attribution; only exact contents prove current coverage.
					if (readback.featureSlot == slotIndex && readback.contentSerial != 0 &&
						readback.width == slot.width && readback.height == slot.height &&
						sampleIsNewest) {
						slot.maskCoverageFrame = readback.frame;
						slot.maskCoverageFeatureSlot = readback.featureSlot;
						slot.maskCoverageWidth = readback.width;
						slot.maskCoverageHeight = readback.height;
						slot.maskPixels = counters[MaskPixels];
						for (std::size_t category = 0; category < 3; ++category) {
							slot.authoredCategoryPixels[category] =
								counters[AuthoredFacePixels + category];
							slot.visibleCategoryPixels[category] =
								counters[VisibleFacePixels + category];
						}
						slot.visibilityRejectedPixels =
							counters[VisibilityRejectedPixels];
						slot.distanceRejectedPixels =
							counters[DistanceRejectedPixels];
						slot.maskDiagnosticKey = readback.diagnosticKey;
						slot.maskCoverageContentSerial = readback.contentSerial;
						slot.maskCoverageSerial = readback.serial;
						slot.maskCoveragePercent = readback.pixelCount ?
						                               100.0f * static_cast<float>(counters[MaskPixels]) /
						                                   static_cast<float>(readback.pixelCount) :
						                               0.0f;
						slot.maskCoverageReady = true;
					}
					readback.pending = false;
				}
			}
		}

		bool Dispatch(
			const CharacterMaskPrepareArgs& a_args,
			ID3D11ShaderResourceView* a_authoredMaskSource,
			ID3D11ShaderResourceView* a_authoredDepthSource,
			Slot& a_slot,
			const ProjectedPlan& a_plan,
			std::uint32_t a_sourceEyeWidth,
			std::uint32_t a_sourceHeight)
		{
			if (a_slot.contentSerial == 0)
				return false;
			const auto diagnosticKey = BuildDiagnosticKey(
				a_args, a_plan, a_sourceEyeWidth, a_sourceHeight);
			const auto samplingPolicyKey = BuildCoverageSamplingPolicyKey(
				a_args, a_sourceEyeWidth, a_sourceHeight);
			const bool samplePending = std::ranges::any_of(
				a_slot.readbacks,
				[](const Readback& a_readback) {
					return a_readback.pending;
				});
			const auto requestAge = static_cast<std::int32_t>(
				a_args.frameId - a_slot.lastCoverageRequestFrame);
			const bool periodicSampleDue =
				a_slot.coverageRequestIssued && requestAge >= 0 &&
				static_cast<std::uint32_t>(requestAge) >=
					CharacterPolicy::kCoverageSampleIntervalFrames;
			const bool measurementDue =
				a_args.settings.debugView != CharacterDebugView::Off && !samplePending &&
				(!a_slot.coverageRequestIssued ||
					a_slot.lastCoverageRequestPolicyKey != samplingPolicyKey ||
					periodicSampleDue);
			Readback* coverageReadback = nullptr;
			if (measurementDue) {
				for (std::uint32_t offset = 0;
					offset < a_slot.readbacks.size(); ++offset) {
					const auto index =
						(a_slot.nextReadbackIndex + offset) %
						static_cast<std::uint32_t>(a_slot.readbacks.size());
					if (!a_slot.readbacks[index].pending) {
						coverageReadback = &a_slot.readbacks[index];
						a_slot.nextReadbackIndex =
							(index + 1u) % static_cast<std::uint32_t>(
											   a_slot.readbacks.size());
						break;
					}
				}
				if (!coverageReadback)
					Increment(snapshot_.readbackDrops);
			}
			MaskConstants constants{};
			constants.outputAndSourceSize[0] = a_args.outputWidth;
			constants.outputAndSourceSize[1] = a_args.outputHeight;
			constants.outputAndSourceSize[2] = a_sourceEyeWidth;
			constants.outputAndSourceSize[3] = a_sourceHeight;
			constants.sourceCrop[0] = a_args.eyeIndex * a_sourceEyeWidth;
			constants.sourceCrop[1] = a_args.viewportCrop.input.left;
			constants.sourceCrop[2] = a_args.viewportCrop.input.top;
			constants.sourceCrop[3] = a_args.viewportCrop.input.Width();
			constants.options[0] = a_args.viewportCrop.input.Height();
			constants.options[1] = static_cast<std::uint32_t>(
				a_plan.regions.size());
			constants.options[2] = a_args.settings.depthAwareFeather ?
			                           a_args.settings.featherRadius :
			                           0;
			constants.options[3] = a_args.settings.depthAwareFeather ? 1u : 0u;
			constants.featherOptions[0] = a_args.settings.featherDepthThreshold;
			constants.featherOptions[1] = static_cast<float>(
				a_args.settings.maskTestMode);
			const bool measureCoverage = coverageReadback != nullptr;
			const bool fullSurfaceDispatch =
				measureCoverage ||
				a_args.settings.debugView != CharacterDebugView::Off;
			constants.featherOptions[2] = measureCoverage ? 1.0f : 0.0f;
			constants.visibilityOptions[0] = kVisibilityDepthThreshold;
			constants.visibilityOptions[1] =
				a_args.settings.visibilityDepthTest &&
						a_args.settings.maskTestMode !=
							CharacterMaskTestMode::AuthoredWithoutVisibilityDepth ?
					1.0f :
					0.0f;
			constants.visibilityOptions[2] =
				a_args.settings.maximumDistanceMeters > 0.0f ?
					a_args.settings.maximumDistanceMeters /
						Util::Units::GAME_UNIT_TO_M :
					0.0f;
			constants.visibilityOptions[3] =
				CharacterRegionPolicy::ResolveDistanceFadeWidth(
					a_args.settings.maximumDistanceMeters) /
				Util::Units::GAME_UNIT_TO_M;
			const auto cameraData = Util::GetCameraData();
			constants.depthLinearization[0] = cameraData.x;
			constants.depthLinearization[1] = cameraData.y;
			constants.depthLinearization[2] = cameraData.z;
			constants.depthLinearization[3] = cameraData.w;
			constants.cameraProjInverse =
				globals::game::frameBufferCached.GetCameraProjInverse(
					a_args.eyeIndex);
			constants.jitter[0] = capturedJitterX_;
			constants.jitter[1] = capturedJitterY_;
			const auto& capturedRegion = capturedSourceRects_[a_args.eyeIndex];
			constants.authoredRegion[0] = capturedRegion.baseX;
			constants.authoredRegion[1] = capturedRegion.baseY;
			constants.authoredRegion[2] = capturedRegion.width;
			constants.authoredRegion[3] = capturedRegion.height;
			constants.categoryStrengths[0] =
				a_args.settings.faces ? a_args.settings.faceStrength : 0.0f;
			constants.categoryStrengths[1] =
				a_args.settings.skin ? a_args.settings.skinStrength : 0.0f;
			constants.categoryStrengths[2] =
				a_args.settings.hair ? a_args.settings.hairStrength : 0.0f;
			if (a_plan.regions.empty() ||
				a_plan.regions.size() > CharacterPolicy::kMaximumEligibilityRegions) {
				return false;
			}
			for (std::size_t index = 0; index < a_plan.regions.size(); ++index) {
				const auto& rect = a_plan.regions[index];
				constants.eligibilityRectangles[index][0] =
					static_cast<float>(rect.minX);
				constants.eligibilityRectangles[index][1] =
					static_cast<float>(rect.minY);
				constants.eligibilityRectangles[index][2] =
					static_cast<float>(rect.maxX);
				constants.eligibilityRectangles[index][3] =
					static_cast<float>(rect.maxY);
			}
			const auto dirtyRegion = UnionCharacterWorkRects(
				a_slot.previousMaskWorkSubrect, a_slot.maskWorkSubrect);
			if (!dirtyRegion.Fits(a_args.outputWidth, a_args.outputHeight))
				return false;
			const auto dispatchOffsetX =
				fullSurfaceDispatch ? 0u : dirtyRegion.baseX;
			const auto dispatchOffsetY =
				fullSurfaceDispatch ? 0u : dirtyRegion.baseY;
			const auto dispatchWidth = fullSurfaceDispatch ?
			                               std::max(a_args.outputWidth, a_args.viewportCrop.input.Width()) :
			                               dirtyRegion.width;
			const auto dispatchHeight = fullSurfaceDispatch ?
			                                std::max(a_args.outputHeight, a_args.viewportCrop.input.Height()) :
			                                dirtyRegion.height;
			constants.dispatchRegion[0] = dispatchOffsetX;
			constants.dispatchRegion[1] = dispatchOffsetY;
			constants.dispatchRegion[2] = dispatchWidth;
			constants.dispatchRegion[3] = dispatchHeight;
			a_args.context->UpdateSubresource(
				constants_.Get(), 0, nullptr, &constants, 0, 0);

			ComputeStateGuard stateGuard(a_args.context);
			if (!stateGuard.Captured())
				return false;
			if (!a_slot.maskInitialized) {
				const std::array<float, 4> clearMask{};
				a_args.context->ClearUnorderedAccessViewFloat(
					a_slot.maskUav.Get(), clearMask.data());
				a_slot.maskInitialized = true;
			}
			if (measureCoverage) {
				const std::array<UINT, 4> clear{};
				a_args.context->ClearUnorderedAccessViewUint(
					a_slot.coverageCounterUav.Get(), clear.data());
			}
			ID3D11Buffer* constantBuffer = constants_.Get();
			std::array<ID3D11ShaderResourceView*, 3> srvs{
				a_authoredMaskSource,
				a_authoredDepthSource,
				a_args.depthGuide,
			};
			std::array<ID3D11UnorderedAccessView*, 2> uavs{
				a_slot.maskUav.Get(),
				a_slot.coverageCounterUav.Get(),
			};
			a_args.context->CSSetShader(shader_.Get(), nullptr, 0);
			a_args.context->CSSetConstantBuffers(0, 1, &constantBuffer);
			a_args.context->CSSetShaderResources(
				0, static_cast<UINT>(srvs.size()), srvs.data());
			a_args.context->CSSetUnorderedAccessViews(
				0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
#ifndef NDEBUG
			std::array<ID3D11ShaderResourceView*, 3> boundSrvs{};
			std::array<ID3D11UnorderedAccessView*, 2> boundUavs{};
			a_args.context->CSGetShaderResources(
				0, static_cast<UINT>(boundSrvs.size()), boundSrvs.data());
			a_args.context->CSGetUnorderedAccessViews(
				0, static_cast<UINT>(boundUavs.size()), boundUavs.data());
			const bool bindingsValid =
				boundSrvs[0] == srvs[0] && boundSrvs[1] == srvs[1] &&
				boundSrvs[2] == srvs[2] &&
				boundUavs[0] == uavs[0] && boundUavs[1] == uavs[1];
			for (auto* view : boundSrvs) {
				if (view)
					view->Release();
			}
			for (auto* view : boundUavs) {
				if (view)
					view->Release();
			}
			if (!bindingsValid)
				return false;
#endif
			{
				CS_PROFILE_SCOPE("Upscaling::DLSS5CharacterMask");
				a_args.context->Dispatch(
					(dispatchWidth + 7u) / 8u,
					(dispatchHeight + 7u) / 8u,
					1);
			}
			a_slot.maskUniform = false;
			a_slot.previousMaskWorkSubrect = a_slot.maskWorkSubrect;

			std::array<ID3D11UnorderedAccessView*, 2> nullUavs{};
			a_args.context->CSSetUnorderedAccessViews(
				0, static_cast<UINT>(nullUavs.size()), nullUavs.data(), nullptr);
			if (measureCoverage) {
				a_args.context->CopyResource(
					coverageReadback->staging.Get(), a_slot.coverageCounter.Get());
				a_args.context->End(coverageReadback->ready.Get());
				coverageReadback->frame = a_args.frameId;
				coverageReadback->eyeIndex = a_args.eyeIndex;
				coverageReadback->featureSlot = a_args.featureSlot;
				coverageReadback->width = a_args.outputWidth;
				coverageReadback->height = a_args.outputHeight;
				coverageReadback->pixelCount =
					static_cast<std::uint64_t>(a_args.outputWidth) *
					a_args.outputHeight;
				coverageReadback->diagnosticKey = diagnosticKey;
				coverageReadback->contentSerial = a_slot.contentSerial;
				coverageReadback->serial = AllocateCoverageSerial();
				coverageReadback->pending = true;
				a_slot.lastCoverageRequestPolicyKey = samplingPolicyKey;
				a_slot.lastCoverageRequestFrame = a_args.frameId;
				a_slot.coverageRequestIssued = true;
			}
			return true;
		}

		mutable std::mutex mutex_;
		CharacterCategoryFramePolicy categoryFramePolicy_{};
		mutable std::mutex rejectionFrameMutex_;
		std::atomic<std::uint32_t> rejectionFrame_{
			std::numeric_limits<std::uint32_t>::max()
		};
		std::array<
			std::atomic<std::uint32_t>,
			static_cast<std::size_t>(CharacterClassificationRejection::Count)>
			currentClassificationRejections_{};
		std::array<
			std::atomic<std::uint64_t>,
			static_cast<std::size_t>(CharacterClassificationRejection::Count)>
			classificationRejections_{};
		std::vector<Observation> observations_;
		std::unordered_set<std::uintptr_t> observationKeys_;
		std::uint32_t unboundedCategoryMask_ = 0;
		std::uint32_t observationFrame_ = std::numeric_limits<std::uint32_t>::max();
		ProjectionKey projectionKey_{};
		std::vector<ProjectedActor> projectedActors_;
		bool projectionCacheValid_ = false;
		std::unordered_map<std::uint32_t, ActorAdmission> actorAdmissions_;
		std::array<Slot, 4> slots_{};
		std::array<std::uint32_t, 2> lastSlotForEye_{ 4, 4 };
		std::uint32_t preparedFrameHistoryNext_ = 0;
		ComPtr<ID3D11ComputeShader> shader_;
		ComPtr<ID3D11Buffer> constants_;
		ComPtr<ID3D11ComputeShader> maskBoundsShader_;
		ComPtr<ID3D11Buffer> maskBoundsConstants_;
		ComPtr<ID3D11Fence> maskBoundsFence_;
		std::uint64_t maskBoundsFenceValue_ = 0;
		bool maskBoundsFenceUnsupported_ = false;
		bool maskBoundsShaderFailed_ = false;
		HRESULT maskBoundsResourceResult_ = S_OK;
		ComPtr<ID3D11Texture2D> capturedCategories_;
		ComPtr<ID3D11ShaderResourceView> capturedCategoriesSrv_;
		ComPtr<ID3D11UnorderedAccessView> capturedCategoriesUav_;
		ComPtr<ID3D11Texture2D> capturedDepth_;
		ComPtr<ID3D11ShaderResourceView> capturedDepthSrv_;
		ComPtr<ID3D11UnorderedAccessView> capturedDepthUav_;
		ComPtr<ID3D11ComputeShader> captureShader_;
		ComPtr<ID3D11Buffer> captureConstants_;
		ComPtr<ID3D11ShaderResourceView> captureSourceCategoriesSrv_;
		bool captureShaderCompileFailed_ = false;
		std::array<ComputeSubrect, 2> capturedSourceRects_{};
		std::uint32_t capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
		bool capturedCategoriesEmpty_ = false;
		std::uint32_t capturedEyeWidth_ = 0;
		std::uint32_t capturedHeight_ = 0;
		std::uint32_t capturedEnabledCategoryMask_ = 0;
		float capturedJitterX_ = 0.0f;
		float capturedJitterY_ = 0.0f;
		std::uint32_t lastReadbackPollFrame_ =
			std::numeric_limits<std::uint32_t>::max();
		std::uint64_t nextCoverageRequestSerial_ = 1;
		std::uint64_t nextPreparedContentSerial_ = 1;
		ComPtr<ID3D11Device> device_;
		bool shaderCompileFailed_ = false;
		void RecordPreparationFailure(const CharacterMaskPrepareArgs& args, std::string detail)
		{
			Increment(snapshot_.preparationFailures);
			auto& failure = snapshot_.lastPreparationFailure;
			const bool changed = failure.sequence == 0 || failure.detail != detail;
			failure = { std::move(detail), snapshot_.preparationFailures, args.generation,
				args.frameId, args.sourceWorldFrame, capturedFrame_, args.featureSlot, args.eyeIndex,
				GetEnabledCharacterCategoryMask(args.settings), capturedEnabledCategoryMask_ };
			if (changed)
				logger::warn("[DLSSNR][Character] Mask preparation failed frame={} sourceFrame={} capturedFrame={} slot={} generation={} requestedCategories=0x{:X} capturedCategories=0x{:X}: {}",
					failure.frame, failure.sourceWorldFrame, failure.capturedFrame, failure.featureSlot,
					failure.generation, failure.requestedCategories, failure.capturedCategories, failure.detail);
		}

		CharacterSnapshot snapshot_{};
	};

	CharacterRendering& CharacterRendering::Instance()
	{
		static CharacterRendering instance;
		return instance;
	}

	CharacterRendering::CharacterRendering() : state_(std::make_unique<State>()) {}

	CharacterRendering::~CharacterRendering() = default;

	CharacterSettings CharacterRendering::ResolveCategorySettings(
		std::uint32_t a_sourceFrame, const CharacterSettings& a_requested) noexcept
	{
		if (!state_)
			return a_requested;
		try {
			std::scoped_lock lock(state_->mutex_);
			return state_->categoryFramePolicy_.Resolve(a_sourceFrame, a_requested);
		} catch (...) {
			auto disabled = a_requested;
			disabled.enabled = false;
			return disabled;
		}
	}

	bool CharacterRendering::ShouldAuthorActor(
		std::uint32_t a_frame,
		std::uint32_t a_actorFormId,
		const CharacterActorAdmissionArgs& a_args) noexcept
	{
		if (!state_ || !a_actorFormId || !a_args.actorIdentity || !IsValidCharacterSettings(a_args.settings))
			return false;
		try {
			std::scoped_lock lock(state_->mutex_);
			if (!state_->BeginObservationFrame(a_frame))
				return false;
			if (!state_->actorAdmissions_.contains(a_actorFormId) &&
				state_->actorAdmissions_.size() >= CharacterPolicy::kMaximumObservationsPerFrame) {
				// Fail closed without growing an unbounded draw-time identity cache.
				Increment(state_->snapshot_.observationCapacityDrops);
				return false;
			}
			auto& admission = state_->actorAdmissions_[a_actorFormId];
			if (admission.valid && admission.frame == a_frame && admission.identity == a_args.actorIdentity)
				return admission.admitted;
			const auto policyKey = BuildSettingsKey(a_args.settings);
			if (!admission.valid || admission.identity != a_args.actorIdentity ||
				admission.policyKey != policyKey || admission.width != a_args.outputWidthPerEye ||
				admission.height != a_args.outputHeight ||
				static_cast<std::uint32_t>(a_frame - admission.frame) > a_args.settings.roiHoldFrames + 1u) {
				admission = {};
			}
			admission.valid = true;
			admission.identity = a_args.actorIdentity;
			admission.frame = a_frame;
			admission.policyKey = policyKey;
			admission.width = a_args.outputWidthPerEye;
			admission.height = a_args.outputHeight;
			const auto validBound = [](const CharacterActorBound& a_bound) {
				return std::isfinite(a_bound.centerX) && std::isfinite(a_bound.centerY) &&
				       std::isfinite(a_bound.centerZ) && std::isfinite(a_bound.radius) && a_bound.radius > 0.0f;
			};
			const auto projectBound = [&](const CharacterActorBound& a_bound, bool& a_uncertain) {
				if (!validBound(a_bound) || !a_args.outputWidthPerEye || !a_args.outputHeight) {
					a_uncertain = true;
					return 0u;
				}
				const State::Observation sphere{
					.center = { a_bound.centerX, a_bound.centerY, a_bound.centerZ },
					.radius = a_bound.radius,
				};
				std::uint32_t pixelSize = 0;
				const auto eyeCount = globals::game::isVR ? 2u : 1u;
				for (std::uint32_t eye = 0; eye < eyeCount; ++eye) {
					CharacterRect projected{};
					const auto projection = state_->ProjectSphere(sphere, GetProjectionEyePosition(eye),
						globals::game::frameBufferCached.GetCameraViewProjUnjittered(eye).Transpose(),
						globals::game::frameBufferCached.GetCameraViewProj(eye).Transpose(),
						a_args.outputWidthPerEye, a_args.outputHeight, projected);
					a_uncertain = a_uncertain || projection == CharacterProjectionResult::Uncertain;
					if (projected.IsValid()) {
						pixelSize = std::max(pixelSize, std::max(
															projected.maxX - projected.minX, projected.maxY - projected.minY));
					}
				}
				return pixelSize;
			};
			bool uncertain = false;
			const CharacterActorBound* detailBound = &a_args.faceBound;
			admission.facePixelSize = validBound(*detailBound) ? projectBound(*detailBound, uncertain) : 0u;
			admission.hasFaceAnchor = admission.facePixelSize > 0u && validBound(a_args.faceBound);
			if (!admission.hasFaceAnchor && !uncertain) {
				// A torso/hair can remain visible with its face outside both eyes.
				// The actor bound is deliberately conservative in that case.
				detailBound = &a_args.actorBound;
				admission.facePixelSize = projectBound(*detailBound, uncertain);
			}
			admission.distanceMeters = 0.0f;
			if (validBound(*detailBound)) {
				const auto eye = GetProjectionAverageEyePosition();
				const float dx = detailBound->centerX - eye.x;
				const float dy = detailBound->centerY - eye.y;
				const float dz = detailBound->centerZ - eye.z;
				admission.distanceMeters = Util::Units::GameUnitsToMeters(std::max(
					0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - detailBound->radius));
				if (!std::isfinite(admission.distanceMeters)) {
					admission.distanceMeters = 0.0f;
					uncertain = true;
				}
			}
			admission.admitted = ResolveCharacterActorAdmission(a_frame, admission.facePixelSize,
				admission.distanceMeters, uncertain, a_args.settings.minimumFacePixelSize,
				a_args.settings.roiHoldFrames, a_args.settings.adaptiveRoiSelection, admission.history);
			return admission.admitted;
		} catch (...) {
			return false;
		}
	}

	bool CharacterRendering::ObserveGeometry(
		std::uint32_t a_frame,
		std::uint32_t a_actorFormId,
		std::uintptr_t a_geometryIdentity,
		CharacterCategory a_category,
		float a_centerX,
		float a_centerY,
		float a_centerZ,
		float a_radius) noexcept
	{
		if (!state_ || !a_actorFormId || !a_geometryIdentity ||
			a_category == CharacterCategory::None ||
			!std::isfinite(a_centerX) || !std::isfinite(a_centerY) ||
			!std::isfinite(a_centerZ) || !std::isfinite(a_radius) || a_radius <= 0.0f) {
			return false;
		}
		try {
			std::scoped_lock lock(state_->mutex_);
			if (!state_->BeginObservationFrame(a_frame))
				return false;
			const auto admission = state_->actorAdmissions_.find(a_actorFormId);
			if (admission == state_->actorAdmissions_.end() ||
				admission->second.frame != a_frame || !admission->second.admitted)
				return false;
			if (state_->observationKeys_.contains(a_geometryIdentity))
				return true;
			if (state_->observations_.size() >=
				CharacterPolicy::kMaximumObservationsPerFrame) {
				Increment(state_->snapshot_.observationCapacityDrops);
				state_->unboundedCategoryMask_ |=
					CharacterPolicy::CategoryBit(a_category);
				// Preserve the exact authored semantic ID. The missing projection is
				// represented as uncertainty and forces a conservative full-eye ROI.
				return true;
			}
			state_->observationKeys_.insert(a_geometryIdentity);
			state_->observations_.push_back({
				.actorFormId = a_actorFormId,
				.geometryIdentity = a_geometryIdentity,
				.category = a_category,
				.center = { a_centerX, a_centerY, a_centerZ },
				.radius = a_radius,
			});
			state_->InvalidateProjectionCache();
			Increment(state_->snapshot_.observations);
			Increment(state_->snapshot_.currentObservations);
			const auto categoryIndex = static_cast<std::size_t>(a_category) - 1u;
			if (categoryIndex <
				state_->snapshot_.currentCategoryObservations.size()) {
				Increment(state_->snapshot_.currentCategoryObservations[categoryIndex]);
			}
			return true;
		} catch (...) {
			// Render-hook observation must not affect the engine draw.
			return false;
		}
	}

	void CharacterRendering::ObserveClassificationRejection(
		std::uint32_t a_frame,
		CharacterClassificationRejection a_reason) noexcept
	{
		if (!state_ || a_reason >= CharacterClassificationRejection::Count)
			return;
		try {
			const auto index = static_cast<std::size_t>(a_reason);
			state_->RecordClassificationRejection(a_frame, index);
		} catch (...) {
			// Classification diagnostics must not affect the engine draw.
		}
	}

	bool CharacterRendering::CaptureAuthoredCategories(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_categorySource,
		ID3D11ShaderResourceView* a_depthSource,
		std::uint32_t a_sourceEyeWidth,
		std::uint32_t a_sourceHeight,
		std::uint32_t a_frame,
		std::uint32_t a_enabledCategoryMask,
		float a_jitterX,
		float a_jitterY) noexcept
	{
		if (!state_)
			return false;
		try {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.categoryCaptureAttempts);
			const auto fail = [&](std::string a_detail) {
				Increment(state_->snapshot_.categoryCaptureFailures);
				state_->InvalidateCaptureMetadata();
				state_->InvalidatePreparedMasks();
				state_->snapshot_.status = "failed";
				state_->snapshot_.detail = std::move(a_detail);
				return false;
			};
			if (!a_device || !a_context || !a_categorySource || !a_depthSource ||
				!a_sourceEyeWidth || !a_sourceHeight ||
				a_frame == std::numeric_limits<std::uint32_t>::max() ||
				(a_enabledCategoryMask & ~0xEu) != 0 ||
				!std::isfinite(a_jitterX) || !std::isfinite(a_jitterY) ||
				a_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
				return fail("character category capture arguments are invalid");
			}
			ComPtr<ID3D11Device> contextDevice;
			a_context->GetDevice(&contextDevice);
			ComPtr<ID3D11Device> categoryDevice;
			a_categorySource->GetDevice(&categoryDevice);
			ComPtr<ID3D11Resource> depthResource;
			a_depthSource->GetResource(&depthResource);
			ComPtr<ID3D11Texture2D> depthTexture;
			ComPtr<ID3D11Device> depthDevice;
			if (!depthResource || FAILED(depthResource.As(&depthTexture)) ||
				!depthTexture) {
				return fail("character depth capture source is not a Texture2D");
			}
			depthTexture->GetDevice(&depthDevice);
			if (!SameIdentity(a_device, contextDevice.Get()) ||
				!SameIdentity(a_device, categoryDevice.Get()) ||
				!SameIdentity(a_device, depthDevice.Get())) {
				return fail("character category capture resources use different devices");
			}

			state_->AdoptDevice(a_device);
			// Expire frame contents while retaining independent region history.
			state_->InvalidatePreparedMasks(true);
			if (!state_->BeginObservationFrame(a_frame))
				return fail("character category capture arrived after a newer observation frame");
			const bool hasSelectedObservation =
				(state_->unboundedCategoryMask_ & a_enabledCategoryMask) != 0 ||
				std::ranges::any_of(
					state_->observations_,
					[&](const State::Observation& a_observation) {
						return (a_enabledCategoryMask &
								   CharacterPolicy::CategoryBit(a_observation.category)) != 0;
					});
			if (!hasSelectedObservation) {
				// A logical empty capture lets the stereo path bypass Feature 18
				// without copying active-stereo G-buffer data every empty frame.
				state_->capturedFrame_ = a_frame;
				state_->capturedCategoriesEmpty_ = true;
				state_->capturedSourceRects_ = {};
				state_->capturedEyeWidth_ = a_sourceEyeWidth;
				state_->capturedHeight_ = a_sourceHeight;
				state_->capturedEnabledCategoryMask_ = a_enabledCategoryMask;
				state_->capturedJitterX_ = a_jitterX;
				state_->capturedJitterY_ = a_jitterY;
				state_->snapshot_.categoryCaptureFrame = a_frame;
				state_->snapshot_.categoryCaptureReady = true;
				state_->snapshot_.categoryCaptureEmpty = true;
				state_->snapshot_.status = "ready_empty";
				state_->snapshot_.detail =
					"no enabled NPC character materials were observed; semantic snapshot bypassed";
				Increment(state_->snapshot_.categoryCaptureSuccesses);
				Increment(state_->snapshot_.categoryCaptureEmptyBypasses);
				return true;
			}

			D3D11_TEXTURE2D_DESC categoryDesc{};
			a_categorySource->GetDesc(&categoryDesc);
			D3D11_TEXTURE2D_DESC depthDesc{};
			depthTexture->GetDesc(&depthDesc);
			D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDesc{};
			a_depthSource->GetDesc(&depthViewDesc);
			const auto activeStereoWidth =
				static_cast<std::uint64_t>(a_sourceEyeWidth) * 2u;
			if (categoryDesc.Format != kCharacterCategoryFormat ||
				categoryDesc.MipLevels != 1 || categoryDesc.ArraySize != 1 ||
				categoryDesc.SampleDesc.Count != 1 ||
				categoryDesc.Usage != D3D11_USAGE_DEFAULT ||
				(categoryDesc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 ||
				(categoryDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
				return fail("character category source has an invalid layout");
			}
			if (depthDesc.Width != categoryDesc.Width ||
				depthDesc.Height != categoryDesc.Height ||
				depthDesc.MipLevels != 1 || depthDesc.ArraySize != 1 ||
				depthDesc.SampleDesc.Count != 1 ||
				depthDesc.Usage != D3D11_USAGE_DEFAULT ||
				depthViewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				depthViewDesc.Texture2D.MostDetailedMip != 0 ||
				depthViewDesc.Texture2D.MipLevels != 1 ||
				!IsSupportedDepthViewFormat(depthViewDesc.Format)) {
				return fail("character depth capture source has an invalid layout");
			}
			if (activeStereoWidth > categoryDesc.Width ||
				a_sourceHeight > categoryDesc.Height) {
				return fail(std::format(
					"character capture extent {}x{} exceeds source {}x{}",
					activeStereoWidth, a_sourceHeight,
					categoryDesc.Width, categoryDesc.Height));
			}
			D3D11_TEXTURE2D_DESC categoryCaptureDesc = categoryDesc;
			categoryCaptureDesc.Width = static_cast<UINT>(activeStereoWidth);
			categoryCaptureDesc.Height = a_sourceHeight;
			D3D11_TEXTURE2D_DESC depthCaptureDesc = depthDesc;
			depthCaptureDesc.Width = static_cast<UINT>(activeStereoWidth);
			depthCaptureDesc.Height = a_sourceHeight;
			if (!state_->EnsureCategoryCapture(a_device, categoryCaptureDesc)) {
				return fail("character category capture texture could not be created");
			}
			if (!state_->EnsureDepthCapture(a_device, depthCaptureDesc)) {
				return fail("character depth capture texture could not be created");
			}
			if (!state_->EnsureCaptureShader(a_device, a_categorySource))
				return fail("character snapshot shader/resources could not be created");

			// Capture only the current projected character area, not the much larger
			// temporally retained provider ROI. Unknown projections still capture
			// the full affected eye. Every shader read checks this current-frame
			// validity rectangle; untouched texels are never stale-mask evidence.
			std::array<ComputeSubrect, 2> sourceRects{};
			for (std::uint32_t eye = 0; eye < sourceRects.size(); ++eye) {
				const auto eyePosition = GetProjectionEyePosition(eye);
				const auto eyeMatrix = globals::game::frameBufferCached
				                           .GetCameraViewProjUnjittered(eye)
				                           .Transpose();
				const auto rasterMatrix = globals::game::frameBufferCached.GetCameraViewProj(eye).Transpose();
				if ((state_->unboundedCategoryMask_ & a_enabledCategoryMask) != 0) {
					sourceRects[eye] = BuildFullComputeSubrect(a_sourceEyeWidth, a_sourceHeight);
					continue;
				}
				for (const auto& observation : state_->observations_) {
					if ((a_enabledCategoryMask & CharacterPolicy::CategoryBit(observation.category)) == 0)
						continue;
					CharacterRect rect{};
					const auto projection = state_->ProjectSphere(observation, eyePosition,
						eyeMatrix, rasterMatrix, a_sourceEyeWidth, a_sourceHeight, rect);
					if (projection == CharacterProjectionResult::Offscreen)
						continue;
					if (!rect.IsValid()) {
						sourceRects[eye] = BuildFullComputeSubrect(a_sourceEyeWidth, a_sourceHeight);
						break;
					}
					sourceRects[eye] = UnionCharacterWorkRects(sourceRects[eye],
						{ rect.minX, rect.minY, rect.maxX - rect.minX, rect.maxY - rect.minY });
				}
				// Cover render jitter, four-input-pixel feather and coverage taps.
				const auto guard = static_cast<std::uint32_t>(std::min<float>(
					static_cast<float>(std::max(a_sourceEyeWidth, a_sourceHeight)),
					std::ceil(std::max(std::abs(a_jitterX), std::abs(a_jitterY))) + 6.0f));
				sourceRects[eye] = ExpandCharacterWorkRect(sourceRects[eye],
					a_sourceEyeWidth, a_sourceHeight, guard);
			}
			OutputMergerStateGuard outputMerger(a_context);
			if (!outputMerger.Captured())
				return fail("character category capture could not preserve output state");
			{
				ComputeStateGuard computeState(a_context);
				if (!computeState.Captured())
					return fail("character category capture could not preserve compute state");
				CS_PROFILE_SCOPE("Upscaling::DLSS5CharacterCategoryCapture");
				std::array<ID3D11ShaderResourceView*, 2> srvs{
					state_->captureSourceCategoriesSrv_.Get(), a_depthSource
				};
				std::array<ID3D11UnorderedAccessView*, 2> uavs{
					state_->capturedCategoriesUav_.Get(), state_->capturedDepthUav_.Get()
				};
				ID3D11Buffer* captureCB = state_->captureConstants_.Get();
				a_context->CSSetShader(state_->captureShader_.Get(), nullptr, 0);
				a_context->CSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
				a_context->CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
				a_context->CSSetConstantBuffers(0, 1, &captureCB);
				for (std::uint32_t eye = 0; eye < sourceRects.size(); ++eye) {
					const auto& rect = sourceRects[eye];
					if (!rect.IsValid())
						continue;
					const std::array<std::uint32_t, 4> constants{
						eye * a_sourceEyeWidth + rect.baseX, rect.baseY, rect.width, rect.height
					};
					a_context->UpdateSubresource(captureCB, 0, nullptr, constants.data(), 0, 0);
					a_context->Dispatch((rect.width + 7u) / 8u, (rect.height + 7u) / 8u, 1);
				}
			}
			state_->capturedSourceRects_ = sourceRects;
			state_->capturedFrame_ = a_frame;
			state_->capturedCategoriesEmpty_ = false;
			state_->capturedEyeWidth_ = a_sourceEyeWidth;
			state_->capturedHeight_ = a_sourceHeight;
			state_->capturedEnabledCategoryMask_ = a_enabledCategoryMask;
			state_->capturedJitterX_ = a_jitterX;
			state_->capturedJitterY_ = a_jitterY;
			state_->snapshot_.categoryCaptureFrame = a_frame;
			state_->snapshot_.categoryCaptureReady = true;
			state_->snapshot_.categoryCaptureEmpty = false;
			Increment(state_->snapshot_.categoryCaptureSuccesses);
			state_->snapshot_.status = "captured";
			state_->snapshot_.detail =
				"same-frame post-terrain active-stereo categories, synchronized pre-decal depth, and render jitter captured";
			return true;
		} catch (const std::exception& exception) {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.categoryCaptureFailures);
			state_->InvalidateCaptureMetadata();
			state_->InvalidatePreparedMasks();
			state_->snapshot_.status = "failed";
			state_->snapshot_.detail = exception.what();
			return false;
		} catch (...) {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.categoryCaptureFailures);
			state_->InvalidateCaptureMetadata();
			state_->InvalidatePreparedMasks();
			state_->snapshot_.status = "failed";
			state_->snapshot_.detail = "unknown character category capture exception";
			return false;
		}
	}

	bool CharacterRendering::PrepareMask(
		const CharacterMaskPrepareArgs& a_args,
		CharacterMaskPrepareResult& a_result) noexcept
	{
		a_result = {};
		if (!state_)
			return false;
		const auto invalidFrame =
			std::numeric_limits<std::uint32_t>::max();
		const bool retainedSource =
			a_args.frameId != invalidFrame &&
			a_args.sourceWorldFrame != invalidFrame &&
			a_args.sourceWorldFrame < a_args.frameId;
		try {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.preparationAttempts);
			state_->snapshot_.enabled = a_args.settings.enabled;
			const std::uint32_t sourceWorldFrame = a_args.sourceWorldFrame;
			const auto fail = [&](std::string a_detail) {
				state_->RecordPreparationFailure(a_args, a_detail);
				// A retained request is only a lookup against the immutable source
				// mask.  Its failure must not destroy that source mask, because a
				// later compositor cycle may still satisfy the exact contract.
				if (!retainedSource) {
					state_->InvalidatePreparedSlot(
						a_args.featureSlot, a_args.eyeIndex, a_args.frameId);
				}
				state_->snapshot_.status =
					retainedSource ? "retained-source-miss" : "failed";
				state_->snapshot_.detail = std::move(a_detail);
				return false;
			};

			if (!a_args.settings.enabled)
				return fail("character mask preparation was requested while disabled");
			if (!a_args.device || !a_args.context || a_args.eyeIndex >= 2 ||
				a_args.featureSlot >= state_->slots_.size() ||
				(a_args.featureSlot & 1u) != a_args.eyeIndex ||
				a_args.frameId == invalidFrame || sourceWorldFrame == invalidFrame ||
				sourceWorldFrame > a_args.frameId ||
				a_args.generation == 0 ||
				!a_args.outputWidth || !a_args.outputHeight ||
				!a_args.viewportCrop.MatchesEvaluationExtents(
					a_args.viewportCrop.input.Width(),
					a_args.viewportCrop.input.Height(),
					a_args.outputWidth,
					a_args.outputHeight) ||
				!IsValidCharacterSettings(a_args.settings)) {
				return fail("character mask preparation arguments are invalid");
			}
			if (a_args.context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return fail("character masks require the immediate D3D11 context");
			ComPtr<ID3D11Device> contextDevice;
			a_args.context->GetDevice(&contextDevice);
			if (!SameIdentity(a_args.device, contextDevice.Get()))
				return fail("character mask context belongs to another D3D11 device");

			state_->AdoptDevice(a_args.device);
			state_->PollReadbacks(a_args.context, a_args.frameId);
			const bool needsAuthoredCategories = UsesAuthoredMask(a_args.settings.maskTestMode) ||
			                                     a_args.settings.maskTestMode == CharacterMaskTestMode::InvertAuthored;
			if (needsAuthoredCategories &&
				(GetEnabledCharacterCategoryMask(a_args.settings) & ~state_->capturedEnabledCategoryMask_) != 0) {
				return fail("character category selection expanded beyond the captured source policy");
			}
			const bool logicalEmptyCapture = state_->capturedCategoriesEmpty_;
			if (state_->capturedFrame_ != sourceWorldFrame ||
				state_->capturedEyeWidth_ != a_args.viewportCrop.fullInput.width ||
				state_->capturedHeight_ != a_args.viewportCrop.fullInput.height ||
				(!logicalEmptyCapture &&
					(!state_->capturedCategoriesSrv_ ||
						!state_->capturedDepthSrv_))) {
				return fail("no correlated character capture with matching logical stereo dimensions is available");
			}

			ComPtr<ID3D11Texture2D> sourceTexture;
			D3D11_TEXTURE2D_DESC sourceDesc{};
			void* sourceIdentity = nullptr;
			std::uint32_t sourceEyeWidth = 0;
			std::uintptr_t currentDepthIdentity = 0;
			std::uintptr_t authoredDepthIdentity = 0;
			if (!logicalEmptyCapture) {
				std::string sourceError;
				if (!state_->ValidateTexture(
						state_->capturedCategoriesSrv_.Get(),
						a_args.device,
						kCharacterCategoryFormat,
						sourceTexture,
						sourceDesc,
						sourceIdentity,
						sourceError)) {
					return fail(std::move(sourceError));
				}
				// The capture owns an exact active-stereo allocation, so its stored
				// logical stride remains authoritative for both eyes.
				sourceEyeWidth = state_->capturedEyeWidth_;
				if (!sourceEyeWidth ||
					static_cast<std::uint64_t>(sourceEyeWidth) * 2u != sourceDesc.Width ||
					state_->capturedHeight_ != sourceDesc.Height) {
					return fail(std::format(
						"character-mask capture {}x{} does not equal packed logical stereo input {}x{} per eye",
						sourceDesc.Width,
						sourceDesc.Height,
						a_args.viewportCrop.fullInput.width,
						a_args.viewportCrop.fullInput.height));
				}
				if (a_args.viewportCrop.input.right > sourceEyeWidth ||
					a_args.viewportCrop.input.bottom > sourceDesc.Height) {
					return fail(std::format(
						"character-mask crop ({},{})-({},{}) exceeds eye-local source {}x{}",
						a_args.viewportCrop.input.left,
						a_args.viewportCrop.input.top,
						a_args.viewportCrop.input.right,
						a_args.viewportCrop.input.bottom,
						sourceEyeWidth,
						sourceDesc.Height));
				}
				ComPtr<ID3D11Texture2D> authoredDepthTexture;
				std::string authoredDepthError;
				if (!state_->ValidateDepthTexture(
						state_->capturedDepthSrv_.Get(), a_args.device,
						sourceDesc.Width, sourceDesc.Height,
						CharacterDepthExtentPolicy::ExactCapture,
						authoredDepthTexture, authoredDepthIdentity,
						authoredDepthError)) {
					return fail(std::move(authoredDepthError));
				}
				ComPtr<ID3D11Texture2D> currentDepthTexture;
				std::string depthError;
				if (!state_->ValidateDepthTexture(
						a_args.depthGuide, a_args.device,
						a_args.viewportCrop.input.Width(),
						a_args.viewportCrop.input.Height(),
						CharacterDepthExtentPolicy::ContainsActiveInput,
						currentDepthTexture, currentDepthIdentity,
						depthError)) {
					return fail(std::move(depthError));
				}
			}

			auto& slot = state_->slots_[a_args.featureSlot];
			const State::PrepareKey key{
				.sourceWorldFrame = sourceWorldFrame,
				.generation = a_args.generation,
				.width = a_args.outputWidth,
				.height = a_args.outputHeight,
				.settings = BuildSettingsKey(a_args.settings),
				.crop = a_args.viewportCrop,
				.authoredMaskIdentity = sourceIdentity,
				.authoredDepthIdentity = authoredDepthIdentity,
				.currentDepthIdentity = currentDepthIdentity,
				.captureJitterX = state_->capturedJitterX_,
				.captureJitterY = state_->capturedJitterY_,
			};
			if (retainedSource) {
				// Retained menu frames reuse the exact mask/ROI that was produced
				// alongside the frozen world inputs. Reprojecting with a newer HMD
				// pose would move the mask over old scene color.
				if (!slot.prepared || slot.contentSerial == 0 ||
					!slot.mask || !slot.maskSrv || !slot.maskUav ||
					slot.width != a_args.outputWidth ||
					slot.height != a_args.outputHeight) {
					return fail("retained character source has no prepared mask");
				}
				if (slot.prepareKey != key) {
					return fail(
						"retained character mask no longer matches the source-frame contract");
				}
				slot.zeroCoverageBypassResolved = false;
				slot.zeroCoverageBypassed = false;
				slot.feature18Disposition =
					CharacterFeature18Disposition::Unresolved;
				auto& eye = state_->snapshot_.eyes[a_args.eyeIndex];
				eye.frame = a_args.frameId;
				eye.sourceWorldFrame = sourceWorldFrame;
				eye.contentSerial = slot.contentSerial;
				eye.featureSlot = a_args.featureSlot;
				eye.evaluationWidth = a_args.outputWidth;
				eye.evaluationHeight = a_args.outputHeight;
				eye.computeSubrect = slot.computeSubrect;
				eye.computeRegions = slot.computeRegions;
				eye.multiRoiReason = slot.multiRoiReason;
				eye.multiRoiPixels = slot.computeRegions.count == 2 ?
				                         slot.computeRegions.regions[0].Area() + slot.computeRegions.regions[1].Area() :
				                         slot.computeSubrect.Area();
				const auto evaluationPixels =
					static_cast<std::uint64_t>(a_args.outputWidth) *
					a_args.outputHeight;
				eye.computeSubrectPixels = slot.computeSubrect.Area();
				eye.computeSubrectCoveragePercent = evaluationPixels ?
				                                        100.0f * static_cast<float>(eye.computeSubrectPixels) /
				                                            static_cast<float>(evaluationPixels) :
				                                        0.0f;
				eye.maskPixels = slot.maskPixels;
				eye.authoredCategoryPixels = slot.authoredCategoryPixels;
				eye.visibleCategoryPixels = slot.visibleCategoryPixels;
				eye.visibilityRejectedPixels = slot.visibilityRejectedPixels;
				eye.distanceRejectedPixels = slot.distanceRejectedPixels;
				eye.maskCoveragePercent = slot.maskCoveragePercent;
				eye.maskCoverageFrame = slot.maskCoverageFrame;
				eye.maskCoverageFeatureSlot = slot.maskCoverageFeatureSlot;
				eye.maskCoverageWidth = slot.maskCoverageWidth;
				eye.maskCoverageHeight = slot.maskCoverageHeight;
				eye.maskCoverageContentSerial =
					slot.maskCoverageContentSerial;
				eye.maskCoverageReady = slot.maskCoverageReady;
				eye.maskCoverageMatchesCurrentPolicy = eye.maskCoverageReady &&
				                                       slot.maskCoverageContentSerial == slot.contentSerial;
				eye.zeroCoverageBypassRequested = !slot.requiresEvaluation;
				eye.zeroCoverageBypassResolved = false;
				eye.zeroCoverageBypassed = false;
				eye.feature18Disposition =
					CharacterFeature18Disposition::Unresolved;
				eye.feature18EvaluationSucceeded = false;
				eye.zeroCoverageCpuProven = slot.zeroCoverageCpuProven;
				State::PublishMaskRoiSnapshot(slot, eye);
				eye.maskPrepared = true;
				eye.evaluationRequired = slot.requiresEvaluation;
				state_->lastSlotForEye_[a_args.eyeIndex] = a_args.featureSlot;
				state_->RecordPreparedFrame(
					a_args.frameId, sourceWorldFrame, a_args.generation,
					a_args.featureSlot, slot.contentSerial,
					a_args.outputWidth, a_args.outputHeight,
					slot.requiresEvaluation, slot.computeRegions.count);
			} else if (!state_->EnsureSlot(
						   slot, a_args.device, a_args.featureSlot,
						   a_args.outputWidth, a_args.outputHeight)) {
				return fail(
					"DLSS5 character-mask GPU resources could not be created");
			} else if (!slot.prepared || slot.contentSerial == 0 ||
					   slot.prepareKey != key) {
				auto sourceArgs = a_args;
				sourceArgs.frameId = sourceWorldFrame;
				auto plan = state_->BuildPlan(sourceArgs);
				const bool authoredMode =
					UsesAuthoredMask(a_args.settings.maskTestMode);
				const bool forcedEmpty =
					a_args.settings.maskTestMode ==
					CharacterMaskTestMode::ForceZero;
				const bool fullOutputMask =
					a_args.settings.maskTestMode ==
						CharacterMaskTestMode::ForceOne ||
					a_args.settings.maskTestMode ==
						CharacterMaskTestMode::ForceHalf ||
					a_args.settings.maskTestMode ==
						CharacterMaskTestMode::InvertAuthored;
				if (authoredMode && !logicalEmptyCapture &&
					plan.projectionUncertain) {
					// Projection is an optimization boundary, not semantic proof. Fall
					// back to an exact full-eye semantic resolve when actor bounds are
					// unavailable so authored face/skin pixels cannot silently vanish.
					plan.regions = { {
						.minX = 0,
						.minY = 0,
						.maxX = a_args.outputWidth,
						.maxY = a_args.outputHeight,
					} };
					plan.fullEyeEligibilityFallback = true;
					plan.eligibilitySignature = HashCombine(
						plan.eligibilitySignature, 0x46554C4C455945ull);
				}
				if (fullOutputMask) {
					plan.regions = { {
						.minX = 0,
						.minY = 0,
						.maxX = a_args.outputWidth,
						.maxY = a_args.outputHeight,
					} };
					plan.fullEyeEligibilityFallback = true;
					plan.eligibilitySignature = HashCombine(
						plan.eligibilitySignature, 0x46554C4C4D41534Bull);
				}
				const auto diagnosticKey = logicalEmptyCapture ?
				                               0u :
				                               state_->BuildDiagnosticKey(
												   sourceArgs, plan, sourceEyeWidth,
												   sourceDesc.Height);
				const bool cpuProvenEmpty = forcedEmpty ||
				                            (authoredMode &&
												(logicalEmptyCapture || plan.regions.empty()));
				const bool computeSubrectContractChanged =
					!slot.computeSubrectContractValid ||
					slot.computeSubrectGeneration != a_args.generation ||
					slot.computeSubrectCrop != a_args.viewportCrop;
				if (computeSubrectContractChanged) {
					slot.stableComputeSubrect = {};
					slot.stableMultiRoi = {};
					slot.stableMaskRoi = {};
					slot.maskRoiAdmission = {};
					slot.computeSubrectGeneration = a_args.generation;
					slot.computeSubrectCrop = a_args.viewportCrop;
					slot.computeSubrectContractValid = true;
				}
				if (slot.multiRoiPolicyKey != key.settings) {
					slot.stableMultiRoi = {};
					slot.stableMaskRoi = {};
					slot.maskRoiAdmission = {};
					slot.multiRoiPolicyKey = key.settings;
				}
				const auto requiredComputeSubrect = cpuProvenEmpty ?
				                                        ComputeSubrect{} :
				                                    fullOutputMask ?
				                                        BuildFullComputeSubrect(
															a_args.outputWidth,
															a_args.outputHeight) :
				                                        BuildCharacterComputeSubrect(
															plan.regions,
															a_args.outputWidth,
															a_args.outputHeight);
				slot.maskWorkSubrect = requiredComputeSubrect;
				if (cpuProvenEmpty || fullOutputMask) {
					// Empty authored masks already break provider history. Do not
					// retain a potentially large stale ROI for the next character.
					// Diagnostic full-eye modes must not contaminate authored ROI state.
					slot.stableComputeSubrect = {};
					slot.computeSubrect = requiredComputeSubrect;
				} else {
					slot.computeSubrect = ResolveStableCharacterComputeSubrect(
						requiredComputeSubrect,
						a_args.outputWidth,
						a_args.outputHeight,
						slot.stableComputeSubrect);
				}
				slot.computeRegions = {};
				slot.multiRoiReason = CharacterMultiRoiReason::Disabled;
				if (a_args.settings.multiRoi) {
					if (!authoredMode || a_args.settings.debugView != CharacterDebugView::Off) {
						slot.multiRoiReason = CharacterMultiRoiReason::DiagnosticMode;
					} else if (cpuProvenEmpty) {
						slot.multiRoiReason = CharacterMultiRoiReason::TooFewActors;
					} else if (plan.fullEyeEligibilityFallback) {
						slot.multiRoiReason = CharacterMultiRoiReason::UncertainCoverage;
					} else {
						slot.computeRegions = ResolveCharacterMultiRoi(
							plan.actorRegions, plan.regions, a_args.outputWidth, a_args.outputHeight,
							sourceWorldFrame, slot.stableMultiRoi, slot.multiRoiReason);
						if (slot.computeRegions.count == 2) {
							// Composition still receives the enclosure, but inference uses
							// the separate rectangles. Never expose stale pixels in gaps.
							slot.computeSubrect = UnionCharacterComputeSubrect(
								slot.computeRegions.regions[0], slot.computeRegions.regions[1]);
						}
					}
				}
				if (slot.computeRegions.count == 0)
					slot.stableMultiRoi = {};
				if (!cpuProvenEmpty &&
					!slot.computeSubrect.Fits(
						a_args.outputWidth, a_args.outputHeight)) {
					return fail("character compute ROI is invalid");
				}
				slot.contentSerial = state_->AllocatePreparedContentSerial();
				slot.requiresEvaluation = !cpuProvenEmpty;
				slot.zeroCoverageBypassResolved = false;
				slot.zeroCoverageBypassed = false;
				slot.feature18Disposition =
					CharacterFeature18Disposition::Unresolved;
				slot.zeroCoverageCpuProven = false;
				slot.maskRoiStatus = a_args.settings.multiRoi ? "not_applicable" : "disabled";
				slot.maskRoiCurrentFrame = false;
				slot.maskRoiGpuProvenEmpty = false;
				slot.maskRoiOccupiedTiles = 0;
				slot.maskRoiRequiredSubrect = {};
				slot.maskRoiReadbackWaitMs = 0.0;
				slot.maskRoiPlanningCpuMs = 0.0;
				slot.maskRoiReadbackFenceValue = 0;
				slot.maskBoundsResolvePending = false;
				if (cpuProvenEmpty || logicalEmptyCapture) {
					float clearValue = 0.0f;
					switch (a_args.settings.maskTestMode) {
					case CharacterMaskTestMode::ForceOne:
					case CharacterMaskTestMode::InvertAuthored:
						clearValue = 1.0f;
						break;
					case CharacterMaskTestMode::ForceHalf:
						clearValue = 0.5f;
						break;
					default:
						break;
					}
					state_->ClearMask(
						slot, a_args.context, sourceWorldFrame,
						a_args.featureSlot, a_args.outputWidth,
						a_args.outputHeight, clearValue);
				} else {
					if (!state_->EnsureShader(a_args.device)) {
						return fail(
							"DLSS5 character-mask compute shader could not be created");
					}
					if (!state_->Dispatch(
							sourceArgs,
							state_->capturedCategoriesSrv_.Get(),
							state_->capturedDepthSrv_.Get(),
							slot, plan,
							sourceEyeWidth, sourceDesc.Height)) {
						return fail("DLSS5 character-mask dispatch failed");
					}
				}
				if (a_args.settings.multiRoi && authoredMode && !cpuProvenEmpty &&
					!logicalEmptyCapture && a_args.settings.debugView == CharacterDebugView::Off) {
					try {
						if (state_->QueueCurrentMaskBounds(a_args, slot, plan, sourceWorldFrame) &&
							!a_args.deferMaskRoiReadback) {
							const auto deadline = std::chrono::steady_clock::now() + kCharacterMaskReadbackBudget;
							const auto completion = state_->SignalMaskBoundsCompletion(a_args.context);
							a_args.context->Flush();
							if (state_->ReadCurrentMaskBounds(a_args, slot, deadline, completion))
								state_->ResolveCurrentMaskBounds(a_args, slot);
						}
					} catch (...) {
						state_->RejectMaskBounds(a_args, slot, "bounds_optimization_failed", E_FAIL);
					}
				} else {
					slot.stableMaskRoi = {};
					slot.maskRoiAdmission = {};
				}
				slot.requiresEvaluation = !(cpuProvenEmpty || slot.maskRoiGpuProvenEmpty);
				slot.zeroCoverageBypassed = false;
				slot.zeroCoverageCpuProven = cpuProvenEmpty;
				if (!slot.requiresEvaluation)
					Increment(state_->snapshot_.provenEmptyFeatureBypassRequests);
				slot.prepareKey = key;
				slot.prepared = true;

				auto& eye = state_->snapshot_.eyes[a_args.eyeIndex];
				eye.frame = a_args.frameId;
				eye.sourceWorldFrame = sourceWorldFrame;
				eye.contentSerial = slot.contentSerial;
				eye.featureSlot = a_args.featureSlot;
				eye.effectiveCategoryMask = GetEnabledCharacterCategoryMask(a_args.settings);
				eye.effectiveCategoryStrengths = {
					a_args.settings.faceStrength, a_args.settings.skinStrength, a_args.settings.hairStrength
				};
				eye.evaluationWidth = a_args.outputWidth;
				eye.evaluationHeight = a_args.outputHeight;
				eye.visibleFaces = plan.visibleFaces;
				eye.visibleCharacterRegions = plan.visibleCharacters;
				eye.selectedCharacterRegions = plan.selectedCharacters;
				eye.adaptivelyCulledCharacterRegions =
					plan.adaptivelyCulledCharacters;
				eye.mergedRegions = static_cast<std::uint32_t>(plan.regions.size());
				eye.regions = plan.regions;
				eye.roiPixels =
					CharacterRegionPolicy::CoveredArea(plan.regions);
				const auto evaluationPixels =
					static_cast<std::uint64_t>(a_args.outputWidth) * a_args.outputHeight;
				eye.computeSubrect = slot.computeSubrect;
				eye.computeRegions = slot.computeRegions;
				eye.multiRoiReason = slot.multiRoiReason;
				eye.multiRoiPixels = slot.computeRegions.count == 2 ?
				                         slot.computeRegions.regions[0].Area() + slot.computeRegions.regions[1].Area() :
				                         slot.computeSubrect.Area();
				eye.computeSubrectPixels = slot.computeSubrect.Area();
				eye.computeSubrectCoveragePercent = evaluationPixels ?
				                                        100.0f * static_cast<float>(eye.computeSubrectPixels) /
				                                            static_cast<float>(evaluationPixels) :
				                                        0.0f;
				eye.roiCoveragePercent = evaluationPixels ?
				                             100.0f * static_cast<float>(eye.roiPixels) /
				                                 static_cast<float>(evaluationPixels) :
				                             0.0f;
				eye.maskPixels = slot.maskPixels;
				eye.authoredCategoryPixels = slot.authoredCategoryPixels;
				eye.visibleCategoryPixels = slot.visibleCategoryPixels;
				eye.visibilityRejectedPixels = slot.visibilityRejectedPixels;
				eye.distanceRejectedPixels = slot.distanceRejectedPixels;
				eye.maskCoveragePercent = slot.maskCoveragePercent;
				eye.maskCoverageFrame = slot.maskCoverageFrame;
				eye.maskCoverageFeatureSlot = slot.maskCoverageFeatureSlot;
				eye.maskCoverageWidth = slot.maskCoverageWidth;
				eye.maskCoverageHeight = slot.maskCoverageHeight;
				eye.maskCoverageContentSerial =
					slot.maskCoverageContentSerial;
				eye.maskCoverageReady = slot.maskCoverageReady;
				eye.maskCoverageMatchesCurrentPolicy =
					eye.maskCoverageReady &&
					slot.maskCoverageContentSerial == slot.contentSerial &&
					slot.maskDiagnosticKey == diagnosticKey;
				eye.zeroCoverageBypassRequested =
					!slot.requiresEvaluation;
				eye.zeroCoverageBypassResolved = false;
				eye.zeroCoverageBypassed = slot.zeroCoverageBypassed;
				eye.feature18Disposition =
					CharacterFeature18Disposition::Unresolved;
				eye.feature18EvaluationSucceeded = false;
				eye.zeroCoverageCpuProven = slot.zeroCoverageCpuProven;
				State::PublishMaskRoiSnapshot(slot, eye);
				eye.fullEyeEligibilityFallback =
					plan.fullEyeEligibilityFallback;
				eye.projectionUncertainActors = plan.projectionUncertainActors;
				eye.projectionClippedGeometry = plan.projectionClippedGeometry;
				eye.projectionFallbackActorFormId = plan.projectionFallbackActorFormId;
				eye.projectionFallbackCategory = plan.projectionFallbackCategory;
				eye.projectionFallbackReason = plan.projectionFallbackReason;
				eye.depthCoordinatesValid = !logicalEmptyCapture;
				eye.authoredStereoWidth = sourceDesc.Width;
				eye.authoredDepthHeight = sourceDesc.Height;
				eye.authoredEyeBaseX = a_args.eyeIndex * sourceEyeWidth;
				eye.currentDepthWidth = a_args.viewportCrop.input.Width();
				eye.currentDepthHeight = a_args.viewportCrop.input.Height();
				eye.inputCropLeft = a_args.viewportCrop.input.left;
				eye.inputCropTop = a_args.viewportCrop.input.top;
				eye.inputCropWidth = a_args.viewportCrop.input.Width();
				eye.inputCropHeight = a_args.viewportCrop.input.Height();
				eye.outputCropLeft = a_args.viewportCrop.output.left;
				eye.outputCropTop = a_args.viewportCrop.output.top;
				eye.outputCropWidth = a_args.viewportCrop.output.Width();
				eye.outputCropHeight = a_args.viewportCrop.output.Height();
				eye.capturedJitterX = state_->capturedJitterX_;
				eye.capturedJitterY = state_->capturedJitterY_;
				eye.maskPrepared = true;
				eye.evaluationRequired = slot.requiresEvaluation;
				state_->lastSlotForEye_[a_args.eyeIndex] = a_args.featureSlot;
				state_->RecordPreparedFrame(
					a_args.frameId, sourceWorldFrame, a_args.generation,
					a_args.featureSlot, slot.contentSerial,
					a_args.outputWidth, a_args.outputHeight,
					slot.requiresEvaluation, slot.computeRegions.count);
			}

			a_result.prepared = true;
			a_result.requiresEvaluation = slot.requiresEvaluation;
			a_result.computeSubrect = slot.computeSubrect;
			a_result.computeRegions = slot.computeRegions;
			Increment(state_->snapshot_.preparationSuccesses);
			state_->snapshot_.status = "ready";
			state_->snapshot_.detail = std::format(
				"CSX character selection mask prepared for eye {} slot {} at {}x{}; evaluation={}; sourceFrame={}; compute ROI=({},{} {}x{}); independent regions={}; inference pixels={}; region decision={}",
				a_args.eyeIndex,
				a_args.featureSlot,
				a_args.outputWidth,
				a_args.outputHeight,
				slot.requiresEvaluation ? "required" : "bypassed-empty",
				sourceWorldFrame,
				slot.computeSubrect.baseX,
				slot.computeSubrect.baseY,
				slot.computeSubrect.width,
				slot.computeSubrect.height,
				slot.requiresEvaluation ? std::max(1u, slot.computeRegions.count) : 0u,
				slot.computeRegions.count == 2 ?
					slot.computeRegions.regions[0].Area() + slot.computeRegions.regions[1].Area() :
					slot.computeSubrect.Area(),
				GetCharacterMultiRoiReasonName(slot.multiRoiReason));
			return true;
		} catch (const std::exception& exception) {
			std::scoped_lock lock(state_->mutex_);
			state_->RecordPreparationFailure(a_args, exception.what());
			if (!retainedSource) {
				state_->InvalidatePreparedSlot(
					a_args.featureSlot, a_args.eyeIndex, a_args.frameId);
			}
			state_->snapshot_.status =
				retainedSource ? "retained-source-miss" : "failed";
			state_->snapshot_.detail = exception.what();
			return false;
		} catch (...) {
			std::scoped_lock lock(state_->mutex_);
			state_->RecordPreparationFailure(a_args, "unknown character-mask preparation exception");
			if (!retainedSource) {
				state_->InvalidatePreparedSlot(
					a_args.featureSlot, a_args.eyeIndex, a_args.frameId);
			}
			state_->snapshot_.status =
				retainedSource ? "retained-source-miss" : "failed";
			state_->snapshot_.detail = "unknown character-mask preparation exception";
			return false;
		}
	}

	bool CharacterRendering::FinalizePreparedMasks(
		std::span<const CharacterMaskPrepareArgs> a_args,
		std::span<CharacterMaskPrepareResult> a_results) noexcept
	{
		if (!state_ || a_args.empty() || a_args.size() > 2 || a_results.size() != a_args.size())
			return false;
		try {
			std::scoped_lock lock(state_->mutex_);
			bool pending = false;
			std::uint32_t slotMask = 0;
			std::uint32_t eyeMask = 0;
			// Validate the entire batch before consuming any evidence. A new mask
			// cannot inherit bounds from a previous frame/epoch or another depth guide.
			for (const auto& args : a_args) {
				if (!args.device || !args.context || args.context != a_args.front().context ||
					args.device != a_args.front().device || args.frameId != a_args.front().frameId ||
					args.context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
					args.featureSlot >= state_->slots_.size() || args.eyeIndex >= 2 ||
					(args.featureSlot & 1u) != args.eyeIndex || (slotMask & (1u << args.featureSlot)) ||
					(eyeMask & (1u << args.eyeIndex)) ||
					args.featureSlot / 2u != a_args.front().featureSlot / 2u)
					return false;
				ComPtr<ID3D11Device> contextDevice;
				args.context->GetDevice(&contextDevice);
				if (!SameIdentity(args.device, contextDevice.Get()) || !SameIdentity(args.device, state_->device_.Get()))
					return false;
				slotMask |= 1u << args.featureSlot;
				eyeMask |= 1u << args.eyeIndex;
				const auto& slot = state_->slots_[args.featureSlot];
				if (state_->FindPreparedSlot(args.featureSlot, args.frameId,
						args.sourceWorldFrame, args.generation, args.outputWidth, args.outputHeight) != &slot ||
					slot.prepareKey.sourceWorldFrame != args.sourceWorldFrame ||
					slot.prepareKey.generation != args.generation ||
					slot.prepareKey.width != args.outputWidth || slot.prepareKey.height != args.outputHeight ||
					slot.prepareKey.crop != args.viewportCrop ||
					slot.prepareKey.settings != BuildSettingsKey(args.settings))
					return false;
				if (slot.prepareKey.currentDepthIdentity != 0) {
					ComPtr<ID3D11Resource> depth;
					if (args.depthGuide)
						args.depthGuide->GetResource(&depth);
					if (GetIdentityToken(depth.Get()) != slot.prepareKey.currentDepthIdentity)
						return false;
				}
				pending = pending || slot.maskBoundsResolvePending;
			}
			const auto deadline = std::chrono::steady_clock::now() + kCharacterMaskReadbackBudget;
			CharacterMaskReadbackCompletion completion{};
			if (pending) {
				// One GPU signal covers both copies before either staging buffer is mapped.
				completion = state_->SignalMaskBoundsCompletion(a_args.front().context);
				a_args.front().context->Flush();
			}
			// Read both eyes before CPU planning can spend the shared GPU deadline.
			std::array<bool, 2> boundsReady{};
			for (std::size_t index = 0; index < a_args.size(); ++index)
				boundsReady[index] = state_->ReadCurrentMaskBounds(a_args[index],
					state_->slots_[a_args[index].featureSlot], deadline, completion);
			std::uint64_t inferencePixels = 0;
			for (std::size_t index = 0; index < a_args.size(); ++index) {
				const auto& args = a_args[index];
				auto& slot = state_->slots_[args.featureSlot];
				if (boundsReady[index])
					state_->ResolveCurrentMaskBounds(args, slot);
				const bool wasRequired = slot.requiresEvaluation;
				slot.requiresEvaluation = !(slot.zeroCoverageCpuProven || slot.maskRoiGpuProvenEmpty);
				if (wasRequired && !slot.requiresEvaluation)
					Increment(state_->snapshot_.provenEmptyFeatureBypassRequests);
				auto& eye = state_->snapshot_.eyes[args.eyeIndex];
				eye.computeSubrect = slot.computeSubrect;
				eye.computeRegions = slot.computeRegions;
				eye.multiRoiReason = slot.multiRoiReason;
				eye.multiRoiPixels = slot.computeRegions.count == 2 ?
				                         slot.computeRegions.regions[0].Area() + slot.computeRegions.regions[1].Area() :
				                         slot.computeSubrect.Area();
				if (slot.requiresEvaluation)
					inferencePixels += eye.multiRoiPixels;
				eye.computeSubrectPixels = slot.computeSubrect.Area();
				const auto pixels = static_cast<std::uint64_t>(args.outputWidth) * args.outputHeight;
				eye.computeSubrectCoveragePercent = pixels ?
				                                        100.0f * static_cast<float>(eye.computeSubrectPixels) / static_cast<float>(pixels) :
				                                        0.0f;
				eye.evaluationRequired = slot.requiresEvaluation;
				eye.zeroCoverageBypassRequested = !slot.requiresEvaluation;
				State::PublishMaskRoiSnapshot(slot, eye);
				state_->RecordPreparedFrame(args.frameId, args.sourceWorldFrame, args.generation,
					args.featureSlot, slot.contentSerial, args.outputWidth, args.outputHeight,
					slot.requiresEvaluation, slot.computeRegions.count);
				a_results[index] = { true, slot.requiresEvaluation, slot.computeSubrect, slot.computeRegions };
			}
			state_->snapshot_.status = "ready";
			state_->snapshot_.detail = std::format(
				"CSX character masks finalized for {} eye(s) at evaluation frame {}; total inference pixels={} (sum of active regions)",
				a_args.size(), a_args.front().frameId, inferencePixels);
			return true;
		} catch (...) {
			return false;
		}
	}

	void CharacterRendering::ResolveFeature18Disposition(
		std::uint32_t a_frameId,
		std::uint32_t a_sourceWorldFrame,
		std::uint64_t a_generation,
		std::uint32_t a_preparedFeatureSlotMask,
		std::uint32_t a_evaluatedFeatureSlotMask,
		std::uint32_t a_successfulFeatureSlotMask,
		std::uint32_t a_bypassedFeatureSlotMask) noexcept
	{
		if (!state_ ||
			a_frameId == std::numeric_limits<std::uint32_t>::max() ||
			a_sourceWorldFrame == std::numeric_limits<std::uint32_t>::max() ||
			a_sourceWorldFrame > a_frameId || a_generation == 0)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			const std::uint32_t validSlotMask =
				(1u << static_cast<std::uint32_t>(state_->slots_.size())) - 1u;
			CharacterPreparedFrameSnapshot* preparedFrame = nullptr;
			for (auto& candidate : state_->snapshot_.preparedFrames) {
				if (candidate.frame == a_frameId) {
					preparedFrame = &candidate;
					break;
				}
			}
			if (!preparedFrame)
				return;
			const auto preparedMask =
				a_preparedFeatureSlotMask & validSlotMask &
				preparedFrame->preparedSlotMask;
			const auto unresolvedMask =
				preparedMask & ~preparedFrame->resolutionRecordedSlotMask;
			const auto evaluatedMask =
				a_evaluatedFeatureSlotMask & unresolvedMask;
			const auto successfulMask =
				a_successfulFeatureSlotMask & evaluatedMask;
			const auto bypassedMask =
				a_bypassedFeatureSlotMask & unresolvedMask & ~evaluatedMask;
			for (std::uint32_t slotIndex = 0;
				slotIndex < state_->slots_.size(); ++slotIndex) {
				const auto slotBit = 1u << slotIndex;
				if ((unresolvedMask & slotBit) == 0)
					continue;
				auto& slot = state_->slots_[slotIndex];
				if (!slot.prepared ||
					preparedFrame->sourceWorldFrames[slotIndex] != a_sourceWorldFrame ||
					preparedFrame->generations[slotIndex] != a_generation ||
					preparedFrame->contentSerials[slotIndex] == 0 ||
					preparedFrame->contentSerials[slotIndex] != slot.contentSerial ||
					slot.prepareKey.sourceWorldFrame != a_sourceWorldFrame ||
					slot.prepareKey.generation != a_generation) {
					continue;
				}
				const bool bypassRequested =
					!slot.requiresEvaluation && (slot.zeroCoverageCpuProven || slot.maskRoiGpuProvenEmpty);
				const bool evaluated = (evaluatedMask & slotBit) != 0;
				const bool successful = (successfulMask & slotBit) != 0;
				const bool bypassed =
					bypassRequested && (bypassedMask & slotBit) != 0;
				const auto disposition = successful ?
				                             CharacterFeature18Disposition::Evaluated :
				                         evaluated ?
				                             CharacterFeature18Disposition::EvaluationFailed :
				                         bypassed ?
				                             CharacterFeature18Disposition::EmptyBypass :
				                             CharacterFeature18Disposition::Aborted;
				slot.zeroCoverageBypassResolved = true;
				slot.zeroCoverageBypassed = bypassed;
				slot.feature18Disposition = disposition;
				preparedFrame->resolutionRecordedSlotMask |= slotBit;
				switch (disposition) {
				case CharacterFeature18Disposition::Evaluated:
					preparedFrame->evaluatedSlotMask |= slotBit;
					preparedFrame->successfulSlotMask |= slotBit;
					break;
				case CharacterFeature18Disposition::EvaluationFailed:
					preparedFrame->evaluatedSlotMask |= slotBit;
					break;
				case CharacterFeature18Disposition::EmptyBypass:
					preparedFrame->bypassedSlotMask |= slotBit;
					break;
				case CharacterFeature18Disposition::Aborted:
					preparedFrame->abortedSlotMask |= slotBit;
					break;
				case CharacterFeature18Disposition::Unresolved:
					break;
				}
				if (bypassed) {
					if (slot.zeroCoverageCpuProven || slot.maskRoiGpuProvenEmpty)
						Increment(state_->snapshot_.provenEmptyFeatureBypasses);
				}

				const auto eyeIndex = slotIndex & 1u;
				auto& eye = state_->snapshot_.eyes[eyeIndex];
				if (eye.frame == a_frameId && eye.featureSlot == slotIndex) {
					eye.zeroCoverageBypassResolved = true;
					eye.zeroCoverageBypassed = bypassed;
					eye.feature18Disposition = disposition;
					eye.feature18EvaluationSucceeded = successful;
				}
			}
		} catch (...) {
			// Diagnostics must never affect the render transaction.
		}
	}

	void CharacterRendering::Reset() noexcept
	{
		if (!state_)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->categoryFramePolicy_ = {};
			state_->observations_.clear();
			state_->observationKeys_.clear();
			state_->unboundedCategoryMask_ = 0;
			state_->observationFrame_ = std::numeric_limits<std::uint32_t>::max();
			state_->InvalidateProjectionCache();
			state_->actorAdmissions_.clear();
			state_->slots_ = {};
			state_->shader_.Reset();
			state_->constants_.Reset();
			state_->ResetMaskBoundsShader();
			state_->shaderCompileFailed_ = false;
			state_->capturedCategories_.Reset();
			state_->capturedCategoriesSrv_.Reset();
			state_->capturedCategoriesUav_.Reset();
			state_->capturedDepth_.Reset();
			state_->capturedDepthSrv_.Reset();
			state_->capturedDepthUav_.Reset();
			state_->captureShader_.Reset();
			state_->captureConstants_.Reset();
			state_->captureSourceCategoriesSrv_.Reset();
			state_->captureShaderCompileFailed_ = false;
			state_->InvalidateCaptureMetadata();
			state_->device_.Reset();
			state_->lastSlotForEye_ = { 4, 4 };
			state_->preparedFrameHistoryNext_ = 0;
			state_->snapshot_ = {};
			state_->ResetClassificationRejections();
		} catch (...) {
		}
	}

	void CharacterRendering::Invalidate(std::uint32_t a_preserveCaptureFrame) noexcept
	{
		if (!state_)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->actorAdmissions_.clear();
			state_->InvalidateProjectionCache();
			// Temporal history does not own the already captured source pixels.
			if (a_preserveCaptureFrame == std::numeric_limits<std::uint32_t>::max() ||
				state_->capturedFrame_ != a_preserveCaptureFrame)
				state_->InvalidateCaptureMetadata();
			state_->InvalidatePreparedMasks();
			state_->snapshot_.status = "invalidated";
			state_->snapshot_.detail.clear();
		} catch (...) {
		}
	}

	void CharacterRendering::ResetShaderCache() noexcept
	{
		if (!state_)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->shader_.Reset();
			state_->constants_.Reset();
			state_->ResetMaskBoundsShader();
			state_->shaderCompileFailed_ = false;
			state_->captureShader_.Reset();
			state_->captureShaderCompileFailed_ = false;
			state_->InvalidateCaptureMetadata();
			state_->InvalidatePreparedMasks();
			state_->snapshot_.status = "shader_cache_reset";
			state_->snapshot_.detail.clear();
		} catch (...) {
		}
	}

	CharacterSnapshot CharacterRendering::GetSnapshot() const
	{
		if (!state_)
			return {};
		std::scoped_lock lock(state_->mutex_);
		auto snapshot = state_->snapshot_;
		state_->PublishClassificationRejections(snapshot);
		return snapshot;
	}

	ComPtr<ID3D11ShaderResourceView> CharacterRendering::GetDebugMaskSrv(
		std::uint32_t a_eyeIndex) const noexcept
	{
		if (!state_ || a_eyeIndex >= 2)
			return {};
		try {
			std::scoped_lock lock(state_->mutex_);
			const auto slot = state_->lastSlotForEye_[a_eyeIndex];
			return slot < state_->slots_.size() && state_->slots_[slot].prepared ?
			           state_->slots_[slot].maskSrv :
			           ComPtr<ID3D11ShaderResourceView>{};
		} catch (...) {
			return {};
		}
	}

	ComPtr<ID3D11ShaderResourceView> CharacterRendering::GetPreparedMaskSrv(
		std::uint32_t a_featureSlot,
		std::uint32_t a_frameId,
		std::uint32_t a_sourceWorldFrame,
		std::uint64_t a_generation,
		std::uint32_t a_width,
		std::uint32_t a_height) const noexcept
	{
		if (!state_ || a_featureSlot >= state_->slots_.size())
			return {};
		try {
			std::scoped_lock lock(state_->mutex_);
			const auto* slot = state_->FindPreparedSlot(a_featureSlot, a_frameId,
				a_sourceWorldFrame, a_generation, a_width, a_height);
			return slot ? slot->maskSrv : ComPtr<ID3D11ShaderResourceView>{};
		} catch (...) {
			return {};
		}
	}

	ComputeSubrect CharacterRendering::GetMaskSupportRect(
		ID3D11ShaderResourceView* a_mask,
		std::uint32_t a_width, std::uint32_t a_height) const noexcept
	{
		const auto full = BuildFullComputeSubrect(a_width, a_height);
		if (!state_ || !a_mask)
			return full;
		try {
			std::scoped_lock lock(state_->mutex_);
			for (const auto& slot : state_->slots_) {
				if (slot.prepared && slot.maskSrv.Get() == a_mask &&
					slot.width == a_width && slot.height == a_height) {
					if (slot.maskUniform)
						return slot.uniformMaskValue == 0.0f ? ComputeSubrect{} : full;
					if (slot.maskRoiCurrentFrame) {
						if (slot.maskRoiGpuProvenEmpty)
							return {};
						if (slot.maskRoiRequiredSubrect.Fits(a_width, a_height))
							return ExpandCharacterWorkRect(slot.maskRoiRequiredSubrect, a_width, a_height, 1);
					}
					// Include the linear sampler footprint around the authored mask.
					return slot.maskWorkSubrect.Fits(a_width, a_height) ?
					           ExpandCharacterWorkRect(slot.maskWorkSubrect, a_width, a_height, 1) :
					           full;
				}
			}
		} catch (...) {
		}
		return full;
	}

	ComputeSubrect CharacterRendering::GetPreparedComputeSubrect(
		std::uint32_t a_featureSlot,
		std::uint32_t a_frameId,
		std::uint32_t a_sourceWorldFrame,
		std::uint64_t a_generation,
		std::uint32_t a_width,
		std::uint32_t a_height) const noexcept
	{
		if (!state_ || a_featureSlot >= state_->slots_.size())
			return {};
		try {
			std::scoped_lock lock(state_->mutex_);
			const auto* slot = state_->FindPreparedSlot(a_featureSlot, a_frameId,
				a_sourceWorldFrame, a_generation, a_width, a_height);
			return slot ? slot->computeSubrect : ComputeSubrect{};
		} catch (...) {
			return {};
		}
	}

	CharacterComputeRegionPlan CharacterRendering::GetPreparedComputeRegions(
		std::uint32_t a_featureSlot,
		std::uint32_t a_frameId,
		std::uint32_t a_sourceWorldFrame,
		std::uint64_t a_generation,
		std::uint32_t a_width,
		std::uint32_t a_height) const noexcept
	{
		if (!state_)
			return {};
		try {
			std::scoped_lock lock(state_->mutex_);
			const auto* slot = state_->FindPreparedSlot(a_featureSlot, a_frameId,
				a_sourceWorldFrame, a_generation, a_width, a_height);
			return slot ? slot->computeRegions : CharacterComputeRegionPlan{};
		} catch (...) {
			return {};
		}
	}
}
