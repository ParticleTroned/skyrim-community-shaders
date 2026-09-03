#include "CharacterRendering.h"

#include "Buffer.h"
#include "Globals.h"
#include "Profiler.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
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
		constexpr std::uint32_t kDiagnosticCounterCount = 8;

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
			add(a_settings.minimumFacePixelSize);
			addFloat(a_settings.roiMargin);
			add(a_settings.maximumRoiRegions);
			add(a_settings.roiHoldFrames);
			add(a_settings.depthAwareFeather);
			add(a_settings.visibilityDepthTest);
			add(a_settings.featherRadius);
			addFloat(a_settings.featherDepthThreshold);
			add(static_cast<std::uint32_t>(a_settings.maskTestMode));
			return hash;
		}

		bool IsFiniteSettings(const CharacterSettings& a_settings) noexcept
		{
			const auto validStrength = [](float a_value) {
				return std::isfinite(a_value) &&
				       a_value >= CharacterPolicy::kMinimumStrength &&
				       a_value <= CharacterPolicy::kMaximumStrength;
			};
			return validStrength(a_settings.faceStrength) &&
			       validStrength(a_settings.skinStrength) &&
			       validStrength(a_settings.hairStrength) &&
			       std::isfinite(a_settings.maximumDistanceMeters) &&
			       a_settings.maximumDistanceMeters >=
			           CharacterPolicy::kMinimumDistanceMeters &&
			       a_settings.maximumDistanceMeters <=
			           CharacterPolicy::kMaximumDistanceMeters &&
			       a_settings.minimumFacePixelSize >=
			           CharacterPolicy::kMinimumFacePixelSize &&
			       a_settings.minimumFacePixelSize <=
			           CharacterPolicy::kMaximumFacePixelSize &&
			       std::isfinite(a_settings.roiMargin) &&
			       a_settings.roiMargin >= CharacterPolicy::kMinimumRoiMargin &&
			       a_settings.roiMargin <= CharacterPolicy::kMaximumRoiMargin &&
			       a_settings.maximumRoiRegions >= 1 &&
			       a_settings.maximumRoiRegions <=
			           CharacterPolicy::kMaximumRoiRegions &&
			       a_settings.roiHoldFrames <=
			           CharacterPolicy::kMaximumRoiHoldFrames &&
			       a_settings.featherRadius <=
			           CharacterPolicy::kMaximumFeatherRadius &&
			       std::isfinite(a_settings.featherDepthThreshold) &&
			       a_settings.featherDepthThreshold >= 0.0f &&
			       a_settings.featherDepthThreshold <=
			           CharacterPolicy::kMaximumFeatherDepthThreshold &&
			       a_settings.maskTestMode < CharacterMaskTestMode::Count;
		}

		bool IsCategoryEnabled(
			CharacterCategory a_category,
			const CharacterSettings& a_settings) noexcept
		{
			switch (a_category) {
			case CharacterCategory::Face:
				return a_settings.faces && a_settings.faceStrength > 0.0f;
			case CharacterCategory::Skin:
				return a_settings.skin && a_settings.skinStrength > 0.0f;
			case CharacterCategory::Hair:
				return a_settings.hair && a_settings.hairStrength > 0.0f;
			default:
				return false;
			}
		}

		bool UsesAuthoredMask(CharacterMaskTestMode a_mode) noexcept
		{
			return a_mode == CharacterMaskTestMode::Authored ||
			       a_mode ==
			           CharacterMaskTestMode::AuthoredWithoutVisibilityDepth;
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

		struct HeldRegion
		{
			CharacterRect rect{};
			std::uint32_t lastSeenFrame = 0;
			std::uint64_t facePriority = 0;
		};

		struct ProjectedActor
		{
			std::uint32_t actorFormId = 0;
			std::array<CharacterRect, 2> faceRects{};
			std::array<CharacterRect, 2> selectedRects{};
			float nearestFaceDistanceUnits =
				std::numeric_limits<float>::max();
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
			std::uint64_t serial = 0;
			bool pending = false;
		};

		struct PrepareKey
		{
			std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
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
			PrepareKey prepareKey{};
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
			std::uint64_t maskDiagnosticKey = 0;
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
			bool zeroCoverageSampleReused = false;
			bool requiresEvaluation = true;
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
			float jitter[4]{};
			float categoryStrengths[4]{};
			float roiRectangles[CharacterPolicy::kMaximumRoiRegions][4]{};
		};
		static_assert(sizeof(MaskConstants) % 16 == 0);

		struct ProjectedPlan
		{
			std::vector<CharacterRect> regions;
			std::uint64_t eligibilitySignature = 0;
			std::uint32_t visibleFaces = 0;
			std::uint32_t visibleCharacters = 0;
			std::uint32_t droppedCharacters = 0;
			bool projectionUncertain = false;
			bool fullEyeEligibilityFallback = false;
		};

		State()
		{
			observationKeys_.reserve(
				CharacterPolicy::kMaximumObservationsPerFrame);
		}

		void BeginObservationFrame(std::uint32_t a_frame)
		{
			if (observationFrame_ == a_frame)
				return;
			observationFrame_ = a_frame;
			observations_.clear();
			observationKeys_.clear();
			InvalidateProjectionCache();
			snapshot_.observationFrame = a_frame;
			snapshot_.currentObservations = 0;
			snapshot_.currentCategoryObservations = {};
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

		void InvalidatePreparedMasks() noexcept
		{
			lastSlotForEye_ = { 4, 4 };
			snapshot_.eyes = {};
			for (auto& slot : slots_) {
				slot.prepared = false;
				slot.prepareKey = {};
				slot.zeroCoverageBypassResolved = false;
				slot.zeroCoverageBypassed = false;
				slot.feature18Disposition =
					CharacterFeature18Disposition::Unresolved;
			}
		}

		void RecordPreparedFrame(
			std::uint32_t a_frame,
			std::uint32_t a_featureSlot,
			std::uint32_t a_width,
			std::uint32_t a_height,
			bool a_requiresEvaluation) noexcept
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
			if (a_featureSlot >= entry->widths.size())
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
			entry->widths[a_featureSlot] = a_width;
			entry->heights[a_featureSlot] = a_height;
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
				slot.prepareKey = {};
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
				preparedFrame.widths[a_featureSlot] = 0;
				preparedFrame.heights[a_featureSlot] = 0;
				if (preparedFrame.preparedSlotMask == 0u)
					preparedFrame = {};
				break;
			}
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
			a_context->ClearUnorderedAccessViewFloat(a_slot.maskUav.Get(), clear.data());
			a_slot.maskCoverageFrame = a_frame;
			a_slot.maskCoverageFeatureSlot = a_featureSlot;
			a_slot.maskCoverageWidth = a_width;
			a_slot.maskCoverageHeight = a_height;
			const auto pixelCount = static_cast<std::uint64_t>(a_width) * a_height;
			a_slot.maskPixels = a_value > (0.5f / 255.0f) ? pixelCount : 0;
			a_slot.authoredCategoryPixels = {};
			a_slot.visibleCategoryPixels = {};
			a_slot.visibilityRejectedPixels = 0;
			a_slot.maskDiagnosticKey = 0;
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
			a_slot.zeroCoverageSampleReused = false;
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

		void AdoptDevice(ID3D11Device* a_device)
		{
			if (device_ && !SameIdentity(device_.Get(), a_device)) {
				slots_ = {};
				shader_.Reset();
				constants_.Reset();
				capturedCategories_.Reset();
				capturedCategoriesSrv_.Reset();
				capturedDepth_.Reset();
				capturedDepthSrv_.Reset();
				InvalidateCaptureMetadata();
				shaderCompileFailed_ = false;
				heldRegions_ = {};
				heldCropValid_ = {};
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
			InvalidateCaptureMetadata();
			InvalidatePreparedMasks();
			D3D11_TEXTURE2D_DESC captureDesc = a_sourceDesc;
			captureDesc.Usage = D3D11_USAGE_DEFAULT;
			captureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			captureDesc.CPUAccessFlags = 0;
			captureDesc.MiscFlags = 0;
			if (FAILED(a_device->CreateTexture2D(
					&captureDesc, nullptr, &capturedCategories_)) ||
				FAILED(a_device->CreateShaderResourceView(
					capturedCategories_.Get(), nullptr,
					&capturedCategoriesSrv_))) {
				capturedCategories_.Reset();
				capturedCategoriesSrv_.Reset();
				return false;
			}
			Util::SetResourceName(
				capturedCategories_.Get(),
				"DLSS5CharacterRendering::FrozenCategories");
			Util::SetResourceName(
				capturedCategoriesSrv_.Get(),
				"DLSS5CharacterRendering::FrozenCategories SRV");
			return true;
		}

		bool EnsureDepthCapture(
			ID3D11Device* a_device,
			const D3D11_TEXTURE2D_DESC& a_sourceDesc,
			const D3D11_SHADER_RESOURCE_VIEW_DESC& a_sourceViewDesc)
		{
			if (capturedDepth_) {
				D3D11_TEXTURE2D_DESC current{};
				capturedDepth_->GetDesc(&current);
				D3D11_SHADER_RESOURCE_VIEW_DESC currentView{};
				capturedDepthSrv_->GetDesc(&currentView);
				if (current.Width == a_sourceDesc.Width &&
					current.Height == a_sourceDesc.Height &&
					current.Format == a_sourceDesc.Format &&
					currentView.Format == a_sourceViewDesc.Format) {
					return true;
				}
			}

			capturedDepth_.Reset();
			capturedDepthSrv_.Reset();
			InvalidateCaptureMetadata();
			InvalidatePreparedMasks();
			D3D11_TEXTURE2D_DESC captureDesc = a_sourceDesc;
			captureDesc.Usage = D3D11_USAGE_DEFAULT;
			captureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			captureDesc.CPUAccessFlags = 0;
			captureDesc.MiscFlags = 0;
			if (FAILED(a_device->CreateTexture2D(
					&captureDesc, nullptr, &capturedDepth_)) ||
				FAILED(a_device->CreateShaderResourceView(
					capturedDepth_.Get(), &a_sourceViewDesc,
					&capturedDepthSrv_))) {
				capturedDepth_.Reset();
				capturedDepthSrv_.Reset();
				return false;
			}
			Util::SetResourceName(
				capturedDepth_.Get(),
				"DLSS5CharacterRendering::FrozenDepth");
			Util::SetResourceName(
				capturedDepthSrv_.Get(),
				"DLSS5CharacterRendering::FrozenDepth SRV");
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
			if (textureDesc.Width != a_expectedWidth ||
				textureDesc.Height != a_expectedHeight ||
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

		bool ProjectSphere(
			const Observation& a_observation,
			const RE::NiPoint3& a_eye,
			const float4x4& a_matrix,
			std::uint32_t a_width,
			std::uint32_t a_height,
			CharacterRect& a_rect) const
		{
			const float3 relative{
				a_observation.center.x - a_eye.x,
				a_observation.center.y - a_eye.y,
				a_observation.center.z - a_eye.z,
			};
			float minX = std::numeric_limits<float>::max();
			float minY = std::numeric_limits<float>::max();
			float maxX = std::numeric_limits<float>::lowest();
			float maxY = std::numeric_limits<float>::lowest();
			bool projected = false;
			bool crossesNearPlane = false;
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
						if (!std::isfinite(clip.w))
							continue;
						if (clip.w <= 1.0e-4f) {
							crossesNearPlane = true;
							continue;
						}
						const float ndcX = clip.x / clip.w;
						const float ndcY = clip.y / clip.w;
						if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
							continue;
						const float pixelX = (ndcX * 0.5f + 0.5f) * a_width;
						const float pixelY = (0.5f - ndcY * 0.5f) * a_height;
						minX = std::min(minX, pixelX);
						minY = std::min(minY, pixelY);
						maxX = std::max(maxX, pixelX);
						maxY = std::max(maxY, pixelY);
						projected = true;
					}
				}
			}
			if (!projected)
				return false;
			// Eligibility only bounds CSX's exact semantic mask. Conservatively
			// cover the eye when a visible bound crosses the near plane so a close
			// face cannot disappear merely because its projected box is unbounded.
			if (crossesNearPlane) {
				a_rect = { .minX = 0, .minY = 0, .maxX = a_width, .maxY = a_height };
				return true;
			}
			if (maxX <= 0.0f || maxY <= 0.0f ||
				minX >= a_width || minY >= a_height) {
				return false;
			}
			a_rect = {
				.minX = static_cast<std::uint32_t>(std::clamp(
					std::floor(minX), 0.0f, static_cast<float>(a_width))),
				.minY = static_cast<std::uint32_t>(std::clamp(
					std::floor(minY), 0.0f, static_cast<float>(a_height))),
				.maxX = static_cast<std::uint32_t>(std::clamp(
					std::ceil(maxX), 0.0f, static_cast<float>(a_width))),
				.maxY = static_cast<std::uint32_t>(std::clamp(
					std::ceil(maxY), 0.0f, static_cast<float>(a_height))),
			};
			return a_rect.IsValid();
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
			actors.reserve(std::min<std::size_t>(
				observations_.size(),
				CharacterPolicy::kMaximumTrackedActorsPerEye));
			const auto averageEye = Util::GetAverageEyePosition();
			const std::array<RE::NiPoint3, 2> eyePositions{
				Util::GetEyePosition(0),
				Util::GetEyePosition(1),
			};
			const std::array<float4x4, 2> eyeMatrices{
				globals::game::frameBufferCached
					.GetCameraViewProjUnjittered(0)
					.Transpose(),
				globals::game::frameBufferCached
					.GetCameraViewProjUnjittered(1)
					.Transpose(),
			};
			for (const auto& observation : observations_) {
				auto& actor = actors[observation.actorFormId];
				actor.actorFormId = observation.actorFormId;
				const bool isFace = observation.category == CharacterCategory::Face;
				const bool isSelected =
					IsCategoryEnabled(observation.category, a_args.settings);
				if (isFace || isSelected) {
					const float dx = observation.center.x - averageEye.x;
					const float dy = observation.center.y - averageEye.y;
					const float dz = observation.center.z - averageEye.z;
					const float surfaceDistance = std::max(
						0.0f,
						std::sqrt(dx * dx + dy * dy + dz * dz) - observation.radius);
					if (isFace) {
						actor.nearestFaceDistanceUnits = std::min(
							actor.nearestFaceDistanceUnits, surfaceDistance);
					}
					if (isSelected) {
						actor.nearestSelectedDistanceUnits = std::min(
							actor.nearestSelectedDistanceUnits, surfaceDistance);
					}
				}
				if (!isFace && !isSelected)
					continue;
				for (std::uint32_t eye = 0; eye < 2; ++eye) {
					CharacterRect projected{};
					if (!ProjectSphere(
							observation, eyePositions[eye], eyeMatrices[eye],
							key.width, key.height, projected)) {
						continue;
					}
					if (isFace) {
						actor.faceRects[eye] = CharacterRegionPolicy::Union(
							actor.faceRects[eye], projected);
					}
					if (isSelected) {
						actor.selectedRects[eye] = CharacterRegionPolicy::Union(
							actor.selectedRects[eye], projected);
					}
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
			RefreshProjectedActors(a_args);
			auto& held = heldRegions_[a_args.featureSlot];
			if (!heldCropValid_[a_args.featureSlot] ||
				heldCrops_[a_args.featureSlot] != a_args.viewportCrop) {
				held.clear();
				heldCrops_[a_args.featureSlot] = a_args.viewportCrop;
				heldCropValid_[a_args.featureSlot] = true;
			}

			const auto& crop = a_args.viewportCrop.output;
			std::unordered_set<std::uint32_t> currentlyProjected;
			currentlyProjected.reserve(projectedActors_.size());
			for (const auto& actor : projectedActors_) {
				const auto& faceRect = actor.faceRects[a_args.eyeIndex];
				const auto& actorRect = actor.selectedRects[a_args.eyeIndex];
				if (!actorRect.IsValid()) {
					result.projectionUncertain =
						result.projectionUncertain ||
						actor.selectedRects[a_args.eyeIndex ^ 1u].IsValid();
					held.erase(actor.actorFormId);
					continue;
				}

				const float marginX =
					(actorRect.maxX - actorRect.minX) * a_args.settings.roiMargin;
				const float marginY =
					(actorRect.maxY - actorRect.minY) * a_args.settings.roiMargin;
				const auto expandedMinX = static_cast<std::uint32_t>(
					std::max(0.0f, std::floor(actorRect.minX - marginX)));
				const auto expandedMinY = static_cast<std::uint32_t>(
					std::max(0.0f, std::floor(actorRect.minY - marginY)));
				const auto expandedMaxX = static_cast<std::uint32_t>(
					std::min(
						static_cast<float>(a_args.viewportCrop.fullOutput.width),
						std::ceil(actorRect.maxX + marginX)));
				const auto expandedMaxY = static_cast<std::uint32_t>(
					std::min(
						static_cast<float>(a_args.viewportCrop.fullOutput.height),
						std::ceil(actorRect.maxY + marginY)));
				if (expandedMaxX <= crop.left || expandedMaxY <= crop.top ||
					expandedMinX >= crop.right || expandedMinY >= crop.bottom) {
					held.erase(actor.actorFormId);
					continue;
				}

				CharacterRect local{
					.minX = std::max(expandedMinX, crop.left) - crop.left,
					.minY = std::max(expandedMinY, crop.top) - crop.top,
					.maxX = std::min(expandedMaxX, crop.right) - crop.left,
					.maxY = std::min(expandedMaxY, crop.bottom) - crop.top,
				};
				local.minX = (local.minX / kRegionQuantization) * kRegionQuantization;
				local.minY = (local.minY / kRegionQuantization) * kRegionQuantization;
				local.maxX = std::min(
					a_args.outputWidth,
					((local.maxX + kRegionQuantization - 1) / kRegionQuantization) *
						kRegionQuantization);
				local.maxY = std::min(
					a_args.outputHeight,
					((local.maxY + kRegionQuantization - 1) / kRegionQuantization) *
						kRegionQuantization);
				if (!local.IsValid()) {
					held.erase(actor.actorFormId);
					continue;
				}
				currentlyProjected.insert(actor.actorFormId);

				std::uint32_t stereoMaximumSize = 0;
				const bool hasFaceAnchor = faceRect.IsValid();
				const auto& stereoAnchors = hasFaceAnchor ?
				                                actor.faceRects :
				                                actor.selectedRects;
				for (const auto& stereoAnchor : stereoAnchors) {
					if (!stereoAnchor.IsValid())
						continue;
					stereoMaximumSize = std::max(
						stereoMaximumSize,
						std::max(
							stereoAnchor.maxX - stereoAnchor.minX,
							stereoAnchor.maxY - stereoAnchor.minY));
				}
				const float nearestDistanceUnits = hasFaceAnchor ?
				                                       actor.nearestFaceDistanceUnits :
				                                       actor.nearestSelectedDistanceUnits;
				const bool eligible =
					std::isfinite(nearestDistanceUnits) &&
					Util::Units::GameUnitsToMeters(nearestDistanceUnits) <=
						a_args.settings.maximumDistanceMeters &&
					stereoMaximumSize >= a_args.settings.minimumFacePixelSize;
				if (eligible) {
					if (hasFaceAnchor)
						++result.visibleFaces;
					++result.visibleCharacters;
					held[actor.actorFormId] = {
						.rect = local,
						.lastSeenFrame = a_args.frameId,
						.facePriority = hasFaceAnchor ?
						                    faceRect.Area() :
						                    actorRect.Area(),
					};
					continue;
				}

				const auto retained = held.find(actor.actorFormId);
				if (retained == held.end())
					continue;
				if (!CharacterRegionPolicy::IsWithinHoldWindow(
						a_args.frameId, retained->second.lastSeenFrame,
						a_args.settings.roiHoldFrames)) {
					held.erase(retained);
				} else {
					retained->second.rect = local;
					retained->second.facePriority = hasFaceAnchor ?
					                                    faceRect.Area() :
					                                    actorRect.Area();
				}
			}

			for (auto it = held.begin(); it != held.end();) {
				if (!currentlyProjected.contains(it->first))
					it = held.erase(it);
				else
					++it;
			}

			std::vector<PrioritizedCharacterRegion> regions;
			regions.reserve(held.size());
			for (const auto& [actorFormId, region] : held) {
				regions.push_back({
					.rect = region.rect,
					.priority = region.facePriority,
					.stableId = actorFormId,
				});
			}
			if (regions.size() > CharacterPolicy::kMaximumTrackedActorsPerEye) {
				std::ranges::sort(regions, [](const auto& a_left, const auto& a_right) {
					if (a_left.priority != a_right.priority)
						return a_left.priority > a_right.priority;
					return a_left.stableId < a_right.stableId;
				});
				result.droppedCharacters += static_cast<std::uint32_t>(
					regions.size() - CharacterPolicy::kMaximumTrackedActorsPerEye);
				regions.resize(CharacterPolicy::kMaximumTrackedActorsPerEye);
			}
			std::uint64_t eligibilityXor = 0;
			std::uint64_t eligibilitySum = 0;
			for (const auto& region : regions) {
				const auto actorHash = HashCombine(
					1469598103934665603ull, region.stableId);
				eligibilityXor ^= actorHash;
				eligibilitySum += actorHash;
			}
			result.eligibilitySignature = HashCombine(
				HashCombine(eligibilityXor, eligibilitySum), regions.size());
			result.droppedCharacters += CharacterRegionPolicy::MergeAndLimit(
				regions, a_args.settings.maximumRoiRegions);
			result.regions.reserve(regions.size());
			for (const auto& region : regions)
				result.regions.push_back(region.rect);
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

		bool HasFreshZeroAuthoredCoverageSample(
			const Slot& a_slot,
			std::uint64_t a_diagnosticKey,
			std::uint32_t a_frame,
			const CharacterSettings& a_settings) const noexcept
		{
			if (!a_slot.maskCoverageReady ||
				a_slot.maskDiagnosticKey != a_diagnosticKey) {
				return false;
			}
			for (std::uint32_t category = 1; category <= 3; ++category) {
				const auto characterCategory =
					static_cast<CharacterCategory>(category);
				if (IsCategoryEnabled(characterCategory, a_settings) &&
					a_slot.authoredCategoryPixels[category - 1u] != 0) {
					return false;
				}
			}
			const auto age = static_cast<std::int32_t>(
				a_frame - a_slot.maskCoverageFrame);
			return age >= 0 &&
			       static_cast<std::uint32_t>(age) <=
			           CharacterPolicy::kZeroCoverageReuseFrames;
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
					const bool sampleIsCurrent =
						!slot.maskCoverageReady ||
						readback.serial >= slot.maskCoverageSerial;
					if (readback.featureSlot == slotIndex && sampleIsCurrent) {
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
						slot.maskDiagnosticKey = readback.diagnosticKey;
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
				!samplePending &&
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
			constants.options[1] = static_cast<std::uint32_t>(a_plan.regions.size());
			constants.options[2] = a_args.settings.depthAwareFeather ?
			                           a_args.settings.featherRadius :
			                           0;
			constants.options[3] = a_args.settings.depthAwareFeather ? 1u : 0u;
			constants.featherOptions[0] = a_args.settings.featherDepthThreshold;
			constants.featherOptions[1] = static_cast<float>(
				a_args.settings.maskTestMode);
			const bool measureCoverage = coverageReadback != nullptr;
			constants.featherOptions[2] = measureCoverage ? 1.0f : 0.0f;
			constants.visibilityOptions[0] = kVisibilityDepthThreshold;
			constants.visibilityOptions[1] =
				a_args.settings.visibilityDepthTest &&
						a_args.settings.maskTestMode !=
							CharacterMaskTestMode::AuthoredWithoutVisibilityDepth ?
					1.0f :
					0.0f;
			const auto cameraData = Util::GetCameraData();
			constants.depthLinearization[0] = cameraData.x;
			constants.depthLinearization[1] = cameraData.y;
			constants.depthLinearization[2] = cameraData.z;
			constants.depthLinearization[3] = cameraData.w;
			constants.jitter[0] = capturedJitterX_;
			constants.jitter[1] = capturedJitterY_;
			constants.categoryStrengths[0] =
				a_args.settings.faces ? a_args.settings.faceStrength : 0.0f;
			constants.categoryStrengths[1] =
				a_args.settings.skin ? a_args.settings.skinStrength : 0.0f;
			constants.categoryStrengths[2] =
				a_args.settings.hair ? a_args.settings.hairStrength : 0.0f;
			for (std::size_t index = 0; index < a_plan.regions.size(); ++index) {
				const auto& rect = a_plan.regions[index];
				constants.roiRectangles[index][0] = static_cast<float>(rect.minX);
				constants.roiRectangles[index][1] = static_cast<float>(rect.minY);
				constants.roiRectangles[index][2] = static_cast<float>(rect.maxX);
				constants.roiRectangles[index][3] = static_cast<float>(rect.maxY);
			}
			a_args.context->UpdateSubresource(
				constants_.Get(), 0, nullptr, &constants, 0, 0);

			ComputeStateGuard stateGuard(a_args.context);
			if (!stateGuard.Captured())
				return false;
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
				const auto dispatchWidth = std::max(
					a_args.outputWidth, a_args.viewportCrop.input.Width());
				const auto dispatchHeight = std::max(
					a_args.outputHeight, a_args.viewportCrop.input.Height());
				a_args.context->Dispatch(
					(dispatchWidth + 7u) / 8u,
					(dispatchHeight + 7u) / 8u,
					1);
			}

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
				coverageReadback->serial = AllocateCoverageSerial();
				coverageReadback->pending = true;
				a_slot.lastCoverageRequestPolicyKey = samplingPolicyKey;
				a_slot.lastCoverageRequestFrame = a_args.frameId;
				a_slot.coverageRequestIssued = true;
			}
			return true;
		}

		mutable std::mutex mutex_;
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
		std::uint32_t observationFrame_ = std::numeric_limits<std::uint32_t>::max();
		ProjectionKey projectionKey_{};
		std::vector<ProjectedActor> projectedActors_;
		bool projectionCacheValid_ = false;
		std::array<std::map<std::uint32_t, HeldRegion>, 4> heldRegions_{};
		std::array<UpscalingDLSS::ViewportCrop, 4> heldCrops_{};
		std::array<bool, 4> heldCropValid_{};
		std::array<Slot, 4> slots_{};
		std::array<std::uint32_t, 2> lastSlotForEye_{ 4, 4 };
		std::uint32_t preparedFrameHistoryNext_ = 0;
		ComPtr<ID3D11ComputeShader> shader_;
		ComPtr<ID3D11Buffer> constants_;
		ComPtr<ID3D11Texture2D> capturedCategories_;
		ComPtr<ID3D11ShaderResourceView> capturedCategoriesSrv_;
		ComPtr<ID3D11Texture2D> capturedDepth_;
		ComPtr<ID3D11ShaderResourceView> capturedDepthSrv_;
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
		ComPtr<ID3D11Device> device_;
		bool shaderCompileFailed_ = false;
		CharacterSnapshot snapshot_{};
	};

	CharacterRendering& CharacterRendering::Instance()
	{
		static CharacterRendering instance;
		return instance;
	}

	CharacterRendering::CharacterRendering() : state_(std::make_unique<State>()) {}

	CharacterRendering::~CharacterRendering() = default;

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
			state_->BeginObservationFrame(a_frame);
			if (state_->observationKeys_.contains(a_geometryIdentity))
				return true;
			if (state_->observations_.size() >=
				CharacterPolicy::kMaximumObservationsPerFrame) {
				Increment(state_->snapshot_.observationCapacityDrops);
				return false;
			}
			state_->observationKeys_.insert(a_geometryIdentity);
			state_->observations_.push_back({
				.actorFormId = a_actorFormId,
				.geometryIdentity = a_geometryIdentity,
				.category = a_category,
				.center = { a_centerX, a_centerY, a_centerZ },
				.radius = a_radius,
			});
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
			if (state_->device_ && SameIdentity(a_device, state_->device_.Get()) &&
				state_->snapshot_.categoryCaptureReady &&
				state_->capturedFrame_ == a_frame &&
				state_->capturedEyeWidth_ == a_sourceEyeWidth &&
				state_->capturedHeight_ == a_sourceHeight &&
				state_->capturedEnabledCategoryMask_ == a_enabledCategoryMask &&
				state_->capturedJitterX_ == a_jitterX &&
				state_->capturedJitterY_ == a_jitterY) {
				Increment(state_->snapshot_.categoryCaptureReuses);
				return true;
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
			state_->InvalidatePreparedMasks();
			state_->BeginObservationFrame(a_frame);
			const bool hasSelectedObservation =
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
			if (categoryDesc.Format != DXGI_FORMAT_R8G8_UNORM ||
				categoryDesc.MipLevels != 1 || categoryDesc.ArraySize != 1 ||
				categoryDesc.SampleDesc.Count != 1 ||
				categoryDesc.Usage != D3D11_USAGE_DEFAULT ||
				(categoryDesc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
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
			if (!state_->EnsureDepthCapture(
					a_device, depthCaptureDesc, depthViewDesc)) {
				return fail("character depth capture texture could not be created");
			}
			OutputMergerStateGuard outputMerger(a_context);
			if (!outputMerger.Captured())
				return fail("character category capture could not preserve output state");
			{
				CS_PROFILE_SCOPE("Upscaling::DLSS5CharacterCategoryCapture");
				const D3D11_BOX activeStereoBox{
					0u, 0u, 0u,
					static_cast<UINT>(activeStereoWidth), a_sourceHeight, 1u
				};
				a_context->CopySubresourceRegion(
					state_->capturedCategories_.Get(), 0, 0, 0, 0,
					a_categorySource, 0, &activeStereoBox);
				a_context->CopySubresourceRegion(
					state_->capturedDepth_.Get(), 0, 0, 0, 0,
					depthTexture.Get(), 0, &activeStereoBox);
			}
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
		try {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.preparationAttempts);
			state_->snapshot_.enabled = a_args.settings.enabled;
			const auto fail = [&](std::string a_detail) {
				Increment(state_->snapshot_.preparationFailures);
				state_->InvalidatePreparedSlot(
					a_args.featureSlot, a_args.eyeIndex, a_args.frameId);
				state_->snapshot_.status = "failed";
				state_->snapshot_.detail = std::move(a_detail);
				return false;
			};

			if (!a_args.settings.enabled)
				return fail("character mask preparation was requested while disabled");
			if (!a_args.device || !a_args.context || a_args.eyeIndex >= 2 ||
				a_args.featureSlot >= state_->slots_.size() ||
				(a_args.featureSlot & 1u) != a_args.eyeIndex ||
				a_args.frameId == std::numeric_limits<std::uint32_t>::max() ||
				!a_args.outputWidth || !a_args.outputHeight ||
				!a_args.viewportCrop.MatchesEvaluationExtents(
					a_args.viewportCrop.input.Width(),
					a_args.viewportCrop.input.Height(),
					a_args.outputWidth,
					a_args.outputHeight) ||
				!IsFiniteSettings(a_args.settings)) {
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
			const bool logicalEmptyCapture = state_->capturedCategoriesEmpty_;
			if (state_->capturedFrame_ != a_args.frameId ||
				state_->capturedEyeWidth_ != a_args.viewportCrop.fullInput.width ||
				state_->capturedHeight_ != a_args.viewportCrop.fullInput.height ||
				(!logicalEmptyCapture &&
					(!state_->capturedCategoriesSrv_ ||
						!state_->capturedDepthSrv_))) {
				return fail("no same-frame character capture with matching logical stereo dimensions is available");
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
						DXGI_FORMAT_R8G8_UNORM,
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
						currentDepthTexture, currentDepthIdentity,
						depthError)) {
					return fail(std::move(depthError));
				}
			}

			auto& slot = state_->slots_[a_args.featureSlot];
			if (!state_->EnsureSlot(
					slot, a_args.device, a_args.featureSlot,
					a_args.outputWidth, a_args.outputHeight)) {
				return fail("DLSS5 character-mask GPU resources could not be created");
			}

			const State::PrepareKey key{
				.frame = a_args.frameId,
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
			if (!slot.prepared || slot.prepareKey != key) {
				auto plan = state_->BuildPlan(a_args);
				const bool authoredMode =
					UsesAuthoredMask(a_args.settings.maskTestMode);
				if (authoredMode && !logicalEmptyCapture &&
					plan.regions.empty() && plan.projectionUncertain) {
					// Projection is an optimization boundary, not semantic proof. Fall
					// back to an exact full-eye semantic resolve when actor bounds are
					// unavailable so authored face/skin pixels cannot silently vanish.
					plan.regions.push_back({
						.minX = 0,
						.minY = 0,
						.maxX = a_args.outputWidth,
						.maxY = a_args.outputHeight,
					});
					plan.fullEyeEligibilityFallback = true;
					plan.eligibilitySignature = HashCombine(
						plan.eligibilitySignature, 0x46554C4C455945ull);
				}
				const auto diagnosticKey = logicalEmptyCapture ?
				                               0u :
				                               state_->BuildDiagnosticKey(
												   a_args, plan, sourceEyeWidth,
												   sourceDesc.Height);
				const bool cpuProvenEmpty =
					authoredMode &&
					(logicalEmptyCapture || plan.regions.empty());
				slot.requiresEvaluation = !cpuProvenEmpty;
				slot.zeroCoverageBypassResolved = false;
				slot.zeroCoverageBypassed = false;
				slot.feature18Disposition =
					CharacterFeature18Disposition::Unresolved;
				slot.zeroCoverageCpuProven = false;
				slot.zeroCoverageSampleReused = false;
				if (logicalEmptyCapture) {
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
						slot, a_args.context, a_args.frameId,
						a_args.featureSlot, a_args.outputWidth,
						a_args.outputHeight, clearValue);
				} else {
					if (!state_->EnsureShader(a_args.device)) {
						return fail(
							"DLSS5 character-mask compute shader could not be created");
					}
					if (!state_->Dispatch(
							a_args,
							state_->capturedCategoriesSrv_.Get(),
							state_->capturedDepthSrv_.Get(),
							slot, plan,
							sourceEyeWidth, sourceDesc.Height)) {
						return fail("DLSS5 character-mask dispatch failed");
					}
				}
				const bool measuredZero =
					authoredMode && !cpuProvenEmpty &&
					state_->HasFreshZeroAuthoredCoverageSample(
						slot, diagnosticKey, a_args.frameId,
						a_args.settings);
				slot.requiresEvaluation =
					!authoredMode || (!cpuProvenEmpty && !measuredZero);
				slot.zeroCoverageBypassed = false;
				slot.zeroCoverageCpuProven = cpuProvenEmpty;
				slot.zeroCoverageSampleReused = measuredZero;
				if (cpuProvenEmpty)
					Increment(state_->snapshot_.provenEmptyFeatureBypassRequests);
				else if (measuredZero)
					Increment(state_->snapshot_.measuredZeroCoverageBypassRequests);
				slot.prepareKey = key;
				slot.prepared = true;

				auto& eye = state_->snapshot_.eyes[a_args.eyeIndex];
				eye.frame = a_args.frameId;
				eye.featureSlot = a_args.featureSlot;
				eye.evaluationWidth = a_args.outputWidth;
				eye.evaluationHeight = a_args.outputHeight;
				eye.visibleFaces = plan.visibleFaces;
				eye.visibleCharacterRegions = plan.visibleCharacters;
				eye.droppedCharacterRegions = plan.droppedCharacters;
				eye.mergedRegions = static_cast<std::uint32_t>(plan.regions.size());
				eye.regions = plan.regions;
				eye.roiPixels = 0;
				for (const auto& rect : plan.regions)
					eye.roiPixels += rect.Area();
				const auto evaluationPixels =
					static_cast<std::uint64_t>(a_args.outputWidth) * a_args.outputHeight;
				eye.roiCoveragePercent = evaluationPixels ?
				                             100.0f * static_cast<float>(eye.roiPixels) /
				                                 static_cast<float>(evaluationPixels) :
				                             0.0f;
				eye.maskPixels = slot.maskPixels;
				eye.authoredCategoryPixels = slot.authoredCategoryPixels;
				eye.visibleCategoryPixels = slot.visibleCategoryPixels;
				eye.visibilityRejectedPixels = slot.visibilityRejectedPixels;
				eye.maskCoveragePercent = slot.maskCoveragePercent;
				eye.maskCoverageFrame = slot.maskCoverageFrame;
				eye.maskCoverageFeatureSlot = slot.maskCoverageFeatureSlot;
				eye.maskCoverageWidth = slot.maskCoverageWidth;
				eye.maskCoverageHeight = slot.maskCoverageHeight;
				eye.maskCoverageReady = slot.maskCoverageReady;
				eye.maskCoverageMatchesCurrentPolicy =
					slot.maskCoverageReady &&
					slot.maskDiagnosticKey == diagnosticKey;
				eye.zeroCoverageBypassRequested =
					!slot.requiresEvaluation;
				eye.zeroCoverageBypassResolved = false;
				eye.zeroCoverageBypassed = slot.zeroCoverageBypassed;
				eye.feature18Disposition =
					CharacterFeature18Disposition::Unresolved;
				eye.feature18EvaluationSucceeded = false;
				eye.zeroCoverageCpuProven = slot.zeroCoverageCpuProven;
				eye.zeroCoverageSampleReused = slot.zeroCoverageSampleReused;
				eye.fullEyeEligibilityFallback =
					plan.fullEyeEligibilityFallback;
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
					a_args.frameId, a_args.featureSlot,
					a_args.outputWidth, a_args.outputHeight,
					slot.requiresEvaluation);
			}

			a_result.prepared = true;
			a_result.requiresEvaluation = slot.requiresEvaluation;
			Increment(state_->snapshot_.preparationSuccesses);
			state_->snapshot_.status = "ready";
			state_->snapshot_.detail = std::format(
				"CSX character selection mask prepared for eye {} slot {} at {}x{}; evaluation={}; private single-subrect ROI remains disabled",
				a_args.eyeIndex,
				a_args.featureSlot,
				a_args.outputWidth,
				a_args.outputHeight,
				slot.requiresEvaluation ? "required" : "bypassed-empty");
			return true;
		} catch (const std::exception& exception) {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.preparationFailures);
			state_->InvalidatePreparedSlot(
				a_args.featureSlot, a_args.eyeIndex, a_args.frameId);
			state_->snapshot_.status = "failed";
			state_->snapshot_.detail = exception.what();
			return false;
		} catch (...) {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.preparationFailures);
			state_->InvalidatePreparedSlot(
				a_args.featureSlot, a_args.eyeIndex, a_args.frameId);
			state_->snapshot_.status = "failed";
			state_->snapshot_.detail = "unknown character-mask preparation exception";
			return false;
		}
	}

	void CharacterRendering::ResolveFeature18Disposition(
		std::uint32_t a_frameId,
		std::uint32_t a_preparedFeatureSlotMask,
		std::uint32_t a_evaluatedFeatureSlotMask,
		std::uint32_t a_successfulFeatureSlotMask,
		std::uint32_t a_bypassedFeatureSlotMask) noexcept
	{
		if (!state_)
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
				if (!slot.prepared || slot.prepareKey.frame != a_frameId) {
					continue;
				}
				const bool bypassRequested =
					!slot.requiresEvaluation &&
					(slot.zeroCoverageCpuProven ||
						slot.zeroCoverageSampleReused);
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
					if (slot.zeroCoverageCpuProven)
						Increment(state_->snapshot_.provenEmptyFeatureBypasses);
					else
						Increment(state_->snapshot_.measuredZeroCoverageBypasses);
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
			state_->observations_.clear();
			state_->observationKeys_.clear();
			state_->observationFrame_ = std::numeric_limits<std::uint32_t>::max();
			state_->InvalidateProjectionCache();
			state_->heldRegions_ = {};
			state_->heldCropValid_ = {};
			state_->slots_ = {};
			state_->shader_.Reset();
			state_->constants_.Reset();
			state_->shaderCompileFailed_ = false;
			state_->capturedCategories_.Reset();
			state_->capturedCategoriesSrv_.Reset();
			state_->capturedDepth_.Reset();
			state_->capturedDepthSrv_.Reset();
			state_->InvalidateCaptureMetadata();
			state_->device_.Reset();
			state_->lastSlotForEye_ = { 4, 4 };
			state_->preparedFrameHistoryNext_ = 0;
			state_->snapshot_ = {};
			state_->ResetClassificationRejections();
		} catch (...) {
		}
	}

	void CharacterRendering::Invalidate() noexcept
	{
		if (!state_)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->heldRegions_ = {};
			state_->heldCropValid_ = {};
			state_->InvalidateProjectionCache();
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
			state_->shaderCompileFailed_ = false;
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
		std::uint32_t a_width,
		std::uint32_t a_height) const noexcept
	{
		if (!state_ || a_featureSlot >= state_->slots_.size())
			return {};
		try {
			std::scoped_lock lock(state_->mutex_);
			const auto& slot = state_->slots_[a_featureSlot];
			return slot.prepared &&
			               slot.prepareKey.frame == a_frameId &&
			               slot.width == a_width &&
			               slot.height == a_height ?
			           slot.maskSrv :
			           ComPtr<ID3D11ShaderResourceView>{};
		} catch (...) {
			return {};
		}
	}
}
