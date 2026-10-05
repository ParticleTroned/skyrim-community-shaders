#include "Renderer.h"
#include "GpuPass.h"

#include "CapacityFallback.h"
#include "ColorPipeline.h"
#include "D3D12Interop.h"
#include "ExperimentalKernelBatch.h"
#include "PipelinePolicy.h"
#include "SourceTransport.h"
#include "Utils/D3D.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "ExecutionEvidenceJson.h"
#	include "MeasuredPlanJson.h"
#	include "MeasuredPlanSearch.h"
#	include <atomic>
#	include "CompactInputLayout.h"
#	include "ComputeStateGuard.h"
#	include "SharedContextPolicy.h"
#	include "ReplayCapture.h"
#	include <exception>
#endif

#include <DirectXTex.h>
#include <SKSE/SKSE.h>
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

	std::optional<std::uint64_t> LogicalTextureBytes(std::uint32_t format, std::uint32_t width, std::uint32_t height) noexcept
	{
		std::size_t row = 0, slice = 0;
		return width && height && SUCCEEDED(DirectX::ComputePitch(static_cast<DXGI_FORMAT>(format), width, height, row, slice)) ?
		           std::optional<std::uint64_t>(slice) :
		           std::nullopt;
	}

	namespace
	{
		constexpr std::uint32_t kMaximumTextureDimension =
			D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
		constexpr std::size_t kMaximumTransitionResourceCount = kMaximumRegionEvaluations * 5;
		static_assert(kMaximumRegionEvaluations == kMaximumExecutionRegions);

		ExecutionTexture DescribeExecutionTexture(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format, ComputeSubrect work)
		{
			return { { width, height }, static_cast<std::uint32_t>(format), work,
				LogicalTextureBytes(format, width, height), LogicalTextureBytes(format, work.width, work.height) };
		}

		void RecordExecutionCopy(const std::shared_ptr<ExecutionEvidence>& evidence, std::size_t region,
			std::optional<std::uint64_t> bytes) noexcept
		{
			if (evidence)
				evidence->Update([&](auto& snapshot) {
					if (bytes)
						snapshot.regions[region].copiedLogicalBytes += *bytes;
					else
						snapshot.regions[region].copyBytesKnown = false;
				});
		}

		struct ExecutionCompletionGuard
		{
			std::shared_ptr<ExecutionEvidence> evidence;
			const RendererStage& stage;
			bool succeeded = false;
			std::optional<RendererStage> failureOverride;
			~ExecutionCompletionGuard()
			{
				if (evidence)
					evidence->Update([&](auto& snapshot) {
						snapshot.finished = true;
						snapshot.succeeded = succeeded;
						snapshot.failureStage = succeeded ? 0u : static_cast<std::uint32_t>(failureOverride.value_or(stage));
					});
			}
		};

		class ExecutionCpuTimer
		{
		public:
			ExecutionCpuTimer(std::shared_ptr<ExecutionEvidence> evidence, std::optional<std::uint64_t> ExecutionSnapshot::* field) :
				evidence_(std::move(evidence)), field_(field), started_(evidence_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
			~ExecutionCpuTimer() { Stop(); }
			void Stop() noexcept
			{
				if (!evidence_)
					return;
				const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - started_)
						.count());
				evidence_->Update([&](auto& snapshot) { snapshot.*field_ = (snapshot.*field_).value_or(0) + elapsed; });
				evidence_.reset();
			}

		private:
			std::shared_ptr<ExecutionEvidence> evidence_;
			std::optional<std::uint64_t> ExecutionSnapshot::* field_;
			std::chrono::steady_clock::time_point started_;
		};

		[[nodiscard]] constexpr bool IsSourceWorldFrameContinuous(
			std::uint32_t a_previous,
			std::uint32_t a_current) noexcept
		{
			return IsSequentialFrame(a_previous, a_current);
		}

		// Re-evaluating one frozen frame also reuses that frame's motion vectors,
		// so it must begin from reset history instead of accumulating them again.
		static_assert(!IsSourceWorldFrameContinuous(41u, 41u));
		static_assert(IsSourceWorldFrameContinuous(41u, 42u));
		static_assert(!IsSourceWorldFrameContinuous(41u, 43u));

		struct CopyDepthGuideConstants
		{
			std::uint32_t offsetX = 0;
			std::uint32_t offsetY = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
		};

		static_assert(sizeof(CopyDepthGuideConstants) == 16);

		[[nodiscard]] ComputeSubrect ResolveComputeSubrect(
			const RendererApplyArgs& a_args) noexcept
		{
			return a_args.computeSubrect.IsValid() ?
			           a_args.computeSubrect :
			           BuildCenteredComputeSubrect(
						   a_args.outputWidth,
						   a_args.outputHeight,
						   a_args.tuning.singleSubrectScale);
		}

		[[nodiscard]] D3D11_BOX MakeCopyBox(
			const ComputeSubrect& a_subrect) noexcept
		{
			return {
				a_subrect.baseX,
				a_subrect.baseY,
				0u,
				a_subrect.baseX + a_subrect.width,
				a_subrect.baseY + a_subrect.height,
				1u,
			};
		}

		void CopyTextureSubrect(
			ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source,
			const ComputeSubrect& sourceRect, std::uint32_t destinationX, std::uint32_t destinationY) noexcept
		{
			const auto box = MakeCopyBox(sourceRect);
			context->CopySubresourceRegion(destination, 0, destinationX, destinationY, 0, source, 0, &box);
		}

		void CopyTextureSubrect(
			ID3D11DeviceContext* a_context,
			ID3D11Resource* a_destination,
			ID3D11Resource* a_source,
			const ComputeSubrect& a_subrect) noexcept
		{
			CopyTextureSubrect(a_context, a_destination, a_source, a_subrect, a_subrect.baseX, a_subrect.baseY);
		}

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
				left.tuning.uiCorrection == right.tuning.uiCorrection &&
				left.tuning.singleSubrectScale == right.tuning.singleSubrectScale;
			if (SameIdentity(left.device, right.device) &&
				SameIdentity(left.context, right.context) &&
				left.frameId == right.frameId &&
				left.sourceWorldFrame == right.sourceWorldFrame &&
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

			return "stereo eyes require one device, context, frame, generation, insertion point, feature mode, reset policy, tuning, ordered route pair, source world frame, and disjoint resources";
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
			       a_tuning.style <= 3 &&
			       std::isfinite(a_tuning.singleSubrectScale) &&
			       a_tuning.singleSubrectScale >= 0.25f &&
			       a_tuning.singleSubrectScale <= 1.0f;
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
				context_->CSGetConstantBuffers(0, 1, &constantBuffer_);
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
					context_->CSSetConstantBuffers(0, 1, &constantBuffer_);
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
				if (constantBuffer_)
					constantBuffer_->Release();
			}

			[[nodiscard]] bool Captured() const noexcept { return captured_; }

		private:
			ID3D11DeviceContext* context_ = nullptr;
			ID3D11ComputeShader* shader_ = nullptr;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classInstances_{};
			UINT classInstanceCount_ = 0;
			ID3D11ShaderResourceView* shaderResource_ = nullptr;
			ID3D11UnorderedAccessView* unorderedAccess_ = nullptr;
			ID3D11Buffer* constantBuffer_ = nullptr;
			bool captured_ = false;
		};

		struct RecordingGuard
		{
			explicit RecordingGuard(D3D12Interop& a_interop) : interop(a_interop) {}
			bool Abort()
			{
				const bool aborted = interop.AbortD3D12();
				active = interop.IsRecording();
				if (aborted && kernelBatch)
					kernelBatch->Aborted();
				return aborted;
			}
			~RecordingGuard() noexcept
			{
				if (!active)
					return;
				try {
					(void)Abort();
				} catch (...) {
					// The outer renderer boundary quarantines unexpected unwind paths.
				}
			}

			D3D12Interop& interop;
			ExperimentalKernelBatch* kernelBatch = nullptr;
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
			bool featureUpscaling = false;
			bool sharedSourceTransport = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			bool compact = false;
#endif

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
			ComputeSubrect computeSubrect{};
			std::uint64_t regionIdentity = 0;
			std::uint64_t colorInputEpoch = 0;
			bool useAutoMask = false;
			bool uiCorrection = false;

			bool operator==(const HistoryKey&) const = default;
		};

#ifdef DEVBENCH_BRIDGE_ENABLED
		struct CapacityKey
		{
			ResourceKey resources{};
			std::uintptr_t device = 0;
			std::uint32_t slot = 0;
			std::uint32_t evaluationCount = 0;
			bool operator==(const CapacityKey&) const = default;
		};
		CapacityRejections<CapacityKey, Runtime::kFeatureSlotCount> capacityRejections_;
		CapacityKey requestedCapacity_{};
		CapacityFallback capacityFallback_{};
		std::uint32_t requestedRegionCount_ = 0;
		std::atomic_bool measuredPlanEnabled_{ false };
		std::atomic_bool measuredPlanContinuous_{ false }, measuredPlanInspectionRequested_{ false };
		bool measuredPlanEvaluating_ = false;
		bool forceFullCoordinates_ = false;
		std::array<CompactInputRetention, Runtime::kFeatureSlotCount> compactRetention_{};
		ComPtr<ID3D11ComputeShader> copyCompactDepthGuideCS_;
		std::optional<MeasuredPlan::Profile> measuredPlanProfile_;
		std::array<MeasuredPlan::State, kLogicalFeatureSlotCount> measuredPlanState_{};
		nlohmann::json measuredPlanDiagnostics_;
		MeasuredPlan::Calibration measuredCalibration_;
		bool measuredCalibrationEvidenceFailed_ = false;
		nlohmann::json measuredCalibrationRecords_ = nlohmann::json::array();
		nlohmann::json measuredCalibrationKeys_ = nlohmann::json::array();
		static std::uint64_t CalibrationMilliseconds()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}
		std::array<RendererApplyArgs, kEyeCount> SelectMeasuredPlanLocked(std::span<const RendererApplyArgs> args);
		struct ValidatedResources;
		void ApplyCompactLayoutLocked(const RendererApplyArgs& args, ValidatedResources& resources) const;
		nlohmann::json MeasuredPlanKeyLocked(std::span<const RendererApplyArgs> args,
			std::span<const ValidatedResources> resources, std::span<const std::uint64_t> clusters, bool& rejected) const;
#endif

		struct Slot
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			std::uint64_t resourceSerial = 0;
#endif
			SharedTexture color;
			SharedTexture depth;
			SharedTexture motionVectors;
			SharedTexture controlMask;
			SharedTexture output;
			Color::Work colorWork;
			std::uint32_t sourceTransportOwnerSlot = 0;
			ResourceKey resourceKey{};
			HistoryKey historyKey{};
			std::uint32_t lastSuccessfulFrame =
				std::numeric_limits<std::uint32_t>::max();
			std::uint32_t lastSuccessfulSourceWorldFrame =
				std::numeric_limits<std::uint32_t>::max();
			bool resourcesValid = false;
			bool historyValid = false;
		};

		struct ValidatedResources
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			bool compactAttempted = false;
#endif
			TextureInfo color;
			TextureInfo depth;
			TextureInfo motionVectors;
			TextureInfo controlMask;
			TextureInfo output;
			std::uintptr_t controlMaskIdentity = 0;
			DXGI_FORMAT depthViewFormat = DXGI_FORMAT_UNKNOWN;
			RoiDescriptor roi{};
			NativeEvaluationLayout nativeLayout{};
			ResourceKey resourceKey{};
			HistoryKey historyKey{};
		};

		struct ValidationFailure
		{
			HRESULT result = S_OK;
			std::string detail;

			explicit operator bool() const noexcept { return FAILED(result); }
		};

#ifdef DEVBENCH_BRIDGE_ENABLED
		void CaptureReplayBatch(std::span<const RendererApplyArgs> args,
			std::span<Slot* const> slots, std::span<const ValidatedResources> resources,
			const std::shared_ptr<ExecutionEvidence>& execution) noexcept;
#endif

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
		bool ApplyRegionBatchLocked(std::span<const RendererApplyArgs> a_args, RendererApplyOutcome& a_outcome
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			std::span<const SharedContext::Plan> a_shared = {}
#endif
		);
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::optional<bool> TrySharedContextBatchLocked(std::span<const RendererApplyArgs>, RendererApplyOutcome&);
		struct SharedContextObservation
		{
			SharedContext::Settings settings{};
			const char* reason = "not_requested";
			std::uint32_t frame = 0, sourceWorldFrame = 0, count = 0, requestedEvaluations = 0;
			bool applied = false, succeeded = false;
			std::array<std::uint32_t, kEyeCount> logicalEyes{};
			std::array<SharedContext::Plan, kEyeCount> plans{};
		};
		std::array<SharedContextObservation, 2> sharedContextObservations_{};
		nlohmann::json SharedContextJsonLocked(std::size_t route) const;
#endif
		bool ResetLocked(bool a_resetShader, bool a_destruction);
		void ShutdownForDestruction() noexcept;

		RendererSnapshot SnapshotLocked()
		{
			RefreshInteropTelemetryLocked();
			return snapshot_;
		}
		bool IsFailureLatchedLocked() const noexcept { return failureLatched_; }
		bool IsQuarantinedLocked() const noexcept { return quarantined_; }

		// Latch a configuration for both eyes of each source/evaluation transaction,
		// including callers that enter through separate public Apply calls.
		void CaptureColorConfiguration(const RendererApplyArgs& args)
		{
			const auto route = args.featureSlot < Runtime::kFeatureSlotCount ?
			                       ClassifyFeatureSlotMask(1u << args.featureSlot) :
			                       FeatureSlotRoute::Unexpected;
			const std::size_t routeIndex = route == FeatureSlotRoute::Submit ? 1u : 0u;
			captureRoute_ = routeIndex;
			const ColorTransactionKey key{ args.frameId, args.sourceWorldFrame, args.generation, args.insertionPoint };
			const bool transactionChanged = !colorTransactionValid_[routeIndex] || key != colorTransactionKeys_[routeIndex];
			if (transactionChanged) {
				colorConfigurations_[routeIndex] = Color::Registry::Instance().Snapshot();
				colorTransactionKeys_[routeIndex] = key;
				colorTransactionValid_[routeIndex] = true;
			}
			colorConfiguration_ = colorConfigurations_[routeIndex];
			auto& capture = captureInputs_[routeIndex];
			wholePass_.reset();
			if (!Color::Registry::Instance().CaptureEvidenceEnabled()) {
				capture = {};
			} else if (transactionChanged || capture.sourceTransactionId != args.executionContext.sourceTransactionId ||
					   capture.captureEpoch != args.executionContext.captureEpoch ||
					   !capture.Matches(static_cast<std::uint32_t>(routeIndex), args.frameId,
						   args.sourceWorldFrame, args.generation, static_cast<std::uint32_t>(args.insertionPoint))) {
				capture = {};
				capture.valid = true;
				capture.frame = args.frameId;
				capture.sourceWorldFrame = args.sourceWorldFrame;
				capture.generation = args.generation;
				capture.sourceTransactionId = args.executionContext.sourceTransactionId;
				capture.captureEpoch = args.executionContext.captureEpoch;
				capture.insertion = static_cast<std::uint32_t>(args.insertionPoint);
				capture.route = static_cast<std::uint32_t>(routeIndex);
				capture.configuration = colorConfiguration_;
			}
			if (capture.valid) {
				try {
					wholePass_ = std::make_shared<Util::PassTimingCapture>();
				} catch (...) {
					++capture.executionEvidenceFailures;
				}
			}
		}
		Color::Configuration colorConfiguration_{};
		std::array<CaptureInputs, 2> captureInputs_{};
		std::size_t captureRoute_ = 0;
		Util::PassTimingHandle wholePass_;
		void FinishCapture(const RendererApplyOutcome& outcome) noexcept
		{
			try {
				for (auto& capture : std::span<CaptureInputs>(&captureInputs_[captureRoute_], 1)) {
					if (!capture.valid || capture.configuration.revision != colorConfiguration_.revision)
						continue;
					capture.attemptedMask |= outcome.evaluationAttemptedFeatureSlotMask;
					capture.succeededMask |= outcome.evaluationSucceededFeatureSlotMask;
					for (std::size_t slot = 0; slot < capture.slots.size(); ++slot) {
						const auto& observation = slots_[slot].colorWork.observation;
						if ((capture.slotMask & (1u << slot)) != 0 && observation.revision == capture.configuration.revision &&
							observation.frame == capture.frame && observation.sourceWorldFrame == capture.sourceWorldFrame &&
							observation.generation == capture.generation && observation.insertion == capture.insertion)
							capture.slots[slot] = observation;
					}
				}
			} catch (...) {
				for (auto& capture : captureInputs_)
					capture.valid = false;
			}
		}
		mutable std::mutex mutex_;
#ifdef DEVBENCH_BRIDGE_ENABLED
		LifetimeDiagnostics lifetimeDiagnostics_;
		LifetimeRecord* activeLifetime_ = nullptr;
		std::uint64_t backendSerial_ = 0, resourceSerial_ = 0;
		struct LifetimeGuard
		{
			State& owner;
			std::optional<LifetimeRecord> record;
			LifetimeRecord* parent = nullptr;
			bool enabled = false;
			int exceptions = 0;
			LifetimeGuard(State& state, LifetimeOperation operation) :
				owner(state)
			{
				if (!Color::Registry::Instance().CaptureEvidenceEnabled() || state.lifetimeDiagnostics_.Frozen())
					return;
				record.emplace();
				enabled = state.BeginLifetimeLocked(*record, operation);
				if (enabled) {
					exceptions = std::uncaught_exceptions();
					parent = state.activeLifetime_;
					owner.activeLifetime_ = &*record;
				}
			}
			~LifetimeGuard() noexcept
			{
				if (enabled) {
					owner.FinishLifetimeLocked(*record, std::uncaught_exceptions() > exceptions);
					owner.activeLifetime_ = parent;
				}
			}
		};
#endif
		std::unique_ptr<ExperimentalKernelBatch> kernelBatch_;
		std::optional<ExperimentalKernelBatch::Mode> kernelBatchMode_;
		std::filesystem::path kernelBatchManifest_;
		RoiExecutionMode roiExecutionMode_ = RoiExecutionMode::AutomaticSingle;
		bool kernelBatchOverride_ = false, kernelInspectUnqualified_ = false;
		bool kernelBatchApplied_ = false;
		bool kernelBatchFallbackLatched_ = false;
		ExperimentalKernelBatch::Status kernelBatchRejection_{};
		std::string kernelBatchFrameReason_ = "off";
		nlohmann::json KernelBatchJsonLocked() const;

	private:
#ifdef DEVBENCH_BRIDGE_ENABLED
		bool BeginLifetimeLocked(LifetimeRecord& record, LifetimeOperation operation) noexcept;
		void FinishLifetimeLocked(LifetimeRecord& record, bool unwinding) noexcept;
		void CaptureLifetimeResourcesLocked(LifetimeRegion& region, const Slot& slot) const noexcept;
#endif
		void FinalizeResourceKeysLocked(const RendererApplyArgs& args, ValidatedResources& resources) const;
		ValidationFailure ValidateLocked(
			const RendererApplyArgs& a_args,
			ValidatedResources& a_resources
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			bool recordEvidence = true
#endif
		);
		bool ValidateD3D12FormatsLocked(
			const ValidatedResources& a_resources,
			std::string& a_detail) const;
		bool EnsureBackendLocked(const RendererApplyArgs& a_args, const std::shared_ptr<ExecutionEvidence>& a_evidence, bool a_batchCandidate);
		bool EnsureSlotLocked(
			std::uint32_t a_slot,
			const ValidatedResources& a_resources,
			const std::shared_ptr<ExecutionEvidence>& a_evidence, std::size_t a_region,
			const Slot* a_sourceOwner);
		bool CopyDepthBatchLocked(
			std::span<const RendererApplyArgs> a_args,
			std::span<Slot* const> a_slots,
			std::span<const ValidatedResources> a_resources,
			const std::shared_ptr<ExecutionEvidence>& a_evidence);
		bool TeardownBackendLocked(
			bool a_resetShader,
			bool a_destruction,
			bool a_countApplyFailure, const std::shared_ptr<ExecutionEvidence>& a_evidence = {});
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

		struct ColorTransactionKey
		{
			std::uint32_t frame, worldFrame;
			std::uint64_t generation;
			InsertionPoint insertion;
			bool operator==(const ColorTransactionKey&) const = default;
		};
		std::array<ColorTransactionKey, 2> colorTransactionKeys_{};
		std::array<Color::Configuration, 2> colorConfigurations_{};
		std::array<bool, 2> colorTransactionValid_{};
		D3D12Interop interop_;
		Color::Pipeline colorPipeline_;
		std::array<Slot, Runtime::kFeatureSlotCount> slots_{};
		ComPtr<ID3D11Device> device_;
		ComPtr<ID3D11DeviceContext> context_;
		ComPtr<ID3D11ComputeShader> copyDepthGuideCS_;
		ComPtr<ID3D11Buffer> copyDepthGuideCB_;
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

#ifdef DEVBENCH_BRIDGE_ENABLED
	bool Renderer::State::BeginLifetimeLocked(LifetimeRecord& record, LifetimeOperation operation) noexcept
	{
		try {
			record.operation = operation;
			record.backendSerial = backendSerial_;
			record.requestFrame = snapshot_.frameId;
			record.sourceFrame = snapshot_.sourceWorldFrame;
			record.generation = snapshot_.generation;
			record.insertion = static_cast<std::uint32_t>(snapshot_.insertionPoint);
			record.before = interop_.GetLifetimeSnapshot();
			return true;
		} catch (...) {
			lifetimeDiagnostics_.DiagnosticFailure();
			return false;
		}
	}

	void Renderer::State::FinishLifetimeLocked(LifetimeRecord& record, bool unwinding) noexcept
	{
		try {
			if (!record.failureObserved) {
				record.stage = static_cast<std::uint32_t>(activeStage_);
				record.after = interop_.GetLifetimeSnapshot();
			}
		} catch (...) {
			lifetimeDiagnostics_.DiagnosticFailure();
		}
		if (unwinding && !record.failureObserved) {
			record.failureObserved = true;
			record.result = E_UNEXPECTED;
			record.succeeded = false;
		}
		lifetimeDiagnostics_.Record(record);
	}

	void Renderer::State::CaptureLifetimeResourcesLocked(LifetimeRegion& region, const Slot& slot) const noexcept
	{
		region.resourceSerial = slot.resourceSerial;
		region.resourcesRebuilt = region.previousResourceSerial != region.resourceSerial;
		const std::array<const SharedTexture*, 5> resources{ &slot.color, &slot.depth, &slot.motionVectors, &slot.output, &slot.controlMask };
		for (std::size_t index = 0; index < resources.size(); ++index) {
			region.resources11[index] = reinterpret_cast<std::uintptr_t>(resources[index]->resource11.Get());
			region.resources12[index] = reinterpret_cast<std::uintptr_t>(resources[index]->resource12.Get());
		}
	}

#endif

	Renderer::State::ValidationFailure Renderer::State::ValidateLocked(
		const RendererApplyArgs& a_args,
		ValidatedResources& a_resources
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		bool recordEvidence
#endif
	)
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
		const auto invalidFrame = std::numeric_limits<std::uint32_t>::max();
		if (a_args.frameId == invalidFrame)
			return fail("Feature 18 evaluation frame is invalid");
		if (a_args.sourceWorldFrame == invalidFrame ||
			a_args.sourceWorldFrame > a_args.frameId) {
			return fail(
				"Feature 18 source world frame is invalid or newer than its evaluation frame");
		}
		if (!a_args.generation)
			return fail("feature-slot generation must be nonzero");
		if (!IsValidInsertionPoint(a_args.insertionPoint))
			return fail("Feature 18 insertion point is invalid");
		if (colorConfiguration_.Enabled() &&
			(a_args.colorWidth != a_args.outputWidth || a_args.colorHeight != a_args.outputHeight))
			return fail("NR colour processing currently requires matching colour/output dimensions; guides may be lower resolution");
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
		const auto expectedFeatureUpscaling = ResolveFeatureUpscaling(
			a_args.guideWidth, a_args.guideHeight,
			a_args.outputWidth, a_args.outputHeight);
		if (!expectedFeatureUpscaling)
			return fail("Feature 18 guide-to-output geometry is unsupported");
		if (a_args.featureUpscaling != *expectedFeatureUpscaling) {
			return fail(
				"Feature 18 upscaling mode does not match its guide-to-output geometry");
		}
		const bool hasExplicitSubrectValue =
			a_args.computeSubrect.baseX || a_args.computeSubrect.baseY ||
			a_args.computeSubrect.width || a_args.computeSubrect.height;
		if (hasExplicitSubrectValue && !a_args.computeSubrect.IsValid())
			return fail("the explicit Feature 18 compute rectangle is incomplete");
		const auto provider = ResolveComputeSubrect(a_args);
		a_resources.roi = a_args.roi.value_or(BuildRoiDescriptor(
			std::nullopt, provider, { a_args.outputWidth, a_args.outputHeight }, false));
		if (a_args.characterVisualIsolation && (!a_args.roi || !a_args.roi->samplingSupport))
			return fail("character evaluation requires prepared ROI roles and current sampling support");
		if (auto violation = GetRoiDescriptorViolation(a_resources.roi, provider,
				{ a_args.outputWidth, a_args.outputHeight });
			!violation.empty())
			return fail(std::string(violation));
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
		a_resources.nativeLayout = BuildNativeEvaluationLayout(
			{ a_args.colorWidth, a_args.colorHeight }, { a_args.guideWidth, a_args.guideHeight },
			a_resources.roi.allocationCapacity, { a_args.controlMaskWidth, a_args.controlMaskHeight },
			a_resources.roi.inferenceContext, motionVectorScale, a_args.featureUpscaling);
		if (!a_resources.nativeLayout.color.valid.Fits(a_args.colorWidth, a_args.colorHeight) ||
			!a_resources.nativeLayout.depth.valid.Fits(a_args.guideWidth, a_args.guideHeight))
			return fail("the Feature 18 compute rectangle could not be mapped to its inputs");
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

#ifdef DEVBENCH_BRIDGE_ENABLED
		ApplyCompactLayoutLocked(a_args, a_resources);
#endif
		FinalizeResourceKeysLocked(a_args, a_resources);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (!recordEvidence)
			return {};
#endif
		for (auto& capture : captureInputs_) {
			if (!capture.Matches(a_args.featureSlot % 4u >= 2u ? 1u : 0u, a_args.frameId,
					a_args.sourceWorldFrame, a_args.generation, static_cast<std::uint32_t>(a_args.insertionPoint)) ||
				a_args.featureSlot >= capture.slots.size())
				continue;
			auto& observation = capture.slots[a_args.featureSlot];
			observation.frame = a_args.frameId;
			observation.sourceWorldFrame = a_args.sourceWorldFrame;
			observation.generation = a_args.generation;
			observation.insertion = capture.insertion;
			observation.slot = a_args.featureSlot;
			observation.revision = capture.configuration.revision;
			observation.mode = capture.configuration.EffectiveMode();
			observation.profile = Color::EffectiveProfile(capture.configuration, capture.insertion);
			observation.bypass = capture.configuration.experiments.transportBypass;
			observation.modelEditShown = capture.configuration.experiments.applyModelEdit;
			observation.lightingPreservation = Color::ResolveReconstructionSettings(capture.configuration.settings).lightingPreservation;
			observation.rect = a_resources.roi.ownedOutput;
			observation.sourceFormat = static_cast<std::uint32_t>(a_resources.resourceKey.colorFormat);
			observation.outputFormat = static_cast<std::uint32_t>(a_resources.resourceKey.outputFormat);
			capture.slotMask |= 1u << a_args.featureSlot;
		}
		return {};
	}

	void Renderer::State::FinalizeResourceKeysLocked(const RendererApplyArgs& a_args, ValidatedResources& a_resources) const
	{
		const bool hasControlMask = a_args.controlMask != nullptr;
		a_resources.resourceKey = {
			.colorWidth = a_resources.nativeLayout.color.backing.width,
			.colorHeight = a_resources.nativeLayout.color.backing.height,
			.guideWidth = a_resources.nativeLayout.depth.backing.width,
			.guideHeight = a_resources.nativeLayout.depth.backing.height,
			.outputWidth = a_resources.nativeLayout.output.backing.width,
			.outputHeight = a_resources.nativeLayout.output.backing.height,
			.controlMaskWidth = a_resources.nativeLayout.controlMask.backing.width,
			.controlMaskHeight = a_resources.nativeLayout.controlMask.backing.height,
			.colorFormat = a_resources.color.desc.Format,
			.motionFormat = a_resources.motionVectors.desc.Format,
			.outputFormat = a_resources.output.desc.Format,
			.controlMaskFormat = hasControlMask ?
			                         a_resources.controlMask.desc.Format :
			                         DXGI_FORMAT_UNKNOWN,
			.controlMaskPresent = hasControlMask,
			.featureUpscaling = a_args.featureUpscaling,
			.sharedSourceTransport = colorConfiguration_.experiments.SharedSourceTransportEnabled(),
		};
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_resources.roi.compactSource) {
			a_resources.resourceKey.compact = true;
			a_resources.resourceKey.sharedSourceTransport = false;
		}
#endif
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
			.computeSubrect = a_resources.roi.inferenceContext,
			.colorInputEpoch = colorConfiguration_.inputEpoch[static_cast<std::size_t>(a_args.insertionPoint)],
			.useAutoMask = a_args.tuning.useAutoMask,
			.uiCorrection = a_args.tuning.uiCorrection,
		};
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
		performance.lastFeatureLogicalEyeCount = telemetry.lastFeatureLogicalEyeCount;
		performance.lastFeatureSlotMask = telemetry.lastFeatureSlotMask;
#ifdef DEVBENCH_BRIDGE_ENABLED
		performance.lastExecutionPlan = telemetry.lastExecutionPlan;
#endif
		performance.lastInsertionPoint = telemetry.lastInsertionPoint;
	}

	void Renderer::State::SetRequestTelemetryLocked(
		const RendererApplyArgs& a_args) noexcept
	{
		snapshot_.featureSlot = a_args.featureSlot;
		snapshot_.frameId = a_args.frameId;
		snapshot_.sourceWorldFrame = a_args.sourceWorldFrame;
		snapshot_.generation = a_args.generation;
		snapshot_.insertionPoint = a_args.insertionPoint;
		snapshot_.colorWidth = a_args.colorWidth;
		snapshot_.colorHeight = a_args.colorHeight;
		snapshot_.guideWidth = a_args.guideWidth;
		snapshot_.guideHeight = a_args.guideHeight;
		snapshot_.outputWidth = a_args.outputWidth;
		snapshot_.outputHeight = a_args.outputHeight;
		snapshot_.computeSubrect =
			a_args.outputWidth && a_args.outputHeight ?
				ResolveComputeSubrect(a_args) :
				ComputeSubrect{};
		snapshot_.controlMaskWidth = a_args.controlMaskWidth;
		snapshot_.controlMaskHeight = a_args.controlMaskHeight;
		snapshot_.featureUpscaling = a_args.featureUpscaling;
		snapshot_.controlMaskPresent = a_args.controlMask != nullptr;
		snapshot_.useAutoMask = a_args.tuning.useAutoMask;
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (colorConfiguration_.experiments.SharedSourceTransportEnabled() && a_latch &&
			(a_stage == RendererStage::ResourceCreation || a_stage == RendererStage::DeviceCompatibility ||
				a_stage == RendererStage::FeatureEvaluate || deviceRemoved || a_forceQuarantine)) {
			const auto native = a_stage == RendererStage::FeatureEvaluate ? Runtime::Instance().CreationCapacityFailure() : CapacityFailure::Unsafe;
			const auto kind = !deviceRemoved && !a_forceQuarantine && (a_result == E_OUTOFMEMORY || native == CapacityFailure::Pressure)    ? CapacityRejectionKind::Pressure :
			                  !deviceRemoved && !a_forceQuarantine && (a_result == E_NOINTERFACE || native == CapacityFailure::Unsupported) ? CapacityRejectionKind::Unsupported :
			                                                                                                                                  CapacityRejectionKind::UnsafeProvider;
			capacityRejections_.Record({ requestedCapacity_, kind, a_result, Runtime::Instance().NgxResult() });
		}
		if (activeLifetime_) {
			activeLifetime_->failureObserved = true;
			activeLifetime_->stage = static_cast<std::uint32_t>(a_stage);
			activeLifetime_->result = deviceRemoved ? removalReason : a_result;
			try {
				activeLifetime_->after = interop_.GetLifetimeSnapshot();
			} catch (...) {
				lifetimeDiagnostics_.DiagnosticFailure();
			}
		}
#endif
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
		if (colorConfiguration_.Enabled() && a_slot < slots_.size()) {
			auto observation = slots_[a_slot].colorWork.observation;
			observation.frame = snapshot_.frameId;
			observation.sourceWorldFrame = snapshot_.sourceWorldFrame;
			observation.slot = a_slot;
			observation.revision = colorConfiguration_.revision;
			observation.failure = snapshot_.detail;
			observation.rect = {};
			observation.generation = snapshot_.generation;
			observation.insertion = static_cast<std::uint32_t>(snapshot_.insertionPoint);
			observation.mode = colorConfiguration_.EffectiveMode();
			observation.processed = false;
			Color::Registry::Instance().Record(observation);
		}
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
		bool a_countApplyFailure, const std::shared_ptr<ExecutionEvidence>& a_evidence)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		LifetimeGuard lifetime(*this, LifetimeOperation::BackendRetirement);
		if (lifetime.enabled) {
			for (std::size_t index = 0; index < slots_.size(); ++index)
				if (slots_[index].resourcesValid)
					lifetime.record->slotMask |= 1u << index;
		}
#endif
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
		if (interop_.IsInitialized() && !interop_.WaitForIdle(a_evidence)) {
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

		if (kernelBatch_) {
			if (!kernelBatch_->Shutdown(true))
				return FailLocked(RendererStage::RuntimeReset, E_FAIL,
					"kernel experiment ownership could not retire after GPU idle",
					Runtime::kFeatureSlotCount, true, true, a_countApplyFailure);
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
		if (interop_.IsInitialized() && !interop_.Shutdown(a_evidence)) {
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
		colorPipeline_.Reset();
		device_.Reset();
		context_.Reset();
		if (a_resetShader) {
			copyDepthGuideCS_.Reset();
#ifdef DEVBENCH_BRIDGE_ENABLED
			copyCompactDepthGuideCS_.Reset();
#endif
			copyDepthGuideCB_.Reset();
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (lifetime.enabled)
			lifetime.record->succeeded = true;
#endif
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
			slot.colorWork.Abandon();
			slot.resourcesValid = false;
			slot.historyValid = false;
		}
		colorPipeline_.Abandon();
	}

	void Renderer::State::AbandonRuntimeOwnershipNoexcept() noexcept
	{
		if (kernelBatch_)
			(void)kernelBatch_->Shutdown(false);
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

	bool Renderer::State::EnsureBackendLocked(const RendererApplyArgs& a_args, const std::shared_ptr<ExecutionEvidence>& a_evidence, bool a_batchCandidate)
	{
		const auto selected = a_args.roiExecutionMode;
		const auto desired = kernelBatchOverride_ ? kernelBatchMode_ :
		                     selected == RoiExecutionMode::Batched ?
		                                            std::optional(ExperimentalKernelBatch::Mode::SharedN2) :
		                                            std::nullopt;
		roiExecutionMode_ = selected;
		const bool methodChanged = desired != kernelBatchMode_;
		if (methodChanged) {
			kernelBatchFallbackLatched_ = false;
			kernelBatchRejection_ = {};
		}
		const bool backendIdentityChanged = device_ &&
		                                    (!SameIdentity(device_.Get(), a_args.device) ||
												!SameIdentity(context_.Get(), a_args.context));
		const auto batchStatus = kernelBatch_ ? kernelBatch_->GetStatus() : ExperimentalKernelBatch::Status{};
		const bool retireAdapter = kernelBatch_ && (methodChanged || backendIdentityChanged || !a_batchCandidate ||
													   kernelBatchFallbackLatched_ || !batchStatus.initialized || batchStatus.epochStale);
		const bool attach = desired && a_batchCandidate && !kernelBatchFallbackLatched_ && (!kernelBatch_ || retireAdapter);
		if (methodChanged || backendIdentityChanged || retireAdapter || attach) {
			ExecutionCpuTimer retirementTimer(a_evidence, &ExecutionSnapshot::resourceRetirementCpuMicroseconds);
			if (!TeardownBackendLocked(false, false, true, a_evidence))
				return false;
			if (kernelBatch_ && !methodChanged)
				kernelBatchRejection_ = kernelBatch_->GetStatus();
			kernelBatch_.reset();
			if (attach) {
				kernelBatchRejection_ = {};
				kernelBatch_ = std::make_unique<ExperimentalKernelBatch>();
			}
		}
		kernelBatchMode_ = desired;
		if (methodChanged || attach) {
			const bool privateBatch = desired == ExperimentalKernelBatch::Mode::SharedN2 || desired == ExperimentalKernelBatch::Mode::ClonedN2;
			kernelBatchManifest_ = privateBatch ? std::filesystem::path("Data/Shaders/Upscaling/NeuralRendering/KernelBatch") /
			                                          (desired == ExperimentalKernelBatch::Mode::ClonedN2 ? "cloned-n2.json" : "shared-n2.json") :
			                                      std::filesystem::path{};
		}
		if (!desired)
			kernelBatchFrameReason_ = "not_selected";

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
		if (!interop_.Initialize(adapter.Get(), a_args.device, a_args.context, a_evidence)) {
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		++backendSerial_;
#endif
		if (kernelBatch_ && kernelBatchMode_ &&
			!kernelBatch_->Initialize(interop_.Device(), runtime.Path(), kernelBatchManifest_, *kernelBatchMode_)) {
			const auto status = kernelBatch_->GetStatus();
			if (!status.retirementProven)
				return FailLocked(RendererStage::RuntimeInitialization, E_FAIL,
					"kernel hook restoration could not be proven after initialization failed",
					a_args.featureSlot, true, true);
			kernelBatchFallbackLatched_ = true;
			kernelBatchRejection_ = status;
			kernelBatchFrameReason_ = status.reason;
			kernelBatch_.reset();
		}
		Increment(snapshot_.counters.runtimeInitializations);
		snapshot_.lastCompletedStage = RendererStage::RuntimeInitialization;
		RefreshRuntimeTelemetryLocked();
		return true;
	}

	bool Renderer::State::EnsureSlotLocked(
		std::uint32_t a_slot,
		const ValidatedResources& a_resources,
		const std::shared_ptr<ExecutionEvidence>& a_evidence, std::size_t a_region,
		const Slot* a_sourceOwner)
	{
		auto& slot = slots_[a_slot];
		const auto ownerSlot = a_sourceOwner ? a_sourceOwner->sourceTransportOwnerSlot : a_slot;
		bool sameOwner = true;
		if (a_resources.resourceKey.sharedSourceTransport) {
			const auto identities = [](const Slot& source) {
				return std::array{ source.color.resource12.Get(), source.depth.resource12.Get(),
					source.motionVectors.resource12.Get(), source.controlMask.resource12.Get() };
			};
			const auto requested = a_sourceOwner ? identities(*a_sourceOwner) : identities(slot);
			sameOwner = CanReuseSourceBinding(slot.sourceTransportOwnerSlot, ownerSlot,
				identities(slot), a_sourceOwner ? &requested : nullptr);
		}
		if (a_evidence)
			a_evidence->Update([&](auto& evidence) {
				evidence.regions[a_region].rebuildReasons = !slot.resourcesValid                                      ? RebuildUnallocated :
				                                            slot.resourceKey != a_resources.resourceKey || !sameOwner ? RebuildResourceContractChanged :
				                                                                                                        RebuildNone;
			});
		if (slot.resourcesValid && slot.resourceKey == a_resources.resourceKey && sameOwner)
			return true;

		if (slot.resourcesValid) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			LifetimeGuard lifetime(*this, LifetimeOperation::SlotRetirement);
			if (lifetime.enabled) {
				lifetime.record->slotMask = 1u << a_slot;
				lifetime.record->regionCount = 1;
				lifetime.record->regions[0].slot = a_slot;
				lifetime.record->regions[0].previousResourceSerial = slot.resourceSerial;
				CaptureLifetimeResourcesLocked(lifetime.record->regions[0], slot);
			}
#endif
			ExecutionCpuTimer retirementTimer(a_evidence, &ExecutionSnapshot::resourceRetirementCpuMicroseconds);
			activeStage_ = RendererStage::ResourceRetirement;
			const bool idle = interop_.WaitForIdle(a_evidence);
			if (!idle) {
				return FailLocked(
					RendererStage::ResourceRetirement,
					interop_.LastError(),
					std::format("slot {} idle wait failed: {}", a_slot, interop_.LastOperation()),
					a_slot,
					true,
					true);
			}
			if (kernelBatch_ && !kernelBatch_->Shutdown(true))
				return FailLocked(RendererStage::ResourceRetirement, E_FAIL,
					"kernel experiment ownership could not retire before feature recreation", a_slot, true, true);
			if (kernelBatchMode_)
				kernelBatchFrameReason_ = "feature_recreated_reconfigure_required";
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
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (lifetime.enabled)
				lifetime.record->succeeded = true;
#endif
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
			true);
		D3D11_TEXTURE2D_DESC controlMaskDesc{};
		if (a_resources.resourceKey.controlMaskPresent) {
			controlMaskDesc = MakeSharedDescription(
				a_resources.resourceKey.controlMaskWidth,
				a_resources.resourceKey.controlMaskHeight,
				a_resources.resourceKey.controlMaskFormat,
				true);
		}

		const auto create = [&](const D3D11_TEXTURE2D_DESC& desc, SharedTexture& texture, const char* name) {
			const bool created = interop_.CreateSharedTexture(desc, texture, name);
			if (a_evidence)
				a_evidence->Update([&](auto& evidence) {
					const auto bytes = LogicalTextureBytes(desc.Format, desc.Width, desc.Height);
					if (created && bytes)
						evidence.regions[a_region].newlyAllocatedLogicalBytes += *bytes;
					else
						evidence.regions[a_region].allocationBytesKnown = false;
				});
			return created;
		};
		// Only source transport is shared; output, baseline and native history stay private.
		if (a_sourceOwner) {
			replacement.color = a_sourceOwner->color;
			replacement.depth = a_sourceOwner->depth;
			replacement.motionVectors = a_sourceOwner->motionVectors;
			replacement.controlMask = a_sourceOwner->controlMask;
		}
		if ((!a_sourceOwner && (!create(colorDesc, replacement.color, (prefix + "Color").c_str()) ||
								   !create(depthDesc, replacement.depth, (prefix + "Depth").c_str()) ||
								   !create(motionDesc, replacement.motionVectors, (prefix + "MotionVectors").c_str()) ||
								   (a_resources.resourceKey.controlMaskPresent &&
									   !create(
										   controlMaskDesc,
										   replacement.controlMask,
										   (prefix + "ControlMask").c_str())))) ||
			!create(outputDesc, replacement.output, (prefix + "Output").c_str())) {
			return FailLocked(
				RendererStage::ResourceCreation,
				interop_.LastError(),
				std::format("slot {} shared-resource creation failed: {}", a_slot, interop_.LastOperation()),
				a_slot,
				true);
		}

#ifdef DEVBENCH_BRIDGE_ENABLED
		replacement.resourceSerial = ++resourceSerial_;
#endif
		replacement.resourceKey = a_resources.resourceKey;
		replacement.sourceTransportOwnerSlot = ownerSlot;
		replacement.resourcesValid = true;
		slot = std::move(replacement);
		Increment(snapshot_.counters.resourceRebuilds);
		snapshot_.lastCompletedStage = RendererStage::ResourceCreation;
		return true;
	}

	bool Renderer::State::CopyDepthBatchLocked(
		std::span<const RendererApplyArgs> a_args,
		std::span<Slot* const> a_slots,
		std::span<const ValidatedResources> a_resources,
		const std::shared_ptr<ExecutionEvidence>& a_evidence)
	{
		if (a_args.empty() || a_args.size() != a_slots.size() ||
			a_args.size() != a_resources.size()) {
			return false;
		}
		// Validation fixes the source view to mip zero and the shader performs an
		// identity Load. Only an already typed R32 source can use a raw ROI copy.
		const auto canCopy = [&](std::size_t index) {
			return a_resources[index].depth.desc.Format == DXGI_FORMAT_R32_FLOAT &&
			       a_resources[index].depthViewFormat == DXGI_FORMAT_R32_FLOAT;
		};
		bool needsShader = false;
		for (std::size_t index = 0; index < a_args.size(); ++index)
			needsShader = needsShader || !canCopy(index);
#ifdef DEVBENCH_BRIDGE_ENABLED
		const bool needsCompactShader = std::ranges::any_of(a_resources, [](const auto& value) {
			return value.roi.compactSource && (value.depth.desc.Format != DXGI_FORMAT_R32_FLOAT || value.depthViewFormat != DXGI_FORMAT_R32_FLOAT);
		});
		if (needsCompactShader && !copyCompactDepthGuideCS_) {
			copyCompactDepthGuideCS_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/CopyCompactDepthGuideCS.hlsl", {}, "cs_5_0", "main")));
			if (!copyCompactDepthGuideCS_)
				return false;
			Util::SetResourceName(copyCompactDepthGuideCS_.Get(), "NeuralRendering::CopyCompactDepthGuideCS");
		}
#endif
		if (needsShader && !copyDepthGuideCS_ && !copyDepthGuideCompileFailed_) {
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
		if (needsShader && !shader)
			return false;
		if (needsShader && !copyDepthGuideCB_) {
			const D3D11_BUFFER_DESC desc{
				.ByteWidth = sizeof(CopyDepthGuideConstants),
				.Usage = D3D11_USAGE_DEFAULT,
				.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
			};
			if (FAILED(a_args.front().device->CreateBuffer(
					&desc, nullptr, &copyDepthGuideCB_))) {
				return false;
			}
			Util::SetResourceName(
				copyDepthGuideCB_.Get(), "NeuralRendering::CopyDepthGuideCB");
		}

		ComputeStateGuard stateGuard(a_args.front().context);
		if (!stateGuard.Captured())
			return false;

		a_args.front().context->CSSetShader(shader, nullptr, 0);
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			auto roi = a_resources[index].nativeLayout.depth.valid;
#ifdef DEVBENCH_BRIDGE_ENABLED
			const auto& compact = a_resources[index].roi.compactSource;
			if (compact)
				roi = *compact;
#endif
			if (canCopy(index)) {
				ID3D11ShaderResourceView* nullSrv = nullptr;
				ID3D11UnorderedAccessView* nullUav = nullptr;
				a_args.front().context->CSSetShaderResources(0, 1, &nullSrv);
				a_args.front().context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
				CS_GPU_PASS_CAPTURE("Upscaling::DLSSNRDepthGuide", a_evidence ? a_evidence->Snapshot().regions[index].depthGuidePass : nullptr);
				CopyTextureSubrect(a_args.front().context, a_slots[index]->depth.resource11.Get(),
					a_resources[index].depth.texture.Get(), roi,
#ifdef DEVBENCH_BRIDGE_ENABLED
					compact ? 0u : roi.baseX, compact ? 0u : roi.baseY);
#else
					roi.baseX, roi.baseY);
#endif
				continue;
			}
			const CopyDepthGuideConstants constants{
				.offsetX = roi.baseX,
				.offsetY = roi.baseY,
				.width = roi.width,
				.height = roi.height,
			};
			a_args.front().context->UpdateSubresource(
				copyDepthGuideCB_.Get(), 0, nullptr, &constants, 0, 0);
			auto* constantBuffer = copyDepthGuideCB_.Get();
			auto* source = a_args[index].depthGuideSRV;
			auto* destination = a_slots[index]->depth.uav11.Get();
#ifdef DEVBENCH_BRIDGE_ENABLED
			a_args.front().context->CSSetShader(compact ? copyCompactDepthGuideCS_.Get() : shader, nullptr, 0);
#endif
			a_args.front().context->CSSetConstantBuffers(
				0, 1, &constantBuffer);
			a_args.front().context->CSSetShaderResources(0, 1, &source);
			a_args.front().context->CSSetUnorderedAccessViews(
				0, 1, &destination, nullptr);
			{
				CS_GPU_PASS_CAPTURE("Upscaling::DLSSNRDepthGuide", a_evidence ? a_evidence->Snapshot().regions[index].depthGuidePass : nullptr);
				a_args.front().context->Dispatch(
					(roi.width + 7u) / 8u,
					(roi.height + 7u) / 8u,
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (const auto shared = TrySharedContextBatchLocked(a_args, a_outcome))
			return *shared;
#endif
		// All regions of both eyes must use immutable inputs and one commit boundary.
		if (colorConfiguration_.Enabled() ||
#ifdef DEVBENCH_BRIDGE_ENABLED
			(measuredPlanEnabled_ && (measuredPlanContinuous_ || measuredPlanInspectionRequested_)) ||
#endif
			a_args[0].computeRegions.count != 0u || a_args[1].computeRegions.count != 0u)
			return ApplyBatchLocked(a_args, a_outcome);
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
			const bool evaluationDiscontinuous = slot.historyValid &&
			                                     !IsSequentialFrame(slot.lastSuccessfulFrame, args.frameId);
			const bool sourceDiscontinuous = slot.historyValid &&
			                                 !IsSourceWorldFrameContinuous(
												 slot.lastSuccessfulSourceWorldFrame,
												 args.sourceWorldFrame);
			const bool discontinuous =
				evaluationDiscontinuous || sourceDiscontinuous;
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

#ifdef DEVBENCH_BRIDGE_ENABLED
	void Renderer::State::ApplyCompactLayoutLocked(const RendererApplyArgs& args, ValidatedResources& resources) const
	{
		if (colorConfiguration_.experiments.CompactInputsEnabled() && !forceFullCoordinates_ && !capacityFallback_.rejected &&
			!colorConfiguration_.Enabled() && args.characterVisualIsolation && args.reset &&
			args.renderingMode == RenderingMode::ReducedResolution && args.featureSlot < slots_.size()) {
			resources.compactAttempted = true;
			if (const auto compact = compactRetention_[args.featureSlot].Select(resources.roi, resources.nativeLayout)) {
				resources.roi = compact->roi;
				resources.nativeLayout = compact->native;
			}
		}
	}

	nlohmann::json Renderer::State::MeasuredPlanKeyLocked(std::span<const RendererApplyArgs> args,
		std::span<const ValidatedResources> resources, std::span<const std::uint64_t> clusters, bool& rejected) const
	{
		using Json = nlohmann::json;
		Json key = Json::array();
		std::array<bool, kMaximumRegionEvaluations> forced{}, rebuilt{};
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& value = args[index];
			const auto& resource = resources[index];
			const auto& slot = slots_[value.featureSlot];
			const auto owner = FindSourceTransportOwner(args, resources, index);
			const auto identities = [](const Slot& source) {
				return std::array{ source.color.resource12.Get(), source.depth.resource12.Get(),
					source.motionVectors.resource12.Get(), source.controlMask.resource12.Get() };
			};
			const auto requested = identities(slots_[args[owner].featureSlot]);
			const bool sameOwner = !resource.resourceKey.sharedSourceTransport ||
			                       ((owner == index || !rebuilt[owner]) && CanReuseSourceBinding(slot.sourceTransportOwnerSlot,
																			   args[owner].featureSlot, identities(slot), owner == index ? nullptr : &requested));
			const bool rebuild = !runtimeReady_ || !interop_.IsInitialized() || !slot.resourcesValid || !sameOwner ||
			                     slot.resourceKey != resource.resourceKey || !device_ || !SameIdentity(device_.Get(), value.device) || !SameIdentity(context_.Get(), value.context);
			rebuilt[index] = rebuild;
			forced[index] = rebuild || value.synchronizedHistoryReset || value.synchronizedHistoryDiscontinuity ||
			                !slot.historyValid || slot.historyKey != resource.historyKey || !IsSequentialFrame(slot.lastSuccessfulFrame, value.frameId) ||
			                !IsSourceWorldFrameContinuous(slot.lastSuccessfulSourceWorldFrame, value.sourceWorldFrame);
			rejected |= capacityRejections_.Rejects({ resource.resourceKey, reinterpret_cast<std::uintptr_t>(value.device),
				value.featureSlot, static_cast<std::uint32_t>(args.size()) });
			const auto& tuning = value.tuning;
			key.push_back({ { "slot", value.featureSlot }, { "layout", Evidence::NativeLayoutJson(resource.nativeLayout) },
				{ "compactSource", resource.roi.compactSource ? Evidence::SubrectJson(*resource.roi.compactSource) : Json(nullptr) },
				{ "mode", value.renderingMode ? Json(GetRenderingModeName(*value.renderingMode)) : Json(nullptr) },
				{ "ownership", Evidence::SubrectJson(resource.roi.ownedOutput) }, { "viewport", Evidence::ViewportJson(value.viewportCrop) },
				{ "insertion", value.insertionPoint }, { "callerReset", value.reset },
				{ "effectiveReset", value.reset || forced[index] }, { "resourceRebuild", rebuild },
				{ "colorFormat", resource.color.desc.Format }, { "outputFormat", resource.output.desc.Format },
				{ "outputHasUav", (resource.output.desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0 },
				{ "depthSourceFormat", resource.depth.desc.Format }, { "depthViewFormat", resource.depthViewFormat },
				{ "motionFormat", resource.motionVectors.desc.Format }, { "controlMask", resource.resourceKey.controlMaskPresent },
				{ "sharedInputs", resource.resourceKey.sharedSourceTransport }, { "sourceOwnerSlot", args[owner].featureSlot },
				{ "tuning", { tuning.intensity, tuning.localToneStrength, tuning.localStructureStrength, tuning.skinStructureStrength,
								tuning.style, tuning.useAutoMask, tuning.uiCorrection } } });
		}
		for (std::size_t i = 0; i < key.size(); ++i)
			for (std::size_t j = i + 1; j < key.size(); ++j)
				if (IsMatchingRegionStereoPair(args[i].featureSlot, clusters[i], args[j].featureSlot, clusters[j])) {
					const bool reset = forced[i] || forced[j];
					key[i]["effectiveReset"] = args[i].reset || reset;
					key[j]["effectiveReset"] = args[j].reset || reset;
				}
		return key;
	}

	std::array<RendererApplyArgs, kEyeCount> Renderer::State::SelectMeasuredPlanLocked(std::span<const RendererApplyArgs> args)
	{
		using Json = nlohmann::json;
		std::array<RendererApplyArgs, kEyeCount> original{};
		std::ranges::copy(args, original.begin());
		measuredPlanDiagnostics_ = { { "reason", "unknown_cost_fallback" }, { "frame", args.front().frameId },
			{ "sourceWorldFrame", args.front().sourceWorldFrame }, { "generation", args.front().generation } };
		ComPtr<IDXGIDevice> dxgi;
		ComPtr<IDXGIAdapter> adapter;
		DXGI_ADAPTER_DESC desc{};
		LARGE_INTEGER driver{};
		if (!args.front().device || FAILED(args.front().device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter)) ||
			FAILED(adapter->GetDesc(&desc)) || FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)) || Runtime::Instance().Hash().empty()) {
			measuredPlanDiagnostics_["reason"] = "backend_identity_unavailable";
			return original;
		}
		const Json identity{ { "buildId", BuildProvenance::GetBuildId() }, { "runtimeHash", Runtime::Instance().Hash() },
			{ "runtimeVersion", Runtime::Instance().Version() }, { "vendor", desc.VendorId }, { "device", desc.DeviceId },
			{ "parameterCoreHash", Runtime::Instance().ParameterCoreHash() },
			{ "subsystem", desc.SubSysId }, { "revision", desc.Revision }, { "driver", driver.QuadPart },
			{ "color", Color::ConfigurationEvidenceJson(colorConfiguration_) } };
		measuredPlanDiagnostics_["identity"] = identity;
		std::array<std::vector<MeasuredPlan::SearchCandidate>, kEyeCount> alternatives;
		Json searchDiagnostics = Json::array();
		std::array<ValidatedResources, kEyeCount> inputResources;
		for (std::size_t eye = 0; eye < args.size(); ++eye) {
			const auto& value = args[eye];
			if (!value.characterVisualIsolation ||
				!GetCharacterRegionSubmissionViolation(value.featureSlot, value.computeRegions, value.computeSubrect,
					value.outputWidth, value.outputHeight, true)
					.empty()) {
				measuredPlanDiagnostics_["reason"] = "no_valid_partition";
				return original;
			}
			const auto* input = value.measuredPlanInput.get();
			if (input && !input->Matches(value.sourceWorldFrame, value.generation, value.featureSlot))
				input = nullptr;
			auto search = MeasuredPlan::Search(value.computeRegions, value.computeSubrect, value.outputWidth, value.outputHeight, input);
			searchDiagnostics.push_back({ { "slot", value.featureSlot }, { "inputValid", search.inputValid },
				{ "coverageSource", !search.inputValid ? "unavailable" : input->gpuCoverage ? "current_gpu_superset" :
																							  "cpu_actor_eligibility" },
				{ "visitedCuts", search.visited }, { "rejectedCuts", search.rejected }, { "coverageChecks", search.coverageChecks }, { "coverageCheckBudget", MeasuredPlan::kCoverageCheckBudget }, { "truncated", search.truncated },
				{ "retainedCandidates", search.candidates.size() }, { "maximumCandidates", MeasuredPlan::kMaximumCandidatesPerEye } });
			alternatives[eye] = std::move(search.candidates);
			auto first = value;
			if (value.computeRegions.count) {
				first.featureSlot = PhysicalRegionFeatureSlot(value.featureSlot, value.computeRegions.regionSlots[0]);
				first.computeSubrect = value.computeRegions.regions[0];
				first.roi = value.computeRegions.roi[0];
			}
			if (ValidateLocked(first, inputResources[eye], false)) {
				measuredPlanDiagnostics_["reason"] = "invalid_source_resources";
				return original;
			}
		}
		measuredPlanDiagnostics_["search"] = std::move(searchDiagnostics);
		std::vector<std::array<RendererApplyArgs, kEyeCount>> plans;
		std::vector<MeasuredPlan::Candidate> candidates;
		Json keys = Json::array();
		for (std::size_t left = 0; left < alternatives[0].size(); ++left)
			for (std::size_t right = 0; right < (args.size() == 2 ? alternatives[1].size() : 1); ++right) {
				auto plan = original;
				Json key = Json::array();
				Json operations = Json::array();
				std::vector<std::uint64_t> clusters;
				std::vector<RendererApplyArgs> physicalArgs;
				std::vector<ValidatedResources> physicalResources;
				std::uint64_t partition = 1469598103934665603ull;
				bool valid = true, rejected = false;
				for (std::size_t eye = 0; eye < args.size(); ++eye) {
					const auto& choice = alternatives[eye][eye ? right : left];
					auto& logical = plan[eye];
					logical.computeRegions = choice.plan;
					operations.push_back(choice.operation);
					partition = (partition ^ choice.id) * 1099511628211ull;
					rejected |= capacityFallback_.rejected && logical.computeRegions.count > kDefaultRegionsPerEye;
					valid &= QualifiedHigherRegionGeometry(logical.computeRegions) &&
					         GetCharacterRegionSubmissionViolation(logical.featureSlot, logical.computeRegions, logical.computeSubrect,
								 logical.outputWidth, logical.outputHeight, true)
					             .empty();
					for (std::uint32_t i = 0; i < std::max(1u, logical.computeRegions.count) && valid; ++i) {
						const auto& regions = logical.computeRegions;
						partition = (partition ^ regions.clusterIdentities[i]) * 1099511628211ull;
						auto physical = logical;
						if (regions.count) {
							physical.featureSlot = PhysicalRegionFeatureSlot(logical.featureSlot, regions.regionSlots[i]);
							physical.computeSubrect = regions.regions[i];
							physical.roi = regions.roi[i];
						}
						physical.computeRegions = {};
						auto resources = inputResources[eye];
						if (regions.count) {
							resources.roi = *physical.roi;
							resources.nativeLayout = BuildNativeEvaluationLayout({ physical.colorWidth, physical.colorHeight },
								{ physical.guideWidth, physical.guideHeight }, resources.roi.allocationCapacity,
								{ physical.controlMaskWidth, physical.controlMaskHeight }, resources.roi.inferenceContext,
								UpscalingDLSS::BuildMotionVectorPixelScale(physical.viewportCrop), physical.featureUpscaling);
							ApplyCompactLayoutLocked(physical, resources);
						}
						FinalizeResourceKeysLocked(physical, resources);
						resources.historyKey.regionIdentity = regions.count ? regions.historyKeys[i] : 0;
						clusters.push_back(regions.count ? regions.clusterIdentities[i] : 0);
						physicalArgs.push_back(std::move(physical));
						physicalResources.push_back(std::move(resources));
					}
				}
				key = MeasuredPlanKeyLocked(physicalArgs, physicalResources, clusters, rejected);
				plans.push_back(std::move(plan));
				candidates.push_back({ key.dump(), partition, valid, rejected });
				keys.push_back({ { "key", std::move(key) }, { "valid", valid }, { "capacityRejected", rejected }, { "operations", std::move(operations) } });
			}
		const auto selected = measuredCalibration_.remaining ?
		                          measuredCalibration_.Select(candidates, identity.dump(), args.front().sourceWorldFrame, args.front().generation, CalibrationMilliseconds()) :
		                          MeasuredPlan::Select(candidates, 0, identity.dump(), measuredPlanProfile_ ? &*measuredPlanProfile_ : nullptr,
									  args.front().sourceWorldFrame, measuredPlanState_[args.front().featureSlot]);
		measuredPlanDiagnostics_["reason"] = selected.reason;
		measuredPlanDiagnostics_["selected"] = selected.index;
		if (std::string_view(selected.reason) != "calibration_candidate" && measuredPlanProfile_ && measuredPlanProfile_->identity == identity.dump() && selected.index < candidates.size())
			if (const auto* cost = measuredPlanProfile_->Find(candidates[selected.index].key))
				measuredPlanDiagnostics_["prediction"] = { { "gpuLowerMs", cost->gpuLowerMs }, { "gpuUpperMs", cost->gpuUpperMs },
					{ "gpuTailMs", cost->gpuTailMs }, { "cpuCriticalUpperMs", cost->cpuCriticalUpperMs },
					{ "transitionUpperMs", cost->transitionUpperMs }, { "residentBytes", cost->residentBytes } };
		measuredPlanDiagnostics_["candidates"] = std::move(keys);
		return plans.empty() ? original : plans[selected.index];
	}
#endif

#ifdef DEVBENCH_BRIDGE_ENABLED
	nlohmann::json Renderer::State::SharedContextJsonLocked(std::size_t route) const
	{
		const auto& value = sharedContextObservations_[route];
		auto eyes = nlohmann::json::array();
		for (std::uint32_t eye = 0; eye < value.count; ++eye) {
			const auto& plan = value.plans[eye];
			auto owned = nlohmann::json::array();
			for (std::uint32_t index = 0; index < SharedContext::OutputCount(plan); ++index)
				if (const auto domain = SharedContext::CopyDomain(plan, index))
					owned.push_back(Evidence::SubrectJson(domain->output));
			eyes.push_back({ { "eye", value.logicalEyes[eye] }, { "inferenceContext", Evidence::SubrectJson(plan.inferenceContext) },
				{ "ownedOutputs", std::move(owned) } });
		}
		return { { "mode", value.settings.mode == SharedContext::Mode::Enclosing ? "enclosing" : value.settings.mode == SharedContext::Mode::FullEye ? "full_eye" :
																																					   "off" },
			{ "halo", value.settings.halo }, { "frame", value.frame }, { "sourceWorldFrame", value.sourceWorldFrame },
			{ "applied", value.applied }, { "succeeded", value.succeeded }, { "reason", value.reason },
			{ "requestedEvaluations", value.requestedEvaluations }, { "plannedEvaluations", value.applied ? value.count : 0 },
			{ "coordinateDomain", "original_output_crop_local" }, { "outputOwnershipPreserved", true },
			{ "productionQualified", false }, { "eyes", std::move(eyes) } };
	}

	std::optional<bool> Renderer::State::TrySharedContextBatchLocked(
		std::span<const RendererApplyArgs> args, RendererApplyOutcome& outcome)
	{
		auto& observation = sharedContextObservations_[captureRoute_];
		const auto settings = colorConfiguration_.experiments.sharedContext;
		if (settings.mode == SharedContext::Mode::Off) {
			if (observation.settings.mode != SharedContext::Mode::Off)
				observation = {};
			return std::nullopt;
		}
		observation = {};
		observation.settings = settings;
		observation.reason = "ineligible_original_plan_retained";
		if (args.empty() || args.size() > kEyeCount)
			return std::nullopt;
		observation.frame = args.front().frameId;
		observation.sourceWorldFrame = args.front().sourceWorldFrame;
		if (measuredPlanEnabled_ && (measuredPlanContinuous_ || measuredPlanInspectionRequested_)) {
			observation.reason = "measured_plan_experiment_active";
			return std::nullopt;
		}
		if (colorConfiguration_.experiments.CompactInputsEnabled() || colorConfiguration_.experiments.SharedSourceTransportEnabled() ||
			colorConfiguration_.experiments.transportBypass) {
			observation.reason = "incompatible_transport_experiment";
			return std::nullopt;
		}
		std::array<RendererApplyArgs, kEyeCount> sharedArgs{};
		for (std::size_t eye = 0; eye < args.size(); ++eye) {
			const auto& logical = args[eye];
			if (!logical.characterVisualIsolation || !logical.renderingMode ||
				*logical.renderingMode != RenderingMode::ReducedResolution || !logical.reset ||
				logical.featureUpscaling || logical.colorWidth != logical.outputWidth || logical.colorHeight != logical.outputHeight ||
				logical.guideWidth != logical.outputWidth || logical.guideHeight != logical.outputHeight ||
				logical.controlMask || !logical.tuning.useAutoMask || logical.tuning.uiCorrection ||
				!GetCharacterRegionSubmissionViolation(logical.featureSlot, logical.computeRegions,
					logical.computeSubrect, logical.outputWidth, logical.outputHeight, true)
					.empty())
				return std::nullopt;
			const auto capacity = UpscalingDLSS::Extent{ logical.outputWidth, logical.outputHeight };
			const auto single = logical.roi.value_or(BuildRoiDescriptor(std::nullopt, logical.computeSubrect, capacity, false));
			const auto descriptors = logical.computeRegions.count ?
			                             std::span<const RoiDescriptor>(logical.computeRegions.roi).first(logical.computeRegions.count) :
			                             std::span<const RoiDescriptor>(&single, 1);
			const auto plan = SharedContext::Build({ logical.computeSubrect, logical.computeRegions }, descriptors,
				capacity, settings, *logical.renderingMode, logical.featureUpscaling, logical.reset);
			if (!plan)
				return std::nullopt;
			observation.logicalEyes[eye] = logical.featureSlot % 2u;
			observation.plans[eye] = *plan;
			observation.requestedEvaluations += SharedContext::OutputCount(*plan);
			auto& physical = sharedArgs[eye];
			physical = logical;
			physical.computeRegions = {};
			physical.computeSubrect = plan->inferenceContext;
			physical.roi = BuildRoiDescriptor(plan->samplingSupport, plan->inferenceContext, capacity, false);
		}
		// Enlarging an already single-call plan cannot amortize native dispatch cost.
		if (settings.mode == SharedContext::Mode::Enclosing && observation.requestedEvaluations == args.size()) {
			observation.reason = "no_coalescing_opportunity";
			return std::nullopt;
		}
		observation.count = static_cast<std::uint32_t>(args.size());
		observation.applied = true;
		observation.reason = "shared_context_selected";
		requestedRegionCount_ = observation.requestedEvaluations;
		const bool success = ApplyRegionBatchLocked(std::span(sharedArgs).first(args.size()), outcome,
			std::span(observation.plans).first(args.size()));
		observation.succeeded = success;
		observation.reason = success ? "shared_context_committed" : "shared_context_execution_failed";
		return success;
	}
#endif

	bool Renderer::State::ApplyBatchLocked(
		std::span<const RendererApplyArgs> args, RendererApplyOutcome& outcome)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (const auto shared = TrySharedContextBatchLocked(args, outcome))
			return *shared;
		outcome = {};
		std::array<RendererApplyArgs, kEyeCount> selected;
		const auto route = args.empty() ? kLogicalFeatureSlotCount : args.front().featureSlot;
		const bool validRoute = !args.empty() && args.size() <= kEyeCount && route < kLogicalFeatureSlotCount;
		const bool inspect = measuredPlanEnabled_ && validRoute && measuredPlanInspectionRequested_.exchange(false);
		const bool measured = measuredPlanEnabled_ && validRoute && (measuredPlanContinuous_ || inspect);
		const auto previousEvaluation = measuredPlanEvaluating_;
		measuredPlanEvaluating_ = measured;
		const SKSE::stl::scope_exit restoreEvaluation([&] { measuredPlanEvaluating_ = previousEvaluation; });
		const auto previousState = measured ? measuredPlanState_[route] : MeasuredPlan::State{};
		const bool calibrating = measured && measuredCalibration_.remaining != 0;
		bool usedFallback = false;
		const SKSE::stl::scope_exit completeSelection([&] {
			if (!measured)
				return;
			if (usedFallback)
				measuredPlanState_[route] = {};
			else if (!outcome.outputPlans[route])
				measuredPlanState_[route] = previousState;
			if (calibrating) {
				try {
					const bool committed = std::ranges::all_of(args, [&](const auto& value) {
						return value.featureSlot < outcome.outputPlans.size() && outcome.outputPlans[value.featureSlot].has_value();
					});
					const bool selectedCandidate = measuredPlanDiagnostics_.value("reason", "") == "calibration_candidate";
					nlohmann::json record{ { "frame", args.front().frameId }, { "sourceWorldFrame", args.front().sourceWorldFrame },
						{ "generation", args.front().generation }, { "reason", measuredPlanDiagnostics_.value("reason", "unavailable") },
						{ "nativeOutputCommitted", committed }, { "fallback", usedFallback }, { "keyIndex", nullptr } };
					record["outputDomains"] = nlohmann::json::array();
					for (const auto& value : args) {
						if (value.featureSlot >= outcome.outputPlans.size())
							continue;
						const auto& output = outcome.outputPlans[value.featureSlot];
						if (output)
							for (std::uint32_t i = 0; i < std::max(1u, output->regions.count); ++i)
								record["outputDomains"].push_back({ { "logicalSlot", value.featureSlot },
									{ "rect", Evidence::SubrectJson(output->regions.count ? output->regions.regions[i] : output->enclosure) } });
					}
					if (selectedCandidate && measuredPlanDiagnostics_.contains("selected")) {
						const auto& key = measuredPlanDiagnostics_["candidates"].at(measuredPlanDiagnostics_["selected"].get<std::size_t>())["key"];
						auto found = std::find(measuredCalibrationKeys_.begin(), measuredCalibrationKeys_.end(), key);
						const auto index = static_cast<std::size_t>(std::distance(measuredCalibrationKeys_.begin(), found));
						if (found == measuredCalibrationKeys_.end())
							measuredCalibrationKeys_.push_back(key);
						record["keyIndex"] = index;
					}
					measuredCalibrationRecords_.push_back(std::move(record));
					if (!selectedCandidate || usedFallback || !committed) {
						if (measuredCalibration_.remaining || selectedCandidate)
							measuredCalibration_.reason = "calibration_execution_rejected";
						measuredCalibration_.remaining = 0;
					}
					measuredPlanState_[route] = {};
				} catch (...) {
					measuredCalibration_.remaining = 0;
					measuredCalibration_.reason = "calibration_evidence_failure";
					measuredCalibrationEvidenceFailed_ = true;
				}
				measuredPlanContinuous_ = measuredPlanProfile_.has_value() || measuredCalibration_.remaining != 0;
			}
		});
		if (measured) {
			selected = SelectMeasuredPlanLocked(args);
			args = std::span(selected).first(args.size());
		}
		requestedRegionCount_ = 0;
		bool higherCount = false;
		higherCount = colorConfiguration_.experiments.CompactInputsEnabled();
		for (const auto& arg : args) {
			requestedRegionCount_ += std::max(1u, arg.computeRegions.count);
			higherCount |= arg.computeRegions.count > kDefaultRegionsPerEye;
			if (!GetCharacterRegionSubmissionViolation(arg.featureSlot, arg.computeRegions,
					arg.computeSubrect, arg.outputWidth, arg.outputHeight, arg.characterVisualIsolation)
					.empty())
				return ApplyRegionBatchLocked(args, outcome);
		}
		if (args.empty() || args.size() > kEyeCount)
			return ApplyRegionBatchLocked(args, outcome);
		const auto fallback = [&] {
			usedFallback = true;
			if (measured) {
				measuredPlanDiagnostics_["executionFallback"] = true;
				measuredPlanDiagnostics_.erase("prediction");
			}
			const auto previous = forceFullCoordinates_;
			forceFullCoordinates_ = true;
			const SKSE::stl::scope_exit restore([&] { forceFullCoordinates_ = previous; });
			std::array<RendererApplyArgs, kEyeCount> merged{};
			for (std::size_t index = 0; index < args.size(); ++index) {
				auto& value = merged[index];
				value = args[index];
				if (value.computeRegions.count) {
					// The validated enclosure initializes every backend-readable sample.
					value.roi = BuildRoiDescriptor(value.computeSubrect, value.computeSubrect,
						{ value.outputWidth, value.outputHeight }, true);
					value.computeRegions = {};
				}
			}
			const auto attempted = outcome.evaluationAttemptedFeatureSlotMask;
			const bool success = ApplyRegionBatchLocked(std::span(merged).first(args.size()), outcome);
			outcome.evaluationAttemptedFeatureSlotMask |= attempted;
			return success;
		};
		if (std::ranges::any_of(args, [](const auto& arg) { return !QualifiedHigherRegionGeometry(arg.computeRegions); }))
			return fallback();
		return capacityFallback_.ApplyBatch(higherCount, [&] { return ApplyRegionBatchLocked(args, outcome); }, [&] {
				if (quarantined_ || FAILED(GetDeviceRemovalReasonLocked(S_OK)))
					return CapacityFailure::Unsafe;
				if (snapshot_.failureStage == RendererStage::ResourceCreation && snapshot_.lastResult == E_OUTOFMEMORY)
					return CapacityFailure::Pressure;
				return snapshot_.failureStage == RendererStage::FeatureEvaluate ?
					Runtime::Instance().CreationCapacityFailure() : CapacityFailure::Unsafe; }, [&] { return TeardownBackendLocked(false, false, false); }, fallback);
#else
		return ApplyRegionBatchLocked(args, outcome);
#endif
	}

	bool Renderer::State::ApplyRegionBatchLocked(
		std::span<const RendererApplyArgs> a_logicalArgs,
		RendererApplyOutcome& a_logicalOutcome
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		std::span<const SharedContext::Plan> a_shared
#endif
	)
	{
		a_logicalOutcome = {};
		RendererApplyOutcome a_outcome{};
		std::array<std::uint32_t, 4> requiredRegionMasks{};
		struct OutcomeGuard
		{
			RendererApplyOutcome& logical;
			const RendererApplyOutcome& physical;
			const std::array<std::uint32_t, 4>& required;
			bool active = true;
			~OutcomeGuard() noexcept
			{
				if (!active)
					return;
				logical.evaluationAttemptedFeatureSlotMask = AggregateRegionEvaluationMask(
					physical.evaluationAttemptedFeatureSlotMask, required, false);
				logical.evaluationSucceededFeatureSlotMask = AggregateRegionEvaluationMask(
					physical.evaluationSucceededFeatureSlotMask, required, true);
			}
		} outcomeGuard{ a_logicalOutcome, a_outcome, requiredRegionMasks };
		auto a_args = a_logicalArgs;
		if (a_args.empty() || a_args.size() > 2)
			return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (!a_shared.empty() && a_shared.size() != a_args.size())
			return false;
#endif
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

		// Validate the original logical stereo pair before allowing same-eye regions
		// to share immutable caller inputs and their final caller-owned destination.
		const auto logicalEyeCount = static_cast<std::uint32_t>(a_args.size());
		std::array<RendererApplyArgs, kMaximumRegionEvaluations> expandedArgs{};
		std::array<std::uint64_t, kMaximumRegionEvaluations> regionIdentities{};
		std::array<std::uint64_t, kMaximumRegionEvaluations> clusterIdentities{};
		std::size_t expandedCount = 0;
		for (const auto& logical : a_logicalArgs) {
			const auto& plan = logical.computeRegions;
			if (auto violation = GetCharacterRegionSubmissionViolation(
					logical.featureSlot, plan, logical.computeSubrect,
					logical.outputWidth, logical.outputHeight, logical.characterVisualIsolation);
				!violation.empty()) {
				return FailLocked(RendererStage::Validation, E_INVALIDARG,
					std::string(violation), logical.featureSlot, false);
			}
			const auto count = std::max(1u, plan.count);
			for (std::uint32_t region = 0; region < count; ++region) {
				auto& physical = expandedArgs[expandedCount];
				physical = logical;
				physical.featureSlot = PhysicalRegionFeatureSlot(logical.featureSlot, plan.count ? plan.regionSlots[region] : 0u);
				physical.computeRegions = {};
				if (region != 0u)
					Increment(snapshot_.counters.attempts);
				if (plan.count != 0u) {
					physical.computeSubrect = plan.regions[region];
					physical.roi = plan.roi[region];
					regionIdentities[expandedCount] = plan.historyKeys[region];
					clusterIdentities[expandedCount] = plan.clusterIdentities[region];
				}
				requiredRegionMasks[logical.featureSlot] |= 1u << physical.featureSlot;
				++expandedCount;
			}
		}
		a_args = std::span<const RendererApplyArgs>(expandedArgs.data(), expandedCount);

		std::array<ValidatedResources, kMaximumRegionEvaluations> resources{};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			const auto& args = a_args[index];
			SetActiveFeatureSlotLocked(args.featureSlot);
			SetRequestTelemetryLocked(args);
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
			resources[index].historyKey.regionIdentity = regionIdentities[index];
		}
		snapshot_.lastCompletedStage = RendererStage::Validation;

#ifdef DEVBENCH_BRIDGE_ENABLED
		if (colorConfiguration_.experiments.SharedSourceTransportEnabled()) {
			for (std::size_t index = 0; index < a_args.size(); ++index) {
				requestedCapacity_ = { resources[index].resourceKey, reinterpret_cast<std::uintptr_t>(a_args[index].device), a_args[index].featureSlot, static_cast<std::uint32_t>(a_args.size()) };
				if (capacityRejections_.Rejects(requestedCapacity_))
					return FailLocked(RendererStage::FailureLatched, E_FAIL,
						"source transport capacity is rejected; explicit nr_reset after safe retirement is required", a_args[index].featureSlot, true);
			}
			requestedCapacity_ = { resources[0].resourceKey, reinterpret_cast<std::uintptr_t>(a_args[0].device), a_args[0].featureSlot, static_cast<std::uint32_t>(a_args.size()) };
		}
#endif

#ifdef DEVBENCH_BRIDGE_ENABLED
		LifetimeGuard lifetime(*this, LifetimeOperation::Batch);
		if (lifetime.enabled) {
			lifetime.record->sourceTransactionId = a_args.front().executionContext.sourceTransactionId;
			if (a_args.front().executionContext.renderingMode)
				lifetime.record->mode = static_cast<std::uint32_t>(*a_args.front().executionContext.renderingMode);
			lifetime.record->regionCount = static_cast<std::uint32_t>(a_args.size());
			for (std::size_t index = 0; index < a_args.size(); ++index) {
				const auto& args = a_args[index];
				const auto& slot = slots_[args.featureSlot];
				auto& region = lifetime.record->regions[index];
				region.slot = args.featureSlot;
				lifetime.record->slotMask |= 1u << args.featureSlot;
				region.previousResourceSerial = slot.resourceSerial;
				region.previousFrame = slot.lastSuccessfulFrame;
				region.previousSourceFrame = slot.lastSuccessfulSourceWorldFrame;
				region.previousHistoryValid = slot.historyValid;
				region.previousContext = slot.historyKey.computeSubrect;
				region.previousRegionIdentity = slot.historyKey.regionIdentity;
				region.regionIdentity = resources[index].historyKey.regionIdentity;
				region.context = resources[index].roi.inferenceContext;
				region.colorSize = { args.colorWidth, args.colorHeight };
				region.guideSize = { args.guideWidth, args.guideHeight };
				region.outputSize = { args.outputWidth, args.outputHeight };
			}
		}
#endif
		std::shared_ptr<ExecutionEvidence> execution;
		auto& capture = captureInputs_[captureRoute_];
		if (capture.valid) {
			try {
				ExecutionDescriptor descriptor{};
				descriptor.submissionId = NextExecutionSubmissionId();
				descriptor.generation = a_args.front().generation;
				descriptor.colorRevision = colorConfiguration_.revision;
				descriptor.inputEpoch = colorConfiguration_.inputEpoch[static_cast<std::size_t>(a_args.front().insertionPoint)];
				descriptor.frame = a_args.front().frameId;
				descriptor.sourceWorldFrame = a_args.front().sourceWorldFrame;
				descriptor.route = ClassifyFeatureSlotMask(1u << a_args.front().featureSlot);
				descriptor.insertion = a_args.front().insertionPoint;
				descriptor.context = a_args.front().executionContext;
				descriptor.logicalEyeCount = logicalEyeCount;
				descriptor.regionCount = static_cast<std::uint32_t>(a_args.size());
#ifdef DEVBENCH_BRIDGE_ENABLED
				descriptor.requestedRegionCount = requestedRegionCount_;
				descriptor.capacityFallback = a_shared.empty() && requestedRegionCount_ != descriptor.regionCount;
				if (colorConfiguration_.experiments.sharedContext.mode != SharedContext::Mode::Off) {
					auto decision = SharedContextJsonLocked(captureRoute_);
					decision.erase("succeeded");
					decision["phase"] = "selection_before_native_execution";
					descriptor.sharedContextDecision = decision.dump();
				}
				if (measuredPlanEvaluating_) {
					nlohmann::json decision;
					for (const auto* field : { "frame", "sourceWorldFrame", "generation", "reason", "identity", "selected", "prediction", "search", "executionFallback" })
						if (measuredPlanDiagnostics_.contains(field))
							decision[field] = measuredPlanDiagnostics_[field];
					if (measuredPlanDiagnostics_.contains("selected"))
						decision["selectedKey"] = measuredPlanDiagnostics_["candidates"].at(measuredPlanDiagnostics_["selected"].get<std::size_t>())["key"];
					decision["capacityFallback"] = forceFullCoordinates_ || descriptor.capacityFallback;
					descriptor.measuredPlanDecision = decision.dump();
				}
#else
				descriptor.requestedRegionCount = descriptor.regionCount;
#endif
				descriptor.colorProcessing = colorConfiguration_.Enabled();
				descriptor.transportBypass = colorConfiguration_.experiments.transportBypass;
				for (std::size_t index = 0; index < a_args.size(); ++index) {
					const auto& args = a_args[index];
					const auto& resource = resources[index];
					auto& region = descriptor.regions[index];
					region.physicalSlot = args.featureSlot;
					region.logicalSlot = args.featureSlot % 4u;
					region.eye = args.featureSlot % 2u;
					region.region = args.featureSlot / 4u;
					region.regionIdentity = regionIdentities[index];
					region.clusterIdentity = clusterIdentities[index];
					region.context = args.executionContext;
					region.characterEvidence = args.characterEvidence;
					region.roi = resource.roi;
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (!a_shared.empty())
						region.sharedOutputOwnership = a_shared[index];
#endif
					region.color = DescribeExecutionTexture(resource.nativeLayout.color.backing.width, resource.nativeLayout.color.backing.height, resource.resourceKey.colorFormat, resource.nativeLayout.color.valid);
					region.depth = DescribeExecutionTexture(resource.nativeLayout.depth.backing.width, resource.nativeLayout.depth.backing.height, DXGI_FORMAT_R32_FLOAT, resource.nativeLayout.depth.valid);
					region.motion = DescribeExecutionTexture(resource.nativeLayout.motion.backing.width, resource.nativeLayout.motion.backing.height, resource.resourceKey.motionFormat, resource.nativeLayout.motion.valid);
					region.output = DescribeExecutionTexture(resource.nativeLayout.output.backing.width, resource.nativeLayout.output.backing.height, resource.resourceKey.outputFormat, resource.nativeLayout.output.valid);
					if (args.controlMask)
						region.controlMask = DescribeExecutionTexture(args.controlMaskWidth, args.controlMaskHeight, resource.resourceKey.controlMaskFormat, resource.nativeLayout.controlMask.valid);
					region.viewportCrop = args.viewportCrop;
					region.nativeLayout = resource.nativeLayout;
					region.motionVectorScaleX = resource.nativeLayout.motionVectorScale[0];
					region.motionVectorScaleY = resource.nativeLayout.motionVectorScale[1];
					region.depthSourceFormat = static_cast<std::uint32_t>(resource.depth.desc.Format);
					region.depthViewFormat = static_cast<std::uint32_t>(resource.depthViewFormat);
					region.characterVisualIsolation = args.characterVisualIsolation;
					region.featureUpscaling = args.featureUpscaling;
					descriptor.plannedPhysicalSlotMask |= 1u << args.featureSlot;
				}
				if (descriptor.submissionId && capture.executionCount < capture.executions.size()) {
					execution = std::make_shared<ExecutionEvidence>(std::move(descriptor));
					capture.executions[capture.executionCount++] = execution;
					execution->Update([&](auto& evidence) {
						evidence.wholePass = wholePass_;
						for (std::size_t index = 0; index < a_args.size(); ++index) {
							auto& region = evidence.regions[index];
							region.depthGuidePass = std::make_shared<Util::PassTimingCapture>();
							region.colorCopyPass = std::make_shared<Util::PassTimingCapture>();
							region.motionCopyPass = std::make_shared<Util::PassTimingCapture>();
							region.controlCopyPass = std::make_shared<Util::PassTimingCapture>();
							region.outputCopyPass = std::make_shared<Util::PassTimingCapture>();
							region.colorPreparePass = std::make_shared<Util::PassTimingCapture>();
							region.colorReconstructPass = std::make_shared<Util::PassTimingCapture>();
						}
					});
				} else {
					++capture.executionEvidenceFailures;
				}
			} catch (...) {
				++capture.executionEvidenceFailures;
			}
		}
		ExecutionCompletionGuard executionCompletion{ execution, activeStage_ };

		activeStage_ = RendererStage::DeviceCompatibility;
		SetActiveFeatureSlotLocked(a_args.front().featureSlot);
		std::optional<ExperimentalKernelBatch::Frame> kernelFrame;
		std::string_view kernelAdmissionReason = "requires_four_evaluations_and_native_inference";
		if ((kernelBatchOverride_ || a_args.front().roiExecutionMode == RoiExecutionMode::Batched) &&
			a_args.size() == 4 && !colorConfiguration_.experiments.transportBypass) {
			ExperimentalKernelBatch::Frame frame;
			frame.sequence = a_args.front().frameId;
			frame.style = a_args.front().tuning.style;
			frame.useAutoMask = a_args.front().tuning.useAutoMask;
			frame.uiCorrection = a_args.front().tuning.uiCorrection;
			frame.singleSubrectScale = a_args.front().tuning.singleSubrectScale;
			frame.inspectUnqualifiedPipeline = kernelInspectUnqualified_;
			for (std::size_t index = 0; index < frame.regions.size(); ++index) {
				const auto& args = a_args[index];
				frame.regions[index] = { (args.featureSlot % kLogicalFeatureSlotCount) % kEyeCount,
					args.featureSlot / kLogicalFeatureSlotCount, args.featureSlot,
					resources[index].nativeLayout, args.reset, args.controlMask != nullptr };
			}
			kernelAdmissionReason = ExperimentalKernelBatch::AdmissionViolation(frame);
			if (kernelAdmissionReason.empty())
				kernelFrame = frame;
		}
		if (!EnsureBackendLocked(a_args.front(), execution, kernelFrame.has_value()))
			return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (lifetime.enabled)
			lifetime.record->backendSerial = backendSerial_;
#endif
		for (auto& slot : slots_)
			colorPipeline_.Poll(a_args.front().context, slot.colorWork);
		activeStage_ = RendererStage::ResourceCreation;
		std::array<Slot*, kMaximumRegionEvaluations> slots{};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (colorConfiguration_.experiments.SharedSourceTransportEnabled())
				requestedCapacity_ = { resources[index].resourceKey, reinterpret_cast<std::uintptr_t>(a_args[index].device), a_args[index].featureSlot, static_cast<std::uint32_t>(a_args.size()) };
#endif
			const auto owner = FindSourceTransportOwner(a_args, std::span<const ValidatedResources>(resources).first(a_args.size()), index);
			const Slot* sourceOwner = owner == index ? nullptr : slots[owner];
			if (!EnsureSlotLocked(a_args[index].featureSlot, resources[index], execution, index, sourceOwner))
				return false;
			slots[index] = &slots_[a_args[index].featureSlot];
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (resources[index].compactAttempted)
				compactRetention_[a_args[index].featureSlot].Commit(resources[index].roi.compactSource);
			if (lifetime.enabled)
				CaptureLifetimeResourcesLocked(lifetime.record->regions[index], *slots[index]);
#endif
			if (execution)
				execution->Update([&](auto& evidence) {
					evidence.regions[index].resourcesReady = true;
					evidence.regions[index].inputTransportOwnerSlot = sourceOwner ?
					                                                      static_cast<std::uint32_t>(sourceOwner - slots_.data()) :
					                                                      a_args[index].featureSlot;
				});
		}

		// Allocate/compile for every physical region before changing any caller output.
		// The legacy raw lane does not allocate or dispatch colour resources.
		if (colorConfiguration_.Enabled()) {
			for (std::size_t index = 0; index < a_args.size(); ++index) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (colorConfiguration_.experiments.SharedSourceTransportEnabled())
					requestedCapacity_ = { resources[index].resourceKey, reinterpret_cast<std::uintptr_t>(a_args[index].device), a_args[index].featureSlot, static_cast<std::uint32_t>(a_args.size()) };
#endif
				const auto profile = Color::EffectiveProfile(colorConfiguration_,
					static_cast<std::uint32_t>(a_args[index].insertionPoint));
				const auto format = resources[index].resourceKey.colorFormat;
				const bool floatStorage = format == DXGI_FORMAT_R11G11B10_FLOAT ||
				                          format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT;
				if (profile.transform != Color::Transform::Identity && !floatStorage)
					return FailLocked(RendererStage::Validation, E_INVALIDARG,
						"NR encoding/proxy experiment requires floating-point processing resources", a_args[index].featureSlot, false);
				const auto oldBaseline = slots[index]->colorWork.baseline.resource.Get();
				const bool colorReady = colorPipeline_.Ensure(a_args[index].device, slots[index]->colorWork,
					resources[index].roi.inferenceContext, resources[index].resourceKey.outputFormat,
					colorConfiguration_.experiments.diagnostics);
				if (execution)
					execution->Update([&](auto& evidence) {
						auto& region = evidence.regions[index];
						const auto& work = slots[index]->colorWork;
						const auto bytes = LogicalTextureBytes(work.format, work.capacityWidth, work.capacityHeight);
						if (colorReady && bytes) {
							region.colorRetainedLogicalBytes = *bytes * 2u;
							if (oldBaseline != work.baseline.resource.Get())
								region.newlyAllocatedLogicalBytes += *bytes * 2u;
						} else {
							region.allocationBytesKnown = false;
						}
					});
				if (!colorReady) {
					return FailLocked(RendererStage::ResourceCreation, E_FAIL,
						"shared NR colour shaders/resources are unavailable", a_args[index].featureSlot, true);
				}
			}
		}

		const auto preparationStarted = std::chrono::steady_clock::now();
		ExecutionCpuTimer preparationTimer(execution, &ExecutionSnapshot::preparationCpuMicroseconds);
		activeStage_ = RendererStage::ColorInputCopy;
		std::uint32_t measurementSlotMask = 0;
		for (const auto& args : a_args)
			measurementSlotMask |= 1u << args.featureSlot;
		const auto measurementBatchId = colorConfiguration_.Enabled() ? colorPipeline_.BeginMeasurementBatch() : 0;
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			if (colorConfiguration_.Enabled()) {
				Color::Observation observation{};
				observation.frame = a_args[index].frameId;
				observation.sourceWorldFrame = a_args[index].sourceWorldFrame;
				observation.slot = a_args[index].featureSlot;
				observation.insertion = static_cast<std::uint32_t>(a_args[index].insertionPoint);
				observation.generation = a_args[index].generation;
				observation.rect = resources[index].roi.ownedOutput;
				observation.sourceFormat = static_cast<std::uint32_t>(resources[index].resourceKey.colorFormat);
				observation.outputFormat = static_cast<std::uint32_t>(resources[index].resourceKey.outputFormat);
				observation.atomicStereo = logicalEyeCount == 2u;
				observation.measurementBatchId = measurementBatchId;
				observation.expectedMeasurementSlotMask = measurementSlotMask;
				if (execution) {
					const auto evidence = execution->Snapshot();
					observation.preparationPass = evidence.regions[index].colorPreparePass;
					observation.reconstructionPass = evidence.regions[index].colorReconstructPass;
				}
				if (!colorPipeline_.Prepare(a_args.front().context, slots[index]->colorWork,
						resources[index].color.texture.Get(), slots[index]->color.resource11.Get(),
						slots[index]->color.uav11.Get(), colorConfiguration_, observation)) {
					return FailLocked(RendererStage::ColorInputCopy, E_FAIL,
						"shared NR colour preparation failed", a_args[index].featureSlot, true);
				}
				RecordExecutionCopy(execution, index, slots[index]->colorWork.observation.copiedLogicalBytes);
			} else {
				CS_GPU_DETAIL_PASS("Upscaling::NRColorInputCopy", execution ? execution->Snapshot().regions[index].colorCopyPass : nullptr);
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (resources[index].roi.compactSource)
					CopyTextureSubrect(a_args.front().context, slots[index]->color.resource11.Get(), resources[index].color.texture.Get(), *resources[index].roi.compactSource, 0, 0);
				else
#endif
					CopyTextureSubrect(
						a_args.front().context,
						slots[index]->color.resource11.Get(),
						resources[index].color.texture.Get(),
						resources[index].nativeLayout.color.valid);
				if (execution)
					RecordExecutionCopy(execution, index, execution->Descriptor().regions[index].color.workLogicalBytes);
			}
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
		if (!CopyDepthBatchLocked(
				a_args,
				std::span(slots.data(), a_args.size()),
				std::span(resources.data(), a_args.size()), execution)) {
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
			CS_GPU_DETAIL_PASS("Upscaling::NRMotionVectorCopy", execution ? execution->Snapshot().regions[index].motionCopyPass : nullptr);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (resources[index].roi.compactSource)
				CopyTextureSubrect(a_args.front().context, slots[index]->motionVectors.resource11.Get(), resources[index].motionVectors.texture.Get(), *resources[index].roi.compactSource, 0, 0);
			else
#endif
				CopyTextureSubrect(
					a_args.front().context,
					slots[index]->motionVectors.resource11.Get(),
					resources[index].motionVectors.texture.Get(),
					resources[index].nativeLayout.motion.valid);
			if (execution)
				RecordExecutionCopy(execution, index, execution->Descriptor().regions[index].motion.workLogicalBytes);
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
				CS_GPU_DETAIL_PASS("Upscaling::NRControlMaskCopy", execution ? execution->Snapshot().regions[index].controlCopyPass : nullptr);
				SetActiveFeatureSlotLocked(a_args[index].featureSlot);
				CopyTextureSubrect(
					a_args.front().context,
					slots[index]->controlMask.resource11.Get(),
					resources[index].controlMask.texture.Get(),
					resources[index].nativeLayout.controlMask.valid);
				Increment(snapshot_.counters.controlMaskCopies);
				if (execution)
					RecordExecutionCopy(execution, index, execution->Descriptor().regions[index].controlMask.workLogicalBytes);
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
		preparationTimer.Stop();

		activeStage_ = RendererStage::CommandBegin;
		ID3D12GraphicsCommandList* commandList = nullptr;
		ExecutionCpuTimer commandBeginTimer(execution, &ExecutionSnapshot::commandBeginCpuMicroseconds);
		const bool commandBegan = interop_.BeginD3D12(&commandList, execution);
		commandBeginTimer.Stop();
		if (!commandBegan || !commandList) {
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
			const auto add = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
				if (!colorConfiguration_.experiments.SharedSourceTransportEnabled()) {
					sharedResources[sharedResourceCount++] = { resource, state };
					return true;
				}
				return AddSourceTransition(sharedResources, sharedResourceCount, FeatureResourceTransition{ resource, state });
			};
			const auto inputState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			if (!add(slots[index]->color.resource12.Get(), colorConfiguration_.experiments.transportBypass ? D3D12_RESOURCE_STATE_COPY_SOURCE : inputState) ||
				!add(slots[index]->depth.resource12.Get(), inputState) ||
				!add(slots[index]->motionVectors.resource12.Get(), inputState) ||
				(a_args[index].controlMask && !add(slots[index]->controlMask.resource12.Get(), inputState)) ||
				!add(slots[index]->output.resource12.Get(), colorConfiguration_.experiments.transportBypass ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
				const bool aborted = recordingGuard.Abort();
				return FailLocked(RendererStage::CommandBegin, E_INVALIDARG,
					"shared resources require conflicting states or exceed the batch capacity", a_args[index].featureSlot, true, !aborted);
			}
			Add(pixelCount, resources[index].roi.inferenceContext.Area());
			featureSlotMask |= 1u << a_args[index].featureSlot;
		}
		const auto resourceSpan =
			std::span(sharedResources.data(), sharedResourceCount);
		TransitionResources(commandList, resourceSpan, true);
		const D3D12InteropSubmissionTiming timing{
			.frameId = a_args.front().frameId,
			.pixelCount = pixelCount,
			.evaluationCount = colorConfiguration_.experiments.transportBypass ? 0u : static_cast<std::uint32_t>(a_args.size()),
			.featureSlotMask = featureSlotMask,
			.insertionPoint = a_args.front().insertionPoint,
			.logicalEyeCount = logicalEyeCount,
			.execution = execution,
		};
		const bool timingStarted = colorConfiguration_.experiments.transportBypass ?
		                               interop_.RecordTransportSubmission(timing) :
		                               interop_.BeginFeatureTiming(commandList, timing);
		if (!timingStarted) {
			const bool aborted = recordingGuard.Abort();
			return FailLocked(
				RendererStage::CommandBegin,
				aborted ? E_FAIL : interop_.LastError(),
				"D3D12 Feature 18 timestamp recording could not begin",
				a_args.front().featureSlot,
				true,
				!aborted);
		}

		std::array<bool, kMaximumRegionEvaluations> forcedHistoryReset{};
		std::array<bool, kMaximumRegionEvaluations> discontinuousHistoryReset{};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			const auto& slot = *slots[index];
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			const bool evaluationDiscontinuous = slot.historyValid &&
			                                     !IsSequentialFrame(
													 slot.lastSuccessfulFrame, a_args[index].frameId);
			const bool sourceDiscontinuous = slot.historyValid &&
			                                 !IsSourceWorldFrameContinuous(
												 slot.lastSuccessfulSourceWorldFrame,
												 a_args[index].sourceWorldFrame);
			discontinuousHistoryReset[index] =
				a_args[index].synchronizedHistoryDiscontinuity ||
				evaluationDiscontinuous || sourceDiscontinuous;
			forcedHistoryReset[index] =
				a_args[index].synchronizedHistoryReset ||
				!slot.historyValid ||
				slot.historyKey != resources[index].historyKey ||
				discontinuousHistoryReset[index];
			if (execution)
				execution->Update([&](auto& evidence) {
					evidence.regions[index].resetReasons = (a_args[index].reset ? ResetCaller : 0u) |
					                                       (!slot.historyValid ? ResetHistoryInvalid : 0u) |
					                                       (slot.historyKey != resources[index].historyKey ? ResetHistoryKeyChanged : 0u) |
					                                       (evaluationDiscontinuous ? ResetEvaluationDiscontinuity : 0u) |
					                                       (sourceDiscontinuous ? ResetSourceDiscontinuity : 0u) |
					                                       (a_args[index].synchronizedHistoryReset || a_args[index].synchronizedHistoryDiscontinuity ? ResetSynchronized : 0u);
				});
		}
		// Synchronize only the same actor cluster across eyes. Two same-eye regions
		// (or different clusters visible in opposite eyes) never share temporal state.
		for (std::size_t left = 0; left < a_args.size(); ++left) {
			for (std::size_t right = 0; right < a_args.size(); ++right) {
				if (!IsMatchingRegionStereoPair(a_args[left].featureSlot, clusterIdentities[left],
						a_args[right].featureSlot, clusterIdentities[right]))
					continue;
				const bool forced = forcedHistoryReset[left] || forcedHistoryReset[right];
				if (execution && forced)
					execution->Update([&](auto& evidence) {
						if (!forcedHistoryReset[left])
							evidence.regions[left].resetReasons |= ResetClusterPeer;
						if (!forcedHistoryReset[right])
							evidence.regions[right].resetReasons |= ResetClusterPeer;
					});
				const bool discontinuous =
					discontinuousHistoryReset[left] || discontinuousHistoryReset[right];
				forcedHistoryReset[left] = forcedHistoryReset[right] = forced;
				discontinuousHistoryReset[left] = discontinuousHistoryReset[right] = discontinuous;
			}
		}

		bool suppressEvaluationTiming = false;
		ExperimentalKernelBatch* activeKernelBatch = nullptr;
		kernelBatchApplied_ = false;
		if (kernelBatch_ && kernelBatchMode_) {
			if (kernelFrame && kernelBatch_->GetStatus().initialized) {
				for (std::size_t index = 0; index < kernelFrame->regions.size(); ++index)
					kernelFrame->regions[index].reset |= forcedHistoryReset[index];
				recordingGuard.kernelBatch = kernelBatch_.get();
				if (kernelBatch_->BeginFrame(commandList, interop_.GetLifetimeSnapshot(), *kernelFrame)) {
					activeKernelBatch = kernelBatch_.get();
					suppressEvaluationTiming = true;
					kernelBatchFrameReason_ = "admitted";
				} else {
					const auto reason = kernelBatch_->GetStatus().reason;
					const bool aborted = recordingGuard.Abort();
					return FailLocked(RendererStage::FeatureEvaluate, E_FAIL,
						std::format("kernel frame ownership admission failed: {}", reason),
						a_args.front().featureSlot, true, !aborted);
				}
			}
		} else if (kernelBatchMode_ && !kernelBatchFallbackLatched_) {
			kernelBatchFrameReason_ = std::string(kernelAdmissionReason);
		}
		activeStage_ = RendererStage::FeatureEvaluate;
		const auto retryIndependent = [&](std::string reason) {
			if (!recordingGuard.Abort())
				return FailLocked(RendererStage::FeatureEvaluate, E_FAIL,
					"kernel admission failed and the unsubmitted list could not be aborted", a_args.front().featureSlot, true, true);
			kernelBatchFallbackLatched_ = true;
			kernelBatchApplied_ = false;
			kernelBatchRejection_ = activeKernelBatch->GetStatus();
			kernelBatchFrameReason_ = std::move(reason);
			executionCompletion.failureOverride = RendererStage::FeatureEvaluate;
			if (!TeardownBackendLocked(false, false, false, execution))
				return false;
			kernelBatchRejection_ = kernelBatch_->GetStatus();
			kernelBatch_.reset();
			outcomeGuard.active = false;
			return ApplyRegionBatchLocked(a_logicalArgs, a_logicalOutcome
#ifdef DEVBENCH_BRIDGE_ENABLED
				,
				a_shared
#endif
			);
		};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			auto& slot = *slots[index];
			const auto& args = a_args[index];
			if (colorConfiguration_.experiments.transportBypass) {
				const auto& roi = resources[index].roi.inferenceContext;
				D3D12_TEXTURE_COPY_LOCATION destination{};
				destination.pResource = slot.output.resource12.Get();
				destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				D3D12_TEXTURE_COPY_LOCATION source{};
				source.pResource = slot.color.resource12.Get();
				source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				const D3D12_BOX box{ roi.baseX, roi.baseY, 0, roi.baseX + roi.width, roi.baseY + roi.height, 1 };
				commandList->CopyTextureRegion(&destination, roi.baseX, roi.baseY, 0, &source, &box);
				if (execution)
					RecordExecutionCopy(execution, index, execution->Descriptor().regions[index].output.workLogicalBytes);
				// No NGX call, inference mask or inference timer is reported for a copy.
				continue;
			}
			SetActiveFeatureSlotLocked(args.featureSlot);
			const bool forcedReset = forcedHistoryReset[index];
			const bool discontinuousReset = discontinuousHistoryReset[index];
			const bool effectiveReset = args.reset || forcedReset;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (lifetime.enabled)
				lifetime.record->regions[index].effectiveReset = effectiveReset;
#endif
			if (args.reset)
				Increment(snapshot_.counters.callerHistoryResets);
			if (forcedReset)
				Increment(snapshot_.counters.forcedHistoryResets);
			if (discontinuousReset)
				Increment(snapshot_.counters.discontinuousHistoryResets);
			bool evaluationAttempted = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (colorConfiguration_.experiments.SharedSourceTransportEnabled())
				requestedCapacity_ = { resources[index].resourceKey, reinterpret_cast<std::uintptr_t>(args.device), args.featureSlot, static_cast<std::uint32_t>(a_args.size()) };
#endif
			RuntimeExecutionEvidence runtimeEvidence{};
			// Runtime diagnostics can throw after evaluation; preserve the actual call outcome.
			const SKSE::stl::scope_exit retainRuntimeEvidence([&]() noexcept {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (lifetime.enabled) {
					auto& region = lifetime.record->regions[index];
					region.createAttempted = runtimeEvidence.createAttempted;
					region.createSucceeded = runtimeEvidence.createSucceeded;
					region.evaluateAttempted = runtimeEvidence.evaluateAttempted;
					region.evaluateSucceeded = runtimeEvidence.evaluateSucceeded;
				}
#endif
				if (execution)
					execution->Update([&](auto& evidence) {
						evidence.regions[index].runtime = runtimeEvidence;
						if (runtimeEvidence.evaluateAttempted)
							evidence.attemptedPhysicalSlotMask |= 1u << args.featureSlot;
						if (runtimeEvidence.evaluateSucceeded)
							evidence.succeededPhysicalSlotMask |= 1u << args.featureSlot;
					});
			});
			if (execution)
				execution->Update([&](auto& evidence) { evidence.regions[index].effectiveReset = effectiveReset; });
			ID3D12GraphicsCommandList* evaluationList = commandList;
			if (activeKernelBatch)
				evaluationList = activeKernelBatch->BeginEvaluation(
					(args.featureSlot % kLogicalFeatureSlotCount) % kEyeCount,
					args.featureSlot / kLogicalFeatureSlotCount);
			bool nativeEvaluated = false;
			try {
				nativeEvaluated = Runtime::Instance().Execute(
					evaluationList,
					args.featureSlot,
					slot.color.resource12.Get(),
					slot.depth.resource12.Get(),
					slot.motionVectors.resource12.Get(),
					slot.output.resource12.Get(),
					args.controlMask ? slot.controlMask.resource12.Get() : nullptr,
					resources[index].nativeLayout,
					args.tuning,
					effectiveReset,
					&evaluationAttempted,
#ifdef DEVBENCH_BRIDGE_ENABLED
					execution || lifetime.enabled ? &runtimeEvidence : nullptr,
#else
					execution ? &runtimeEvidence : nullptr,
#endif
					execution && !suppressEvaluationTiming ? &interop_ : nullptr,
					static_cast<std::uint32_t>(index), activeKernelBatch);
			} catch (...) {
				if (activeKernelBatch) {
					const auto status = activeKernelBatch->GetStatus();
					if (status.canFallback)
						return retryIndependent(status.reason);
				}
				throw;
			}
			const bool evaluationRecorded = !activeKernelBatch || activeKernelBatch->EndEvaluation();
			if (evaluationAttempted) {
				Increment(snapshot_.counters.featureEvaluations);
				a_outcome.evaluationAttemptedFeatureSlotMask |= 1u << args.featureSlot;
			}
			if (!evaluationRecorded) {
				const auto status = activeKernelBatch->GetStatus();
				if (status.canFallback)
					return retryIndependent(status.reason);
				const bool aborted = recordingGuard.Abort();
				return FailLocked(RendererStage::FeatureEvaluate, E_FAIL,
					std::format("kernel evaluation failed: {}", status.reason), args.featureSlot, true, !aborted);
			}
			if (!nativeEvaluated) {
				const std::string runtimeDetail = Runtime::Instance().Detail();
				const bool aborted = recordingGuard.Abort();
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
					logicalEyeCount == 2u);
			});
		}
		snapshot_.lastCompletedStage = RendererStage::FeatureEvaluate;

		if (activeKernelBatch && !activeKernelBatch->FinishFrame()) {
			const auto status = activeKernelBatch->GetStatus();
			if (status.canFallback)
				return retryIndependent(status.reason);
			const bool aborted = recordingGuard.Abort();
			return FailLocked(RendererStage::FeatureEvaluate, E_FAIL,
				std::format("kernel batch submission failed: {}", status.reason), a_args.front().featureSlot, true, !aborted);
		}

		if (!colorConfiguration_.experiments.transportBypass && !interop_.EndFeatureTiming(commandList)) {
			const bool aborted = recordingGuard.Abort();
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
		if (activeKernelBatch && !activeKernelBatch->Submitted(interop_.GetLifetimeSnapshot()))
			return FailLocked(RendererStage::CommandEnd, E_FAIL,
				"kernel batch submission fence could not be proven; external output was withheld",
				a_args.front().featureSlot, true, true);
		if (activeKernelBatch) {
			const auto status = activeKernelBatch->GetStatus();
			const bool privateBatch = kernelBatchMode_ == ExperimentalKernelBatch::Mode::SharedN2 || kernelBatchMode_ == ExperimentalKernelBatch::Mode::ClonedN2;
			kernelBatchApplied_ = !status.warmup && privateBatch;
			if (status.warmup && !status.reason.empty())
				kernelBatchFrameReason_ = status.reason;
			if (privateBatch && status.warmup && !status.graphFamilyMatchesQualified) {
				kernelBatchFallbackLatched_ = true;
				kernelBatchRejection_ = status;
				kernelBatchFrameReason_ = status.reason;
			}
		}
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

		// Reconstruct ALL physical regions before the first external write. This is
		// shared by standard, single-ROI and multi-ROI NR, before character blending.
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (Replay::IsArmed())
			CaptureReplayBatch(a_args, std::span(slots.data(), a_args.size()),
				std::span(resources.data(), a_args.size()), execution);
#endif
		if (colorConfiguration_.Enabled()) {
			for (std::size_t index = 0; index < a_args.size(); ++index) {
				const auto copiedBefore = slots[index]->colorWork.observation.copiedLogicalBytes;
				if (!colorPipeline_.Reconstruct(a_args.front().context, slots[index]->colorWork,
						slots[index]->output.resource11.Get(), slots[index]->output.srv11.Get(),
						slots[index]->color.srv11.Get(), colorConfiguration_)) {
					return FailLocked(RendererStage::OutputCommit, E_FAIL,
						"shared NR colour reconstruction failed before pair commit", a_args[index].featureSlot, true);
				}
				RecordExecutionCopy(execution, index, slots[index]->colorWork.observation.copiedLogicalBytes - copiedBefore);
			}
			if (const HRESULT reason = GetDeviceRemovalReasonLocked(S_OK); FAILED(reason))
				return FailLocked(RendererStage::OutputCommit, reason,
					"device removal during private NR colour reconstruction", a_args.front().featureSlot, true);
		}

#ifdef DEVBENCH_BRIDGE_ENABLED
		// Validate every copy domain before either eye can change caller-owned output.
		for (std::size_t index = 0; index < a_shared.size(); ++index) {
			if (resources[index].roi.compactSource || resources[index].roi.inferenceContext != a_shared[index].inferenceContext ||
				(colorConfiguration_.Enabled() && slots[index]->colorWork.observation.rect != a_shared[index].inferenceContext))
				return FailLocked(RendererStage::OutputCommit, E_INVALIDARG, "shared context output coordinates changed before commit", a_args[index].featureSlot, true);
			for (std::uint32_t region = 0; region < SharedContext::OutputCount(a_shared[index]); ++region)
				if (!SharedContext::CopyDomain(a_shared[index], region))
					return FailLocked(RendererStage::OutputCommit, E_INVALIDARG, "shared context ownership exceeds initialized output", a_args[index].featureSlot, true);
		}
#endif
		// Both private inputs are prepared before submission and neither caller-owned
		// output is written until every eye has recorded successfully.
		const auto outputCommitStarted = std::chrono::steady_clock::now();
		ExecutionCpuTimer outputCommitTimer(execution, &ExecutionSnapshot::commitCpuMicroseconds);
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			CS_GPU_DETAIL_PASS("Upscaling::NROutputCommit", execution ? execution->Snapshot().regions[index].outputCopyPass : nullptr);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (!a_shared.empty()) {
				Color::ComputeStateGuard<5> guard(a_args.front().context);
				for (std::uint32_t region = 0; region < SharedContext::OutputCount(a_shared[index]); ++region) {
					const auto domain = *SharedContext::CopyDomain(a_shared[index], region);
					CopyTextureSubrect(a_args.front().context, resources[index].output.texture.Get(),
						colorConfiguration_.Enabled() ? slots[index]->colorWork.result.resource.Get() : slots[index]->output.resource11.Get(),
						colorConfiguration_.Enabled() ? domain.contextLocal : domain.output, domain.output.baseX, domain.output.baseY);
					RecordExecutionCopy(execution, index, LogicalTextureBytes(resources[index].resourceKey.outputFormat, domain.output.width, domain.output.height));
				}
			} else
#endif
				if (colorConfiguration_.Enabled()) {
				colorPipeline_.Commit(a_args.front().context, slots[index]->colorWork,
					resources[index].output.texture.Get());
			} else {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (const auto& compact = resources[index].roi.compactSource)
					CopyTextureSubrect(a_args.front().context, resources[index].output.texture.Get(), slots[index]->output.resource11.Get(), resources[index].roi.ownedOutput,
						compact->baseX + resources[index].roi.ownedOutput.baseX, compact->baseY + resources[index].roi.ownedOutput.baseY);
				else
#endif
					CopyTextureSubrect(
						a_args.front().context,
						resources[index].output.texture.Get(),
						slots[index]->output.resource11.Get(),
						resources[index].roi.ownedOutput);
			}
			if (execution) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (a_shared.empty())
#endif
					RecordExecutionCopy(execution, index, LogicalTextureBytes(resources[index].resourceKey.outputFormat, resources[index].roi.ownedOutput.width, resources[index].roi.ownedOutput.height));
				execution->Update([&](auto& evidence) {
					evidence.regions[index].outputCopyEnqueued = true;
				});
			}
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
		outputCommitTimer.Stop();
		if (execution)
			execution->Update([&](auto& evidence) {
				for (std::size_t index = 0; index < a_args.size(); ++index) {
					evidence.regions[index].privateOutputCommitted = true;
					evidence.committedPhysicalSlotMask |= 1u << a_args[index].featureSlot;
				}
			});
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			slots[index]->historyKey = resources[index].historyKey;
			slots[index]->lastSuccessfulFrame = a_args[index].frameId;
			slots[index]->lastSuccessfulSourceWorldFrame =
				a_args[index].sourceWorldFrame;
			slots[index]->historyValid = !colorConfiguration_.experiments.transportBypass;
		}
		activeStage_ = RendererStage::Complete;
		for (const auto& args : a_args) {
			SetActiveFeatureSlotLocked(args.featureSlot);
			SucceedLocked(args.featureSlot);
		}
		RefreshInteropTelemetryLocked();
		executionCompletion.succeeded = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (!a_shared.empty())
			for (std::size_t index = 0; index < a_logicalArgs.size(); ++index)
				a_logicalOutcome.outputPlans[a_logicalArgs[index].featureSlot] = a_shared[index].output;
		if (measuredPlanEvaluating_)
			for (const auto& logical : a_logicalArgs)
				a_logicalOutcome.outputPlans[logical.featureSlot] = CharacterOutputPlan{ logical.computeSubrect, logical.computeRegions };
		if (lifetime.enabled)
			lifetime.record->succeeded = true;
#endif
		return true;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	void Renderer::State::CaptureReplayBatch(std::span<const RendererApplyArgs> args,
		std::span<Slot* const> slots, std::span<const ValidatedResources> resources,
		const std::shared_ptr<ExecutionEvidence>& execution) noexcept
	try {
		Replay::Batch batch;
		batch.supported = !args.empty() && args.size() <= 2 &&
		                  !colorConfiguration_.experiments.transportBypass && colorConfiguration_.experiments.applyModelEdit;
		batch.unsupportedReason = "replay requires proven full initialized auto-mask inputs with at most two eyes";
		if (args.empty()) {
			Replay::Fail(batch.unsupportedReason);
			return;
		}
		const auto& first = args.front();
		const auto& context = first.executionContext;
		batch.supported &= context.renderingMode.has_value() && context.sourceTransactionId != 0 &&
		                   context.jitterPixels.has_value() && context.sourceColorOrigin.has_value() && context.sourceGuideOrigin.has_value();
		if (!batch.supported) {
			Replay::Fail(batch.unsupportedReason);
			return;
		}
		const auto& tuning = first.tuning;
		batch.metadata = {
			{ "frame", first.frameId }, { "sourceWorldFrame", first.sourceWorldFrame }, { "generation", first.generation },
			{ "insertionPoint", static_cast<uint32_t>(first.insertionPoint) },
			{ "mode", static_cast<uint32_t>(*context.renderingMode) },
			{ "arrangement", static_cast<uint32_t>(ResolvePipelineArrangement(*context.renderingMode)) },
			{ "source", Evidence::ContextJson(context) }, { "colorRevision", colorConfiguration_.revision },
			{ "inputEpoch", colorConfiguration_.inputEpoch[static_cast<size_t>(first.insertionPoint)] },
			{ "colorConfiguration", Color::ConfigurationEvidenceJson(colorConfiguration_) },
			{ "characterSelection", first.characterVisualIsolation },
			{ "tuning", { { "intensity", tuning.intensity }, { "localToneStrength", tuning.localToneStrength },
							{ "localStructureStrength", tuning.localStructureStrength }, { "skinStructureStrength", tuning.skinStructureStrength },
							{ "style", tuning.style }, { "useAutoMask", tuning.useAutoMask }, { "uiCorrection", tuning.uiCorrection },
							{ "singleSubrectScale", tuning.singleSubrectScale } } },
			{ "execution", execution ? Evidence::ExecutionJson(*execution) : nlohmann::json(nullptr) },
			{ "stage", "native_nr_before_colour_reconstruction_and_csx_composite" }
		};
		BuildProvenance::AttachProducer(batch.metadata);
		auto& runtime = Runtime::Instance();
		batch.runtime = { { "path", runtime.Path().string() }, { "sha256", runtime.Hash() }, { "version", runtime.Version() } };
		ComPtr<IDXGIDevice> dxgi;
		ComPtr<IDXGIAdapter> adapter;
		DXGI_ADAPTER_DESC desc{};
		if (FAILED(first.device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter)) ||
			FAILED(adapter->GetDesc(&desc))) {
			Replay::Fail("replay adapter identity unavailable");
			return;
		}
		batch.adapter = { { "vendorId", desc.VendorId }, { "deviceId", desc.DeviceId },
			{ "description", std::filesystem::path(desc.Description).string() },
			{ "luid", { { "low", desc.AdapterLuid.LowPart }, { "high", desc.AdapterLuid.HighPart } } } };
		LARGE_INTEGER driverVersion{};
		batch.adapter["driverVersion"] = SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion)) ?
		                                     nlohmann::json(driverVersion.QuadPart) :
		                                     nlohmann::json(nullptr);
		for (size_t index = 0; index < args.size(); ++index) {
			const auto& value = args[index];
			const auto& rect = resources[index].roi.inferenceContext;
			const auto& guideRect = resources[index].nativeLayout.depth.valid;
			const auto& colorRect = resources[index].nativeLayout.color.valid;
			batch.supported &= value.featureSlot < 4 && !value.controlMask && value.tuning.useAutoMask &&
			                   value.colorWidth == value.outputWidth && value.colorHeight == value.outputHeight &&
			                   guideRect.baseX == 0 && guideRect.baseY == 0 && guideRect.width == value.guideWidth && guideRect.height == value.guideHeight &&
			                   colorRect.baseX == 0 && colorRect.baseY == 0 && colorRect.width == value.colorWidth && colorRect.height == value.colorHeight &&
			                   rect.baseX == 0 && rect.baseY == 0 && rect.width == value.outputWidth && rect.height == value.outputHeight &&
			                   value.executionContext.sourceTransactionId == context.sourceTransactionId;
			const auto& nativeLayout = resources[index].nativeLayout;
			batch.eyes.push_back({ .slot = value.featureSlot, .outputSubrect = rect, .motionVectorScale = nativeLayout.motionVectorScale, .featureUpscaling = value.featureUpscaling, .color = slots[index]->color.resource11.Get(), .depth = slots[index]->depth.resource11.Get(), .motion = slots[index]->motionVectors.resource11.Get(), .output = slots[index]->output.resource11.Get(), .metadata = { { "source", Evidence::ContextJson(value.executionContext) }, { "viewport", Evidence::ViewportJson(value.viewportCrop) }, { "nativeLayout", Evidence::NativeLayoutJson(nativeLayout) }, { "callerReset", value.reset }, { "synchronizedHistoryReset", value.synchronizedHistoryReset } } });
		}
		Replay::OfferBatch(first.device, first.context, batch);
	} catch (...) {
		Replay::Fail("native replay metadata could not be retained");
	}
#endif

	bool Renderer::State::ResetLocked(bool a_resetShader, bool a_destruction)
	{
		if (!TeardownBackendLocked(a_resetShader, a_destruction, false))
			return false;
		kernelBatchFallbackLatched_ = false;
		kernelBatchRejection_ = {};
		kernelBatchApplied_ = false;
		return true;
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
		// Construct shared services first so Renderer teardown precedes their teardown.
		(void)Runtime::Instance();
		(void)Color::Registry::Instance();
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
			state_->CaptureColorConfiguration(a_args);
			CS_GPU_PASS_CAPTURE("Upscaling::DLSSNeuralRendering", state_->wholePass_);
			const bool succeeded = state_->ApplyLocked(a_args, outcome);
			state_->FinishCapture(outcome);
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
			state_->FinishCapture(outcome);
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
			state_->CaptureColorConfiguration(a_args[0]);
			CS_GPU_PASS_CAPTURE("Upscaling::DLSSNeuralRenderingStereo", state_->wholePass_);
			const bool succeeded = state_->ApplyStereoLocked(a_args, outcome);
			Increment(
				succeeded ? state_->snapshot_.counters.stereoSuccesses :
							state_->snapshot_.counters.stereoFailures);
			state_->FinishCapture(outcome);
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
			state_->FinishCapture(outcome);
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
			state_->CaptureColorConfiguration(a_args[0]);
			CS_GPU_PASS_CAPTURE("Upscaling::DLSSNeuralRenderingSequentialStereo", state_->wholePass_);
			const bool succeeded =
				state_->ApplySequentialStereoLocked(a_args, outcome);
			state_->FinishCapture(outcome);
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
			state_->FinishCapture(outcome);
			if (a_outcome)
				*a_outcome = outcome;
			state_->SetActiveFeatureSlotLocked(Runtime::kFeatureSlotCount);
			return false;
		}
	}

	std::array<CaptureInputs, 2> Renderer::GetCaptureInputs() const
	{
		std::scoped_lock lock(state_->mutex_);
		(void)state_->interop_.GetTelemetry();
		return state_->captureInputs_;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	bool Renderer::MeasuredPlanSearchEnabled() const noexcept
	{
		return state_->measuredPlanEnabled_.load(std::memory_order_relaxed) &&
		       (state_->measuredPlanContinuous_.load(std::memory_order_relaxed) || state_->measuredPlanInspectionRequested_.load(std::memory_order_relaxed));
	}

	nlohmann::json Renderer::MeasuredPlanControl(const nlohmann::json& request)
	{
		std::unique_lock lock(state_->mutex_, std::try_to_lock);
		if (!lock.owns_lock())
			return { { "ok", false }, { "errorCode", "renderer_busy" }, { "mutationApplied", false } };
		const auto action = request.at("action").get<std::string>();
		if (action == "status" && state_->measuredCalibration_.remaining && State::CalibrationMilliseconds() - state_->measuredCalibration_.startedMilliseconds >= MeasuredPlan::Calibration::kMaximumMilliseconds) {
			state_->measuredCalibration_.remaining = 0;
			state_->measuredCalibration_.reason = "calibration_expired";
		}
		state_->measuredPlanContinuous_ = state_->measuredPlanProfile_.has_value() || state_->measuredCalibration_.remaining != 0;
		if (action == "configure") {
			if (!request.at("enabled").is_boolean())
				throw std::invalid_argument("enabled must be boolean");
			state_->measuredPlanEnabled_ = request.at("enabled").get<bool>();
			state_->measuredPlanState_ = {};
			state_->measuredCalibration_.remaining = 0;
			state_->measuredCalibration_.reason = "calibration_cancelled";
			state_->measuredPlanInspectionRequested_ = state_->measuredPlanEnabled_.load();
		} else if (action == "calibration_start") {
			try {
				const auto& count = request.at("frameCount");
				if (!count.is_number_unsigned() || count.get<std::uint64_t>() < 1 || count.get<std::uint64_t>() > MeasuredPlan::Calibration::kMaximumFrames)
					throw std::invalid_argument("frameCount must be 1..600");
				if (!state_->measuredPlanEnabled_ || state_->measuredPlanProfile_ || state_->measuredCalibration_.remaining)
					throw std::invalid_argument("enable search without a profile or active calibration before starting");
				const auto& diagnostics = state_->measuredPlanDiagnostics_;
				if (!request.at("identity").is_object() || request.at("identity") != diagnostics.at("identity"))
					throw std::invalid_argument("calibration identity does not match the observed backend");
				const auto key = MeasuredPlan::CalibrationKey(request.at("key"));
				bool found = false;
				for (const auto& candidate : diagnostics.at("candidates"))
					found |= candidate.at("valid").get<bool>() && !candidate.at("capacityRejected").get<bool>() &&
					         MeasuredPlan::CalibrationKey(candidate.at("key")) == key;
				if (!found)
					throw std::invalid_argument("calibration key is not a current admissible candidate");
				MeasuredPlan::Calibration calibration{ request.at("identity").dump(), key, count.get<std::uint32_t>() };
				calibration.generation = diagnostics.at("generation").get<std::uint64_t>();
				calibration.lastFrame = diagnostics.at("sourceWorldFrame").get<std::uint32_t>();
				calibration.startedMilliseconds = State::CalibrationMilliseconds();
				calibration.reason = "calibration_armed";
				state_->measuredCalibration_ = std::move(calibration);
				state_->measuredCalibrationRecords_.clear();
				state_->measuredCalibrationKeys_.clear();
				state_->measuredCalibrationEvidenceFailed_ = false;
				state_->measuredPlanState_ = {};
			} catch (const std::exception& error) {
				return { { "ok", false }, { "errorCode", "measured_calibration_rejected" }, { "error", error.what() }, { "mutationApplied", false } };
			}
		} else if (action == "calibration_cancel") {
			state_->measuredCalibration_.remaining = 0;
			state_->measuredCalibration_.reason = "calibration_cancelled";
			state_->measuredPlanState_ = {};
		} else if (action == "load_profile") {
			std::optional<MeasuredPlan::Profile> profile;
			try {
				if (state_->measuredCalibration_.remaining)
					throw std::invalid_argument("cancel the active calibration before loading a profile");
				profile = MeasuredPlan::ReadProfile(request.at("profile"));
				if (!state_->measuredPlanDiagnostics_.contains("identity") || profile->identity != state_->measuredPlanDiagnostics_["identity"].dump())
					throw std::invalid_argument("profile identity does not match the current observed backend");
			} catch (const std::exception& error) {
				return { { "ok", false }, { "errorCode", "measured_profile_rejected" }, { "error", error.what() }, { "mutationApplied", false } };
			}
			state_->measuredPlanProfile_ = std::move(profile);
			state_->measuredPlanState_ = {};
		} else if (action == "clear_profile") {
			state_->measuredPlanProfile_.reset();
			state_->measuredPlanState_ = {};
		} else if (action != "status")
			throw std::invalid_argument("unknown measured-plan action");
		if (action == "status" && state_->measuredPlanEnabled_)
			state_->measuredPlanInspectionRequested_ = true;
		state_->measuredPlanContinuous_ = state_->measuredPlanProfile_.has_value() || state_->measuredCalibration_.remaining != 0;
		return { { "ok", true }, { "action", action }, { "enabled", state_->measuredPlanEnabled_.load(std::memory_order_relaxed) },
			{ "inspectionPending", state_->measuredPlanInspectionRequested_.load() },
			{ "profileLoaded", state_->measuredPlanProfile_.has_value() }, { "productionProfileAdopted", false },
			{ "calibration", { { "remainingFrames", state_->measuredCalibration_.remaining }, { "reason", state_->measuredCalibration_.reason },
								 { "evidenceComplete", !state_->measuredCalibrationEvidenceFailed_ }, { "keys", state_->measuredCalibrationKeys_ },
								 { "records", state_->measuredCalibrationRecords_ } } },
			{ "diagnostics", state_->measuredPlanDiagnostics_ } };
	}
#endif

	bool Renderer::Reset(bool a_clearTransportRejections)
	{
		std::scoped_lock lock(state_->mutex_);
		try {
			const bool reset = state_->ResetLocked(false, false);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (reset && a_clearTransportRejections) {
				state_->capacityRejections_ = {};
				state_->capacityFallback_ = {};
				state_->compactRetention_ = {};
			}
#else
			(void)a_clearTransportRejections;
#endif
			return reset;
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

	nlohmann::json Renderer::State::KernelBatchJsonLocked() const
	{
		using Json = nlohmann::json;
		const auto status = kernelBatch_ ? kernelBatch_->GetStatus() : kernelBatchRejection_;
		return { { "selectedMode", RoiExecutionModeName(roiExecutionMode_) },
			{ "effectiveMode", kernelBatchApplied_ ? "batched" : roiExecutionMode_ == RoiExecutionMode::AutomaticSingle ? "automatic_single" :
																														  "independent" },
			{ "reason", status.reason.empty() ? kernelBatchFrameReason_ : status.reason },
			{ "backend", kernelBatchMode_ ? ExperimentalKernelBatch::ModeName(*kernelBatchMode_) : "original" },
			{ "initialized", status.initialized }, { "failed", status.failed }, { "epochStale", status.epochStale },
			{ "warmup", status.warmup }, { "frames", status.frames }, { "warmupFrames", status.warmupFrames },
			{ "descriptorRefreshFrames", status.descriptorRefreshFrames },
			{ "batchedFrames", status.batchedFrames }, { "logicalLaunches", status.logicalLaunches },
			{ "physicalLaunches", status.physicalLaunches }, { "privateLaunches", status.privateLaunches },
			{ "pendingFrames", status.pendingFrames }, { "retirementProven", status.retirementProven },
			{ "graphMatchesQualified", status.graphMatchesQualified }, { "graphFamilyMatchesQualified", status.graphFamilyMatchesQualified }, { "graphLaunches", status.graphLaunches },
			{ "graphIdentities", status.graphIdentities }, { "inspection", status.inspection },
			{ "graphFamilyIdentities", status.graphFamilyIdentities }, { "regionPairs", status.regionPairs },
			{ "devbenchOverride", kernelBatchOverride_ }, { "inspectUnqualifiedPipeline", kernelInspectUnqualified_ } };
	}

	nlohmann::json Renderer::GetKernelBatchStatus() const
	{
		std::scoped_lock lock(state_->mutex_);
		return state_->KernelBatchJsonLocked();
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	nlohmann::json Renderer::KernelBatchControl(const nlohmann::json& request)
	{
		using Json = nlohmann::json;
		std::unique_lock lock(state_->mutex_, std::try_to_lock);
		if (!lock.owns_lock())
			return { { "ok", false }, { "errorCode", "renderer_busy" }, { "mutationApplied", false } };
		const auto action = request.at("action").get<std::string>();
		if (action == "status")
			return { { "ok", true }, { "mutationApplied", false }, { "status", state_->KernelBatchJsonLocked() } };
		if (action != "configure")
			return { { "ok", false }, { "errorCode", "kernel_batch_invalid_action" }, { "mutationApplied", false } };
		const auto name = request.at("mode").get<std::string>();
		std::optional<ExperimentalKernelBatch::Mode> mode;
		for (auto candidate : { ExperimentalKernelBatch::Mode::Original, ExperimentalKernelBatch::Mode::LayerControl,
				 ExperimentalKernelBatch::Mode::ClonedN2, ExperimentalKernelBatch::Mode::SharedN2 })
			if (name == ExperimentalKernelBatch::ModeName(candidate))
				mode = candidate;
		if (!mode && name != "user_choice")
			return { { "ok", false }, { "errorCode", "kernel_batch_invalid_mode" }, { "mutationApplied", false } };
		const bool inspect = request.value("inspectUnqualifiedPipeline", false);
		if (inspect && mode != ExperimentalKernelBatch::Mode::Original)
			return { { "ok", false }, { "errorCode", "kernel_inspection_requires_original_mode" }, { "mutationApplied", false } };
		const bool batch = mode == ExperimentalKernelBatch::Mode::ClonedN2 || mode == ExperimentalKernelBatch::Mode::SharedN2;
		const auto manifest = batch ? std::filesystem::path("Data/Shaders/Upscaling/NeuralRendering/KernelBatch") /
		                                  (mode == ExperimentalKernelBatch::Mode::ClonedN2 ? "cloned-n2.json" : "shared-n2.json") :
		                              std::filesystem::path{};
		if (batch && !std::filesystem::is_regular_file(manifest))
			return { { "ok", false }, { "errorCode", "kernel_catalog_missing" }, { "mutationApplied", false } };
		if (!state_->TeardownBackendLocked(false, false, false))
			return { { "ok", false }, { "errorCode", "kernel_retirement_failed" }, { "mutationApplied", true }, { "status", state_->KernelBatchJsonLocked() } };
		state_->kernelBatch_.reset();
		state_->kernelBatchMode_ = mode;
		state_->kernelBatchManifest_ = manifest;
		state_->kernelBatchOverride_ = mode.has_value();
		state_->kernelInspectUnqualified_ = inspect;
		state_->kernelBatchApplied_ = false;
		state_->kernelBatchFallbackLatched_ = false;
		state_->kernelBatchRejection_ = {};
		state_->kernelBatchFrameReason_ = "awaiting_eligible_frame";
		if (mode)
			state_->kernelBatch_ = std::make_unique<ExperimentalKernelBatch>();
		return { { "ok", true }, { "mutationApplied", true }, { "status", state_->KernelBatchJsonLocked() } };
	}

	nlohmann::json Renderer::GetSourceTransportDiagnostics() const
	{
		using json = nlohmann::json;
		std::scoped_lock lock(state_->mutex_);
		json textures = json::array(), slots = json::array(), rejections = json::array();
		std::uint64_t inputBytes = 0, outputBytes = 0, colorBytes = 0, retiredLeaseBytes = 0;
		std::uint64_t activeBytes = 0, cachedBytes = 0, activeColorBytes = 0, cachedColorBytes = 0;
		bool bytesKnown = !state_->quarantined_;
		std::vector<ID3D12Resource*> unique;
		const auto add = [&](const SharedTexture& texture, const char* role, std::uint64_t& total, std::uint32_t slotMask = 0) {
			if (!texture.resource12)
				return;
			const auto found = std::ranges::find(unique, texture.resource12.Get());
			if (found != unique.end()) {
				auto& record = textures[static_cast<std::size_t>(found - unique.begin())];
				record["slotMask"] = record["slotMask"].get<std::uint32_t>() | slotMask;
				return;
			}
			unique.push_back(texture.resource12.Get());
			const auto bytes = LogicalTextureBytes(texture.desc.Format, texture.desc.Width, texture.desc.Height);
			bytesKnown &= bytes.has_value();
			total += bytes.value_or(0);
			textures.push_back({ { "identity", std::to_string(reinterpret_cast<std::uintptr_t>(texture.resource12.Get())) },
				{ "role", role }, { "slotMask", slotMask }, { "logicalBytes", bytes ? json(*bytes) : json(nullptr) } });
		};
		const auto nativeMask = Runtime::Instance().GetResidentFeatureMask();
		std::uint32_t activeMask = 0, cachedMask = 0;
		for (std::size_t index = 0; index < state_->slots_.size(); ++index) {
			const auto& slot = state_->slots_[index];
			if (!slot.resourcesValid)
				continue;
			const bool active = slot.lastSuccessfulFrame == state_->snapshot_.frameId;
			(active ? activeMask : cachedMask) |= 1u << index;
			for (const auto* input : { &slot.color, &slot.depth, &slot.motionVectors, &slot.controlMask })
				add(*input, "input", inputBytes, 1u << index);
			add(slot.output, "private_output", outputBytes, 1u << index);
			const auto color = slot.colorWork.baseline.resource ? LogicalTextureBytes(slot.colorWork.format,
																	  slot.colorWork.capacityWidth, slot.colorWork.capacityHeight) :
			                                                      std::optional<std::uint64_t>(0);
			colorBytes += color.value_or(0) * 2u;
			(active ? activeColorBytes : cachedColorBytes) += color.value_or(0) * 2u;
			bytesKnown &= color.has_value();
			slots.push_back({ { "slot", index }, { "activeInLastRequest", active },
				{ "compactStorage", slot.resourceKey.compact },
				{ "compactMinimumSide", state_->compactRetention_[index].minimumSide },
				{ "compactFullCoordinateFallback", state_->compactRetention_[index].fullCoordinates },
				{ "nativeResident", (nativeMask & (1u << index)) != 0 },
				{ "inputIdentity", std::to_string(reinterpret_cast<std::uintptr_t>(slot.color.resource12.Get())) },
				{ "privateOutputIdentity", std::to_string(reinterpret_cast<std::uintptr_t>(slot.output.resource12.Get())) },
				{ "historyIdentity", slot.historyKey.regionIdentity }, { "historyValid", slot.historyValid },
				{ "sourceTransportOwnerSlot", slot.sourceTransportOwnerSlot },
				{ "guideCapacity", { slot.resourceKey.guideWidth, slot.resourceKey.guideHeight } },
				{ "outputCapacity", { slot.resourceKey.outputWidth, slot.resourceKey.outputHeight } } });
		}
		const auto leases = state_->interop_.GetResourceLeases();
		for (const auto& texture : leases)
			add(texture, "lease_only_retained", retiredLeaseBytes);
		for (auto& texture : textures) {
			const auto mask = texture["slotMask"].get<std::uint32_t>();
			texture["active"] = (mask & activeMask) != 0;
			texture["cachedOnly"] = (mask & cachedMask) != 0 && (mask & activeMask) == 0;
			const auto bytes = texture["logicalBytes"].is_null() ? 0 : texture["logicalBytes"].get<std::uint64_t>();
			if (mask & activeMask)
				activeBytes += bytes;
			else if (mask & cachedMask)
				cachedBytes += bytes;
		}
		for (std::size_t index = 0; index < state_->capacityRejections_.count; ++index) {
			const auto& entry = state_->capacityRejections_.entries[index];
			rejections.push_back({ { "slot", entry.key.slot }, { "evaluationCount", entry.key.evaluationCount }, { "device", std::to_string(entry.key.device) },
				{ "class", entry.kind == CapacityRejectionKind::Pressure ? "transient_pressure" : entry.kind == CapacityRejectionKind::Unsupported ? "unsupported_envelope" :
																																					 "unsafe_provider_failure" },
				{ "result", entry.result }, { "nativeResult", entry.nativeResult },
				{ "colorCapacity", { entry.key.resources.colorWidth, entry.key.resources.colorHeight } },
				{ "guideCapacity", { entry.key.resources.guideWidth, entry.key.resources.guideHeight } },
				{ "outputCapacity", { entry.key.resources.outputWidth, entry.key.resources.outputHeight } },
				{ "controlMaskCapacity", { entry.key.resources.controlMaskWidth, entry.key.resources.controlMaskHeight } },
				{ "formats", { entry.key.resources.colorFormat, entry.key.resources.motionFormat, entry.key.resources.outputFormat, entry.key.resources.controlMaskFormat } },
				{ "controlMaskPresent", entry.key.resources.controlMaskPresent }, { "featureUpscaling", entry.key.resources.featureUpscaling },
				{ "sharedSourceTransport", entry.key.resources.sharedSourceTransport } });
		}
		return { { "schemaVersion", 1 }, { "requested", state_->colorConfiguration_.experiments.SharedSourceTransportEnabled() },
			{ "sharedContext", { state_->SharedContextJsonLocked(0), state_->SharedContextJsonLocked(1) } },
			{ "capacityFallback", { { "active", state_->capacityFallback_.rejected }, { "recoveries", state_->capacityFallback_.recoveries },
									  { "reason", state_->capacityFallback_.reason == CapacityFailure::Pressure ? "pressure" : state_->capacityFallback_.reason == CapacityFailure::Unsupported ? "unsupported" :
																																																  "none" },
									  { "policy", "one_enclosing_context_per_eye_after_fenced_recoverable_failure_until_explicit_nr_reset" } } },
			{ "maximumPhysicalContexts", Runtime::kFeatureSlotCount }, { "allocationPolicy", "lazy_exact_capacity_no_speculative_prewarm" },
			{ "activeSlotMask", activeMask }, { "cachedSlotMask", cachedMask }, { "nativeSlotMask", nativeMask },
			{ "sharedInputLogicalBytes", inputBytes }, { "privateOutputLogicalBytes", outputBytes }, { "privateColorLogicalBytes", colorBytes },
			{ "activeTransportLogicalBytes", activeBytes }, { "cachedOnlyTransportLogicalBytes", cachedBytes },
			{ "activePrivateColorLogicalBytes", activeColorBytes }, { "cachedPrivateColorLogicalBytes", cachedColorBytes },
			{ "leaseOnlyRetainedLogicalBytes", retiredLeaseBytes }, { "leaseCount", leases.size() }, { "logicalBytesKnown", bytesKnown },
			{ "logicalByteScope", "unique_transport_and_private_baseline_result_textures_only_excludes_exposure_queries_buffers_and_native_allocations" },
			{ "nativeAllocationBytes", nullptr }, { "nativeAllocationBytesReason", "provider_does_not_expose_residency_bytes" },
			{ "quarantined", state_->quarantined_ }, { "physicalResidencyMeasured", false },
			{ "nativeResidencyKnown", !state_->quarantined_ },
			{ "abandonedOwnershipBytes", nullptr }, { "abandonedOwnershipPresent", state_->quarantined_ },
			{ "rejections", rejections }, { "rejectionLedgerSaturated", state_->capacityRejections_.saturated },
			{ "recovery", "explicit_nr_reset_after_successful_both_api_retirement_or_new_process" },
			{ "resources", textures }, { "slots", slots } };
	}

	LifetimeSnapshot Renderer::GetLifetimeDiagnostics() const
	{
		std::scoped_lock lock(state_->mutex_);
		return state_->lifetimeDiagnostics_.Snapshot();
	}
#endif
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
