#include "Renderer.h"

#include "D3D12Interop.h"
#include "PipelinePolicy.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <wrl/client.h>

namespace NeuralRendering
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		constexpr std::uint32_t kMaximumTextureDimension =
			D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
		constexpr std::size_t kMaximumTransitionResourceCount = 10;

		void Increment(std::uint64_t& a_counter) noexcept
		{
			if (a_counter != std::numeric_limits<std::uint64_t>::max())
				++a_counter;
		}

		void Add(std::uint64_t& a_counter, std::uint64_t a_value) noexcept
		{
			if (a_value > std::numeric_limits<std::uint64_t>::max() - a_counter)
				a_counter = std::numeric_limits<std::uint64_t>::max();
			else
				a_counter += a_value;
		}

		void RecordCpuDuration(
			std::uint64_t& a_samples,
			std::uint64_t& a_totalMicroseconds,
			std::uint64_t& a_lastMicroseconds,
			std::uint64_t& a_maximumMicroseconds,
			std::chrono::steady_clock::time_point a_started) noexcept
		{
			const auto elapsed = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - a_started)
					.count());
			Increment(a_samples);
			Add(a_totalMicroseconds, elapsed);
			a_lastMicroseconds = elapsed;
			a_maximumMicroseconds = std::max(a_maximumMicroseconds, elapsed);
		}

		template <class Callback>
		void LogOnce(bool& a_logged, Callback&& a_callback) noexcept
		{
			if (a_logged)
				return;
			a_logged = true;
			try {
				std::forward<Callback>(a_callback)();
			} catch (...) {
				// Diagnostics must never change renderer success or fallback behavior.
			}
		}

		bool IsDeviceLossResult(HRESULT a_result) noexcept
		{
			return a_result == DXGI_ERROR_DEVICE_REMOVED ||
			       a_result == DXGI_ERROR_DEVICE_RESET ||
			       a_result == DXGI_ERROR_DEVICE_HUNG ||
			       a_result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		bool IsDeviceLossReason(HRESULT a_result) noexcept
		{
			return IsDeviceLossResult(a_result) ||
			       a_result == DXGI_ERROR_INVALID_CALL;
		}

		bool SameIdentity(IUnknown* a_left, IUnknown* a_right) noexcept
		{
			if (!a_left || !a_right)
				return false;

			ComPtr<IUnknown> leftIdentity;
			ComPtr<IUnknown> rightIdentity;
			return SUCCEEDED(a_left->QueryInterface(IID_PPV_ARGS(&leftIdentity))) &&
			       SUCCEEDED(a_right->QueryInterface(IID_PPV_ARGS(&rightIdentity))) &&
			       leftIdentity.Get() == rightIdentity.Get();
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

		std::string GetStereoPairContractViolation(
			const std::array<RendererApplyArgs, 2>& a_args)
		{
			const auto& left = a_args[0];
			const auto& right = a_args[1];
			const std::array<ID3D11Resource*, 5> leftResources{
				left.colorInput, left.depthGuide, left.motionVectors, left.colorOutput,
				left.controlMask.Get()
			};
			const std::array<ID3D11Resource*, 5> rightResources{
				right.colorInput, right.depthGuide, right.motionVectors, right.colorOutput,
				right.controlMask.Get()
			};
			const bool resourcesOverlap = std::ranges::any_of(
				leftResources,
				[&](ID3D11Resource* a_leftResource) {
					return std::ranges::any_of(
						rightResources,
						[&](ID3D11Resource* a_rightResource) {
							return SameIdentity(a_leftResource, a_rightResource);
						});
				});
			const bool tuningMatches =
				left.tuning.intensity == right.tuning.intensity &&
				left.tuning.localToneStrength == right.tuning.localToneStrength &&
				left.tuning.localStructureStrength == right.tuning.localStructureStrength &&
				left.tuning.skinStructureStrength == right.tuning.skinStructureStrength &&
				left.tuning.style == right.tuning.style &&
				left.tuning.useAutoMask == right.tuning.useAutoMask &&
				left.tuning.uiCorrection == right.tuning.uiCorrection;
			if (SameIdentity(left.device, right.device) &&
				SameIdentity(left.context, right.context) &&
				left.frameId == right.frameId &&
				left.generation == right.generation &&
				left.insertionPoint == right.insertionPoint &&
				left.featureUpscaling == right.featureUpscaling &&
				left.reset == right.reset &&
				left.synchronizedHistoryReset == right.synchronizedHistoryReset &&
				left.synchronizedHistoryDiscontinuity ==
					right.synchronizedHistoryDiscontinuity &&
				tuningMatches &&
				IsOrderedStereoFeatureSlotPair(left.featureSlot, right.featureSlot) &&
				!resourcesOverlap) {
				return {};
			}

			return "stereo eyes require one device, context, frame, generation, insertion point, feature mode, reset policy, tuning, ordered route pair, and disjoint resources";
		}

		bool IsFiniteTuning(const Tuning& a_tuning) noexcept
		{
			const auto validStrength = [](float a_value) {
				return std::isfinite(a_value) && a_value >= 0.0f && a_value <= 2.0f;
			};
			return validStrength(a_tuning.intensity) &&
			       validStrength(a_tuning.localToneStrength) &&
			       validStrength(a_tuning.localStructureStrength) &&
			       validStrength(a_tuning.skinStructureStrength) &&
			       a_tuning.style <= 3;
		}

		bool IsMotionVectorFormat(DXGI_FORMAT a_format) noexcept
		{
			return a_format == DXGI_FORMAT_R16G16_FLOAT ||
			       a_format == DXGI_FORMAT_R32G32_FLOAT;
		}

		bool SupportsD3D11Format(
			ID3D11Device* a_device,
			DXGI_FORMAT a_format,
			UINT a_requiredSupport) noexcept
		{
			UINT support = 0;
			return a_device && a_format != DXGI_FORMAT_UNKNOWN &&
			       SUCCEEDED(a_device->CheckFormatSupport(a_format, &support)) &&
			       (support & a_requiredSupport) == a_requiredSupport;
		}

		bool SupportsD3D11SharedFormat(
			ID3D11Device* a_device,
			DXGI_FORMAT a_format) noexcept
		{
			if (!a_device || a_format == DXGI_FORMAT_UNKNOWN)
				return false;

			D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{ .InFormat = a_format };
			return SUCCEEDED(a_device->CheckFeatureSupport(
					   D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support))) &&
			       (support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_SHAREABLE) != 0;
		}

		bool SupportsD3D12Format(
			ID3D12Device* a_device,
			DXGI_FORMAT a_format,
			D3D12_FORMAT_SUPPORT1 a_requiredSupport1,
			D3D12_FORMAT_SUPPORT2 a_requiredSupport2) noexcept
		{
			if (!a_device || a_format == DXGI_FORMAT_UNKNOWN)
				return false;

			D3D12_FEATURE_DATA_FORMAT_SUPPORT support{ .Format = a_format };
			return SUCCEEDED(a_device->CheckFeatureSupport(
					   D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
			       (support.Support1 & a_requiredSupport1) == a_requiredSupport1 &&
			       (support.Support2 & a_requiredSupport2) == a_requiredSupport2;
		}

		struct TextureInfo
		{
			ComPtr<ID3D11Texture2D> texture;
			D3D11_TEXTURE2D_DESC desc{};
		};

		bool GetTextureInfo(ID3D11Resource* a_resource, TextureInfo& a_info) noexcept
		{
			a_info = {};
			if (!a_resource || FAILED(a_resource->QueryInterface(IID_PPV_ARGS(&a_info.texture))))
				return false;
			a_info.texture->GetDesc(&a_info.desc);
			return true;
		}

		bool HasExactTextureContract(
			const D3D11_TEXTURE2D_DESC& a_desc,
			std::uint32_t a_width,
			std::uint32_t a_height) noexcept
		{
			return a_desc.Width == a_width &&
			       a_desc.Height == a_height &&
			       a_desc.MipLevels == 1 &&
			       a_desc.ArraySize == 1 &&
			       a_desc.SampleDesc.Count == 1 &&
			       a_desc.SampleDesc.Quality == 0 &&
			       a_desc.Usage == D3D11_USAGE_DEFAULT &&
			       a_desc.CPUAccessFlags == 0;
		}

		D3D11_TEXTURE2D_DESC MakeSharedDescription(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format,
			bool a_shaderResource)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = a_format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS |
			                 (a_shaderResource ? D3D11_BIND_SHADER_RESOURCE : 0u);
			return desc;
		}

		void Abandon(SharedTexture& a_texture) noexcept
		{
			(void)a_texture.resource11.Detach();
			(void)a_texture.srv11.Detach();
			(void)a_texture.uav11.Detach();
			(void)a_texture.resource12.Detach();
			a_texture.desc = {};
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
				context_->CSGetShader(
					&shader_, classInstances_.data(), &classInstanceCount_);
				context_->CSGetShaderResources(0, 1, &shaderResource_);
				context_->CSGetUnorderedAccessViews(0, 1, &unorderedAccess_);
				captured_ = true;
			}

			ComputeStateGuard(const ComputeStateGuard&) = delete;
			ComputeStateGuard& operator=(const ComputeStateGuard&) = delete;

			~ComputeStateGuard() noexcept
			{
				if (captured_) {
					ID3D11ShaderResourceView* nullShaderResource = nullptr;
					ID3D11UnorderedAccessView* nullUnorderedAccess = nullptr;
					context_->CSSetShaderResources(0, 1, &nullShaderResource);
					context_->CSSetUnorderedAccessViews(0, 1, &nullUnorderedAccess, nullptr);
					context_->CSSetShader(
						shader_, classInstances_.data(), classInstanceCount_);
					context_->CSSetShaderResources(0, 1, &shaderResource_);
					context_->CSSetUnorderedAccessViews(0, 1, &unorderedAccess_, nullptr);
				}

				if (shader_)
					shader_->Release();
				for (UINT index = 0; index < classInstanceCount_; ++index) {
					if (classInstances_[index])
						classInstances_[index]->Release();
				}
				if (shaderResource_)
					shaderResource_->Release();
				if (unorderedAccess_)
					unorderedAccess_->Release();
			}

			[[nodiscard]] bool Captured() const noexcept { return captured_; }

		private:
			ID3D11DeviceContext* context_ = nullptr;
			ID3D11ComputeShader* shader_ = nullptr;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classInstances_{};
			UINT classInstanceCount_ = 0;
			ID3D11ShaderResourceView* shaderResource_ = nullptr;
			ID3D11UnorderedAccessView* unorderedAccess_ = nullptr;
			bool captured_ = false;
		};

		struct RecordingGuard
		{
			explicit RecordingGuard(D3D12Interop& a_interop) : interop(a_interop) {}
			~RecordingGuard() noexcept
			{
				if (!active)
					return;
				try {
					(void)interop.AbortD3D12();
				} catch (...) {
					// The outer renderer boundary quarantines unexpected unwind paths.
				}
			}

			D3D12Interop& interop;
			bool active = true;
		};

		struct FeatureResourceTransition
		{
			ID3D12Resource* resource = nullptr;
			D3D12_RESOURCE_STATES featureState =
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		};

		void TransitionResources(
			ID3D12GraphicsCommandList* a_commandList,
			std::span<const FeatureResourceTransition> a_resources,
			bool a_toFeature)
		{
			std::array<D3D12_RESOURCE_BARRIER, kMaximumTransitionResourceCount> barriers{};
			for (std::size_t index = 0; index < a_resources.size(); ++index) {
				const auto& resource = a_resources[index];
				auto& barrier = barriers[index];
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = resource.resource;
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = a_toFeature ?
				                                     D3D12_RESOURCE_STATE_COMMON :
				                                     resource.featureState;
				barrier.Transition.StateAfter = a_toFeature ?
				                                    resource.featureState :
				                                    D3D12_RESOURCE_STATE_COMMON;
			}
			a_commandList->ResourceBarrier(
				static_cast<UINT>(a_resources.size()), barriers.data());
		}
	}

	const char* ToString(RendererStage a_stage)
	{
		switch (a_stage) {
		case RendererStage::None:
			return "none";
		case RendererStage::Validation:
			return "validation";
		case RendererStage::FailureLatched:
			return "failure_latched";
		case RendererStage::DeviceCompatibility:
			return "device_compatibility";
		case RendererStage::InteropInitialization:
			return "interop_initialization";
		case RendererStage::RuntimeProbe:
			return "runtime_probe";
		case RendererStage::RuntimeInitialization:
			return "runtime_initialization";
		case RendererStage::ResourceRetirement:
			return "resource_retirement";
		case RendererStage::ResourceCreation:
			return "resource_creation";
		case RendererStage::ColorInputCopy:
			return "color_input_copy";
		case RendererStage::DepthGuideCopy:
			return "depth_guide_copy";
		case RendererStage::MotionVectorCopy:
			return "motion_vector_copy";
		case RendererStage::ControlMaskCopy:
			return "control_mask_copy";
		case RendererStage::CommandBegin:
			return "command_begin";
		case RendererStage::FeatureEvaluate:
			return "feature_evaluate";
		case RendererStage::CommandEnd:
			return "command_end";
		case RendererStage::OutputCommit:
			return "output_commit";
		case RendererStage::ResetWait:
			return "reset_wait";
		case RendererStage::RuntimeReset:
			return "runtime_reset";
		case RendererStage::InteropShutdown:
			return "interop_shutdown";
		case RendererStage::DeviceRemoved:
			return "device_removed";
		case RendererStage::Quarantined:
			return "quarantined";
		case RendererStage::Complete:
			return "complete";
		default:
			return "unknown";
		}
	}

	class Renderer::State
	{
	public:
		friend class Renderer;

		struct ResourceKey
		{
			std::uint32_t colorWidth = 0;
			std::uint32_t colorHeight = 0;
			std::uint32_t guideWidth = 0;
			std::uint32_t guideHeight = 0;
			std::uint32_t outputWidth = 0;
			std::uint32_t outputHeight = 0;
			std::uint32_t controlMaskWidth = 0;
			std::uint32_t controlMaskHeight = 0;
			DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
			DXGI_FORMAT motionFormat = DXGI_FORMAT_UNKNOWN;
			DXGI_FORMAT outputFormat = DXGI_FORMAT_UNKNOWN;
			DXGI_FORMAT controlMaskFormat = DXGI_FORMAT_UNKNOWN;
			bool controlMaskPresent = false;

			bool operator==(const ResourceKey&) const = default;
		};

		struct HistoryKey
		{
			ResourceKey resources{};
			std::uint64_t generation = 0;
			InsertionPoint insertionPoint = kDefaultInsertionPoint;
			UpscalingDLSS::ViewportCrop viewportCrop{};
			DXGI_FORMAT depthSourceFormat = DXGI_FORMAT_UNKNOWN;
			DXGI_FORMAT depthViewFormat = DXGI_FORMAT_UNKNOWN;
			std::uint32_t intensity = 0;
			std::uint32_t localToneStrength = 0;
			std::uint32_t localStructureStrength = 0;
			std::uint32_t skinStructureStrength = 0;
			std::uint32_t style = 0;
			std::uintptr_t controlMaskIdentity = 0;
			bool featureUpscaling = false;
			bool useAutoMask = false;
			bool uiCorrection = false;

			bool operator==(const HistoryKey&) const = default;
		};

		struct Slot
		{
			SharedTexture color;
			SharedTexture depth;
			SharedTexture motionVectors;
			SharedTexture controlMask;
			SharedTexture output;
			ResourceKey resourceKey{};
			HistoryKey historyKey{};
			std::uint32_t lastSuccessfulFrame =
				std::numeric_limits<std::uint32_t>::max();
			bool resourcesValid = false;
			bool historyValid = false;
		};

		struct ValidatedResources
		{
			TextureInfo color;
			TextureInfo depth;
			TextureInfo motionVectors;
			TextureInfo controlMask;
			TextureInfo output;
			std::uintptr_t controlMaskIdentity = 0;
			DXGI_FORMAT depthViewFormat = DXGI_FORMAT_UNKNOWN;
			ResourceKey resourceKey{};
			HistoryKey historyKey{};
		};

		struct ValidationFailure
		{
			HRESULT result = S_OK;
			std::string detail;

			explicit operator bool() const noexcept { return FAILED(result); }
		};

		bool ApplyLocked(
			const RendererApplyArgs& a_args,
			RendererApplyOutcome& a_outcome);
		bool ApplyStereoLocked(
			const std::array<RendererApplyArgs, 2>& a_args,
			RendererApplyOutcome& a_outcome);
		bool ApplySequentialStereoLocked(
			const std::array<RendererApplyArgs, 2>& a_args,
			RendererApplyOutcome& a_outcome);
		bool ApplyBatchLocked(
			std::span<const RendererApplyArgs> a_args,
			RendererApplyOutcome& a_outcome);
		bool ResetLocked(bool a_resetShader, bool a_destruction);
		void ShutdownForDestruction() noexcept;

		RendererSnapshot SnapshotLocked()
		{
			RefreshInteropTelemetryLocked();
			return snapshot_;
		}
		bool IsFailureLatchedLocked() const noexcept { return failureLatched_; }
		bool IsQuarantinedLocked() const noexcept { return quarantined_; }

		mutable std::mutex mutex_;

	private:
		ValidationFailure ValidateLocked(
			const RendererApplyArgs& a_args,
			ValidatedResources& a_resources) const;
		bool ValidateD3D12FormatsLocked(
			const ValidatedResources& a_resources,
			std::string& a_detail) const;
		bool EnsureBackendLocked(const RendererApplyArgs& a_args);
		bool EnsureSlotLocked(
			std::uint32_t a_slot,
			const ValidatedResources& a_resources);
		bool CopyDepthBatchLocked(
			std::span<const RendererApplyArgs> a_args,
			std::span<Slot* const> a_slots);
		bool TeardownBackendLocked(
			bool a_resetShader,
			bool a_destruction,
			bool a_countApplyFailure);
		void AbandonRuntimeOwnershipNoexcept() noexcept;
		void AbandonSlotsLocked() noexcept;
		void QuarantineAfterUnexpectedFailureLocked(
			RendererStage a_stage,
			std::uint32_t a_slot,
			bool a_applyFailure,
			std::uint64_t a_failuresBefore) noexcept;
		void RefreshRuntimeTelemetryLocked();
		void RefreshInteropTelemetryLocked();
		void SetRequestTelemetryLocked(const RendererApplyArgs& a_args) noexcept;
		void SetActiveFeatureSlotLocked(std::uint32_t a_slot) noexcept;
		[[nodiscard]] std::uint32_t ActiveFeatureSlotOrLocked(
			std::uint32_t a_fallback) const noexcept;
		HRESULT GetDeviceRemovalReasonLocked(HRESULT a_candidate) const noexcept;
		bool FailLocked(
			RendererStage a_stage,
			HRESULT a_result,
			std::string a_detail,
			std::uint32_t a_slot,
			bool a_latch,
			bool a_forceQuarantine = false,
			bool a_countApplyFailure = true);
		void SucceedLocked(std::uint32_t a_slot) noexcept;

		D3D12Interop interop_;
		std::array<Slot, Runtime::kFeatureSlotCount> slots_{};
		ComPtr<ID3D11Device> device_;
		ComPtr<ID3D11DeviceContext> context_;
		ComPtr<ID3D11ComputeShader> copyDepthGuideCS_;
		bool copyDepthGuideCompileFailed_ = false;
		RendererSnapshot snapshot_{};
		bool runtimeReady_ = false;
		bool runtimeTouched_ = false;
		bool failureLatched_ = false;
		bool quarantined_ = false;
		bool runtimeProbeResultLogged_ = false;
		bool runtimeInitializationResultLogged_ = false;
		std::array<bool, Runtime::kFeatureSlotCount> slotEvaluateSuccessLogged_{};
		RendererStage activeStage_ = RendererStage::None;
		std::uint32_t activeFeatureSlot_ = Runtime::kFeatureSlotCount;
	};

	Renderer::State::ValidationFailure Renderer::State::ValidateLocked(
		const RendererApplyArgs& a_args,
		ValidatedResources& a_resources) const
	{
		a_resources = {};
		const auto fail = [](std::string a_detail) {
			return ValidationFailure{ E_INVALIDARG, std::move(a_detail) };
		};

		if (!a_args.device || !a_args.context)
			return fail("D3D11 device and immediate context are required");
		if (a_args.context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			return fail("deferred D3D11 contexts are not supported");
		if (a_args.featureSlot >= Runtime::kFeatureSlotCount)
			return fail(std::format(
				"feature slot {} is outside [0,{})",
				a_args.featureSlot,
				Runtime::kFeatureSlotCount));
		if (!a_args.generation)
			return fail("feature-slot generation must be nonzero");
		if (!IsValidInsertionPoint(a_args.insertionPoint))
			return fail("Feature 18 insertion point is invalid");
		if (!a_args.colorInput || !a_args.depthGuide || !a_args.depthGuideSRV ||
			!a_args.motionVectors || !a_args.colorOutput) {
			return fail("color, depth, motion-vector, and output resources are required");
		}

		const auto validDimension = [](std::uint32_t a_value) {
			return a_value > 0 && a_value <= kMaximumTextureDimension;
		};
		if (!validDimension(a_args.colorWidth) ||
			!validDimension(a_args.colorHeight) ||
			!validDimension(a_args.guideWidth) ||
			!validDimension(a_args.guideHeight) ||
			!validDimension(a_args.outputWidth) ||
			!validDimension(a_args.outputHeight)) {
			return fail("one or more dimensions are zero or exceed the D3D11 Texture2D limit");
		}
		const bool hasControlMask = a_args.controlMask != nullptr;
		if (hasControlMask &&
			(!validDimension(a_args.controlMaskWidth) ||
				!validDimension(a_args.controlMaskHeight))) {
			return fail("control-mask dimensions are zero or exceed the D3D11 Texture2D limit");
		}
		if (!hasControlMask && (a_args.controlMaskWidth || a_args.controlMaskHeight)) {
			return fail("control-mask dimensions must be zero when no mask is supplied");
		}
		if (hasControlMask &&
			(a_args.controlMaskWidth != a_args.outputWidth ||
				a_args.controlMaskHeight != a_args.outputHeight)) {
			return fail("the control mask must exactly match the Feature 18 output extent");
		}
		if (!a_args.viewportCrop.MatchesEvaluationExtents(
				a_args.guideWidth,
				a_args.guideHeight,
				a_args.outputWidth,
				a_args.outputHeight)) {
			return fail("Feature 18 crop does not match the physical guide and output extents");
		}
		if (a_args.colorWidth != a_args.viewportCrop.output.Width() ||
			a_args.colorHeight != a_args.viewportCrop.output.Height()) {
			return fail("Feature 18 color input does not match the exact output crop extent");
		}
		const auto motionVectorScale =
			UpscalingDLSS::BuildMotionVectorPixelScale(a_args.viewportCrop);
		if (!motionVectorScale.valid)
			return fail("Feature 18 motion-vector crop metadata is invalid");
		if (!IsFiniteTuning(a_args.tuning))
			return fail("Feature 18 tuning values are outside their validated ranges");
		if (a_args.tuning.uiCorrection)
			return fail("UI correction is outside the safe Feature 18 contract");
		if (hasControlMask == a_args.tuning.useAutoMask) {
			return fail(hasControlMask ?
							"a control mask requires automatic masking to be disabled" :
							"automatic masking is required when no control mask is supplied");
		}

		ComPtr<ID3D11Device> contextDevice;
		a_args.context->GetDevice(&contextDevice);
		if (!SameIdentity(a_args.device, contextDevice.Get()))
			return fail("the immediate context does not belong to the supplied device");

		struct InputContract
		{
			ID3D11Resource* resource;
			TextureInfo* info;
			std::uint32_t width;
			std::uint32_t height;
			const char* name;
		};
		const std::array<InputContract, 4> contracts{
			InputContract{ a_args.colorInput, &a_resources.color, a_args.colorWidth, a_args.colorHeight, "color input" },
			InputContract{ a_args.depthGuide, &a_resources.depth, a_args.guideWidth, a_args.guideHeight, "depth guide" },
			InputContract{ a_args.motionVectors, &a_resources.motionVectors, a_args.guideWidth, a_args.guideHeight, "motion vectors" },
			InputContract{ a_args.colorOutput, &a_resources.output, a_args.outputWidth, a_args.outputHeight, "color output" },
		};
		for (const auto& contract : contracts) {
			if (!GetTextureInfo(contract.resource, *contract.info))
				return fail(std::format("{} is not a Texture2D", contract.name));
			if (!HasExactTextureContract(contract.info->desc, contract.width, contract.height)) {
				return fail(std::format(
					"{} does not match the exact {}x{} single-sample, single-subresource DEFAULT contract",
					contract.name,
					contract.width,
					contract.height));
			}
			ComPtr<ID3D11Device> resourceDevice;
			contract.info->texture->GetDevice(&resourceDevice);
			if (!SameIdentity(a_args.device, resourceDevice.Get()))
				return fail(std::format("{} belongs to a different D3D11 device", contract.name));
		}
		if (hasControlMask) {
			if (!GetTextureInfo(a_args.controlMask.Get(), a_resources.controlMask))
				return fail("control mask is not a Texture2D");
			if (!HasExactTextureContract(
					a_resources.controlMask.desc,
					a_args.controlMaskWidth,
					a_args.controlMaskHeight)) {
				return fail(std::format(
					"control mask does not match the exact {}x{} single-sample, single-subresource DEFAULT contract",
					a_args.controlMaskWidth,
					a_args.controlMaskHeight));
			}
			if (a_resources.controlMask.desc.Format != DXGI_FORMAT_R8_UNORM)
				return fail("control mask format must be R8_UNORM");
			if ((a_resources.controlMask.desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0)
				return fail("control mask must be shader-resource capable");

			ComPtr<ID3D11Device> maskDevice;
			a_resources.controlMask.texture->GetDevice(&maskDevice);
			if (!SameIdentity(a_args.device, maskDevice.Get()))
				return fail("control mask belongs to a different D3D11 device");
			a_resources.controlMaskIdentity = GetIdentityToken(a_args.controlMask.Get());
			if (!a_resources.controlMaskIdentity)
				return fail("control mask COM identity could not be resolved");
		}

		if (a_resources.color.desc.Format != a_resources.output.desc.Format)
			return fail("color input and output formats differ");
		if (!IsMotionVectorFormat(a_resources.motionVectors.desc.Format))
			return fail(std::format(
				"motion-vector format {} is not R16G16_FLOAT or R32G32_FLOAT",
				static_cast<std::uint32_t>(a_resources.motionVectors.desc.Format)));

		ComPtr<ID3D11Resource> depthViewResource;
		a_args.depthGuideSRV->GetResource(&depthViewResource);
		if (!SameIdentity(a_args.depthGuide, depthViewResource.Get()))
			return fail("the depth SRV does not reference the supplied depth guide");
		D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDesc{};
		a_args.depthGuideSRV->GetDesc(&depthViewDesc);
		if (depthViewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
			depthViewDesc.Texture2D.MostDetailedMip != 0 ||
			depthViewDesc.Texture2D.MipLevels != 1) {
			return fail("the depth guide SRV must expose Texture2D mip zero only");
		}
		a_resources.depthViewFormat = depthViewDesc.Format;
		if (depthViewDesc.Format == DXGI_FORMAT_UNKNOWN ||
			!SupportsD3D11Format(
				a_args.device,
				depthViewDesc.Format,
				D3D11_FORMAT_SUPPORT_TEXTURE2D |
					D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
					D3D11_FORMAT_SUPPORT_SHADER_LOAD)) {
			return fail("the depth guide SRV format is not sampleable");
		}

		D3D11_FEATURE_DATA_D3D11_OPTIONS5 options5{};
		if (FAILED(a_args.device->CheckFeatureSupport(
				D3D11_FEATURE_D3D11_OPTIONS5, &options5, sizeof(options5))) ||
			options5.SharedResourceTier < D3D11_SHARED_RESOURCE_TIER_1) {
			return fail("the D3D11 device does not support shared NT-handle resources");
		}

		constexpr UINT inputSupport = D3D11_FORMAT_SUPPORT_TEXTURE2D |
		                              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
		                              D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
		constexpr UINT outputSupport = D3D11_FORMAT_SUPPORT_TEXTURE2D |
		                               D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
		if (!SupportsD3D11Format(a_args.device, a_resources.color.desc.Format, inputSupport))
			return fail("the color format cannot back a shared SRV/UAV texture");
		if (!SupportsD3D11SharedFormat(a_args.device, a_resources.color.desc.Format))
			return fail("the color format is not shareable across D3D11 and D3D12");
		if (!SupportsD3D11Format(a_args.device, a_resources.motionVectors.desc.Format, inputSupport))
			return fail("the motion-vector format cannot back a shared SRV/UAV texture");
		if (!SupportsD3D11SharedFormat(a_args.device, a_resources.motionVectors.desc.Format))
			return fail("the motion-vector format is not shareable across D3D11 and D3D12");
		if (!SupportsD3D11Format(a_args.device, DXGI_FORMAT_R32_FLOAT, inputSupport))
			return fail("R32_FLOAT depth-guide sharing is unsupported");
		if (!SupportsD3D11SharedFormat(a_args.device, DXGI_FORMAT_R32_FLOAT))
			return fail("R32_FLOAT depth guides are not shareable across D3D11 and D3D12");
		if (!SupportsD3D11Format(a_args.device, a_resources.output.desc.Format, outputSupport))
			return fail("the output format cannot back a shared UAV texture");
		if (!SupportsD3D11SharedFormat(a_args.device, a_resources.output.desc.Format))
			return fail("the output format is not shareable across D3D11 and D3D12");
		if (hasControlMask &&
			!SupportsD3D11Format(a_args.device, DXGI_FORMAT_R8_UNORM, inputSupport)) {
			return fail("R8_UNORM control masks cannot back a shared SRV/UAV texture");
		}
		if (hasControlMask &&
			!SupportsD3D11SharedFormat(a_args.device, DXGI_FORMAT_R8_UNORM)) {
			return fail("R8_UNORM control masks are not shareable across D3D11 and D3D12");
		}

		a_resources.resourceKey = {
			.colorWidth = a_args.colorWidth,
			.colorHeight = a_args.colorHeight,
			.guideWidth = a_args.guideWidth,
			.guideHeight = a_args.guideHeight,
			.outputWidth = a_args.outputWidth,
			.outputHeight = a_args.outputHeight,
			.controlMaskWidth = hasControlMask ? a_args.controlMaskWidth : 0,
			.controlMaskHeight = hasControlMask ? a_args.controlMaskHeight : 0,
			.colorFormat = a_resources.color.desc.Format,
			.motionFormat = a_resources.motionVectors.desc.Format,
			.outputFormat = a_resources.output.desc.Format,
			.controlMaskFormat = hasControlMask ?
			                         a_resources.controlMask.desc.Format :
			                         DXGI_FORMAT_UNKNOWN,
			.controlMaskPresent = hasControlMask,
		};
		a_resources.historyKey = {
			.resources = a_resources.resourceKey,
			.generation = a_args.generation,
			.insertionPoint = a_args.insertionPoint,
			.viewportCrop = a_args.viewportCrop,
			.depthSourceFormat = a_resources.depth.desc.Format,
			.depthViewFormat = a_resources.depthViewFormat,
			.intensity = std::bit_cast<std::uint32_t>(a_args.tuning.intensity),
			.localToneStrength = std::bit_cast<std::uint32_t>(a_args.tuning.localToneStrength),
			.localStructureStrength = std::bit_cast<std::uint32_t>(a_args.tuning.localStructureStrength),
			.skinStructureStrength = std::bit_cast<std::uint32_t>(a_args.tuning.skinStructureStrength),
			.style = a_args.tuning.style,
			.controlMaskIdentity = a_resources.controlMaskIdentity,
			.featureUpscaling = a_args.featureUpscaling,
			.useAutoMask = a_args.tuning.useAutoMask,
			.uiCorrection = a_args.tuning.uiCorrection,
		};
		return {};
	}

	bool Renderer::State::ValidateD3D12FormatsLocked(
		const ValidatedResources& a_resources,
		std::string& a_detail) const
	{
		auto* device = interop_.Device();
		const auto sampled = static_cast<D3D12_FORMAT_SUPPORT1>(
			D3D12_FORMAT_SUPPORT1_TEXTURE2D |
			D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
		const auto texture = D3D12_FORMAT_SUPPORT1_TEXTURE2D;
		const auto typedStore = D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
		if (!SupportsD3D12Format(
				device, a_resources.resourceKey.colorFormat, sampled,
				D3D12_FORMAT_SUPPORT2_NONE)) {
			a_detail = "the D3D12 device cannot sample the color format";
			return false;
		}
		if (!SupportsD3D12Format(
				device, DXGI_FORMAT_R32_FLOAT, sampled,
				D3D12_FORMAT_SUPPORT2_NONE)) {
			a_detail = "the D3D12 device cannot sample R32_FLOAT depth guides";
			return false;
		}
		if (!SupportsD3D12Format(
				device, a_resources.resourceKey.motionFormat, sampled,
				D3D12_FORMAT_SUPPORT2_NONE)) {
			a_detail = "the D3D12 device cannot sample the motion-vector format";
			return false;
		}
		if (a_resources.resourceKey.controlMaskPresent &&
			!SupportsD3D12Format(
				device, a_resources.resourceKey.controlMaskFormat, sampled,
				D3D12_FORMAT_SUPPORT2_NONE)) {
			a_detail = "the D3D12 device cannot sample the R8_UNORM control-mask format";
			return false;
		}
		if (!SupportsD3D12Format(
				device, a_resources.resourceKey.outputFormat, texture, typedStore)) {
			a_detail = "the D3D12 device cannot store typed UAV output in the color format";
			return false;
		}
		return true;
	}

	void Renderer::State::RefreshRuntimeTelemetryLocked()
	{
		auto& runtime = Runtime::Instance();
		snapshot_.status = ToString(runtime.Status());
		snapshot_.trust = ToString(runtime.Trust());
		snapshot_.runtimeFailureStage = ToString(runtime.FailureStage());
		snapshot_.runtimePath = runtime.Path().string();
		snapshot_.runtimeHash = runtime.Hash();
		snapshot_.runtimeVersion = runtime.Version();
		snapshot_.parameterCorePath = runtime.ParameterCorePath().string();
		snapshot_.parameterCoreHash = runtime.ParameterCoreHash();
		snapshot_.parameterCoreTrust = ToString(runtime.CoreTrust());
		snapshot_.parameterCoreSource = ToString(runtime.CoreSource());
		snapshot_.ngxResult = runtime.NgxResult();
		snapshot_.runtimeSuccessfulFrames = runtime.SuccessfulFrames();
		snapshot_.runtimeProxyHits = runtime.LastPathProxyHits();
		snapshot_.runtimeProxyInstalled = runtime.LastPathProxyInstalled();
		if (snapshot_.detail.empty())
			snapshot_.detail = runtime.Detail();
		snapshot_.successes = snapshot_.counters.successes;
		snapshot_.failures = snapshot_.counters.failures;
	}

	void Renderer::State::RefreshInteropTelemetryLocked()
	{
		const auto telemetry = interop_.GetTelemetry();
		auto& performance = snapshot_.performance;
		performance.commandSubmissions = telemetry.commandSubmissions;
		performance.mainCommandSubmissions = telemetry.mainCommandSubmissions;
		performance.submitCommandSubmissions = telemetry.submitCommandSubmissions;
		performance.stereoCommandSubmissions = telemetry.stereoCommandSubmissions;
		performance.mainStereoCommandSubmissions = telemetry.mainStereoCommandSubmissions;
		performance.submitStereoCommandSubmissions = telemetry.submitStereoCommandSubmissions;
		performance.backpressureWaits = telemetry.backpressureWaits;
		performance.backpressureWaitMicroseconds = telemetry.backpressureWaitMicroseconds;
		performance.maximumBackpressureWaitMicroseconds =
			telemetry.maximumBackpressureWaitMicroseconds;
		performance.featureGpuSamples = telemetry.featureGpuSamples;
		performance.featureGpuReadbackFailures = telemetry.featureGpuReadbackFailures;
		performance.featureGpuMicroseconds = telemetry.featureGpuMicroseconds;
		performance.mainFeatureGpuSamples = telemetry.mainFeatureGpuSamples;
		performance.mainFeatureGpuMicroseconds = telemetry.mainFeatureGpuMicroseconds;
		performance.submitFeatureGpuSamples = telemetry.submitFeatureGpuSamples;
		performance.submitFeatureGpuMicroseconds = telemetry.submitFeatureGpuMicroseconds;
		performance.featureGpuSamplesByInsertionPoint =
			telemetry.featureGpuSamplesByInsertionPoint;
		performance.featureGpuMicrosecondsByInsertionPoint =
			telemetry.featureGpuMicrosecondsByInsertionPoint;
		performance.unexpectedFeatureSlotMaskSamples =
			telemetry.unexpectedFeatureSlotMaskSamples;
		performance.invalidInsertionPointSamples =
			telemetry.invalidInsertionPointSamples;
		performance.lastFeatureGpuMicroseconds = telemetry.lastFeatureGpuMicroseconds;
		performance.maximumFeatureGpuMicroseconds = telemetry.maximumFeatureGpuMicroseconds;
		performance.lastFeaturePixelCount = telemetry.lastFeaturePixelCount;
		performance.lastFeatureFrameId = telemetry.lastFeatureFrameId;
		performance.lastFeatureEvaluationCount = telemetry.lastFeatureEvaluationCount;
		performance.lastFeatureSlotMask = telemetry.lastFeatureSlotMask;
		performance.lastInsertionPoint = telemetry.lastInsertionPoint;
	}

	void Renderer::State::SetRequestTelemetryLocked(
		const RendererApplyArgs& a_args) noexcept
	{
		snapshot_.featureSlot = a_args.featureSlot;
		snapshot_.frameId = a_args.frameId;
		snapshot_.generation = a_args.generation;
		snapshot_.insertionPoint = a_args.insertionPoint;
		snapshot_.colorWidth = a_args.colorWidth;
		snapshot_.colorHeight = a_args.colorHeight;
		snapshot_.guideWidth = a_args.guideWidth;
		snapshot_.guideHeight = a_args.guideHeight;
		snapshot_.outputWidth = a_args.outputWidth;
		snapshot_.outputHeight = a_args.outputHeight;
		snapshot_.controlMaskWidth = a_args.controlMaskWidth;
		snapshot_.controlMaskHeight = a_args.controlMaskHeight;
		snapshot_.featureUpscaling = a_args.featureUpscaling;
		snapshot_.controlMaskPresent = a_args.controlMask != nullptr;
		snapshot_.outputCommitted = false;
	}

	void Renderer::State::SetActiveFeatureSlotLocked(
		std::uint32_t a_slot) noexcept
	{
		activeFeatureSlot_ = a_slot < Runtime::kFeatureSlotCount ?
		                         a_slot :
		                         Runtime::kFeatureSlotCount;
	}

	std::uint32_t Renderer::State::ActiveFeatureSlotOrLocked(
		std::uint32_t a_fallback) const noexcept
	{
		if (activeFeatureSlot_ < Runtime::kFeatureSlotCount)
			return activeFeatureSlot_;
		return a_fallback < Runtime::kFeatureSlotCount ?
		           a_fallback :
		           Runtime::kFeatureSlotCount;
	}

	HRESULT Renderer::State::GetDeviceRemovalReasonLocked(
		HRESULT a_candidate) const noexcept
	{
		if (device_) {
			const HRESULT reason = device_->GetDeviceRemovedReason();
			if (IsDeviceLossReason(reason))
				return reason;
		}
		if (auto* device12 = interop_.Device()) {
			const HRESULT reason = device12->GetDeviceRemovedReason();
			if (IsDeviceLossReason(reason))
				return reason;
		}
		return IsDeviceLossResult(a_candidate) ? a_candidate : S_OK;
	}

	bool Renderer::State::FailLocked(
		RendererStage a_stage,
		HRESULT a_result,
		std::string a_detail,
		std::uint32_t a_slot,
		bool a_latch,
		bool a_forceQuarantine,
		bool a_countApplyFailure)
	{
		SetActiveFeatureSlotLocked(a_slot);
		if (a_countApplyFailure) {
			Increment(snapshot_.counters.failures);
			if (a_slot < Runtime::kFeatureSlotCount)
				Increment(snapshot_.counters.slotFailures[a_slot]);
		}
		const auto stageIndex = static_cast<std::size_t>(a_stage);
		if (stageIndex < snapshot_.counters.failuresByStage.size())
			Increment(snapshot_.counters.failuresByStage[stageIndex]);
		if (a_stage == RendererStage::Validation)
			Increment(snapshot_.counters.validationFailures);

		const HRESULT removalReason = GetDeviceRemovalReasonLocked(a_result);
		const bool deviceRemoved = FAILED(removalReason);
		if (deviceRemoved) {
			if (!quarantined_)
				Increment(snapshot_.counters.deviceRemovals);
			a_result = removalReason;
			a_detail = std::format(
				"{} failed at {}; device removal reason=0x{:08X}",
				a_detail,
				ToString(a_stage),
				static_cast<std::uint32_t>(removalReason));
		}

		if (deviceRemoved || a_forceQuarantine) {
			const bool enteringQuarantine = !quarantined_;
			if (enteringQuarantine)
				Increment(snapshot_.counters.quarantines);
			quarantined_ = true;
			failureLatched_ = true;
			if (enteringQuarantine)
				AbandonRuntimeOwnershipNoexcept();
		} else if (a_latch) {
			failureLatched_ = true;
		}

		snapshot_.failureStage = a_stage;
		snapshot_.failureFeatureSlot = a_slot;
		snapshot_.lastResult = a_result;
		snapshot_.failureLatched = failureLatched_;
		snapshot_.quarantined = quarantined_;
		snapshot_.outputCommitted = false;
		snapshot_.detail = std::move(a_detail);
		RefreshRuntimeTelemetryLocked();
		snapshot_.successes = snapshot_.counters.successes;
		snapshot_.failures = snapshot_.counters.failures;
		if (a_stage != RendererStage::FailureLatched &&
			a_stage != RendererStage::Quarantined) {
			logger::error(
				"[DLSSNR] Renderer failed at {}: {} (hr=0x{:08X}, ngx=0x{:08X}, latched={}, quarantined={})",
				ToString(a_stage),
				snapshot_.detail,
				static_cast<std::uint32_t>(a_result),
				snapshot_.ngxResult,
				failureLatched_,
				quarantined_);
		}
		return false;
	}

	void Renderer::State::SucceedLocked(std::uint32_t a_slot) noexcept
	{
		Increment(snapshot_.counters.successes);
		if (a_slot < Runtime::kFeatureSlotCount)
			Increment(snapshot_.counters.slotSuccesses[a_slot]);
		Increment(snapshot_.counters.outputCommits);
		snapshot_.successes = snapshot_.counters.successes;
		snapshot_.failures = snapshot_.counters.failures;
		snapshot_.lastCompletedStage = RendererStage::Complete;
		snapshot_.failureStage = RendererStage::None;
		snapshot_.failureFeatureSlot = Runtime::kFeatureSlotCount;
		snapshot_.lastResult = S_OK;
		snapshot_.failureLatched = false;
		snapshot_.quarantined = false;
		snapshot_.outputCommitted = true;
	}

	bool Renderer::State::TeardownBackendLocked(
		bool a_resetShader,
		bool a_destruction,
		bool a_countApplyFailure)
	{
		Increment(snapshot_.counters.resetAttempts);
		if (quarantined_) {
			Increment(snapshot_.counters.resetFailures);
			snapshot_.failureLatched = true;
			snapshot_.quarantined = true;
			snapshot_.outputCommitted = false;
			if (a_destruction)
				AbandonSlotsLocked();
			return false;
		}

		if (interop_.IsRecording() && !interop_.AbortD3D12()) {
			Increment(snapshot_.counters.resetFailures);
			const bool failed = FailLocked(
				RendererStage::CommandEnd,
				interop_.LastError(),
				std::format("could not abort D3D12 recording: {}", interop_.LastOperation()),
				Runtime::kFeatureSlotCount,
				true,
				true,
				a_countApplyFailure);
			if (a_destruction)
				AbandonSlotsLocked();
			return failed;
		}
		if (interop_.IsInitialized() && !interop_.WaitForIdle()) {
			Increment(snapshot_.counters.resetFailures);
			const bool failed = FailLocked(
				RendererStage::ResetWait,
				interop_.LastError(),
				std::format("bounded GPU idle wait failed: {}", interop_.LastOperation()),
				Runtime::kFeatureSlotCount,
				true,
				true,
				a_countApplyFailure);
			if (a_destruction)
				AbandonSlotsLocked();
			return failed;
		}

		auto& runtime = Runtime::Instance();
		if (runtimeReady_ && !runtime.ResetFeatures()) {
			Increment(snapshot_.counters.resetFailures);
			const bool failed = FailLocked(
				RendererStage::RuntimeReset,
				E_FAIL,
				std::format("Feature 18 release failed: {}", runtime.Detail()),
				Runtime::kFeatureSlotCount,
				true,
				true,
				a_countApplyFailure);
			if (a_destruction)
				AbandonSlotsLocked();
			return failed;
		}
		if (runtimeTouched_ && !runtime.Shutdown()) {
			Increment(snapshot_.counters.resetFailures);
			const bool failed = FailLocked(
				RendererStage::RuntimeReset,
				E_FAIL,
				std::format("Feature 18 shutdown failed: {}", runtime.Detail()),
				Runtime::kFeatureSlotCount,
				true,
				true,
				a_countApplyFailure);
			if (a_destruction)
				AbandonSlotsLocked();
			return failed;
		}
		if (interop_.IsInitialized() && !interop_.Shutdown()) {
			Increment(snapshot_.counters.resetFailures);
			const bool failed = FailLocked(
				RendererStage::InteropShutdown,
				interop_.LastError(),
				std::format("D3D12 interop shutdown failed: {}", interop_.LastOperation()),
				Runtime::kFeatureSlotCount,
				true,
				true,
				a_countApplyFailure);
			if (a_destruction)
				AbandonSlotsLocked();
			return failed;
		}

		slots_ = {};
		device_.Reset();
		context_.Reset();
		if (a_resetShader) {
			copyDepthGuideCS_.Reset();
			copyDepthGuideCompileFailed_ = false;
		}
		runtimeReady_ = false;
		runtimeTouched_ = false;
		failureLatched_ = false;
		quarantined_ = false;
		Increment(snapshot_.counters.resetSuccesses);
		snapshot_.lastCompletedStage = RendererStage::InteropShutdown;
		snapshot_.failureStage = RendererStage::None;
		snapshot_.failureFeatureSlot = Runtime::kFeatureSlotCount;
		snapshot_.lastResult = S_OK;
		snapshot_.failureLatched = false;
		snapshot_.quarantined = false;
		snapshot_.outputCommitted = false;
		snapshot_.detail.clear();
		RefreshRuntimeTelemetryLocked();
		return true;
	}

	void Renderer::State::AbandonSlotsLocked() noexcept
	{
		for (auto& slot : slots_) {
			Abandon(slot.color);
			Abandon(slot.depth);
			Abandon(slot.motionVectors);
			Abandon(slot.controlMask);
			Abandon(slot.output);
			slot.resourcesValid = false;
			slot.historyValid = false;
		}
	}

	void Renderer::State::AbandonRuntimeOwnershipNoexcept() noexcept
	{
		if (!runtimeTouched_)
			return;
		try {
			Runtime::Instance().AbandonUnsafe();
		} catch (...) {
		}
		runtimeReady_ = false;
	}

	void Renderer::State::QuarantineAfterUnexpectedFailureLocked(
		RendererStage a_stage,
		std::uint32_t a_slot,
		bool a_applyFailure,
		std::uint64_t a_failuresBefore) noexcept
	{
		if (a_applyFailure && snapshot_.counters.failures == a_failuresBefore) {
			Increment(snapshot_.counters.failures);
			if (a_slot < Runtime::kFeatureSlotCount)
				Increment(snapshot_.counters.slotFailures[a_slot]);
			const auto stageIndex = static_cast<std::size_t>(a_stage);
			if (stageIndex < snapshot_.counters.failuresByStage.size())
				Increment(snapshot_.counters.failuresByStage[stageIndex]);
			if (a_stage == RendererStage::Validation)
				Increment(snapshot_.counters.validationFailures);
		}
		if (!a_applyFailure &&
			snapshot_.counters.resetFailures < snapshot_.counters.resetAttempts) {
			Increment(snapshot_.counters.resetFailures);
		}

		const bool enteringQuarantine = !quarantined_;
		quarantined_ = true;
		failureLatched_ = true;
		activeStage_ = RendererStage::Quarantined;
		if (enteringQuarantine)
			Increment(snapshot_.counters.quarantines);

		// Ownership must detach before any later destructor can attempt release.
		AbandonRuntimeOwnershipNoexcept();
		AbandonSlotsLocked();
		interop_.AbandonUnsafe();

		snapshot_.failureStage = a_stage;
		snapshot_.failureFeatureSlot = a_slot;
		snapshot_.lastResult = E_FAIL;
		snapshot_.failureLatched = true;
		snapshot_.quarantined = true;
		snapshot_.outputCommitted = false;
		snapshot_.successes = snapshot_.counters.successes;
		snapshot_.failures = snapshot_.counters.failures;
		snapshot_.status.clear();
		snapshot_.runtimeFailureStage.clear();
		snapshot_.detail.clear();
		try {
			snapshot_.status = ToString(Runtime::Instance().Status());
			snapshot_.runtimeFailureStage =
				ToString(Runtime::Instance().FailureStage());
			snapshot_.detail = a_applyFailure ?
			                       "renderer apply threw; unsafe backend ownership was intentionally retained" :
			                       "renderer teardown threw; unsafe backend ownership was intentionally retained";
		} catch (...) {
		}
	}

	bool Renderer::State::EnsureBackendLocked(const RendererApplyArgs& a_args)
	{
		const bool backendIdentityChanged = device_ &&
		                                    (!SameIdentity(device_.Get(), a_args.device) ||
												!SameIdentity(context_.Get(), a_args.context));
		if (backendIdentityChanged) {
			if (!TeardownBackendLocked(false, false, true))
				return false;
		}

		if (runtimeReady_ && interop_.IsInitialized())
			return true;

		ComPtr<IDXGIDevice> dxgiDevice;
		ComPtr<IDXGIAdapter> adapter;
		activeStage_ = RendererStage::DeviceCompatibility;
		HRESULT result = a_args.device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
		if (SUCCEEDED(result))
			result = dxgiDevice->GetAdapter(&adapter);
		if (FAILED(result)) {
			return FailLocked(
				RendererStage::DeviceCompatibility,
				result,
				"could not resolve the D3D11 device adapter",
				a_args.featureSlot,
				true);
		}

		activeStage_ = RendererStage::InteropInitialization;
		if (!interop_.Initialize(adapter.Get(), a_args.device, a_args.context)) {
			return FailLocked(
				RendererStage::InteropInitialization,
				interop_.LastError(),
				std::format("D3D11/D3D12 interop initialization failed: {}", interop_.LastOperation()),
				a_args.featureSlot,
				true);
		}
		device_ = a_args.device;
		context_ = a_args.context;
		Increment(snapshot_.counters.interopInitializations);
		snapshot_.lastCompletedStage = RendererStage::InteropInitialization;

		auto& runtime = Runtime::Instance();
		runtimeTouched_ = true;
		activeStage_ = RendererStage::RuntimeProbe;
		const bool probeSucceeded = runtime.Probe();
		LogOnce(runtimeProbeResultLogged_, [&]() {
			logger::info(
				"[DLSSNR] Runtime probe {}: status={}, trust={}, version={}, path={}",
				probeSucceeded ? "succeeded" : "failed",
				ToString(runtime.Status()),
				ToString(runtime.Trust()),
				runtime.Version(),
				runtime.Path().string());
		});
		if (!probeSucceeded) {
			return FailLocked(
				RendererStage::RuntimeProbe,
				E_FAIL,
				std::format("Feature 18 runtime probe failed: {}", runtime.Detail()),
				a_args.featureSlot,
				true);
		}
		snapshot_.lastCompletedStage = RendererStage::RuntimeProbe;

		activeStage_ = RendererStage::RuntimeInitialization;
		const bool initializationSucceeded = runtime.Initialize(interop_.Device());
		LogOnce(runtimeInitializationResultLogged_, [&]() {
			logger::info(
				"[DLSSNR] Runtime initialization {}: status={}, stage={}, ngx=0x{:08X}",
				initializationSucceeded ? "succeeded" : "failed",
				ToString(runtime.Status()),
				ToString(runtime.FailureStage()),
				runtime.NgxResult());
		});
		if (!initializationSucceeded) {
			const bool rollbackUnsafe =
				runtime.Status() == RuntimeStatus::ShutdownFailed ||
				runtime.Status() == RuntimeStatus::UnsafeAbandoned;
			return FailLocked(
				RendererStage::RuntimeInitialization,
				E_FAIL,
				std::format("Feature 18 runtime initialization failed: {}", runtime.Detail()),
				a_args.featureSlot,
				true,
				rollbackUnsafe);
		}
		runtimeReady_ = true;
		Increment(snapshot_.counters.runtimeInitializations);
		snapshot_.lastCompletedStage = RendererStage::RuntimeInitialization;
		RefreshRuntimeTelemetryLocked();
		return true;
	}

	bool Renderer::State::EnsureSlotLocked(
		std::uint32_t a_slot,
		const ValidatedResources& a_resources)
	{
		auto& slot = slots_[a_slot];
		if (slot.resourcesValid && slot.resourceKey == a_resources.resourceKey)
			return true;

		if (slot.resourcesValid) {
			activeStage_ = RendererStage::ResourceRetirement;
			if (!interop_.WaitForIdle()) {
				return FailLocked(
					RendererStage::ResourceRetirement,
					interop_.LastError(),
					std::format("slot {} idle wait failed: {}", a_slot, interop_.LastOperation()),
					a_slot,
					true,
					true);
			}
			if (!Runtime::Instance().ResetFeature(a_slot)) {
				return FailLocked(
					RendererStage::ResourceRetirement,
					E_FAIL,
					std::format("slot {} Feature 18 release failed: {}", a_slot, Runtime::Instance().Detail()),
					a_slot,
					true,
					true);
			}
			slot = {};
			snapshot_.lastCompletedStage = RendererStage::ResourceRetirement;
		}

		activeStage_ = RendererStage::DeviceCompatibility;
		std::string formatDetail;
		if (!ValidateD3D12FormatsLocked(a_resources, formatDetail)) {
			return FailLocked(
				RendererStage::DeviceCompatibility,
				E_NOINTERFACE,
				std::move(formatDetail),
				a_slot,
				true);
		}

		activeStage_ = RendererStage::ResourceCreation;
		Slot replacement;
		const auto prefix = std::format("NeuralRendering::Slot{}::", a_slot);
		const auto colorDesc = MakeSharedDescription(
			a_resources.resourceKey.colorWidth,
			a_resources.resourceKey.colorHeight,
			a_resources.resourceKey.colorFormat,
			true);
		const auto depthDesc = MakeSharedDescription(
			a_resources.resourceKey.guideWidth,
			a_resources.resourceKey.guideHeight,
			DXGI_FORMAT_R32_FLOAT,
			true);
		const auto motionDesc = MakeSharedDescription(
			a_resources.resourceKey.guideWidth,
			a_resources.resourceKey.guideHeight,
			a_resources.resourceKey.motionFormat,
			true);
		const auto outputDesc = MakeSharedDescription(
			a_resources.resourceKey.outputWidth,
			a_resources.resourceKey.outputHeight,
			a_resources.resourceKey.outputFormat,
			false);
		D3D11_TEXTURE2D_DESC controlMaskDesc{};
		if (a_resources.resourceKey.controlMaskPresent) {
			controlMaskDesc = MakeSharedDescription(
				a_resources.resourceKey.controlMaskWidth,
				a_resources.resourceKey.controlMaskHeight,
				a_resources.resourceKey.controlMaskFormat,
				true);
		}

		if (!interop_.CreateSharedTexture(colorDesc, replacement.color, (prefix + "Color").c_str()) ||
			!interop_.CreateSharedTexture(depthDesc, replacement.depth, (prefix + "Depth").c_str()) ||
			!interop_.CreateSharedTexture(motionDesc, replacement.motionVectors, (prefix + "MotionVectors").c_str()) ||
			(a_resources.resourceKey.controlMaskPresent &&
				!interop_.CreateSharedTexture(
					controlMaskDesc,
					replacement.controlMask,
					(prefix + "ControlMask").c_str())) ||
			!interop_.CreateSharedTexture(outputDesc, replacement.output, (prefix + "Output").c_str())) {
			return FailLocked(
				RendererStage::ResourceCreation,
				interop_.LastError(),
				std::format("slot {} shared-resource creation failed: {}", a_slot, interop_.LastOperation()),
				a_slot,
				true);
		}

		replacement.resourceKey = a_resources.resourceKey;
		replacement.resourcesValid = true;
		slot = std::move(replacement);
		Increment(snapshot_.counters.resourceRebuilds);
		snapshot_.lastCompletedStage = RendererStage::ResourceCreation;
		return true;
	}

	bool Renderer::State::CopyDepthBatchLocked(
		std::span<const RendererApplyArgs> a_args,
		std::span<Slot* const> a_slots)
	{
		if (a_args.empty() || a_args.size() != a_slots.size())
			return false;
		if (!copyDepthGuideCS_ && !copyDepthGuideCompileFailed_) {
			copyDepthGuideCS_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/CopyDepthGuideCS.hlsl",
				{},
				"cs_5_0",
				"main")));
			copyDepthGuideCompileFailed_ = !copyDepthGuideCS_;
			if (copyDepthGuideCS_)
				Util::SetResourceName(copyDepthGuideCS_.Get(), "NeuralRendering::CopyDepthGuideCS");
		}
		auto* shader = copyDepthGuideCS_.Get();
		if (!shader)
			return false;

		ComputeStateGuard stateGuard(a_args.front().context);
		if (!stateGuard.Captured())
			return false;

		a_args.front().context->CSSetShader(shader, nullptr, 0);
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			auto* source = a_args[index].depthGuideSRV;
			auto* destination = a_slots[index]->depth.uav11.Get();
			a_args.front().context->CSSetShaderResources(0, 1, &source);
			a_args.front().context->CSSetUnorderedAccessViews(
				0, 1, &destination, nullptr);
			{
				CS_PROFILE_SCOPE("Upscaling::DLSSNRDepthGuide");
				a_args.front().context->Dispatch(
					(a_args[index].guideWidth + 7u) / 8u,
					(a_args[index].guideHeight + 7u) / 8u,
					1);
			}
		}
		return true;
	}

	bool Renderer::State::ApplyLocked(
		const RendererApplyArgs& a_args,
		RendererApplyOutcome& a_outcome)
	{
		return ApplyBatchLocked(std::span(&a_args, 1), a_outcome);
	}

	bool Renderer::State::ApplyStereoLocked(
		const std::array<RendererApplyArgs, 2>& a_args,
		RendererApplyOutcome& a_outcome)
	{
		return ApplyBatchLocked(a_args, a_outcome);
	}

	bool Renderer::State::ApplySequentialStereoLocked(
		const std::array<RendererApplyArgs, 2>& a_args,
		RendererApplyOutcome& a_outcome)
	{
		a_outcome = {};
		SetActiveFeatureSlotLocked(a_args[0].featureSlot);
		if (quarantined_ || failureLatched_) {
			return ApplyBatchLocked(a_args, a_outcome);
		}
		SetActiveFeatureSlotLocked(a_args[1].featureSlot);
		if (!GetStereoPairContractViolation(a_args).empty()) {
			return ApplyBatchLocked(a_args, a_outcome);
		}

		std::array<ValidatedResources, 2> resources{};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			if (ValidateLocked(a_args[index], resources[index]))
				return ApplyBatchLocked(a_args, a_outcome);
		}

		bool synchronizeForcedReset =
			a_args[0].synchronizedHistoryReset ||
			!runtimeReady_ || !interop_.IsInitialized();
		bool synchronizeDiscontinuousReset =
			a_args[0].synchronizedHistoryDiscontinuity;
		if (device_ &&
			(!SameIdentity(device_.Get(), a_args[0].device) ||
				!SameIdentity(context_.Get(), a_args[0].context))) {
			synchronizeForcedReset = true;
		}
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			const auto& args = a_args[index];
			SetActiveFeatureSlotLocked(args.featureSlot);
			const auto& slot = slots_[args.featureSlot];
			const bool discontinuous = slot.historyValid &&
			                           !IsSequentialFrame(slot.lastSuccessfulFrame, args.frameId);
			const bool forced =
				!slot.resourcesValid ||
				slot.resourceKey != resources[index].resourceKey ||
				!slot.historyValid ||
				slot.historyKey != resources[index].historyKey ||
				discontinuous;
			synchronizeForcedReset = synchronizeForcedReset || forced;
			synchronizeDiscontinuousReset =
				synchronizeDiscontinuousReset || discontinuous;
		}

		auto synchronizedArgs = a_args;
		for (auto& args : synchronizedArgs) {
			args.synchronizedHistoryReset = synchronizeForcedReset;
			args.synchronizedHistoryDiscontinuity =
				synchronizeDiscontinuousReset;
		}

		RendererApplyOutcome leftOutcome{};
		if (!ApplyBatchLocked(std::span(&synchronizedArgs[0], 1), leftOutcome)) {
			a_outcome = leftOutcome;
			return false;
		}
		a_outcome.evaluationAttemptedFeatureSlotMask =
			leftOutcome.evaluationAttemptedFeatureSlotMask;
		a_outcome.evaluationSucceededFeatureSlotMask =
			leftOutcome.evaluationSucceededFeatureSlotMask;
		RendererApplyOutcome rightOutcome{};
		const bool rightSucceeded = ApplyBatchLocked(
			std::span(&synchronizedArgs[1], 1), rightOutcome);
		a_outcome.evaluationAttemptedFeatureSlotMask |=
			rightOutcome.evaluationAttemptedFeatureSlotMask;
		a_outcome.evaluationSucceededFeatureSlotMask |=
			rightOutcome.evaluationSucceededFeatureSlotMask;
		return rightSucceeded;
	}

	bool Renderer::State::ApplyBatchLocked(
		std::span<const RendererApplyArgs> a_args,
		RendererApplyOutcome& a_outcome)
	{
		a_outcome = {};
		if (a_args.empty() || a_args.size() > 2)
			return false;
		SetActiveFeatureSlotLocked(a_args.front().featureSlot);
		for (const auto& args : a_args) {
			SetActiveFeatureSlotLocked(args.featureSlot);
			Increment(snapshot_.counters.attempts);
			SetRequestTelemetryLocked(args);
		}
		SetActiveFeatureSlotLocked(a_args.front().featureSlot);
		activeStage_ = RendererStage::Validation;

		if (quarantined_) {
			for ([[maybe_unused]] const auto& args : a_args)
				Increment(snapshot_.counters.quarantinedBypasses);
			snapshot_.failureLatched = true;
			snapshot_.quarantined = true;
			snapshot_.outputCommitted = false;
			return false;
		}
		if (failureLatched_) {
			for ([[maybe_unused]] const auto& args : a_args)
				Increment(snapshot_.counters.latchedBypasses);
			snapshot_.failureLatched = true;
			snapshot_.quarantined = quarantined_;
			snapshot_.outputCommitted = false;
			return false;
		}

		snapshot_.failureStage = RendererStage::None;
		snapshot_.failureFeatureSlot = Runtime::kFeatureSlotCount;
		snapshot_.lastCompletedStage = RendererStage::None;
		snapshot_.lastResult = S_OK;
		snapshot_.detail.clear();
		snapshot_.colorFormat = 0;
		snapshot_.depthSourceFormat = 0;
		snapshot_.depthViewFormat = 0;
		snapshot_.motionVectorFormat = 0;
		snapshot_.outputFormat = 0;
		snapshot_.controlMaskFormat = 0;

		if (a_args.size() == 2) {
			SetActiveFeatureSlotLocked(a_args[1].featureSlot);
			std::array<RendererApplyArgs, 2> stereoArgs{ a_args[0], a_args[1] };
			if (auto violation = GetStereoPairContractViolation(stereoArgs);
				!violation.empty()) {
				return FailLocked(
					RendererStage::Validation,
					E_INVALIDARG,
					std::move(violation),
					a_args[1].featureSlot,
					false);
			}
		}

		std::array<ValidatedResources, 2> resources{};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			const auto& args = a_args[index];
			SetActiveFeatureSlotLocked(args.featureSlot);
			auto validation = ValidateLocked(args, resources[index]);
			snapshot_.colorFormat =
				static_cast<std::uint32_t>(resources[index].color.desc.Format);
			snapshot_.depthSourceFormat =
				static_cast<std::uint32_t>(resources[index].depth.desc.Format);
			snapshot_.depthViewFormat =
				static_cast<std::uint32_t>(resources[index].depthViewFormat);
			snapshot_.motionVectorFormat =
				static_cast<std::uint32_t>(resources[index].motionVectors.desc.Format);
			snapshot_.outputFormat =
				static_cast<std::uint32_t>(resources[index].output.desc.Format);
			snapshot_.controlMaskFormat =
				static_cast<std::uint32_t>(resources[index].controlMask.desc.Format);
			if (validation) {
				return FailLocked(
					RendererStage::Validation,
					validation.result,
					std::move(validation.detail),
					args.featureSlot,
					false);
			}
		}
		snapshot_.lastCompletedStage = RendererStage::Validation;

		activeStage_ = RendererStage::DeviceCompatibility;
		SetActiveFeatureSlotLocked(a_args.front().featureSlot);
		if (!EnsureBackendLocked(a_args.front()))
			return false;
		activeStage_ = RendererStage::ResourceCreation;
		std::array<Slot*, 2> slots{};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			if (!EnsureSlotLocked(a_args[index].featureSlot, resources[index]))
				return false;
			slots[index] = &slots_[a_args[index].featureSlot];
		}

		const auto preparationStarted = std::chrono::steady_clock::now();
		activeStage_ = RendererStage::ColorInputCopy;
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			a_args.front().context->CopyResource(
				slots[index]->color.resource11.Get(), resources[index].color.texture.Get());
		}
		if (const HRESULT reason = GetDeviceRemovalReasonLocked(S_OK); FAILED(reason)) {
			return FailLocked(
				RendererStage::ColorInputCopy,
				reason,
				"device removal followed the D3D11 color-input copy",
				a_args.front().featureSlot,
				true);
		}
		snapshot_.lastCompletedStage = RendererStage::ColorInputCopy;

		activeStage_ = RendererStage::DepthGuideCopy;
		if (!CopyDepthBatchLocked(a_args, std::span(slots.data(), a_args.size()))) {
			return FailLocked(
				RendererStage::DepthGuideCopy,
				E_FAIL,
				"CopyDepthGuideCS compilation or dispatch setup failed",
				a_args.front().featureSlot,
				true);
		}
		for ([[maybe_unused]] const auto& args : a_args)
			Increment(snapshot_.counters.depthGuideCopies);
		if (const HRESULT reason = GetDeviceRemovalReasonLocked(S_OK); FAILED(reason)) {
			return FailLocked(
				RendererStage::DepthGuideCopy,
				reason,
				"device removal followed the D3D11 depth-guide dispatch",
				a_args.front().featureSlot,
				true);
		}
		snapshot_.lastCompletedStage = RendererStage::DepthGuideCopy;

		activeStage_ = RendererStage::MotionVectorCopy;
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			a_args.front().context->CopyResource(
				slots[index]->motionVectors.resource11.Get(),
				resources[index].motionVectors.texture.Get());
		}
		if (const HRESULT reason = GetDeviceRemovalReasonLocked(S_OK); FAILED(reason)) {
			return FailLocked(
				RendererStage::MotionVectorCopy,
				reason,
				"device removal followed the D3D11 motion-vector copy",
				a_args.front().featureSlot,
				true);
		}
		snapshot_.lastCompletedStage = RendererStage::MotionVectorCopy;

		if (std::ranges::any_of(
				a_args, [](const auto& args) { return args.controlMask != nullptr; })) {
			activeStage_ = RendererStage::ControlMaskCopy;
			for (std::size_t index = 0; index < a_args.size(); ++index) {
				if (!a_args[index].controlMask)
					continue;
				SetActiveFeatureSlotLocked(a_args[index].featureSlot);
				a_args.front().context->CopyResource(
					slots[index]->controlMask.resource11.Get(),
					resources[index].controlMask.texture.Get());
				Increment(snapshot_.counters.controlMaskCopies);
			}
			if (const HRESULT reason = GetDeviceRemovalReasonLocked(S_OK); FAILED(reason)) {
				return FailLocked(
					RendererStage::ControlMaskCopy,
					reason,
					"device removal followed the D3D11 control-mask copy",
					a_args.front().featureSlot,
					true);
			}
			snapshot_.lastCompletedStage = RendererStage::ControlMaskCopy;
		}
		RecordCpuDuration(
			snapshot_.performance.d3d11PreparationCpuEnqueueSamples,
			snapshot_.performance.d3d11PreparationCpuEnqueueMicroseconds,
			snapshot_.performance.lastD3D11PreparationCpuEnqueueMicroseconds,
			snapshot_.performance.maximumD3D11PreparationCpuEnqueueMicroseconds,
			preparationStarted);

		activeStage_ = RendererStage::CommandBegin;
		ID3D12GraphicsCommandList* commandList = nullptr;
		if (!interop_.BeginD3D12(&commandList) || !commandList) {
			return FailLocked(
				RendererStage::CommandBegin,
				interop_.LastError(),
				std::format("D3D12 command recording could not begin: {}", interop_.LastOperation()),
				a_args.front().featureSlot,
				true);
		}
		RecordingGuard recordingGuard(interop_);
		snapshot_.lastCompletedStage = RendererStage::CommandBegin;

		std::array<FeatureResourceTransition, kMaximumTransitionResourceCount>
			sharedResources{};
		std::size_t sharedResourceCount = 0;
		std::uint64_t pixelCount = 0;
		std::uint32_t featureSlotMask = 0;
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			const auto addInput = [&](ID3D12Resource* a_resource) {
				sharedResources[sharedResourceCount++] = {
					.resource = a_resource,
					.featureState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				};
			};
			addInput(slots[index]->color.resource12.Get());
			addInput(slots[index]->depth.resource12.Get());
			addInput(slots[index]->motionVectors.resource12.Get());
			if (a_args[index].controlMask)
				addInput(slots[index]->controlMask.resource12.Get());
			sharedResources[sharedResourceCount++] = {
				.resource = slots[index]->output.resource12.Get(),
				.featureState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			};
			Add(
				pixelCount,
				static_cast<std::uint64_t>(a_args[index].outputWidth) *
					a_args[index].outputHeight);
			featureSlotMask |= 1u << a_args[index].featureSlot;
		}
		const auto resourceSpan =
			std::span(sharedResources.data(), sharedResourceCount);
		TransitionResources(commandList, resourceSpan, true);
		if (!interop_.BeginFeatureTiming(
				commandList,
				D3D12InteropSubmissionTiming{
					.frameId = a_args.front().frameId,
					.pixelCount = pixelCount,
					.evaluationCount = static_cast<std::uint32_t>(a_args.size()),
					.featureSlotMask = featureSlotMask,
					.insertionPoint = a_args.front().insertionPoint,
				})) {
			const bool aborted = interop_.AbortD3D12();
			recordingGuard.active = interop_.IsRecording();
			return FailLocked(
				RendererStage::CommandBegin,
				aborted ? E_FAIL : interop_.LastError(),
				"D3D12 Feature 18 timestamp recording could not begin",
				a_args.front().featureSlot,
				true,
				!aborted);
		}

		std::array<bool, 2> forcedHistoryReset{};
		std::array<bool, 2> discontinuousHistoryReset{};
		bool synchronizeForcedReset = false;
		bool synchronizeDiscontinuousReset = false;
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			const auto& slot = *slots[index];
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			discontinuousHistoryReset[index] =
				a_args[index].synchronizedHistoryDiscontinuity ||
				(slot.historyValid &&
					!IsSequentialFrame(slot.lastSuccessfulFrame, a_args[index].frameId));
			forcedHistoryReset[index] =
				a_args[index].synchronizedHistoryReset ||
				!slot.historyValid ||
				slot.historyKey != resources[index].historyKey ||
				discontinuousHistoryReset[index];
			synchronizeForcedReset =
				synchronizeForcedReset || forcedHistoryReset[index];
			synchronizeDiscontinuousReset =
				synchronizeDiscontinuousReset || discontinuousHistoryReset[index];
		}
		const bool stereoBatch = a_args.size() == 2;

		activeStage_ = RendererStage::FeatureEvaluate;
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			auto& slot = *slots[index];
			const auto& args = a_args[index];
			const auto motionVectorScale =
				UpscalingDLSS::BuildMotionVectorPixelScale(args.viewportCrop);
			SetActiveFeatureSlotLocked(args.featureSlot);
			const bool forcedReset = stereoBatch ?
			                             synchronizeForcedReset :
			                             forcedHistoryReset[index];
			const bool discontinuousReset = stereoBatch ?
			                                    synchronizeDiscontinuousReset :
			                                    discontinuousHistoryReset[index];
			const bool effectiveReset = args.reset || forcedReset;
			if (args.reset)
				Increment(snapshot_.counters.callerHistoryResets);
			if (forcedReset)
				Increment(snapshot_.counters.forcedHistoryResets);
			if (discontinuousReset)
				Increment(snapshot_.counters.discontinuousHistoryResets);
			bool evaluationAttempted = false;
			const bool evaluated = Runtime::Instance().Execute(
				commandList,
				args.featureSlot,
				slot.color.resource12.Get(),
				slot.depth.resource12.Get(),
				slot.motionVectors.resource12.Get(),
				slot.output.resource12.Get(),
				args.controlMask ? slot.controlMask.resource12.Get() : nullptr,
				args.colorWidth,
				args.colorHeight,
				args.guideWidth,
				args.guideHeight,
				args.outputWidth,
				args.outputHeight,
				args.controlMaskWidth,
				args.controlMaskHeight,
				motionVectorScale.x,
				motionVectorScale.y,
				args.featureUpscaling,
				args.tuning,
				effectiveReset,
				&evaluationAttempted);
			if (evaluationAttempted) {
				Increment(snapshot_.counters.featureEvaluations);
				a_outcome.evaluationAttemptedFeatureSlotMask |= 1u << args.featureSlot;
			}
			if (!evaluated) {
				const std::string runtimeDetail = Runtime::Instance().Detail();
				const bool aborted = interop_.AbortD3D12();
				recordingGuard.active = interop_.IsRecording();
				return FailLocked(
					RendererStage::FeatureEvaluate,
					aborted ? E_FAIL : interop_.LastError(),
					aborted ?
						std::format("Feature 18 evaluation failed: {}", runtimeDetail) :
						std::format(
							"Feature 18 evaluation failed and command abort failed at {}: {}",
							interop_.LastOperation(),
							runtimeDetail),
					args.featureSlot,
					true,
					!aborted);
			}
			a_outcome.evaluationSucceededFeatureSlotMask |=
				1u << args.featureSlot;
			LogOnce(slotEvaluateSuccessLogged_[args.featureSlot], [&]() {
				logger::info(
					"[DLSSNR] First Feature 18 evaluate succeeded: slot={}, color={}x{}, guides={}x{}, output={}x{}, controlMask={}x{}, upscaling={}, reset={}, stereoBatch={}",
					args.featureSlot,
					args.colorWidth,
					args.colorHeight,
					args.guideWidth,
					args.guideHeight,
					args.outputWidth,
					args.outputHeight,
					args.controlMaskWidth,
					args.controlMaskHeight,
					args.featureUpscaling,
					effectiveReset,
					a_args.size() == 2);
			});
		}
		snapshot_.lastCompletedStage = RendererStage::FeatureEvaluate;

		if (!interop_.EndFeatureTiming(commandList)) {
			const bool aborted = interop_.AbortD3D12();
			recordingGuard.active = interop_.IsRecording();
			return FailLocked(
				RendererStage::CommandEnd,
				aborted ? E_FAIL : interop_.LastError(),
				"D3D12 Feature 18 timestamp recording could not end",
				a_args.front().featureSlot,
				true,
				!aborted);
		}
		TransitionResources(commandList, resourceSpan, false);
		activeStage_ = RendererStage::CommandEnd;
		if (!interop_.EndD3D12()) {
			recordingGuard.active = interop_.IsRecording();
			return FailLocked(
				RendererStage::CommandEnd,
				interop_.LastError(),
				std::format("D3D12 command submission failed: {}", interop_.LastOperation()),
				a_args.front().featureSlot,
				true);
		}
		recordingGuard.active = false;
		snapshot_.lastCompletedStage = RendererStage::CommandEnd;

		activeStage_ = RendererStage::OutputCommit;
		RefreshRuntimeTelemetryLocked();
		if (const HRESULT reason = GetDeviceRemovalReasonLocked(S_OK); FAILED(reason)) {
			return FailLocked(
				RendererStage::OutputCommit,
				reason,
				"device removal was detected before the external output commit",
				a_args.front().featureSlot,
				true);
		}

		// Both private inputs are prepared before submission and neither caller-owned
		// output is written until every eye has recorded successfully.
		const auto outputCommitStarted = std::chrono::steady_clock::now();
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			a_args.front().context->CopyResource(
				resources[index].output.texture.Get(), slots[index]->output.resource11.Get());
		}
		if (const HRESULT reason = GetDeviceRemovalReasonLocked(S_OK); FAILED(reason)) {
			return FailLocked(
				RendererStage::OutputCommit,
				reason,
				"device removal followed the external output commit",
				a_args.front().featureSlot,
				true);
		}
		RecordCpuDuration(
			snapshot_.performance.outputCommitCpuEnqueueSamples,
			snapshot_.performance.outputCommitCpuEnqueueMicroseconds,
			snapshot_.performance.lastOutputCommitCpuEnqueueMicroseconds,
			snapshot_.performance.maximumOutputCommitCpuEnqueueMicroseconds,
			outputCommitStarted);
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			slots[index]->historyKey = resources[index].historyKey;
			slots[index]->lastSuccessfulFrame = a_args[index].frameId;
			slots[index]->historyValid = true;
		}
		activeStage_ = RendererStage::Complete;
		for (const auto& args : a_args) {
			SetActiveFeatureSlotLocked(args.featureSlot);
			SucceedLocked(args.featureSlot);
		}
		RefreshInteropTelemetryLocked();
		return true;
	}

	bool Renderer::State::ResetLocked(bool a_resetShader, bool a_destruction)
	{
		return TeardownBackendLocked(a_resetShader, a_destruction, false);
	}

	void Renderer::State::ShutdownForDestruction() noexcept
	{
		try {
			std::scoped_lock lock(mutex_);
			if (!TeardownBackendLocked(true, true, false)) {
				AbandonRuntimeOwnershipNoexcept();
				AbandonSlotsLocked();
			}
		} catch (...) {
			AbandonRuntimeOwnershipNoexcept();
			AbandonSlotsLocked();
		}
	}

	Renderer& Renderer::Instance()
	{
		static Renderer instance;
		return instance;
	}

	Renderer::Renderer()
	{
		// Construct the runtime first so Renderer teardown precedes runtime teardown.
		(void)Runtime::Instance();
		state_ = new State();
	}

	Renderer::~Renderer()
	{
		if (!state_)
			return;
		state_->ShutdownForDestruction();
		delete state_;
		state_ = nullptr;
	}

	bool Renderer::Apply(
		const RendererApplyArgs& a_args,
		RendererApplyOutcome* a_outcome)
	{
		std::scoped_lock lock(state_->mutex_);
		RendererApplyOutcome outcome{};
		const auto failuresBefore = state_->snapshot_.counters.failures;
		state_->SetActiveFeatureSlotLocked(a_args.featureSlot);
		try {
			CS_PROFILE_SCOPE("Upscaling::DLSSNeuralRendering");
			const bool succeeded = state_->ApplyLocked(a_args, outcome);
			if (a_outcome)
				*a_outcome = outcome;
			state_->SetActiveFeatureSlotLocked(Runtime::kFeatureSlotCount);
			return succeeded;
		} catch (...) {
			const auto failureFeatureSlot =
				state_->ActiveFeatureSlotOrLocked(a_args.featureSlot);
			state_->QuarantineAfterUnexpectedFailureLocked(
				state_->activeStage_,
				failureFeatureSlot,
				true,
				failuresBefore);
			if (a_outcome)
				*a_outcome = outcome;
			state_->SetActiveFeatureSlotLocked(Runtime::kFeatureSlotCount);
			return false;
		}
	}

	bool Renderer::ApplyStereo(
		const std::array<RendererApplyArgs, 2>& a_args,
		RendererApplyOutcome* a_outcome)
	{
		std::scoped_lock lock(state_->mutex_);
		RendererApplyOutcome outcome{};
		Increment(state_->snapshot_.counters.stereoAttempts);
		const auto failuresBefore = state_->snapshot_.counters.failures;
		state_->SetActiveFeatureSlotLocked(a_args[0].featureSlot);
		try {
			CS_PROFILE_SCOPE("Upscaling::DLSSNeuralRenderingStereo");
			const bool succeeded = state_->ApplyStereoLocked(a_args, outcome);
			Increment(
				succeeded ? state_->snapshot_.counters.stereoSuccesses :
							state_->snapshot_.counters.stereoFailures);
			if (a_outcome)
				*a_outcome = outcome;
			state_->SetActiveFeatureSlotLocked(Runtime::kFeatureSlotCount);
			return succeeded;
		} catch (...) {
			const auto failureFeatureSlot =
				state_->ActiveFeatureSlotOrLocked(a_args[0].featureSlot);
			state_->QuarantineAfterUnexpectedFailureLocked(
				state_->activeStage_,
				failureFeatureSlot,
				true,
				failuresBefore);
			Increment(state_->snapshot_.counters.stereoFailures);
			if (a_outcome)
				*a_outcome = outcome;
			state_->SetActiveFeatureSlotLocked(Runtime::kFeatureSlotCount);
			return false;
		}
	}

	bool Renderer::ApplySequentialStereo(
		const std::array<RendererApplyArgs, 2>& a_args,
		RendererApplyOutcome* a_outcome)
	{
		std::scoped_lock lock(state_->mutex_);
		RendererApplyOutcome outcome{};
		const auto failuresBefore = state_->snapshot_.counters.failures;
		state_->SetActiveFeatureSlotLocked(a_args[0].featureSlot);
		try {
			CS_PROFILE_SCOPE("Upscaling::DLSSNeuralRenderingSequentialStereo");
			const bool succeeded =
				state_->ApplySequentialStereoLocked(a_args, outcome);
			if (a_outcome)
				*a_outcome = outcome;
			state_->SetActiveFeatureSlotLocked(Runtime::kFeatureSlotCount);
			return succeeded;
		} catch (...) {
			const auto failureFeatureSlot =
				state_->ActiveFeatureSlotOrLocked(a_args[0].featureSlot);
			state_->QuarantineAfterUnexpectedFailureLocked(
				state_->activeStage_,
				failureFeatureSlot,
				true,
				failuresBefore);
			if (a_outcome)
				*a_outcome = outcome;
			state_->SetActiveFeatureSlotLocked(Runtime::kFeatureSlotCount);
			return false;
		}
	}

	bool Renderer::Reset()
	{
		std::scoped_lock lock(state_->mutex_);
		try {
			return state_->ResetLocked(false, false);
		} catch (const std::exception& exception) {
			state_->QuarantineAfterUnexpectedFailureLocked(
				RendererStage::Quarantined,
				Runtime::kFeatureSlotCount,
				false,
				0);
			try {
				logger::error("[DLSSNR] Renderer reset threw: {}", exception.what());
			} catch (...) {
			}
			return false;
		} catch (...) {
			state_->QuarantineAfterUnexpectedFailureLocked(
				RendererStage::Quarantined,
				Runtime::kFeatureSlotCount,
				false,
				0);
			try {
				logger::error("[DLSSNR] Renderer reset threw an unknown exception");
			} catch (...) {
			}
			return false;
		}
	}

	void Renderer::ResetShaderCache()
	{
		std::scoped_lock lock(state_->mutex_);
		try {
			(void)state_->ResetLocked(true, false);
		} catch (const std::exception& exception) {
			state_->QuarantineAfterUnexpectedFailureLocked(
				RendererStage::Quarantined,
				Runtime::kFeatureSlotCount,
				false,
				0);
			try {
				logger::error("[DLSSNR] Shader-cache reset threw: {}", exception.what());
			} catch (...) {
			}
		} catch (...) {
			state_->QuarantineAfterUnexpectedFailureLocked(
				RendererStage::Quarantined,
				Runtime::kFeatureSlotCount,
				false,
				0);
			try {
				logger::error("[DLSSNR] Shader-cache reset threw an unknown exception");
			} catch (...) {
			}
		}
	}

	RendererSnapshot Renderer::GetSnapshot() const
	{
		std::scoped_lock lock(state_->mutex_);
		return state_->SnapshotLocked();
	}

	bool Renderer::IsFailureLatched() const
	{
		std::scoped_lock lock(state_->mutex_);
		return state_->IsFailureLatchedLocked();
	}

	bool Renderer::IsQuarantined() const
	{
		std::scoped_lock lock(state_->mutex_);
		return state_->IsQuarantinedLocked();
	}
}
