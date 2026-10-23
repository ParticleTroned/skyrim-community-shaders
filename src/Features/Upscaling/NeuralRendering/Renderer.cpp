#include "Renderer.h"
#include "DevelopmentDiagnostics.h"
#include "Globals.h"
#include "GpuPass.h"

#include "CapacityFallback.h"
#include "CharacterRendering.h"
#include "ColorPipeline.h"
#include "ComputeStateGuard.h"
#include "D3D12Interop.h"
#include "MemoryResolutionPolicy.h"
#include "ModelResolution.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "PackedCropEvidenceJson.h"
#endif
#include "PipelinePolicy.h"
#include "ShaderInvalidation.h"
#include "Utils/ComIdentity.h"
#include "Utils/D3D.h"
#include "Utils/GpuMemoryBudget.h"
#include "Utils/RendererContextAccess.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "ExecutionEvidenceJson.h"
#	include "CompactInputLayout.h"
#	include "ReplayCapture.h"
#	include <exception>
#endif

#include <DirectXTex.h>
#include <SKSE/SKSE.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <dxgi1_4.h>
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
		constexpr std::size_t kMaximumTransitionResourceCount = kEyeCount * 5;
		static_assert(kEyeCount <= kMaximumExecutionRegions);

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
				evidence_(std::move(evidence)), field_(field), started_(evidence_ ? DiagnosticNow() : std::chrono::steady_clock::time_point{}) {}
			~ExecutionCpuTimer() { Stop(); }
			void Stop() noexcept
			{
				if (!evidence_)
					return;
				const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
					DiagnosticNow() - started_)
						.count());
				evidence_->Update([&](auto& snapshot) { snapshot.*field_ = (snapshot.*field_).value_or(0) + elapsed; });
				evidence_.reset();
			}

		private:
			std::shared_ptr<ExecutionEvidence> evidence_;
			std::optional<std::uint64_t> ExecutionSnapshot::* field_;
			std::chrono::steady_clock::time_point started_;
		};

#ifdef DEVBENCH_BRIDGE_ENABLED
		class PackedCropCpuTimer
		{
		public:
			PackedCropCpuTimer(PackedCropDiagnostics& diagnostics, std::uint64_t sequence,
				std::optional<std::uint64_t> PackedCropRebuildTimings::* field) :
				diagnostics_(diagnostics), sequence_(sequence), field_(field),
				started_(sequence ? DiagnosticNow() : std::chrono::steady_clock::time_point{}) {}
			~PackedCropCpuTimer() { Stop(); }
			void Stop() noexcept
			{
				if (!sequence_)
					return;
				const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(DiagnosticNow() - started_).count());
				diagnostics_.Update(sequence_, [&](auto& timings) { timings.*field_ = (timings.*field_).value_or(0) + elapsed; });
				sequence_ = 0;
			}

		private:
			PackedCropDiagnostics& diagnostics_;
			std::uint64_t sequence_;
			std::optional<std::uint64_t> PackedCropRebuildTimings::* field_;
			std::chrono::steady_clock::time_point started_;
		};
#endif

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
			if constexpr (!kDevelopmentDiagnostics) {
				return;
			} else {
				if (a_counter != std::numeric_limits<std::uint64_t>::max())
					++a_counter;
			}
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
			if constexpr (!kDevelopmentDiagnostics) {
				return;
			} else {
				const auto elapsed = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(
						DiagnosticNow() - a_started)
						.count());
				Increment(a_samples);
				Add(a_totalMicroseconds, elapsed);
				a_lastMicroseconds = elapsed;
				a_maximumMicroseconds = std::max(a_maximumMicroseconds, elapsed);
			}
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

		using Util::SameIdentity;

		std::uintptr_t GetIdentityToken(IUnknown* a_object) noexcept
		{
			return reinterpret_cast<std::uintptr_t>(Util::GetComIdentity(a_object).Get());
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
				left.tuning.networkPreset == right.tuning.networkPreset &&
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
				left.modelResolutionPercent == right.modelResolutionPercent &&
				left.reconstructionFilter == right.reconstructionFilter &&
				left.pressureResolutionEnabled == right.pressureResolutionEnabled &&
				left.parallelEyes == right.parallelEyes &&
				left.renderingMode == right.renderingMode &&
				left.providerBlending == right.providerBlending &&
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
			return a_tuning.networkPreset <= kNetworkNewer &&
			       validStrength(a_tuning.intensity) &&
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

		using RendererComputeStateGuard = ComputeStateGuard<1, 1, ComputeStatePolicy::PreserveBindings>;

		struct RecordingGuard
		{
			explicit RecordingGuard(D3D12Interop& a_interop) : interop(a_interop) {}
			bool Abort()
			{
				const bool aborted = interop.AbortD3D12();
				active = interop.IsRecording();
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
			std::uint32_t networkPreset = kNetworkDefault;
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
			bool providerBlending = false;
			bool featureUpscaling = false;
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
			std::optional<ModelResolutionHistory> modelResolutionHistory;
			DXGI_FORMAT colorSourceFormat = DXGI_FORMAT_UNKNOWN;
			DXGI_FORMAT colorDestinationFormat = DXGI_FORMAT_UNKNOWN;
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
		CapacityFallback capacityFallback_{};
		std::uint32_t requestedRegionCount_ = 0;
		std::shared_ptr<ExecutionEvidence> modelPreparationEvidence_;
		bool forceFullCoordinates_ = false;
		std::array<CompactInputRetention, Runtime::kFeatureSlotCount> compactRetention_{};

#	ifdef DEVBENCH_BRIDGE_ENABLED
		std::array<PackedCropAdmissionDiagnostics, Runtime::kFeatureSlotCount> packedCropAdmission_{};
		PackedCropAdmissionDiagnostics* BeginPackedCropAdmissionLocked(const RendererApplyArgs&) noexcept;
#	endif
		ComPtr<ID3D11ComputeShader> copyCompactDepthGuideCS_;
		struct ValidatedResources;
		void ApplyCompactLayoutLocked(const RendererApplyArgs& args, ValidatedResources& resources) const;
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
			ResourceKey resourceKey{};
			HistoryKey historyKey{};
			std::uint32_t lastSuccessfulFrame =
				std::numeric_limits<std::uint32_t>::max();
			std::uint32_t lastSuccessfulSourceWorldFrame =
				std::numeric_limits<std::uint32_t>::max();
			std::uint64_t lastUsedMs = 0;
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
			RendererApplyOutcome& a_outcome, bool a_memoryAdmitted = false);
		bool ApplyModelResolutionLocked(std::span<const RendererApplyArgs>, RendererApplyOutcome&, bool sequential = false);
		bool ApplyBatchLocked(
			std::span<const RendererApplyArgs> a_args,
			RendererApplyOutcome& a_outcome, bool a_memoryAdmitted = false);
		bool ApplyRegionBatchLocked(std::span<const RendererApplyArgs> a_args, RendererApplyOutcome& a_outcome, bool a_memoryAdmitted = false);
		bool ResetLocked(bool a_resetShader, bool a_destruction,
			BackendRetirementPolicy a_policy = BackendRetirementPolicy::ReleaseBackend);
		void ApplyShaderInvalidationLocked();
		void ServicePrewarmLocked(const PrewarmKey*, const char*, std::uint64_t);
		void PreparePrewarmFeatureLocked();
		PrewarmPolicy prewarm_{};
		Util::GpuMemoryBudget::Reservation prewarmReservation_;
		std::atomic<bool> prewarmPending_{ false };

		ShaderInvalidation shaderInvalidation_;
		bool ServiceRetainedBackendLocked(bool a_requested = true);
		bool ServiceStallRecoveryLocked(bool a_requested);
		MemoryRetirementPolicy memoryRetirement_{};
		std::atomic_bool backendMaintenancePending_{ false };
		std::uint64_t nextRetainedBackendCheckMs_ = 0;
		bool RetireMemoryPressureLocked();
		void FinishMemoryRecoveryLocked(bool a_succeeded, RendererApplyOutcome& a_outcome);
		std::optional<std::uint64_t> EstimateAdditionalMemoryLocked(std::span<const RendererApplyArgs>, std::span<const ValidatedResources>) const;
		MemoryBudgetSample SampleMemoryBudgetLocked(ID3D11Device*, std::uint64_t a_nowMs, bool a_force);
		bool AdmitMemoryLocked(std::span<const RendererApplyArgs> a_args, std::span<const ValidatedResources> a_resources, std::uint64_t a_additionalBytes = 0);
		bool RetireSlotLocked(std::uint32_t a_slot, const std::shared_ptr<ExecutionEvidence>& a_evidence = {});
		bool ReclaimMemoryLocked(std::span<const RendererApplyArgs>, std::span<const ValidatedResources>, std::uint64_t a_nowMs, bool& a_reclaimed);
		MemoryConservationPolicy memoryConservation_{};
		MemoryRecoveryPolicy memoryRecovery_{};
		MemoryResolutionPolicy memoryResolution_{};
		static_assert(std::atomic_uint32_t::is_always_lock_free);
		std::atomic_uint32_t modelResolutionCeiling_{ kMaximumModelResolutionPercent };
		void PublishModelResolutionCeilingLocked() noexcept
		{
			modelResolutionCeiling_.store(memoryResolution_.enabled ? memoryResolution_.ceilingPercent : kMaximumModelResolutionPercent, std::memory_order_relaxed);
		}
		std::array<std::optional<ResourceKey>, Runtime::kFeatureSlotCount> memoryCapacityKeys_{};
		struct ResolutionLatch
		{
			std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
			std::uint32_t sourceFrame = std::numeric_limits<std::uint32_t>::max();
			std::uint32_t percent = 100;
		};
		std::array<ResolutionLatch, Runtime::kFeatureSlotCount / kEyeCount> resolutionLatches_{};
		std::array<std::uint32_t, Runtime::kFeatureSlotCount> successfulResolutionPercents_{};
		std::uint32_t restorationPreviousPercent_ = 0;
		struct ResolutionRestoration
		{
			bool active = false, admitted = false, rejected = false;
			std::uint32_t workingPercent = 0;
		} resolutionRestoration_{};
		bool RetryWorkingResolutionLocked(std::span<RendererApplyArgs> args);
		void FinishMemoryResolutionLocked(std::span<const RendererApplyArgs> args, bool succeeded);
		void PrepareMemoryResolutionLocked(std::span<RendererApplyArgs> args);
		void ResolutionChangedLocked();
		MemoryBudgetSample memorySample_{};
		Util::GpuMemoryBudget::Reservation memoryReservation_;
		ComPtr<ID3D11Device> memorySampleDevice_;
		std::uint64_t nextMemorySampleMs_ = 0;
		void PublishMemoryBudgetWorkLocked(bool requested = true)
		{
			auto& budget = Util::GpuMemoryBudget::Get();
			if (!requested || failureLatched_ || quarantined_) {
				budget.SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0);
				return;
			}
			const auto phase = memoryRecovery_.phase;
			budget.SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering,
				phase == MemoryRecoveryPhase::Retiring || phase == MemoryRecoveryPhase::Rebuilding,
				phase == MemoryRecoveryPhase::Waiting   ? memoryRecovery_.admissionDemandBytes :
				phase == MemoryRecoveryPhase::Probation ? MemoryRecoveryPolicy::kMinimumHeadroom :
														  0);
		}
		void FinishMemoryAdmissionLocked()
		{
			if (memoryReservation_)
				Util::GpuMemoryBudget::Get().Sample(memorySampleDevice_.Get(), 0, true);
			memoryReservation_.Reset();
			PublishMemoryBudgetWorkLocked();
		}

#ifdef DEVBENCH_BRIDGE_ENABLED
		std::uint64_t simulatedPressureUntilMs_ = 0;
		bool simulateConservationOnly_ = false;
#endif
		void ShutdownForDestruction() noexcept;

		RendererSnapshot SnapshotLocked()
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			snapshot_.prewarm = prewarm_;
			snapshot_.nativeLifecycle = Runtime::Instance().GetLifecycleCounts();
			snapshot_.prewarmSubmission = interop_.GetLifetimeSnapshot();
			snapshot_.prewarmReservedBytes = prewarmReservation_.Bytes();
			snapshot_.shaderGenerationRequested = shaderInvalidation_.Requested();
			snapshot_.shaderGenerationApplied = shaderInvalidation_.Applied();
#endif
			RefreshInteropTelemetryLocked();
			snapshot_.memoryConservation = memoryConservation_;
			snapshot_.memoryRetirement = memoryRetirement_;
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
#ifdef DEVBENCH_BRIDGE_ENABLED
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
#endif
		}
		Color::Configuration colorConfiguration_{};
		std::array<CaptureInputs, 2> captureInputs_{};
		std::size_t captureRoute_ = 0;
		Util::PassTimingHandle wholePass_;
		void FinishCapture(const RendererApplyOutcome& outcome) noexcept
		{
			if constexpr (!kDevelopmentDiagnostics) {
				return;
			} else {
				try {
					for (auto& capture : std::span<CaptureInputs>(&captureInputs_[captureRoute_], 1)) {
						if (!capture.valid || capture.configuration.revision != colorConfiguration_.revision)
							continue;
						capture.attemptedMask |= outcome.evaluationAttemptedFeatureSlotMask;
						capture.succeededMask |= outcome.evaluationSucceededFeatureSlotMask;
						for (std::size_t slot = 0; slot < slots_.size(); ++slot) {
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

	private:
#ifdef DEVBENCH_BRIDGE_ENABLED
		void MergeModelPreparationEvidenceLocked(ExecutionEvidence& execution) const noexcept;
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
		bool EnsureBackendLocked(const RendererApplyArgs& a_args, const std::shared_ptr<ExecutionEvidence>& a_evidence);
		bool EnsureSlotLocked(
			std::uint32_t a_slot,
			const ValidatedResources& a_resources,
			const std::shared_ptr<ExecutionEvidence>& a_evidence, std::size_t a_region);
		bool CopyDepthBatchLocked(
			std::span<const RendererApplyArgs> a_args,
			std::span<Slot* const> a_slots,
			std::span<const ValidatedResources> a_resources,
			const std::shared_ptr<ExecutionEvidence>& a_evidence);
		bool TeardownBackendLocked(
			bool a_resetShader,
			bool a_destruction,
			bool a_countApplyFailure, const std::shared_ptr<ExecutionEvidence>& a_evidence = {},
			BackendRetirementPolicy a_policy = BackendRetirementPolicy::ReleaseBackend);
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
		/** Observes a terminal failure before ownership detaches, without GPU commands or waits. */
		void LogFailureDiagnosticsLocked(RendererStage a_stage, HRESULT a_result, std::uint32_t a_slot) noexcept;
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
		ModelResolution modelResolution_;
		std::array<Slot, Runtime::kFeatureSlotCount> slots_{};
		ComPtr<ID3D11Device> device_;
		ComPtr<ID3D11DeviceContext> context_;
		ComPtr<ID3D11ComputeShader> copyDepthGuideCS_;
		ComPtr<ID3D11Buffer> copyDepthGuideCB_;
		bool copyDepthGuideCompileFailed_ = false;
		ComPtr<ID3D11ComputeShader> actorProtectionCS_;
		ComPtr<ID3D11Buffer> actorProtectionCB_;
		bool actorProtectionCompileFailed_ = false;
		bool PrepareActorProtection(const RendererApplyArgs&, Slot&);
		RendererSnapshot snapshot_{};
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::uint64_t diagnosticSourceTransaction_ = 0;
#endif
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
		if (!IsValidReconstructionFilter(a_args.reconstructionFilter))
			return fail("NR reconstruction filter is invalid");
		if (!IsValidModelResolutionPercent(a_args.modelResolutionPercent))
			return fail("NR model resolution must be an integer percentage from 30 to 100");
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
		if (a_args.providerBlending) {
			if (!hasControlMask || !a_args.actorSelection || !a_args.characterVisualIsolation ||
				!a_args.tuning.useAutoMask || a_resources.nativeLayout.color != a_resources.nativeLayout.output ||
				(a_args.actorSelectionSupport.IsValid() && !a_args.actorSelectionSupport.Fits(a_args.outputWidth, a_args.outputHeight)))
				return fail("actor provider blending requires matching colour/output, automatic masking and prepared actor selection");
			ComPtr<ID3D11Resource> selectionResource;
			a_args.actorSelection->GetResource(&selectionResource);
			D3D11_SHADER_RESOURCE_VIEW_DESC selectionView{};
			a_args.actorSelection->GetDesc(&selectionView);
			if (!SameIdentity(selectionResource.Get(), a_args.controlMask.Get()) ||
				selectionView.Format != DXGI_FORMAT_R8_UNORM || selectionView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				selectionView.Texture2D.MostDetailedMip != 0 || selectionView.Texture2D.MipLevels != 1)
				return fail("actor selection view does not match the provider mask source");
		} else if (hasControlMask == a_args.tuning.useAutoMask) {
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
			observation.sourceFormat = static_cast<std::uint32_t>(a_resources.color.desc.Format);
			observation.outputFormat = static_cast<std::uint32_t>(a_resources.output.desc.Format);
			capture.slotMask |= 1u << a_args.featureSlot;
		}
		return {};
	}

	void Renderer::State::FinalizeResourceKeysLocked(const RendererApplyArgs& a_args, ValidatedResources& a_resources) const
	{
		const bool hasControlMask = a_args.controlMask != nullptr;
		const auto profile = Color::EffectiveProfile(colorConfiguration_, static_cast<std::uint32_t>(a_args.insertionPoint));
		const bool transformed = profile.transform != Color::Transform::Identity;
		// Transform in private floating-point storage; preserve the caller's format at commit.
		const auto processingFormat = a_resources.color.desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
		                                      a_resources.output.desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ?
		                                  DXGI_FORMAT_R32G32B32A32_FLOAT :
		                                  DXGI_FORMAT_R16G16B16A16_FLOAT;
		a_resources.resourceKey = {
			.networkPreset = a_args.tuning.networkPreset,
			.colorWidth = a_resources.nativeLayout.color.backing.width,
			.colorHeight = a_resources.nativeLayout.color.backing.height,
			.guideWidth = a_resources.nativeLayout.depth.backing.width,
			.guideHeight = a_resources.nativeLayout.depth.backing.height,
			.outputWidth = a_resources.nativeLayout.output.backing.width,
			.outputHeight = a_resources.nativeLayout.output.backing.height,
			.controlMaskWidth = a_resources.nativeLayout.controlMask.backing.width,
			.controlMaskHeight = a_resources.nativeLayout.controlMask.backing.height,
			.colorFormat = transformed ? processingFormat : a_resources.color.desc.Format,
			.motionFormat = a_resources.motionVectors.desc.Format,
			.outputFormat = transformed ? processingFormat : a_resources.output.desc.Format,
			.controlMaskFormat = hasControlMask ?
			                         a_resources.controlMask.desc.Format :
			                         DXGI_FORMAT_UNKNOWN,
			.controlMaskPresent = hasControlMask,
			.providerBlending = a_args.providerBlending,
			.featureUpscaling = a_args.featureUpscaling,
		};
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_resources.roi.compactSource) {
			a_resources.resourceKey.compact = true;
		}
#endif
		a_resources.historyKey = {
			.resources = a_resources.resourceKey,
			.generation = a_args.generation,
			.insertionPoint = a_args.insertionPoint,
			.viewportCrop = a_args.viewportCrop,
			.modelResolutionHistory = a_args.modelResolutionHistory,
			.colorSourceFormat = a_resources.color.desc.Format,
			.colorDestinationFormat = a_resources.output.desc.Format,
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
		if constexpr (!kDevelopmentDiagnostics) {
			if (failureLatched_ || quarantined_) {
				snapshot_.ngxResult = Runtime::Instance().NgxResult();
				if (snapshot_.detail.empty())
					snapshot_.detail = Runtime::Instance().Detail();
			}
			return;
		} else {
			auto& runtime = Runtime::Instance();
			snapshot_.status = ToString(runtime.Status());
#ifdef DEVBENCH_BRIDGE_ENABLED
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
#endif
			if (snapshot_.detail.empty())
				snapshot_.detail = runtime.Detail();
			snapshot_.successes = snapshot_.counters.successes;
			snapshot_.failures = snapshot_.counters.failures;
		}
	}

	void Renderer::State::RefreshInteropTelemetryLocked()
	{
		if constexpr (!kDevelopmentDiagnostics) {
			return;
		} else {
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
			performance.parallelCommandSubmissions = telemetry.parallelCommandSubmissions;
			performance.lastFeatureParallel = telemetry.lastFeatureParallel;
			performance.lastEvaluationGpuMicroseconds = telemetry.lastEvaluationGpuMicroseconds;
			performance.lastExecutionPlan = telemetry.lastExecutionPlan;
#endif
			performance.lastInsertionPoint = telemetry.lastInsertionPoint;
		}
	}

	void Renderer::State::SetRequestTelemetryLocked(
		const RendererApplyArgs& a_args) noexcept
	{
		if constexpr (!kDevelopmentDiagnostics) {
			return;
		} else {
			snapshot_.featureSlot = a_args.featureSlot;
			snapshot_.frameId = a_args.frameId;
			snapshot_.sourceWorldFrame = a_args.sourceWorldFrame;
			snapshot_.generation = a_args.generation;
			snapshot_.insertionPoint = a_args.insertionPoint;
			snapshot_.modelResolutionPercent = a_args.modelResolutionPercent;
			snapshot_.reconstructionFilter = a_args.reconstructionFilter;
			snapshot_.modelSharedInputs = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			snapshot_.modelOutputSelected = false;
			diagnosticSourceTransaction_ = a_args.executionContext.sourceTransactionId;
#endif
#ifdef DEVBENCH_BRIDGE_ENABLED
			snapshot_.packedCropLayout = a_args.modelResolutionHistory ? a_args.modelResolutionHistory->packedCrops : PackedCropLayout{};
#endif
			snapshot_.modelSourceWidth = snapshot_.modelWidth = a_args.colorWidth;
			snapshot_.modelSourceHeight = snapshot_.modelHeight = a_args.colorHeight;
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
			snapshot_.providerBlending = a_args.providerBlending;
			snapshot_.useAutoMask = a_args.tuning.useAutoMask;
			snapshot_.outputCommitted = false;
		}
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

	void Renderer::State::LogFailureDiagnosticsLocked(
		RendererStage a_stage, HRESULT a_result, std::uint32_t a_slot) noexcept
	{
		if (quarantined_)
			return;
		try {
			if (!spdlog::should_log(spdlog::level::err))
				return;
			const auto fences = interop_.GetLifetimeSnapshot();
			auto* device12 = interop_.Device();
			const HRESULT reason11 = device_ ? device_->GetDeviceRemovedReason() : E_POINTER;
			const HRESULT reason12 = device12 ? device12->GetDeviceRemovedReason() : E_POINTER;
			Util::GpuMemoryBudget::Snapshot memory;
			bool memoryQueryThrew = false;
			if (device_) {
				try {
					memory = Util::GpuMemoryBudget::Get().Sample(device_.Get(), 0, true);
				} catch (...) {
					memory.result = E_FAIL;
					memoryQueryThrew = true;
				}
			} else {
				memory.result = E_POINTER;
			}
			const auto observedAtMs = GetTickCount64();
			logger::error(
				"[DLSSNR][Failure] stage={}, hr=0x{:08X}, slot={}, requested_model_percent={}, effective_model_percent={}",
				ToString(a_stage), static_cast<std::uint32_t>(a_result), a_slot,
				snapshot_.requestedModelResolutionPercent, snapshot_.effectiveModelResolutionPercent);
			logger::error(
				"[DLSSNR][Failure] fences: initialized={}, recording={}, issued={}, completed={}, completed_known={}, device_removed={}, contexts=[{},{},{}]",
				fences.initialized, fences.recording, fences.issued, fences.completed,
				fences.completedKnown, fences.deviceRemoved, fences.contexts[0], fences.contexts[1], fences.contexts[2]);
			if (fences.parallelQueue)
				logger::error("[DLSSNR][Failure] parallel fences: issued={}, completed={}, session_disabled={}",
					fences.parallelIssued, fences.parallelCompleted, interop_.IsParallelDisabled());
			logger::error(
				"[DLSSNR][Failure] devices: d3d11_present={}, d3d11_removal_hr=0x{:08X}, d3d12_present={}, d3d12_removal_hr=0x{:08X}",
				static_cast<bool>(device_), static_cast<std::uint32_t>(reason11),
				device12 != nullptr, static_cast<std::uint32_t>(reason12));
			logger::error(
				"[DLSSNR][Failure] memory: query_attempted={}, query_threw={}, hr=0x{:08X}, fresh={}, observed_at_ms={}, sampled_at_ms={}, sequence={}, generation={}, usage_bytes={}, budget_bytes={}, reservation_bytes={}, available_reservation_bytes={}, pending_bytes={}, recovery_demand_bytes={}, priority_work={}",
				static_cast<bool>(device_), memoryQueryThrew, static_cast<std::uint32_t>(memory.result), memory.Fresh(observedAtMs),
				observedAtMs, memory.sampledAtMs, memory.sequence, memory.generation,
				memory.local.CurrentUsage, memory.local.Budget, memory.local.CurrentReservation,
				memory.local.AvailableForReservation, memory.pendingBytes, memory.recoveryDemandBytes, memory.priorityWork);
			for (std::size_t index = 0; index < slots_.size(); ++index) {
				const auto& slot = slots_[index];
				if (!slot.resourcesValid)
					continue;
				const auto& key = slot.resourceKey;
				logger::error(
					"[DLSSNR][Failure] resident_slot={}: color={}x{}, guide={}x{}, output={}x{}, mask={}x{}, upscaling={}, history_valid={}, last_success_frame={}, last_success_source_frame={}",
					index, key.colorWidth, key.colorHeight, key.guideWidth, key.guideHeight,
					key.outputWidth, key.outputHeight, key.controlMaskWidth, key.controlMaskHeight,
					key.featureUpscaling, slot.historyValid, slot.lastSuccessfulFrame, slot.lastSuccessfulSourceWorldFrame);
			}
		} catch (...) {
			try {
				logger::error("[DLSSNR][Failure] Diagnostic snapshot failed; renderer failure handling continues");
			} catch (...) {
				OutputDebugStringA("[DLSSNR][Failure] Diagnostic snapshot and logging failed; renderer failure handling continues\n");
			}
		}
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
		const HRESULT originalResult = a_result;
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
			LogFailureDiagnosticsLocked(a_stage, originalResult, a_slot);
			snapshot_.stallRecovery.Cancel();
			const bool enteringQuarantine = !quarantined_;
			if (enteringQuarantine)
				Increment(snapshot_.counters.quarantines);
			quarantined_ = true;
			failureLatched_ = true;
			if (enteringQuarantine)
				AbandonRuntimeOwnershipNoexcept();
		} else if (a_countApplyFailure &&
				   ((a_stage == RendererStage::ResourceCreation && a_result == E_OUTOFMEMORY) ||
					   (a_stage == RendererStage::FeatureEvaluate && Runtime::Instance().CreationCapacityFailure() == CapacityFailure::Pressure))) {
			memoryRecovery_.Suspend(GetTickCount64(), true);
			snapshot_.memoryRecovery = memoryRecovery_;
			logger::warn("[DLSSNR][Memory] Recoverable allocation failure; retiring NR before automatic retry");
		} else if (a_latch) {
			if (!failureLatched_)
				LogFailureDiagnosticsLocked(a_stage, originalResult, a_slot);
			if (!failureLatched_ && a_stage == RendererStage::CommandBegin &&
				a_result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) && interop_.LastOperation() == "BeginD3D12 backpressure") {
				if (snapshot_.stallRecovery.Arm(GetTickCount64()))
					logger::warn("[DLSSNR][Recovery] Transient queue stall; waiting for completion and {} ms cooldown before attempt {}/{}",
						snapshot_.stallRecovery.CooldownMs(), snapshot_.stallRecovery.attempts + 1, StallRecoveryPolicy::kMaximumAttempts);
				else
					logger::error("[DLSSNR][Recovery] Automatic stall recovery attempts exhausted; NR remains paused");
			}
			failureLatched_ = true;
		}
		if (failureLatched_ || quarantined_) {
			memoryRetirement_.Released();
			backendMaintenancePending_.store(snapshot_.stallRecovery.pending, std::memory_order_relaxed);
			Util::GpuMemoryBudget::Get().SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0);
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
		bool a_countApplyFailure, const std::shared_ptr<ExecutionEvidence>& a_evidence,
		BackendRetirementPolicy a_policy)
	{
		backendMaintenancePending_.store(false, std::memory_order_relaxed);
		const bool retainBackend = a_policy == BackendRetirementPolicy::RetainHealthyBackend &&
		                           !a_resetShader && !a_destruction && !failureLatched_ && !quarantined_ &&
		                           runtimeReady_ && interop_.IsInitialized() && Runtime::Instance().Status() == RuntimeStatus::Initialized &&
		                           device_ && SUCCEEDED(device_->GetDeviceRemovedReason()) && SUCCEEDED(GetDeviceRemovalReasonLocked(S_OK));
		// No admitted work has touched an already-empty backend since its completed drain.
		const bool alreadyRetained = retainBackend && memoryRetirement_.backendRetained;
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

		if (!alreadyRetained && interop_.IsRecording() && !interop_.AbortD3D12()) {
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
		if (!alreadyRetained && interop_.IsInitialized() && !interop_.WaitForIdle(a_evidence)) {
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
		if (!alreadyRetained && runtimeReady_ && !(retainBackend ? runtime.ReleasePassResources() : runtime.ResetFeatures())) {
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
		if (!retainBackend && runtimeTouched_ && !runtime.Shutdown()) {
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
		if (!retainBackend && interop_.IsInitialized() && !interop_.Shutdown(a_evidence)) {
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
		prewarmReservation_.Reset();
		prewarm_ = {};
		prewarmPending_.store(false, std::memory_order_relaxed);
		colorPipeline_.Reset();
		modelResolution_.Reset();
		if (!retainBackend) {
			device_.Reset();
			context_.Reset();
		}
		if (a_resetShader) {
			copyDepthGuideCS_.Reset();
			actorProtectionCS_.Reset();
			actorProtectionCompileFailed_ = false;
			actorProtectionCB_.Reset();
#ifdef DEVBENCH_BRIDGE_ENABLED
			copyCompactDepthGuideCS_.Reset();
#endif
			copyDepthGuideCB_.Reset();
			copyDepthGuideCompileFailed_ = false;
		}
		runtimeReady_ = retainBackend;
		runtimeTouched_ = retainBackend;
		memoryRecovery_.SetStartupKind(retainBackend ? MemoryStartupKind::Warm : MemoryStartupKind::Cold);
		if (retainBackend) {
			const bool newlyRetained = !memoryRetirement_.backendRetained;
			memoryRetirement_.Retained(GetTickCount64());
			if (newlyRetained)
				logger::info("[DLSSNR][Memory] Released NR textures and model features; retaining healthy initialized backend");
			nextRetainedBackendCheckMs_ = 0;
			backendMaintenancePending_.store(true, std::memory_order_relaxed);
		} else {
			memoryRetirement_.Released();
		}
		snapshot_.stallRecovery.Cancel();
		failureLatched_ = false;
		quarantined_ = false;
		Increment(snapshot_.counters.resetSuccesses);
		snapshot_.lastCompletedStage = retainBackend ? RendererStage::ResourceRetirement : RendererStage::InteropShutdown;
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
		LogFailureDiagnosticsLocked(a_stage, E_FAIL, a_slot);
		snapshot_.stallRecovery.Cancel();
		memoryRetirement_.Released();
		backendMaintenancePending_.store(false, std::memory_order_relaxed);
		Util::GpuMemoryBudget::Get().SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0);
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

	bool Renderer::State::EnsureBackendLocked(const RendererApplyArgs& a_args, const std::shared_ptr<ExecutionEvidence>& a_evidence)
	{
		const bool backendIdentityChanged = device_ &&
		                                    (!SameIdentity(device_.Get(), a_args.device) ||
												!SameIdentity(context_.Get(), a_args.context));
		if (backendIdentityChanged) {
			ExecutionCpuTimer retirementTimer(a_evidence, &ExecutionSnapshot::resourceRetirementCpuMicroseconds);
			if (!TeardownBackendLocked(false, false, true, a_evidence))
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
		Increment(snapshot_.counters.runtimeInitializations);
		snapshot_.lastCompletedStage = RendererStage::RuntimeInitialization;
		RefreshRuntimeTelemetryLocked();
		return true;
	}

	void Renderer::State::ResolutionChangedLocked()
	{
		PublishModelResolutionCeilingLocked();
		memoryRecovery_.InvalidateCapacityLearning();
		restorationPreviousPercent_ = 0;
		logger::info("[DLSSNR][Memory] Pressure resolution ceiling {}%; requested {}% (session only)",
			memoryResolution_.ceilingPercent, snapshot_.requestedModelResolutionPercent);
	}

	void Renderer::State::PrepareMemoryResolutionLocked(std::span<RendererApplyArgs> args)
	{
		const SKSE::stl::scope_exit planCrops([&] { modelResolution_.PlanPackedCrops(args, resolutionRestoration_.active); });
		resolutionRestoration_ = {};
		if (args.empty())
			return;
		const auto& first = args.front();
		const auto requested = first.modelResolutionPercent;
		if (!IsValidModelResolutionPercent(requested) || std::ranges::any_of(args, [&](const auto& arg) {
				return arg.featureSlot >= Runtime::kFeatureSlotCount || arg.modelResolutionPercent != requested ||
			           arg.pressureResolutionEnabled != first.pressureResolutionEnabled;
			}))
			return;
		if (snapshot_.requestedModelResolutionPercent != requested || snapshot_.pressureResolutionEnabled != first.pressureResolutionEnabled) {
			resolutionLatches_ = {};
			successfulResolutionPercents_ = {};
			restorationPreviousPercent_ = 0;
			memoryResolution_.ClearHealthyWindow();
		}
		const bool enabling = first.pressureResolutionEnabled && !memoryResolution_.enabled;
		if (memoryResolution_.enabled != first.pressureResolutionEnabled) {
			memoryResolution_.SetEnabled(first.pressureResolutionEnabled);
			PublishModelResolutionCeilingLocked();
		}
		snapshot_.requestedModelResolutionPercent = requested;
		snapshot_.pressureResolutionEnabled = first.pressureResolutionEnabled;
		if (enabling && memoryRecovery_.phase == MemoryRecoveryPhase::Waiting && memoryResolution_.OnRetired(requested, GetTickCount64()))
			ResolutionChangedLocked();
		const auto route = first.featureSlot / kEyeCount;
		if (route >= resolutionLatches_.size())
			return;
		auto& latch = resolutionLatches_[route];
		if (latch.frame != first.frameId || latch.sourceFrame != first.sourceWorldFrame) {
			latch = { first.frameId, first.sourceWorldFrame, memoryResolution_.EffectivePercent(requested) };
		}
		auto workingPercent = kMaximumModelResolutionPercent;
		for (const auto& arg : args) {
			const auto previous = successfulResolutionPercents_[arg.featureSlot] ? successfulResolutionPercents_[arg.featureSlot] : restorationPreviousPercent_;
			if (IsValidModelResolutionPercent(previous))
				workingPercent = std::min(workingPercent, previous);
		}
		if (memoryResolution_.enabled && latch.percent > workingPercent &&
			(!device_ || (SameIdentity(device_.Get(), first.device) && SameIdentity(context_.Get(), first.context))))
			resolutionRestoration_ = { true, false, false, workingPercent };
		snapshot_.effectiveModelResolutionPercent = latch.percent;
		for (auto& arg : args) {
			if (arg.modelResolutionPercent == requested && arg.pressureResolutionEnabled == first.pressureResolutionEnabled)
				arg.modelResolutionPercent = latch.percent;
		}
	}

	bool Renderer::State::RetryWorkingResolutionLocked(std::span<RendererApplyArgs> args)
	{
		if (!resolutionRestoration_.rejected || args.empty())
			return false;
		const auto workingPercent = resolutionRestoration_.workingPercent;
		if (memoryResolution_.RejectRestoration(workingPercent))
			PublishModelResolutionCeilingLocked();
		restorationPreviousPercent_ = 0;
		const auto route = args.front().featureSlot / kEyeCount;
		resolutionLatches_[route].percent = workingPercent;
		for (auto& arg : args)
			arg.modelResolutionPercent = workingPercent;
		modelResolution_.PlanPackedCrops(args);
		snapshot_.effectiveModelResolutionPercent = workingPercent;
		resolutionRestoration_ = {};
		logger::info("[DLSSNR][Memory] Retaining working NR at {}%; restoration replacement lacks headroom", workingPercent);
		return true;
	}

	void Renderer::State::FinishMemoryResolutionLocked(std::span<const RendererApplyArgs> args, bool succeeded)
	{
		if (succeeded && !args.empty()) {
			for (const auto& arg : args)
				successfulResolutionPercents_[arg.featureSlot] = arg.modelResolutionPercent;
			if (resolutionRestoration_.active)
				modelResolution_.ReleaseUnscaledSlots(args);
		}
		resolutionRestoration_ = {};
	}

	bool Renderer::State::RetireMemoryPressureLocked()
	{
		if (memoryRecovery_.phase != MemoryRecoveryPhase::Retiring)
			return true;
		if (failureLatched_ || quarantined_ || !TeardownBackendLocked(false, false, false, {}, BackendRetirementPolicy::RetainHealthyBackend)) {
			snapshot_.memoryRecovery = memoryRecovery_;
			return false;
		}
		const auto now = GetTickCount64();
		successfulResolutionPercents_ = {};
		memoryRecovery_.Retired(now);
		if (memoryResolution_.OnRetired(snapshot_.requestedModelResolutionPercent, now))
			ResolutionChangedLocked();
		snapshot_.memoryRecovery = memoryRecovery_;
		nextMemorySampleMs_ = 0;
		return true;
	}

	bool Renderer::State::ServiceStallRecoveryLocked(bool a_requested)
	{
		auto& recovery = snapshot_.stallRecovery;
		if (!recovery.pending)
			return false;
		if (!a_requested || quarantined_ || !failureLatched_) {
			recovery.Cancel();
			backendMaintenancePending_.store(memoryRetirement_.backendRetained, std::memory_order_relaxed);
			return false;
		}
		const auto now = GetTickCount64();
		if (!recovery.ShouldPoll(now))
			return false;
		const HRESULT removed = GetDeviceRemovalReasonLocked(S_OK);
		if (FAILED(removed))
			return FailLocked(RendererStage::DeviceRemoved, removed,
				"device removal detected while waiting for transient stall recovery", Runtime::kFeatureSlotCount, true, true, false);
		const auto decision = recovery.Observe(now, interop_.GetLifetimeSnapshot());
		if (decision == StallRecoveryDecision::Waiting)
			return false;
		if (decision != StallRecoveryDecision::Recover)
			return FailLocked(RendererStage::ResetWait,
				decision == StallRecoveryDecision::Expired ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : E_UNEXPECTED,
				"outstanding GPU completion could not be established; automatic recovery is unsafe",
				Runtime::kFeatureSlotCount, true, true, false);
		recovery.BeginAttempt();
		backendMaintenancePending_.store(false, std::memory_order_relaxed);
		activeStage_ = RendererStage::ResetWait;
		if (!ResetLocked(false, false))
			return false;
		logger::info("[DLSSNR][Recovery] Queue work completed and safe teardown succeeded; automatic retry {}/{} may rebuild NR",
			recovery.attempts, StallRecoveryPolicy::kMaximumAttempts);
		return true;
	}

	bool Renderer::State::ServiceRetainedBackendLocked(bool a_requested)
	{
		if (!memoryRetirement_.backendRetained)
			return true;
		if (failureLatched_ || quarantined_) {
			memoryRetirement_.Released();
			backendMaintenancePending_.store(false, std::memory_order_relaxed);
			return false;
		}
		auto now = GetTickCount64();
		if (now < nextRetainedBackendCheckMs_)
			return true;
		nextRetainedBackendCheckMs_ = now + MemoryRecoveryPolicy::kSampleIntervalMs;
		const auto sample = SampleMemoryBudgetLocked(device_.Get(), now, false);
		now = GetTickCount64();
		auto preview = memoryRecovery_;
		const auto pending = Util::GpuMemoryBudget::Get().PendingBytes(Util::GpuMemoryBudget::Owner::NeuralRendering);
		const bool recovering = a_requested && memoryRecovery_.phase == MemoryRecoveryPhase::Waiting;
		if (!recovering)
			preview.phase = MemoryRecoveryPhase::Ready;
		(void)preview.Admit(sample, recovering ? memoryRecovery_.requiredBytes : 0, now, pending);
		const bool blocked = preview.phase == MemoryRecoveryPhase::Retiring ||
		                     (preview.phase == MemoryRecoveryPhase::Waiting && !preview.healthyWindow);
		const auto reason = memoryRetirement_.Observe(sample, now, blocked, recovering);
		if (reason == MemoryRetirementReason::None)
			return true;
		if (!TeardownBackendLocked(false, false, false))
			return false;
		memoryRetirement_.Released(reason);
		memoryRecovery_.ResourcesRetired(GetTickCount64());
		if (a_requested)
			FinishMemoryAdmissionLocked();
		else
			Util::GpuMemoryBudget::Get().SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0);
		snapshot_.memoryRecovery = memoryRecovery_;
		nextMemorySampleMs_ = 0;
		logger::info("[DLSSNR][Memory] Released retained backend: {}; observed application usage {:.1f}/{:.1f} MiB before full teardown",
			ToString(reason), static_cast<double>(sample.usageBytes) / MemoryRecoveryPolicy::kMiB,
			static_cast<double>(sample.budgetBytes) / MemoryRecoveryPolicy::kMiB);
		return true;
	}

	void Renderer::State::FinishMemoryRecoveryLocked(bool a_succeeded, RendererApplyOutcome& a_outcome)
	{
		if (a_succeeded) {
			const auto previous = memoryRecovery_.phase;
			const auto now = GetTickCount64();
			memoryRecovery_.Succeeded(now);
			if (previous != memoryRecovery_.phase)
				logger::info("[DLSSNR][Memory] Recovery phase {} -> {}; demand {:.1f} MiB, rapid relapses={}",
					ToString(previous), ToString(memoryRecovery_.phase),
					static_cast<double>(memoryRecovery_.admissionDemandBytes) / MemoryRecoveryPolicy::kMiB, memoryRecovery_.rapidRelapses);
			const auto previousCeiling = memoryResolution_.ceilingPercent;
			if (memoryResolution_.enabled && memoryResolution_.ObserveSuccess(snapshot_.requestedModelResolutionPercent, memoryRecovery_.sample, now,
												 Util::GpuMemoryBudget::Get().PendingBytes(Util::GpuMemoryBudget::Owner::NeuralRendering),
												 memoryConservation_.active, memoryRecovery_.phase == MemoryRecoveryPhase::Ready)) {
				restorationPreviousPercent_ = previousCeiling;
				PublishModelResolutionCeilingLocked();
			}
		} else {
			if (memoryRecovery_.phase != MemoryRecoveryPhase::Waiting)
				memoryRecovery_.BreakActiveObservation();
			else if (memoryResolution_.WhileWaiting(snapshot_.requestedModelResolutionPercent, memoryRecovery_.sample,
						 GetTickCount64(), !memoryRecovery_.healthyWindow))
				ResolutionChangedLocked();
			memoryResolution_.ClearHealthyWindow();
			if (!RetireMemoryPressureLocked())
				a_outcome.memoryPressureBypass = false;
		}
		snapshot_.memoryRecovery = memoryRecovery_;
	}

	std::optional<std::uint64_t> Renderer::State::EstimateAdditionalMemoryLocked(
		std::span<const RendererApplyArgs> a_args, std::span<const ValidatedResources> a_resources) const
	{
		const bool replacingBackend = device_ && (!SameIdentity(device_.Get(), a_args.front().device) ||
													 !SameIdentity(context_.Get(), a_args.front().context));
		std::uint64_t additionalBytes = 0;
		const auto append = [&](DXGI_FORMAT format, UpscalingDLSS::Extent extent) {
			const auto bytes = LogicalTextureBytes(format, extent.width, extent.height);
			if (!bytes)
				return false;
			additionalBytes += *bytes;
			return true;
		};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			const auto& resource = a_resources[index];
			const auto& slot = slots_[a_args[index].featureSlot];
			const bool replacing = replacingBackend || !slot.resourcesValid || slot.resourceKey != resource.resourceKey;
			if (replacing) {
				const auto& layout = resource.nativeLayout;
				const bool preparedNative = !replacingBackend && (prewarm_.unusedMask & FeatureSlotBit(a_args[index].featureSlot)) &&
				                            Runtime::Instance().HasCompatibleFeature(a_args[index].featureSlot, layout, resource.resourceKey.providerBlending, resource.resourceKey.networkPreset);
				if (!preparedNative)
					additionalBytes += MemoryRecoveryPolicy::kNativeFeatureReserve;
				if (!append(resource.resourceKey.colorFormat, layout.color.backing) ||
					!append(DXGI_FORMAT_R32_FLOAT, layout.depth.backing) ||
					!append(resource.resourceKey.motionFormat, layout.motion.backing) ||
					!append(resource.resourceKey.outputFormat, layout.output.backing) ||
					(resource.resourceKey.controlMaskPresent && !append(resource.resourceKey.controlMaskFormat, layout.controlMask.backing)))
					return std::nullopt;
			}
			const auto& roi = resource.roi.inferenceContext;
			if (colorConfiguration_.Enabled() && (replacing || !slot.colorWork.Fits(roi, resource.output.desc.Format))) {
				const auto retained = replacing ? std::array<std::uint32_t, 2>{} :
				                                  std::array{ slot.colorWork.capacityWidth, slot.colorWork.capacityHeight };
				const auto [width, height] = Color::Work::AllocationExtent(roi, retained);
				if (!append(resource.output.desc.Format, { width, height }) || !append(resource.output.desc.Format, { width, height }))
					return std::nullopt;
			}
		}
		return additionalBytes;
	}

	MemoryBudgetSample Renderer::State::SampleMemoryBudgetLocked(ID3D11Device* a_device, std::uint64_t a_nowMs, bool a_force)
	{
		if (memorySampleDevice_.Get() != a_device) {
			memoryRecovery_.InvalidateCapacityLearning();
			memoryCapacityKeys_ = {};
			memoryResolution_.ClearHealthyWindow();
			memorySampleDevice_ = a_device;
			memorySample_ = {};
			nextMemorySampleMs_ = 0;
			memoryRecovery_.ClearHealthyWindow();
			memoryConservation_.ClearHealthyWindow();
		}
		if (a_nowMs >= nextMemorySampleMs_ || a_force) {
			const auto observation = Util::GpuMemoryBudget::Get().Sample(a_device, MemoryRecoveryPolicy::kSampleIntervalMs, a_force);
			memorySample_ = { observation.local.Budget, observation.local.CurrentUsage,
				observation.Fresh(GetTickCount64()), observation.result, false, observation.sampledAtMs };
			nextMemorySampleMs_ = a_nowMs + MemoryRecoveryPolicy::kSampleIntervalMs;
		}
		auto sample = memorySample_;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_nowMs < simulatedPressureUntilMs_) {
			const auto budget = sample.IsFresh(a_nowMs) ? sample.budgetBytes : 16 * 1024 * MemoryRecoveryPolicy::kMiB;
			const auto usage = simulateConservationOnly_ ? budget / 100 * 81 : budget;
			// Elevating pressure must never hide a genuine low-headroom sample.
			sample = { budget, std::max(sample.usageBytes, usage), true, S_OK, true, a_nowMs };
		}
#endif
		return sample;
	}

	bool Renderer::State::ReclaimMemoryLocked(std::span<const RendererApplyArgs> args,
		std::span<const ValidatedResources> resources, std::uint64_t nowMs, bool& reclaimed)
	{
		reclaimed = false;
		if (!memoryConservation_.active || nowMs < memoryConservation_.nextReclaimMs ||
			memoryRecovery_.phase != MemoryRecoveryPhase::Ready || !interop_.IsInitialized() ||
			!SameIdentity(device_.Get(), args.front().device) || !SameIdentity(context_.Get(), args.front().context))
			return true;

		std::uint32_t protectedMask = 0, trimMask = 0;
		std::array<std::uint64_t, Runtime::kFeatureSlotCount> trimmedBytes{};
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto slotIndex = args[index].featureSlot;
			// Separate eye calls still own both slots of the current stereo route.
			protectedMask |= FeatureSlotBit(slotIndex) | FeatureSlotBit(slotIndex ^ 1u);
			const auto& slot = slots_[slotIndex];
			const auto& work = slot.colorWork;
			const auto& resource = resources[index];
			if (!slot.resourcesValid || slot.resourceKey != resource.resourceKey ||
				!memoryConservation_.CanTrimColorBuffers(slotIndex) || !work.baseline.resource || !work.result.resource)
				continue;
			const auto retained = LogicalTextureBytes(work.format, work.capacityWidth, work.capacityHeight);
			std::optional<std::uint64_t> needed = 0;
			if (colorConfiguration_.Enabled()) {
				if (!work.Fits(resource.roi.inferenceContext, resource.output.desc.Format))
					continue;
				const auto [width, height] = Color::Work::AllocationExtent(resource.roi.inferenceContext);
				needed = LogicalTextureBytes(work.format, width, height);
			}
			if (retained && needed && MemoryConservationPolicy::WorthTrimming(*retained * 2, *needed * 2)) {
				trimMask |= FeatureSlotBit(slotIndex);
				trimmedBytes[slotIndex] = (*retained - *needed) * 2;
			}
		}

		for (std::uint32_t index = 0; index < slots_.size(); ++index) {
			const auto& slot = slots_[index];
			if (!slot.resourcesValid || (protectedMask & FeatureSlotBit(index)) ||
				slot.lastSuccessfulFrame == args.front().frameId || IsSequentialFrame(slot.lastSuccessfulFrame, args.front().frameId) ||
				!MemoryConservationPolicy::IsInactive(slot.lastUsedMs, nowMs))
				continue;
			std::uint64_t bytes = 0;
			bool bytesKnown = true;
			const auto appendBytes = [&](DXGI_FORMAT format, std::uint32_t width, std::uint32_t height) {
				const auto size = LogicalTextureBytes(format, width, height);
				bytesKnown &= size.has_value();
				bytes += size.value_or(0);
			};
			for (const auto* texture : { &slot.color, &slot.depth, &slot.motionVectors, &slot.controlMask, &slot.output })
				if (texture->resource12)
					appendBytes(texture->desc.Format, texture->desc.Width, texture->desc.Height);
			for (const auto* texture : { &slot.colorWork.baseline, &slot.colorWork.result })
				if (texture->resource)
					appendBytes(slot.colorWork.format, slot.colorWork.capacityWidth, slot.colorWork.capacityHeight);
			const auto modelBytes = modelResolution_.RetainedBytes(index);
			bytesKnown &= modelBytes.has_value();
			bytes += modelBytes.value_or(0);
			if (!RetireSlotLocked(index))
				return false;
			modelResolution_.ReleaseSlot(index);
			++memoryConservation_.retiredSlots;
			memoryConservation_.reclaimedLogicalBytesKnown &= bytesKnown;
			memoryConservation_.reclaimedLogicalBytes += bytes;
			reclaimed = true;
		}
		if (trimMask) {
			activeStage_ = RendererStage::ResourceRetirement;
			if (!interop_.WaitForIdle())
				return FailLocked(RendererStage::ResourceRetirement, interop_.LastError(),
					std::format("colour buffer retirement idle wait failed: {}", interop_.LastOperation()),
					args.front().featureSlot, true, true);
			for (std::uint32_t index = 0; index < slots_.size(); ++index) {
				if (!(trimMask & FeatureSlotBit(index)))
					continue;
				auto& slot = slots_[index];
				// These scratch images are rewritten each evaluation; native history stays intact.
				slot.colorWork.baseline = {};
				slot.colorWork.result = {};
				slot.colorWork.capacityWidth = slot.colorWork.capacityHeight = 0;
				slot.colorWork.prepared = false;
				memoryConservation_.RecordColorTrim(index, trimmedBytes[index]);
			}
			snapshot_.lastCompletedStage = RendererStage::ResourceRetirement;
			reclaimed = true;
		}
		if (reclaimed) {
			memoryConservation_.nextReclaimMs = GetTickCount64() + MemoryConservationPolicy::kReclaimIntervalMs;
			logger::info("[DLSSNR][Memory] Reclaimed optional NR capacity: retired slots={}, trimmed colour buffers={}, cumulative counted logical bytes={}, byte count complete={}",
				memoryConservation_.retiredSlots, memoryConservation_.trimmedColorBuffers, memoryConservation_.reclaimedLogicalBytes, memoryConservation_.reclaimedLogicalBytesKnown);
		}
		return true;
	}

	bool Renderer::State::AdmitMemoryLocked(
		std::span<const RendererApplyArgs> a_args,
		std::span<const ValidatedResources> a_resources, std::uint64_t a_additionalBytes)
	{
		// A failed optional create must retire before the first real native call.
		if (prewarm_.failed && prewarm_.backendOwned)
			return false;
		const auto& first = a_args.front();
		SetRequestTelemetryLocked(first);
		if (!ServiceRetainedBackendLocked())
			return false;
		if (const HRESULT reason = first.device->GetDeviceRemovedReason(); FAILED(reason))
			return FailLocked(RendererStage::DeviceRemoved, reason, "NR memory admission found a lost D3D11 device", first.featureSlot, true);
		if (const HRESULT reason = GetDeviceRemovalReasonLocked(S_OK); FAILED(reason))
			return FailLocked(RendererStage::DeviceRemoved, reason, "NR memory admission found device loss", first.featureSlot, true);
		auto additionalBytes = EstimateAdditionalMemoryLocked(a_args, a_resources);
		if (!additionalBytes)
			return FailLocked(RendererStage::Validation, E_INVALIDARG, "NR allocation size could not be established", first.featureSlot, false);
		*additionalBytes = Util::GpuMemoryBudget::Add(*additionalBytes, a_additionalBytes);
		auto nowMs = GetTickCount64();
		auto sample = SampleMemoryBudgetLocked(first.device, nowMs,
			*additionalBytes && memoryRecovery_.phase != MemoryRecoveryPhase::Waiting);
		nowMs = GetTickCount64();
		const bool wasConserving = memoryConservation_.active;
		const auto pendingBytes = Util::GpuMemoryBudget::Get().PendingBytes(Util::GpuMemoryBudget::Owner::NeuralRendering);
		if (resolutionRestoration_.active && !resolutionRestoration_.admitted) {
			auto preview = memoryRecovery_;
			if (!sample.IsFresh(nowMs) || !preview.Admit(sample, *additionalBytes, nowMs, pendingBytes)) {
				resolutionRestoration_.rejected = true;
				return false;
			}
			resolutionRestoration_.admitted = true;
		}
		memoryConservation_.Update(sample, Util::GpuMemoryBudget::Add(*additionalBytes, pendingBytes), memoryRecovery_.phase != MemoryRecoveryPhase::Ready, nowMs);
		if (wasConserving != memoryConservation_.active)
			logger::info("[DLSSNR][Memory] {}", memoryConservation_.active ?
													"Memory conservation active; reclaiming optional NR capacity without changing quality" :
													"Sustained headroom recovered; normal NR caching resumed");
		bool reclaimed = false;
		if (!ReclaimMemoryLocked(a_args, a_resources, nowMs, reclaimed))
			return false;
		SetActiveFeatureSlotLocked(first.featureSlot);
		if (reclaimed) {
			additionalBytes = EstimateAdditionalMemoryLocked(a_args, a_resources);
			if (!additionalBytes)
				return FailLocked(RendererStage::Validation, E_INVALIDARG, "NR replacement size could not be established", first.featureSlot, false);
			*additionalBytes = Util::GpuMemoryBudget::Add(*additionalBytes, a_additionalBytes);
			nowMs = GetTickCount64();
			sample = SampleMemoryBudgetLocked(first.device, nowMs, true);
		}
		nowMs = GetTickCount64();
		const auto previous = memoryRecovery_.phase;
		bool capacityChanged = false;
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			auto& key = memoryCapacityKeys_[a_args[index].featureSlot];
			capacityChanged |= key && *key != a_resources[index].resourceKey;
			key = a_resources[index].resourceKey;
		}
		if (capacityChanged)
			memoryRecovery_.InvalidateCapacityLearning();
		const bool admitted = memoryRecovery_.Admit(sample, *additionalBytes, nowMs, pendingBytes);
		if (admitted && memoryRetirement_.backendRetained) {
			memoryRetirement_.Released();
			backendMaintenancePending_.store(false, std::memory_order_relaxed);
		}
		Util::GpuMemoryBudget::Get().SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering,
			memoryRecovery_.phase == MemoryRecoveryPhase::Retiring || (admitted && *additionalBytes != 0),
			admitted ? 0 : memoryRecovery_.admissionDemandBytes);
		if (admitted && *additionalBytes)
			memoryReservation_ = Util::GpuMemoryBudget::Get().Reserve(Util::GpuMemoryBudget::Owner::NeuralRendering, first.device, *additionalBytes);
		if (memoryRecovery_.phase == MemoryRecoveryPhase::Retiring) {
			if (previous != MemoryRecoveryPhase::Retiring)
				logger::warn("[DLSSNR][Memory] Temporarily using the baseline while retiring NR under memory pressure: usage {:.1f}/{:.1f} MiB, additional {:.1f} MiB, recovery demand {:.1f} MiB, retry level={}",
					static_cast<double>(sample.usageBytes) / MemoryRecoveryPolicy::kMiB,
					static_cast<double>(sample.budgetBytes) / MemoryRecoveryPolicy::kMiB,
					static_cast<double>(*additionalBytes) / MemoryRecoveryPolicy::kMiB,
					static_cast<double>(memoryRecovery_.admissionDemandBytes) / MemoryRecoveryPolicy::kMiB, memoryRecovery_.retryLevel);
			if (!RetireMemoryPressureLocked())
				return false;
		}
		if (!admitted) {
			++memoryRecovery_.bypasses;
			snapshot_.outputCommitted = false;
		} else if (previous == MemoryRecoveryPhase::Waiting) {
			logger::info("[DLSSNR][Memory] Headroom recovered; rebuilding NR with fresh temporal history: usage {:.1f}/{:.1f} MiB, recovery demand {:.1f} MiB, pending {:.1f} MiB",
				static_cast<double>(sample.usageBytes) / MemoryRecoveryPolicy::kMiB,
				static_cast<double>(sample.budgetBytes) / MemoryRecoveryPolicy::kMiB,
				static_cast<double>(memoryRecovery_.admissionDemandBytes) / MemoryRecoveryPolicy::kMiB,
				static_cast<double>(pendingBytes) / MemoryRecoveryPolicy::kMiB);
		}
		snapshot_.memoryRecovery = memoryRecovery_;
		return admitted;
	}

	bool Renderer::State::RetireSlotLocked(std::uint32_t a_slot, const std::shared_ptr<ExecutionEvidence>& a_evidence)
	{
		auto& slot = slots_[a_slot];
		if (!slot.resourcesValid && !(prewarm_.unusedMask & FeatureSlotBit(a_slot)))
			return true;
		SetActiveFeatureSlotLocked(a_slot);
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		auto& cropDiagnostics = modelResolution_.CropDiagnostics();
		const auto cropEvent = cropDiagnostics.CostEvent(a_slot, snapshot_.frameId, snapshot_.sourceWorldFrame, snapshot_.generation, diagnosticSourceTransaction_);
		PackedCropCpuTimer cropRetirement(cropDiagnostics, cropEvent, &PackedCropRebuildTimings::retirementMicroseconds);
		PackedCropCpuTimer cropDrain(cropDiagnostics, cropEvent, &PackedCropRebuildTimings::gpuDrainMicroseconds);
#endif
		const bool idle = interop_.WaitForIdle(a_evidence);
#ifdef DEVBENCH_BRIDGE_ENABLED
		cropDrain.Stop();
		cropDiagnostics.Update(cropEvent, [&](auto& timings) { timings.gpuDrainSucceeded = idle; });
#endif
		if (!idle) {
			return FailLocked(
				RendererStage::ResourceRetirement,
				interop_.LastError(),
				std::format("slot {} idle wait failed: {}", a_slot, interop_.LastOperation()),
				a_slot,
				true,
				true);
		}
		// Preserve completed measurements before releasing their slot-owned readbacks.
		colorPipeline_.Poll(context_.Get(), slot.colorWork);
#ifdef DEVBENCH_BRIDGE_ENABLED
		PackedCropCpuTimer cropRelease(cropDiagnostics, cropEvent, &PackedCropRebuildTimings::releaseMicroseconds);
#endif
		const bool released = Runtime::Instance().ResetFeature(a_slot);
#ifdef DEVBENCH_BRIDGE_ENABLED
		cropRelease.Stop();
		cropDiagnostics.Update(cropEvent, [&](auto& timings) { timings.releaseSucceeded = released; });
#endif
		if (!released) {
			return FailLocked(
				RendererStage::ResourceRetirement,
				E_FAIL,
				std::format("slot {} Feature 18 release failed: {}", a_slot, Runtime::Instance().Detail()),
				a_slot,
				true,
				true);
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		for (const auto& readback : slot.colorWork.readbacks)
			if (readback.pending)
				Color::Registry::Instance().DropMeasurement();
#endif
		slot = {};
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (lifetime.enabled)
			lifetime.record->succeeded = true;
#endif
		snapshot_.lastCompletedStage = RendererStage::ResourceRetirement;
		return true;
	}

	bool Renderer::State::EnsureSlotLocked(
		std::uint32_t a_slot,
		const ValidatedResources& a_resources,
		const std::shared_ptr<ExecutionEvidence>& a_evidence, std::size_t a_region)
	{
		auto& slot = slots_[a_slot];
		if (a_evidence)
			a_evidence->Update([&](auto& evidence) {
				evidence.regions[a_region].rebuildReasons = !slot.resourcesValid                        ? RebuildUnallocated :
				                                            slot.resourceKey != a_resources.resourceKey ? RebuildResourceContractChanged :
				                                                                                          RebuildNone;
			});
		if (slot.resourcesValid && slot.resourceKey == a_resources.resourceKey)
			return true;

		const bool prepared = !slot.resourcesValid && (prewarm_.unusedMask & FeatureSlotBit(a_slot)) &&
		                      Runtime::Instance().HasCompatibleFeature(a_slot, a_resources.nativeLayout, a_resources.resourceKey.providerBlending, a_resources.resourceKey.networkPreset);
		if (!prepared && !RetireSlotLocked(a_slot, a_evidence))
			return false;
		prewarm_.Adopt(a_slot, prepared);

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
#ifdef DEVBENCH_BRIDGE_ENABLED
		auto& cropDiagnostics = modelResolution_.CropDiagnostics();
		const auto cropEvent = cropDiagnostics.CostEvent(a_slot, snapshot_.frameId, snapshot_.sourceWorldFrame, snapshot_.generation, diagnosticSourceTransaction_);
		PackedCropCpuTimer cropAllocation(cropDiagnostics, cropEvent, &PackedCropRebuildTimings::sharedAllocationMicroseconds);
		cropDiagnostics.Update(cropEvent, [](auto& timings) { timings.sharedAllocationSucceeded = false; });
#endif
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
		if (!create(colorDesc, replacement.color, (prefix + "Color").c_str()) ||
			!create(depthDesc, replacement.depth, (prefix + "Depth").c_str()) ||
			!create(motionDesc, replacement.motionVectors, (prefix + "MotionVectors").c_str()) ||
			(a_resources.resourceKey.controlMaskPresent &&
				!create(controlMaskDesc, replacement.controlMask,
					(prefix + (a_resources.resourceKey.providerBlending ? "ActorProtection" : "ControlMask")).c_str())) ||
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
		replacement.resourcesValid = true;
		slot = std::move(replacement);
#ifdef DEVBENCH_BRIDGE_ENABLED
		cropDiagnostics.Update(cropEvent, [](auto& timings) { timings.sharedAllocationSucceeded = true; });
#endif
		Increment(snapshot_.counters.resourceRebuilds);
		snapshot_.lastCompletedStage = RendererStage::ResourceCreation;
		return true;
	}

	bool Renderer::State::PrepareActorProtection(const RendererApplyArgs& args, Slot& slot)
	{
		if (actorProtectionCompileFailed_)
			return false;
		if (!actorProtectionCS_) {
			actorProtectionCompileFailed_ = true;
			actorProtectionCS_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/DLSS5ActorProtectionCS.hlsl", {}, "cs_5_0", "main")));
			if (!actorProtectionCS_)
				return false;
			actorProtectionCompileFailed_ = false;
			Util::SetResourceName(actorProtectionCS_.Get(), "NeuralRendering::ActorProtectionCS");
		}
		struct Constants
		{
			ComputeSubrect evaluation, selection;
		};
		static_assert(sizeof(Constants) == 32);
		if (!actorProtectionCB_) {
			const D3D11_BUFFER_DESC desc{ .ByteWidth = sizeof(Constants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER };
			if (FAILED(args.device->CreateBuffer(&desc, nullptr, &actorProtectionCB_)))
				return false;
			Util::SetResourceName(actorProtectionCB_.Get(), "NeuralRendering::ActorProtectionCB");
		}
		const Constants constants{ ResolveComputeSubrect(args), args.actorSelectionSupport };
		RendererComputeStateGuard guard(args.context);
		if (!guard.Captured() || !slot.controlMask.uav11)
			return false;
		args.context->UpdateSubresource(actorProtectionCB_.Get(), 0, nullptr, &constants, 0, 0);
		auto* cb = actorProtectionCB_.Get();
		auto* input = args.actorSelection.Get();
		auto* output = slot.controlMask.uav11.Get();
		args.context->CSSetShader(actorProtectionCS_.Get(), nullptr, 0);
		args.context->CSSetConstantBuffers(0, 1, &cb);
		args.context->CSSetShaderResources(0, 1, &input);
		args.context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		CS_GPU_PASS("NeuralRendering::ActorProtection");
		args.context->Dispatch((constants.evaluation.width + 7u) / 8u, (constants.evaluation.height + 7u) / 8u, 1u);
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
		bool alreadyPrepared = true;
		for (std::size_t index = 0; index < a_args.size(); ++index)
			alreadyPrepared &= SameIdentity(a_resources[index].depth.texture.Get(), a_slots[index]->depth.resource11.Get());
		if (alreadyPrepared)
			return true;
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

		RendererComputeStateGuard stateGuard(a_args.front().context);
		if (!stateGuard.Captured())
			return false;

		a_args.front().context->CSSetShader(shader, nullptr, 0);
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			if (SameIdentity(a_resources[index].depth.texture.Get(), a_slots[index]->depth.resource11.Get()))
				continue;
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

#ifdef DEVBENCH_BRIDGE_ENABLED
	PackedCropAdmissionDiagnostics* Renderer::State::BeginPackedCropAdmissionLocked(const RendererApplyArgs& args) noexcept
	{
		// Model proxies must not overwrite the original eye's admission decision.
		if (args.featureSlot >= packedCropAdmission_.size() || args.modelResolutionHistory)
			return nullptr;
		auto& value = packedCropAdmission_[args.featureSlot];
		value = {};
		value.frame = args.frameId;
		value.sourceFrame = args.sourceWorldFrame;
		value.generation = args.generation;
		value.modelResolutionPercent = args.modelResolutionPercent;
		value.baselineSourcePixels = args.computeSubrect.Area();
		value.planning = args.packedCropPlanning;
		value.reason = args.packedCrops.count >= 2 ? PackedCropReason::NotExamined : value.planning.reason;
		value.stage = args.packedCrops.count >= 2 ? PackedCropStage::ModelProjection : PackedCropStage::SourcePlanning;
		return &value;
	}
#endif

	bool Renderer::State::ApplyLocked(
		const RendererApplyArgs& a_args,
		RendererApplyOutcome& a_outcome)
	{
		if (!resolutionRestoration_.active)
			modelResolution_.ReleaseUnscaledSlots(std::span(&a_args, 1));
		return ApplyBatchLocked(std::span(&a_args, 1), a_outcome);
	}

	bool Renderer::State::ApplyStereoLocked(
		const std::array<RendererApplyArgs, 2>& a_args,
		RendererApplyOutcome& a_outcome)
	{
		if (!resolutionRestoration_.active)
			modelResolution_.ReleaseUnscaledSlots(a_args);
		return ApplyBatchLocked(a_args, a_outcome);
	}

	bool Renderer::State::ApplySequentialStereoLocked(
		const std::array<RendererApplyArgs, 2>& a_args,
		RendererApplyOutcome& a_outcome, bool a_memoryAdmitted)
	{
		a_outcome = {};
		// Colour reconstruction commits both eyes against one immutable input snapshot.
		if (ModelResolution::Required(a_args[0]
#ifdef DEVBENCH_BRIDGE_ENABLED
				,
				BeginPackedCropAdmissionLocked(a_args[0])
#endif
					) ||
			ModelResolution::Required(a_args[1]
#ifdef DEVBENCH_BRIDGE_ENABLED
				,
				BeginPackedCropAdmissionLocked(a_args[1])
#endif
					))
			return ApplyModelResolutionLocked(a_args, a_outcome, true);
		if (!a_memoryAdmitted && !resolutionRestoration_.active)
			modelResolution_.ReleaseUnscaledSlots(a_args);
		if (colorConfiguration_.Enabled())
			return ApplyBatchLocked(a_args, a_outcome, a_memoryAdmitted);
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

		if (!a_memoryAdmitted && !AdmitMemoryLocked(a_args, resources)) {
			a_outcome.memoryPressureBypass = !failureLatched_ && !quarantined_ && memoryRecovery_.phase != MemoryRecoveryPhase::Ready;
			return false;
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

		// Only this validated pair may reuse admission; fallback allocations recheck it.
		RendererApplyOutcome leftOutcome{};
		if (!ApplyBatchLocked(std::span(&synchronizedArgs[0], 1), leftOutcome, true)) {
			a_outcome = leftOutcome;
			return false;
		}
		a_outcome.evaluationAttemptedFeatureSlotMask =
			leftOutcome.evaluationAttemptedFeatureSlotMask;
		a_outcome.evaluationSucceededFeatureSlotMask =
			leftOutcome.evaluationSucceededFeatureSlotMask;
		RendererApplyOutcome rightOutcome{};
		const bool rightSucceeded = ApplyBatchLocked(
			std::span(&synchronizedArgs[1], 1), rightOutcome, true);
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
#endif

#ifdef DEVBENCH_BRIDGE_ENABLED
	void Renderer::State::MergeModelPreparationEvidenceLocked(ExecutionEvidence& execution) const noexcept
	{
		if (modelPreparationEvidence_)
			execution.MergeResourcePreparation(*modelPreparationEvidence_);
		else
			execution.Update([](auto& evidence) {
				evidence.evidenceFailed = true;
				for (auto& region : evidence.regions)
					region.allocationBytesKnown = false;
			});
	}
#endif

	bool Renderer::State::ApplyModelResolutionLocked(std::span<const RendererApplyArgs> args, RendererApplyOutcome& outcome, bool sequential)
	{
		outcome = {};
		if (args.empty() || args.size() > kEyeCount)
			return false;
		if (failureLatched_ || quarantined_)
			return ApplyRegionBatchLocked(args, outcome);
		SetRequestTelemetryLocked(args.front());
		const bool packingWasAvailable = modelResolution_.PackingAvailable();
		HRESULT preparationResult = S_OK;
		if (!modelResolution_.PreparePacking(args, preparationResult))
			return FailLocked(preparationResult == E_INVALIDARG ? RendererStage::Validation : RendererStage::ResourceCreation,
				preparationResult, "NR model shader preparation failed", args.front().featureSlot, preparationResult != E_INVALIDARG);
		std::array<RendererApplyArgs, kEyeCount> fallback{};
		if (!modelResolution_.PackingAvailable()) {
			if (packingWasAvailable)
				logger::warn("[DLSSNR] Multi-Crop shaders unavailable (0x{:08X}); retaining single-region NR until shader reset",
					static_cast<std::uint32_t>(modelResolution_.PackingFailure()));
			std::ranges::copy(args, fallback.begin());
			auto working = std::span(fallback.data(), args.size());
			modelResolution_.PlanPackedCrops(working, resolutionRestoration_.active);
			args = working;
		}
		const auto& first = args.front();
		// A backend identity change retires its slots; staged proxies survive that reset.
		const bool stableBackend = !device_ || (SameIdentity(device_.Get(), first.device) && SameIdentity(context_.Get(), first.context));
		const bool sharedTargets = stableBackend && ModelResolution::CanUseSharedTargets(args,
														colorConfiguration_.experiments.CompactInputsEnabled());
		const auto sharing = !sharedTargets ? ModelResolution::Sharing::Private : colorConfiguration_.Enabled() ? ModelResolution::Sharing::Guides :
		                                                                                                          ModelResolution::Sharing::All;
		bool sharedInputsPrepared = false;
		SetRequestTelemetryLocked(first);
		const SKSE::stl::scope_exit restoreTelemetry([&] {
			if constexpr (kDevelopmentDiagnostics) {
				const auto matched = std::ranges::find_if(args, [&](const auto& value) { return value.featureSlot == snapshot_.featureSlot; });
				const auto& observed = matched != args.end() ? *matched : first;
				RendererApplyArgs telemetryProxy;
				const bool projected = ModelResolution::Project(observed, telemetryProxy);
				const auto extent = projected ? UpscalingDLSS::Extent{ telemetryProxy.outputWidth, telemetryProxy.outputHeight } : UpscalingDLSS::Extent{};
#ifdef DEVBENCH_BRIDGE_ENABLED
				snapshot_.modelOutputSelected = outcome.WasOutputSelected(observed.featureSlot);
				snapshot_.packedCropLayout = projected && telemetryProxy.modelResolutionHistory ? telemetryProxy.modelResolutionHistory->packedCrops : PackedCropLayout{};
#endif
				snapshot_.modelResolutionPercent = observed.modelResolutionPercent;
				snapshot_.reconstructionFilter = observed.reconstructionFilter;
				snapshot_.modelSharedInputs = sharedInputsPrepared;
				snapshot_.modelSourceWidth = observed.outputWidth;
				snapshot_.modelSourceHeight = observed.outputHeight;
				snapshot_.modelWidth = extent.width;
				snapshot_.modelHeight = extent.height;
			}
		});
		if (args.size() == 2) {
			if (auto violation = GetStereoPairContractViolation({ args[0], args[1] }); !violation.empty())
				return FailLocked(RendererStage::Validation, E_INVALIDARG, std::move(violation), first.featureSlot, false);
		}
		std::array<ValidatedResources, kEyeCount> resources{};
		std::array<RendererApplyArgs, kEyeCount> projected{};
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& input = args[index];
			auto& resource = resources[index];
			if (auto failure = ValidateLocked(input, resource
#ifdef DEVBENCH_BRIDGE_ENABLED
					,
					false
#endif
				);
				failure)
				return FailLocked(RendererStage::Validation, failure.result, std::move(failure.detail), input.featureSlot, false);
			auto& proxy = projected[index];
			if (!ModelResolution::Project(input, proxy
#ifdef DEVBENCH_BRIDGE_ENABLED
					,
					BeginPackedCropAdmissionLocked(input)
#endif
						))
				return FailLocked(RendererStage::Validation, E_INVALIDARG,
					"model resolution requires supported colour/output grids and valid guide geometry", input.featureSlot, false);
			resource.roi = proxy.roi.value_or(BuildRoiDescriptor(std::nullopt, proxy.computeSubrect,
				{ proxy.outputWidth, proxy.outputHeight }, false));
			resource.nativeLayout = BuildNativeEvaluationLayout(
				{ proxy.colorWidth, proxy.colorHeight }, { proxy.guideWidth, proxy.guideHeight },
				{ proxy.outputWidth, proxy.outputHeight }, { proxy.controlMaskWidth, proxy.controlMaskHeight },
				proxy.computeSubrect, UpscalingDLSS::BuildMotionVectorPixelScale(proxy.viewportCrop), proxy.featureUpscaling);
			FinalizeResourceKeysLocked(proxy, resource);
		}
		const auto additional = modelResolution_.AdditionalBytes(args, sharing);
		if (!additional)
			return FailLocked(RendererStage::Validation, E_INVALIDARG,
				"NR model-resolution allocation size could not be established", first.featureSlot, false);
		if (!AdmitMemoryLocked(std::span(projected.data(), args.size()), std::span(resources.data(), args.size()), *additional)) {
			outcome.memoryPressureBypass = !failureLatched_ && !quarantined_ && memoryRecovery_.phase != MemoryRecoveryPhase::Ready;
			return false;
		}

		std::shared_ptr<ExecutionEvidence> preparationEvidence;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (sharedTargets && captureInputs_[captureRoute_].valid) {
			try {
				ExecutionDescriptor descriptor{};
				descriptor.regionCount = static_cast<std::uint32_t>(args.size());
				for (std::size_t index = 0; index < args.size(); ++index)
					descriptor.regions[index].physicalSlot = args[index].featureSlot;
				preparationEvidence = std::make_shared<ExecutionEvidence>(std::move(descriptor));
			} catch (const std::bad_alloc&) {
				++captureInputs_[captureRoute_].executionEvidenceFailures;
			}
		}
		const auto previousPreparation = std::exchange(modelPreparationEvidence_, preparationEvidence);
		const SKSE::stl::scope_exit restorePreparation([&] { modelPreparationEvidence_ = previousPreparation; });
#endif
		std::array<ModelResolution::Targets, kEyeCount> targets{};
		if (sharedTargets) {
			if (!EnsureBackendLocked(first, preparationEvidence))
				return false;
			for (std::size_t index = 0; index < args.size(); ++index) {
				if (!EnsureSlotLocked(args[index].featureSlot, resources[index], preparationEvidence, index))
					return false;
				const auto& slot = slots_[args[index].featureSlot];
				const auto retain = [](const SharedTexture& texture) {
					return Color::Texture{ texture.resource11, texture.srv11, texture.uav11 };
				};
				targets[index].depth = retain(slot.depth);
				targets[index].motion = retain(slot.motionVectors);
				if (sharing == ModelResolution::Sharing::All) {
					targets[index].color = retain(slot.color);
					targets[index].output = retain(slot.output);
				}
			}
		}
		ModelResolution::Batch batch;
		HRESULT result = S_OK;
		activeStage_ = RendererStage::ResourceCreation;
		if (!modelResolution_.Prepare(args, batch, result, sharedTargets ? std::span(targets.data(), args.size()) : std::span<const ModelResolution::Targets>{}))
			return FailLocked(RendererStage::ResourceCreation, result,
				"NR model-resolution input preparation failed; original render input retained", first.featureSlot, true);
		sharedInputsPrepared = sharedTargets;
		const bool nativeSucceeded = sequential ? ApplySequentialStereoLocked(batch.arguments, outcome, true) :
		                                          ApplyBatchLocked(std::span(batch.arguments.data(), batch.count), outcome, true);
		if (!nativeSucceeded)
			return false;
		activeStage_ = RendererStage::OutputCommit;
		if (!modelResolution_.Reconstruct(args, batch, result))
			return FailLocked(RendererStage::OutputCommit, result,
				"NR model-resolution reconstruction failed before pair commit", first.featureSlot, true);
		modelResolution_.Commit(args, batch);
		if (const auto removed = first.device->GetDeviceRemovedReason(); FAILED(removed))
			return FailLocked(RendererStage::OutputCommit, removed,
				"device removal followed NR model-resolution pair commit", first.featureSlot, true);
		for (std::size_t index = 0; index < args.size(); ++index)
			if (batch.selectedOutputs[index])
				outcome.selectedOutputFeatureSlotMask |= 1u << args[index].featureSlot;
		activeStage_ = RendererStage::Complete;
		return true;
	}

	bool Renderer::State::ApplyBatchLocked(
		std::span<const RendererApplyArgs> args, RendererApplyOutcome& outcome, bool a_memoryAdmitted)
	{
		if (std::ranges::any_of(args, [this](const auto& value) {
				return ModelResolution::Required(value
#ifdef DEVBENCH_BRIDGE_ENABLED
					,
					BeginPackedCropAdmissionLocked(value)
#endif
				);
			}))
			return ApplyModelResolutionLocked(args, outcome);
#ifdef DEVBENCH_BRIDGE_ENABLED
		requestedRegionCount_ = static_cast<std::uint32_t>(args.size());
		if (args.empty() || args.size() > kEyeCount)
			return ApplyRegionBatchLocked(args, outcome);
		const auto fallback = [&] {
			const auto previous = forceFullCoordinates_;
			forceFullCoordinates_ = true;
			const SKSE::stl::scope_exit restore([&] { forceFullCoordinates_ = previous; });
			const auto attempted = outcome.evaluationAttemptedFeatureSlotMask;
			const bool success = ApplyRegionBatchLocked(args, outcome);
			outcome.evaluationAttemptedFeatureSlotMask |= attempted;
			return success;
		};
		return capacityFallback_.ApplyBatch(colorConfiguration_.experiments.CompactInputsEnabled(), [&] { return ApplyRegionBatchLocked(args, outcome, a_memoryAdmitted); }, [&] {
				if (memoryRecovery_.phase == MemoryRecoveryPhase::Retiring || memoryRecovery_.phase == MemoryRecoveryPhase::Waiting ||
					quarantined_ || FAILED(GetDeviceRemovalReasonLocked(S_OK)))
					return CapacityFailure::Unsafe;
				if (snapshot_.failureStage == RendererStage::ResourceCreation && snapshot_.lastResult == E_OUTOFMEMORY)
					return CapacityFailure::Pressure;
				return snapshot_.failureStage == RendererStage::FeatureEvaluate ?
					Runtime::Instance().CreationCapacityFailure() : CapacityFailure::Unsafe; }, [&] { return TeardownBackendLocked(false, false, false); }, fallback);
#else
		return ApplyRegionBatchLocked(args, outcome, a_memoryAdmitted);
#endif
	}

	bool Renderer::State::ApplyRegionBatchLocked(
		std::span<const RendererApplyArgs> a_args,
		RendererApplyOutcome& a_outcome, bool a_memoryAdmitted)
	{
		a_outcome = {};
		if (a_args.empty() || a_args.size() > kEyeCount)
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

		for (const auto& args : a_args) {
			if (args.featureSlot >= kLogicalFeatureSlotCount) {
				SetRequestTelemetryLocked(args);
				return FailLocked(RendererStage::Validation, E_INVALIDARG,
					"NR requires a valid main or submit eye feature slot", args.featureSlot, false);
			}
		}

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

		const auto logicalEyeCount = static_cast<std::uint32_t>(a_args.size());

		std::array<ValidatedResources, kEyeCount> resources{};
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
			resources[index].historyKey.regionIdentity = 0;
		}
		snapshot_.lastCompletedStage = RendererStage::Validation;
		if (!a_memoryAdmitted && !AdmitMemoryLocked(a_args, std::span(resources.data(), a_args.size()))) {
			a_outcome.memoryPressureBypass = !failureLatched_ && !quarantined_ && memoryRecovery_.phase != MemoryRecoveryPhase::Ready;
			return false;
		}

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
				descriptor.capacityFallback = forceFullCoordinates_;
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
					region.regionIdentity = 0;
					region.clusterIdentity = 0;
					region.context = args.executionContext;
					region.characterEvidence = args.characterEvidence;
					region.roi = resource.roi;
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
		if (!EnsureBackendLocked(a_args.front(), execution))
			return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (lifetime.enabled)
			lifetime.record->backendSerial = backendSerial_;
#endif
		for (auto& slot : slots_)
			colorPipeline_.Poll(a_args.front().context, slot.colorWork);
		activeStage_ = RendererStage::ResourceCreation;
		std::array<Slot*, kEyeCount> slots{};
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			if (!EnsureSlotLocked(a_args[index].featureSlot, resources[index], execution, index))
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
					evidence.regions[index].inputTransportOwnerSlot = a_args[index].featureSlot;
				});
		}

#ifdef DEVBENCH_BRIDGE_ENABLED
		if (execution && SameIdentity(a_args.front().colorInput, slots.front()->color.resource11.Get()))
			MergeModelPreparationEvidenceLocked(*execution);
#endif

		// Allocate/compile for every physical region before changing any caller output.
		// The legacy raw lane does not allocate or dispatch colour resources.
		if (colorConfiguration_.Enabled()) {
			for (std::size_t index = 0; index < a_args.size(); ++index) {
				const auto profile = Color::EffectiveProfile(colorConfiguration_,
					static_cast<std::uint32_t>(a_args[index].insertionPoint));
				const auto format = resources[index].resourceKey.colorFormat;
				const bool floatStorage = format == DXGI_FORMAT_R11G11B10_FLOAT ||
				                          format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT;
				if (profile.transform != Color::Transform::Identity && !floatStorage)
					return FailLocked(RendererStage::Validation, E_INVALIDARG,
						"NR encoding/proxy experiment requires floating-point processing resources", a_args[index].featureSlot, false);
				const auto oldBaseline = slots[index]->colorWork.baseline.resource.Get();
				HRESULT colorResult = S_OK;
#ifdef DEVBENCH_BRIDGE_ENABLED
				auto& cropDiagnostics = modelResolution_.CropDiagnostics();
				const auto& input = a_args[index];
				const auto colorEvent = !slots[index]->colorWork.Fits(resources[index].roi.inferenceContext, resources[index].output.desc.Format) ?
				                            cropDiagnostics.CostEvent(input.featureSlot, input.frameId, input.sourceWorldFrame, input.generation, input.executionContext.sourceTransactionId) :
				                            0;
				PackedCropCpuTimer colorAllocation(cropDiagnostics, colorEvent, &PackedCropRebuildTimings::colorAllocationMicroseconds);
#endif
				const bool colorReady = colorPipeline_.Ensure(a_args[index].device, slots[index]->colorWork,
					resources[index].roi.inferenceContext, resources[index].output.desc.Format,
					colorConfiguration_.experiments.diagnostics, colorResult);
#ifdef DEVBENCH_BRIDGE_ENABLED
				colorAllocation.Stop();
				cropDiagnostics.Update(colorEvent, [&](auto& timings) { timings.colorAllocationSucceeded = colorReady; });
#endif
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
					return FailLocked(RendererStage::ResourceCreation, colorResult,
						"shared NR colour shaders/resources are unavailable", a_args[index].featureSlot, true);
				}
			}
		}

		const auto preparationStarted = DiagnosticNow();
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
				if (a_args[index].modelResolutionHistory) {
					const auto& packed = a_args[index].modelResolutionHistory->packedCrops;
					if (packed.count > kMaximumPackedCrops)
						return FailLocked(RendererStage::ColorInputCopy, E_INVALIDARG, "invalid NR colour crop count", a_args[index].featureSlot, true);
					observation.reconstructionRegions.count = packed.count;
					for (std::uint32_t tile = 0; tile < packed.count; ++tile)
						observation.reconstructionRegions.regions[tile] = packed.tiles[tile].packed;
				}
				observation.sourceFormat = static_cast<std::uint32_t>(resources[index].color.desc.Format);
				observation.outputFormat = static_cast<std::uint32_t>(resources[index].output.desc.Format);
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
			} else if (!SameIdentity(slots[index]->color.resource11.Get(), resources[index].color.texture.Get())) {
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
		for (const auto& args : a_args)
			if (!SameIdentity(args.depthGuide, slots_[args.featureSlot].depth.resource11.Get()))
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
			if (SameIdentity(slots[index]->motionVectors.resource11.Get(), resources[index].motionVectors.texture.Get()))
				continue;
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
				if (a_args[index].providerBlending) {
					if (!PrepareActorProtection(a_args[index], *slots[index]))
						return FailLocked(RendererStage::ControlMaskCopy, E_FAIL, "actor protection preparation failed; preserving baseline", a_args[index].featureSlot, true);
					continue;
				}
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

		ID3D12GraphicsCommandList* parallelCommands = nullptr;
		// Feature creation stays on the primary queue; only independent, resident eye models overlap.
		if (a_args.size() == 2 && a_args[0].parallelEyes && !interop_.IsParallelDisabled() && !colorConfiguration_.experiments.transportBypass &&
			Runtime::Instance().HasCompatibleFeature(a_args[0].featureSlot, resources[0].nativeLayout, a_args[0].providerBlending, a_args[0].tuning.networkPreset) &&
			Runtime::Instance().HasCompatibleFeature(a_args[1].featureSlot, resources[1].nativeLayout, a_args[1].providerBlending, a_args[1].tuning.networkPreset))
			parallelCommands = interop_.ParallelList();

		std::array<FeatureResourceTransition, kMaximumTransitionResourceCount>
			sharedResources{};
		std::array<std::size_t, kEyeCount + 1> resourceOffsets{};
		std::size_t sharedResourceCount = 0;
		std::uint64_t pixelCount = 0;
		std::uint32_t featureSlotMask = 0;
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			const auto add = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
				if (!resource || sharedResourceCount == sharedResources.size())
					return false;
				sharedResources[sharedResourceCount++] = { resource, state };
				return true;
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
			resourceOffsets[index + 1] = sharedResourceCount;
			Add(pixelCount, resources[index].roi.inferenceContext.Area());
			featureSlotMask |= 1u << a_args[index].featureSlot;
		}
		const auto resourceSpan =
			std::span(sharedResources.data(), sharedResourceCount);
		const auto transitionEyes = [&](bool enter) {
			if (!parallelCommands) {
				TransitionResources(commandList, resourceSpan, enter);
				return;
			}
			for (std::size_t index = 0; index < a_args.size(); ++index)
				TransitionResources(index == 1 && parallelCommands ? parallelCommands : commandList,
					resourceSpan.subspan(resourceOffsets[index], resourceOffsets[index + 1] - resourceOffsets[index]), enter);
		};
		transitionEyes(true);
		const D3D12InteropSubmissionTiming timing{
			.frameId = a_args.front().frameId,
			.pixelCount = pixelCount,
			.evaluationCount = colorConfiguration_.experiments.transportBypass ? 0u : static_cast<std::uint32_t>(a_args.size()),
			.featureSlotMask = featureSlotMask,
			.insertionPoint = a_args.front().insertionPoint,
			.logicalEyeCount = logicalEyeCount,
			.execution = execution,
		};
		bool timingScopeOpened = false;
		const bool timingStarted = colorConfiguration_.experiments.transportBypass ?
		                               interop_.RecordTransportSubmission(timing) :
		                               interop_.BeginFeatureTiming(commandList, timing, timingScopeOpened);
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

		std::array<bool, kEyeCount> forcedHistoryReset{};
		std::array<bool, kEyeCount> discontinuousHistoryReset{};
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
		// Both eye histories reset together while retaining separate native state.
		for (std::size_t left = 0; left < a_args.size(); ++left) {
			for (std::size_t right = 0; right < a_args.size(); ++right) {
				if (!IsOrderedStereoFeatureSlotPair(a_args[left].featureSlot,
						a_args[right].featureSlot))
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

		activeStage_ = RendererStage::FeatureEvaluate;
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
			RuntimeExecutionEvidence runtimeEvidence{};
#ifdef DEVBENCH_BRIDGE_ENABLED
			auto& cropDiagnostics = modelResolution_.CropDiagnostics();
			const auto cropEvent = cropDiagnostics.Pending(args.featureSlot, args.frameId, args.sourceWorldFrame, args.generation, args.executionContext.sourceTransactionId);
#endif
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
				cropDiagnostics.Update(cropEvent, [&](auto& timings) {
					timings.nativeCreateMicroseconds = runtimeEvidence.createCpuMicroseconds;
					timings.nativeEvaluateMicroseconds = runtimeEvidence.evaluateCpuMicroseconds;
					if (runtimeEvidence.createAttempted)
						timings.nativeCreateSucceeded = runtimeEvidence.createSucceeded;
				});
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
			const bool nativeEvaluated = Runtime::Instance().Execute(
				index == 1 && parallelCommands ? parallelCommands : commandList,
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
				execution || lifetime.enabled || cropEvent ? &runtimeEvidence : nullptr,
#else
				execution ? &runtimeEvidence : nullptr,
#endif
				&interop_,
				static_cast<std::uint32_t>(index), args.providerBlending);
			if (evaluationAttempted) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				cropDiagnostics.RecordEvaluation(args.featureSlot,
					args.modelResolutionHistory && args.modelResolutionHistory->packedCrops.count != 0,
					nativeEvaluated, resources[index].roi.inferenceContext.Area());
#endif
				Increment(snapshot_.counters.featureEvaluations);
				a_outcome.evaluationAttemptedFeatureSlotMask |= 1u << args.featureSlot;
			}
			if (!nativeEvaluated) {
				if (parallelCommands)
					interop_.DisableParallel("native evaluation failed with parallel eyes");
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

		if (!colorConfiguration_.experiments.transportBypass && !interop_.EndFeatureTiming(commandList, timingScopeOpened)) {
			const bool aborted = recordingGuard.Abort();
			return FailLocked(
				RendererStage::CommandEnd,
				aborted ? E_FAIL : interop_.LastError(),
				"D3D12 Feature 18 timestamp recording could not end",
				a_args.front().featureSlot,
				true,
				!aborted);
		}
		transitionEyes(false);
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

		// Reconstruct ALL physical regions before the first external write. This is
		// shared by scene and Actor NR, before material-strength blending.
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

		// Both private inputs are prepared before submission and neither caller-owned
		// output is written until every eye has recorded successfully.
		const auto outputCommitStarted = DiagnosticNow();
		ExecutionCpuTimer outputCommitTimer(execution, &ExecutionSnapshot::commitCpuMicroseconds);
		for (std::size_t index = 0; index < a_args.size(); ++index) {
			SetActiveFeatureSlotLocked(a_args[index].featureSlot);
			CS_GPU_DETAIL_PASS("Upscaling::NROutputCommit", execution ? execution->Snapshot().regions[index].outputCopyPass : nullptr);
			if (colorConfiguration_.Enabled()) {
				colorPipeline_.Commit(a_args.front().context, slots[index]->colorWork,
					resources[index].output.texture.Get());
			} else if (!SameIdentity(resources[index].output.texture.Get(), slots[index]->output.resource11.Get())) {
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
			if (execution && (colorConfiguration_.Enabled() || !SameIdentity(resources[index].output.texture.Get(), slots[index]->output.resource11.Get()))) {
				RecordExecutionCopy(execution, index, LogicalTextureBytes(resources[index].output.desc.Format, resources[index].roi.ownedOutput.width, resources[index].roi.ownedOutput.height));
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
			slots[index]->lastUsedMs = GetTickCount64();
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
			{ "providerBlending", first.providerBlending },
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

	bool Renderer::State::ResetLocked(bool a_resetShader, bool a_destruction, BackendRetirementPolicy a_policy)
	{
		if (!TeardownBackendLocked(a_resetShader, a_destruction, false, {}, a_policy))
			return false;
		memoryReservation_.Reset();
		Util::GpuMemoryBudget::Get().SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0);
		const bool pressureRetirement = memoryRecovery_.phase == MemoryRecoveryPhase::Retiring;
		const auto now = GetTickCount64();
		memoryRecovery_.ResourcesRetired(now);
		if (pressureRetirement) {
			if (memoryResolution_.OnRetired(snapshot_.requestedModelResolutionPercent, now))
				ResolutionChangedLocked();
		} else if (memoryRecovery_.phase == MemoryRecoveryPhase::Waiting) {
			memoryResolution_.MarkResourcesRetired(now);
		}
		memoryResolution_.ClearHealthyWindow();
		resolutionLatches_ = {};
		successfulResolutionPercents_ = {};
		restorationPreviousPercent_ = 0;
		resolutionRestoration_ = {};
		memoryConservation_ = {};
		snapshot_.memoryRecovery = memoryRecovery_;
		memorySample_ = {};
		nextMemorySampleMs_ = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		simulatedPressureUntilMs_ = 0;
#endif
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
		const SKSE::stl::scope_exit releaseMemory([&] {
			state_->FinishMemoryAdmissionLocked();
		});
		RendererApplyOutcome outcome{};
		const auto failuresBefore = state_->snapshot_.counters.failures;
		state_->SetActiveFeatureSlotLocked(a_args.featureSlot);
		try {
			auto args = a_args;
			state_->PrepareMemoryResolutionLocked({ &args, 1 });
			state_->CaptureColorConfiguration(a_args);
			CS_GPU_PASS_CAPTURE("Upscaling::DLSSNeuralRendering", state_->wholePass_);
			bool succeeded = state_->ApplyLocked(args, outcome);
			if (state_->RetryWorkingResolutionLocked({ &args, 1 }))
				succeeded = state_->ApplyLocked(args, outcome);
			state_->FinishMemoryResolutionLocked({ &args, 1 }, succeeded);
			state_->FinishMemoryRecoveryLocked(succeeded, outcome);
			if (succeeded)
				Runtime::Instance().AcknowledgePresentedFeatures(outcome.evaluationSucceededFeatureSlotMask);
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
		const SKSE::stl::scope_exit releaseMemory([&] {
			state_->FinishMemoryAdmissionLocked();
		});
		RendererApplyOutcome outcome{};
		Increment(state_->snapshot_.counters.stereoAttempts);
		const auto failuresBefore = state_->snapshot_.counters.failures;
		state_->SetActiveFeatureSlotLocked(a_args[0].featureSlot);
		try {
			auto args = a_args;
			state_->PrepareMemoryResolutionLocked(args);
			state_->CaptureColorConfiguration(a_args[0]);
			CS_GPU_PASS_CAPTURE("Upscaling::DLSSNeuralRenderingStereo", state_->wholePass_);
			bool succeeded = state_->ApplyStereoLocked(args, outcome);
			if (state_->RetryWorkingResolutionLocked(args))
				succeeded = state_->ApplyStereoLocked(args, outcome);
			state_->FinishMemoryResolutionLocked(args, succeeded);
			state_->FinishMemoryRecoveryLocked(succeeded, outcome);
			if (succeeded)
				Runtime::Instance().AcknowledgePresentedFeatures(outcome.evaluationSucceededFeatureSlotMask);
			if (!outcome.memoryPressureBypass)
				Increment(succeeded ? state_->snapshot_.counters.stereoSuccesses : state_->snapshot_.counters.stereoFailures);
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
		const SKSE::stl::scope_exit releaseMemory([&] {
			state_->FinishMemoryAdmissionLocked();
		});
		RendererApplyOutcome outcome{};
		const auto failuresBefore = state_->snapshot_.counters.failures;
		state_->SetActiveFeatureSlotLocked(a_args[0].featureSlot);
		try {
			auto args = a_args;
			state_->PrepareMemoryResolutionLocked(args);
			state_->CaptureColorConfiguration(a_args[0]);
			CS_GPU_PASS_CAPTURE("Upscaling::DLSSNeuralRenderingSequentialStereo", state_->wholePass_);
			bool succeeded = state_->ApplySequentialStereoLocked(args, outcome);
			if (state_->RetryWorkingResolutionLocked(args))
				succeeded = state_->ApplySequentialStereoLocked(args, outcome);
			state_->FinishMemoryResolutionLocked(args, succeeded);
			state_->FinishMemoryRecoveryLocked(succeeded, outcome);
			if (succeeded)
				Runtime::Instance().AcknowledgePresentedFeatures(outcome.evaluationSucceededFeatureSlotMask);
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

	bool Renderer::Reset(bool a_clearTransportRejections, BackendRetirementPolicy a_policy)
	{
		std::scoped_lock lock(state_->mutex_);
		try {
			const bool reset = state_->ResetLocked(false, false, a_policy);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (reset && a_clearTransportRejections) {
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

	void Renderer::ServiceBackendMaintenance(bool a_requested)
	{
		if (!state_->backendMaintenancePending_.load(std::memory_order_relaxed) && !state_->shaderInvalidation_.Pending())
			return;
		Util::RendererOwnership owner(Util::GetRendererContextLock(globals::game::renderer, globals::d3d::context));
		if (!owner)
			return;
		std::unique_lock lock(state_->mutex_, std::try_to_lock);
		if (!lock.owns_lock())
			return;
		try {
			state_->ApplyShaderInvalidationLocked();
			if (state_->snapshot_.stallRecovery.pending)
				(void)state_->ServiceStallRecoveryLocked(a_requested);
			else if (state_->failureLatched_ || state_->quarantined_)
				state_->backendMaintenancePending_.store(false, std::memory_order_relaxed);
			else if (state_->memoryRecovery_.phase == MemoryRecoveryPhase::Retiring) {
				(void)state_->RetireMemoryPressureLocked();
				state_->PublishMemoryBudgetWorkLocked(a_requested);
			} else
				(void)state_->ServiceRetainedBackendLocked(a_requested);
		} catch (...) {
			state_->QuarantineAfterUnexpectedFailureLocked(
				RendererStage::Quarantined, Runtime::kFeatureSlotCount, false, 0);
		}
	}

	bool Renderer::HasPrewarmWork() const noexcept
	{
		return state_->prewarmPending_.load(std::memory_order_relaxed);
	}

	bool Renderer::ServicePrewarm(const PrewarmKey* request, const char* deferReason, std::uint64_t opportunity)
	{
		Util::RendererOwnership owner(Util::GetRendererContextLock(globals::game::renderer, globals::d3d::context));
		if (!owner)
			return false;
		std::unique_lock lock(state_->mutex_, std::try_to_lock);
		if (!lock.owns_lock())
			return false;
		try {
			state_->ServicePrewarmLocked(request, deferReason, opportunity);
		} catch (...) {
			state_->QuarantineAfterUnexpectedFailureLocked(RendererStage::ResourceCreation, Runtime::kFeatureSlotCount, false, 0);
		}
		return true;
	}

	void Renderer::State::ServicePrewarmLocked(const PrewarmKey* request, const char* deferReason, std::uint64_t opportunity)
	{
		auto& policy = prewarm_;
		if (!policy.BeginOpportunity(opportunity))
			return;
		if (failureLatched_ || quarantined_ || snapshot_.stallRecovery.pending) {
			policy.stage = "deferred";
			policy.reason = "renderer_fault";
			prewarmPending_.store(false, std::memory_order_relaxed);
			return;
		}
		const auto now = GetTickCount64();
		if (memoryRecovery_.phase == MemoryRecoveryPhase::Retiring) {
			(void)RetireMemoryPressureLocked();
			policy.failed = true;
			policy.reason = "memory_policy_retirement";
			return;
		}
		const auto fences = interop_.GetLifetimeSnapshot();
		if (fences.deviceRemoved) {
			(void)FailLocked(RendererStage::DeviceRemoved, DXGI_ERROR_DEVICE_REMOVED,
				"device removal detected while servicing optional prewarm", Runtime::kFeatureSlotCount, true, true, false);
			return;
		}
		const bool completed = !interop_.IsInitialized() ||
		                       (fences.completedKnown && !fences.recording && !fences.deviceRemoved && fences.completed >= fences.issued);
		const bool changed = request && policy.captured && *request != policy.key;
		const bool disabled = !request && std::string_view(deferReason) == "disabled";
		const bool expired = policy.Expired(now);
		const bool cancelled = !request && std::string_view(deferReason) != "awaiting_main_or_loading_menu";
		if (changed || cancelled)
			policy.CancelRequest();
		const bool retiring = policy.cancelled || expired || policy.failed;
		if (retiring && (policy.unusedMask || policy.backendOwned)) {
			policy.stage = "retiring";
			policy.reason = expired ? "unused_lease_expired" : (changed ? "request_changed" : (cancelled ? deferReason : policy.reason));
			if (!completed)
				return;
			for (std::uint32_t slot = 0; slot < slots_.size(); ++slot) {
				if (!(policy.unusedMask & FeatureSlotBit(slot)))
					continue;
				if (!Runtime::Instance().ResetFeature(slot)) {
					(void)FailLocked(RendererStage::ResourceRetirement, E_FAIL, "unused prewarm feature release failed", slot, true, true, false);
					return;
				}
				policy.unusedMask &= ~FeatureSlotBit(slot);
				return;
			}
			if (policy.backendOwned && !Runtime::Instance().GetResidentFeatureMask()) {
				const bool stop = policy.SuppressRetryAfterRetirement(now);
				const auto retiredPolicy = policy;
				const auto retiredReason = policy.reason;
				if (!TeardownBackendLocked(false, false, false))
					return;
				// A retired failed/expired request cannot restart optional work unchanged.
				policy.failed = policy.captured = stop;
				policy.key = retiredPolicy.key;
#ifdef DEVBENCH_BRIDGE_ENABLED
				policy.stages = retiredPolicy.stages;
				policy.stageMicroseconds = retiredPolicy.stageMicroseconds;
				policy.stageCpuMicroseconds = retiredPolicy.stageCpuMicroseconds;
#endif
				policy.stage = stop ? "deferred" : "disabled";
				policy.reason = retiredReason;
				return;
			}
		}
		if (completed && prewarmReservation_) {
			const auto sample = Util::GpuMemoryBudget::Get().Sample(device_.Get(), 0, true);
			if (sample.Fresh(GetTickCount64()))
				prewarmReservation_.Reset();
		}
		prewarmPending_.store(policy.unusedMask || policy.backendOwned || bool(prewarmReservation_), std::memory_order_relaxed);
		if (!request) {
			policy.stage = "deferred";
			policy.reason = deferReason;
			if (disabled && !policy.unusedMask && !policy.backendOwned)
				policy.captured = false;
			return;
		}
		if (policy.captured && *request == policy.key && (policy.failed || expired))
			return;
		if (!policy.captured || changed)
			policy.Capture(*request, now);
		if ((policy.preparedMask | policy.adoptedMask) == request->slotMask) {
			policy.stage = completed ? "ready" : "submitted";
			policy.reason = "native_features_only";
			return;
		}
		if (now < policy.retryAtMs || !completed)
			return;
		policy.retryAtMs = now + PrewarmPolicy::kRetryIntervalMs;
		if (memoryRecovery_.phase != MemoryRecoveryPhase::Ready || memoryConservation_.active || memoryRetirement_.backendRetained) {
			policy.reason = "memory_policy";
			return;
		}
		if (runtimeReady_ && !policy.backendOwned) {
			policy.stage = "deferred";
			policy.reason = "backend_already_in_use";
			return;
		}
		if (request->slotMask != 0xcu || !GetNativeEvaluationLayoutViolation(request->layout, false).empty()) {
			policy.reason = "unsupported_native_contract";
			policy.failed = true;
			return;
		}
		auto* device = reinterpret_cast<ID3D11Device*>(request->device);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(request->context);
		if (!device || !context || (device_ && (!SameIdentity(device_.Get(), device) || !SameIdentity(context_.Get(), context)))) {
			policy.reason = "device_changed";
			return;
		}
		// Reserve both opaque models and conservative first-pair transport overlap.
		const auto& extent = request->layout.creation.output;
		if (!extent.width || !extent.height || extent.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || extent.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
			policy.reason = "invalid_dimensions";
			policy.failed = true;
			return;
		}
		const auto bytes = 2 * MemoryRecoveryPolicy::kNativeFeatureReserve +
		                   std::uint64_t(extent.width) * extent.height * 128u;
		const auto budget = SampleMemoryBudgetLocked(device, GetTickCount64(), true);
		const auto sample = Util::GpuMemoryBudget::Get().Sample(device, MemoryRecoveryPolicy::kSampleIntervalMs);
		auto preview = memoryRecovery_;
		if (!budget.IsFresh(GetTickCount64()) || sample.priorityWork ||
			!preview.Admit(budget, bytes, GetTickCount64(), sample.pendingBytes)) {
			policy.stage = "deferred";
			policy.reason = "memory_admission";
			policy.failed = policy.backendOwned;
			return;
		}
		prewarmReservation_ = Util::GpuMemoryBudget::Get().Reserve(Util::GpuMemoryBudget::Owner::NeuralRendering, device, bytes);
		if (!prewarmReservation_) {
			policy.reason = "reservation_unavailable";
			return;
		}
		prewarmPending_.store(true, std::memory_order_relaxed);
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto stageIndex = !runtimeReady_ || !interop_.IsInitialized() ? 0u : ((policy.preparedMask & FeatureSlotBit(2)) ? 2u : 1u);
		const auto started = std::chrono::steady_clock::now();
		const SKSE::stl::scope_exit timing([&] {
			++policy.stages;
			const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
			policy.stageMicroseconds += elapsed;
			policy.stageCpuMicroseconds[stageIndex] += elapsed;
		});
#endif
		if (!runtimeReady_ || !interop_.IsInitialized()) {
			RendererApplyArgs args{};
			args.device = device;
			args.context = context;
			args.featureSlot = 2;
			policy.stage = "backend";
			policy.backendOwned = true;
			policy.failed = !EnsureBackendLocked(args, {});
			return;
		}
		PreparePrewarmFeatureLocked();
	}

	void Renderer::State::PreparePrewarmFeatureLocked()
	{
		auto& policy = prewarm_;
		for (std::uint32_t slot = 2; slot < Runtime::kFeatureSlotCount; ++slot) {
			if (policy.preparedMask & FeatureSlotBit(slot))
				continue;
			if (Runtime::Instance().GetResidentFeatureMask() & FeatureSlotBit(slot)) {
				if (!Runtime::Instance().HasCompatibleFeature(slot, policy.key.layout, false, policy.key.networkPreset)) {
					policy.reason = "live_incompatible_feature";
					policy.failed = true;
					return;
				}
				policy.preparedMask |= FeatureSlotBit(slot);
				continue;
			}
			ID3D12GraphicsCommandList* commands = nullptr;
			policy.stage = "creating";
			if (!interop_.BeginD3D12(&commands)) {
				policy.failed = true;
				(void)FailLocked(RendererStage::CommandBegin, interop_.LastError(), "prewarm command begin failed", slot, true, true, false);
				return;
			}
			if (!Runtime::Instance().PrepareFeature(commands, slot, policy.key.layout, nullptr, nullptr, false, nullptr, policy.key.networkPreset)) {
				const bool aborted = interop_.AbortD3D12();
				policy.failed = true;
				policy.stage = "failed";
				policy.reason = "native_creation_failed";
				policy.unusedMask |= Runtime::Instance().GetResidentFeatureMask() & FeatureSlotBit(slot);
				if (!aborted || FAILED(GetDeviceRemovalReasonLocked(S_OK)))
					(void)FailLocked(RendererStage::ResourceCreation, E_FAIL, "unsafe native prewarm failure", slot, true, true, false);
				else if (Runtime::Instance().CreationCapacityFailure() == CapacityFailure::Pressure)
					memoryRecovery_.Suspend(GetTickCount64(), true);
				logger::warn("[DLSSNR] Optional native prewarm deferred after creation failure: {}", Runtime::Instance().Detail());
				return;
			}
			policy.preparedMask |= FeatureSlotBit(slot);
			policy.unusedMask |= FeatureSlotBit(slot);
			if (!interop_.EndD3D12()) {
				policy.failed = true;
				(void)FailLocked(RendererStage::CommandEnd, interop_.LastError(), "prewarm submission failed", slot, true, true, false);
			}
			return;
		}
	}

	void Renderer::State::ApplyShaderInvalidationLocked()
	{
		const auto generation = shaderInvalidation_.Requested();
		if (generation == shaderInvalidation_.Applied())
			return;
		copyDepthGuideCS_.Reset();
		copyDepthGuideCompileFailed_ = false;
		actorProtectionCS_.Reset();
		actorProtectionCompileFailed_ = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		copyCompactDepthGuideCS_.Reset();
#endif
		colorPipeline_.InvalidateShaders();
		modelResolution_.InvalidateShaders();
		CharacterRendering::Instance().ResetShaderCache();
		// Generic HLSL reloads can change guide interpretation without changing native capacity.
		for (auto& slot : slots_)
			slot.historyValid = false;
		shaderInvalidation_.Applied(generation);
	}

	void Renderer::ResetShaderCache()
	{
		state_->shaderInvalidation_.Request();
	}

	RendererSnapshot Renderer::GetSnapshot() const
	{
		std::scoped_lock lock(state_->mutex_);
		auto result = state_->SnapshotLocked();
		const auto parallel = state_->interop_.GetParallelStatus();
		result.parallelEyesAvailable = parallel.available;
		result.parallelEyesDisabled = parallel.disabled;
		result.parallelEyesLastSubmission = parallel.lastSubmission;
		return result;
	}

	std::uint32_t Renderer::ModelResolutionCeiling() const noexcept
	{
		return state_->modelResolutionCeiling_.load(std::memory_order_relaxed);
	}

	Util::GpuMemoryBudget::Reservation Renderer::ReserveOutputAllocation(ID3D11Device* device, std::uint64_t bytes)
	{
		std::scoped_lock lock(state_->mutex_);
		if (!device || !bytes || state_->failureLatched_ || state_->quarantined_ ||
			(state_->device_ && !SameIdentity(state_->device_.Get(), device)))
			return {};
		if (state_->memoryRecovery_.phase == MemoryRecoveryPhase::Retiring) {
			state_->snapshot_.memoryRecovery = state_->memoryRecovery_;
			state_->PublishMemoryBudgetWorkLocked();
			state_->backendMaintenancePending_.store(true, std::memory_order_relaxed);
			return {};
		}
		auto& service = Util::GpuMemoryBudget::Get();
		const auto initial = service.Sample(device, MemoryRecoveryPolicy::kSampleIntervalMs);
		auto reservation = service.Reserve(Util::GpuMemoryBudget::Owner::NeuralRendering, device, bytes);
		if (!reservation)
			return {};
		const auto sample = state_->SampleMemoryBudgetLocked(device, GetTickCount64(), true);
		const auto shared = service.Sample(device, MemoryRecoveryPolicy::kSampleIntervalMs);
		const auto now = GetTickCount64();
		if (!sample.IsFresh(now) || !shared.Fresh(now) || shared.generation != initial.generation || shared.pendingBytes < reservation.Bytes())
			return {};
		// Keep all other tickets; required bytes count this replacement once.
		const auto pending = shared.pendingBytes - reservation.Bytes();
		if (!state_->memoryRecovery_.CanAllocateOutput(sample, bytes, now, pending)) {
			(void)state_->memoryRecovery_.Admit(sample, bytes, now, pending);
			state_->snapshot_.memoryRecovery = state_->memoryRecovery_;
			state_->PublishMemoryBudgetWorkLocked();
			if (state_->memoryRecovery_.phase == MemoryRecoveryPhase::Retiring)
				state_->backendMaintenancePending_.store(true, std::memory_order_relaxed);
			return {};
		}
		// Creation changes physical usage before the caller refreshes the shared observation.
		state_->memorySample_.valid = false;
		state_->nextMemorySampleMs_ = 0;
		return reservation;
	}

	bool Renderer::NotifyDlssMemoryPressure()
	{
		std::scoped_lock lock(state_->mutex_);
		if (state_->failureLatched_ || state_->quarantined_)
			return false;
		state_->memoryRecovery_.ReportDlssWarning(GetTickCount64());
		state_->snapshot_.memoryRecovery = state_->memoryRecovery_;
		state_->PublishMemoryBudgetWorkLocked();
		if (state_->memoryRecovery_.phase == MemoryRecoveryPhase::Retiring)
			state_->backendMaintenancePending_.store(true, std::memory_order_relaxed);
		state_->nextMemorySampleMs_ = 0;
		return true;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	nlohmann::json Renderer::GetSourceTransportDiagnostics() const
	{
		using json = nlohmann::json;
		std::scoped_lock lock(state_->mutex_);
		json textures = json::array(), slots = json::array();
		std::uint64_t inputBytes = 0, outputBytes = 0, colorBytes = 0, modelBytes = 0, retiredLeaseBytes = 0;
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
			const auto model = state_->modelResolution_.RetainedBytes(static_cast<std::uint32_t>(index));
			modelBytes += model.value_or(0);
			bytesKnown &= model.has_value();
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
				{ "modelResolutionPrivateLogicalBytes", model ? json(*model) : json(nullptr) },
				{ "compactStorage", slot.resourceKey.compact },
				{ "compactMinimumSide", state_->compactRetention_[index].minimumSide },
				{ "compactFullCoordinateFallback", state_->compactRetention_[index].fullCoordinates },
				{ "nativeResident", (nativeMask & (1u << index)) != 0 },
				{ "inputIdentity", std::to_string(reinterpret_cast<std::uintptr_t>(slot.color.resource12.Get())) },
				{ "privateOutputIdentity", std::to_string(reinterpret_cast<std::uintptr_t>(slot.output.resource12.Get())) },
				{ "historyIdentity", slot.historyKey.regionIdentity }, { "historyValid", slot.historyValid },
				{ "packedCrops", PackedCropLayoutJson(slot.historyKey.modelResolutionHistory ? slot.historyKey.modelResolutionHistory->packedCrops : PackedCropLayout{}) },
				{ "sourceTransportOwnerSlot", index },
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
		auto admission = json::array();
		for (std::uint32_t slot = 0; slot < state_->packedCropAdmission_.size(); ++slot) {
			const auto& value = state_->packedCropAdmission_[slot];
			if (value.frame != std::numeric_limits<std::uint32_t>::max())
				admission.push_back(PackedCropAdmissionJson(value, slot));
		}
		return { { "schemaVersion", 4 }, { "ownership", "private_per_eye" },
			{ "lastModelOutputSelected", state_->snapshot_.modelOutputSelected },
			{ "capacityFallback", { { "active", state_->capacityFallback_.rejected }, { "recoveries", state_->capacityFallback_.recoveries },
									  { "reason", state_->capacityFallback_.reason == CapacityFailure::Pressure ? "pressure" : state_->capacityFallback_.reason == CapacityFailure::Unsupported ? "unsupported" :
																																																  "none" },
									  { "policy", "one_enclosing_context_per_eye_after_fenced_recoverable_failure_until_explicit_nr_reset" } } },
			{ "maximumPhysicalContexts", Runtime::kFeatureSlotCount }, { "allocationPolicy", "lazy_exact_capacity_no_speculative_prewarm" },
			{ "activeSlotMask", activeMask }, { "cachedSlotMask", cachedMask }, { "nativeSlotMask", nativeMask },
			{ "sharedInputLogicalBytes", inputBytes }, { "privateOutputLogicalBytes", outputBytes }, { "privateColorLogicalBytes", colorBytes },
			{ "modelResolutionPrivateLogicalBytes", modelBytes },
			{ "packedCoverageGuard", state_->modelResolution_.GetPackedCoverageDiagnostics() },
			{ "packedCropAdmission", std::move(admission) },
			{ "packedCropDiagnostics", PackedCropDiagnosticsJson(state_->modelResolution_.CropDiagnostics()) },
			{ "activeTransportLogicalBytes", activeBytes }, { "cachedOnlyTransportLogicalBytes", cachedBytes },
			{ "activePrivateColorLogicalBytes", activeColorBytes }, { "cachedPrivateColorLogicalBytes", cachedColorBytes },
			{ "leaseOnlyRetainedLogicalBytes", retiredLeaseBytes }, { "leaseCount", leases.size() }, { "logicalBytesKnown", bytesKnown },
			{ "logicalByteScope", "unique_transport_private_color_and_model_resolution_textures_excludes_exposure_queries_buffers_and_native_allocations" },
			{ "nativeAllocationBytes", nullptr }, { "nativeAllocationBytesReason", "provider_does_not_expose_residency_bytes" },
			{ "quarantined", state_->quarantined_ }, { "physicalResidencyMeasured", false },
			{ "nativeResidencyKnown", !state_->quarantined_ },
			{ "abandonedOwnershipBytes", nullptr }, { "abandonedOwnershipPresent", state_->quarantined_ },
			{ "recovery", "explicit_nr_reset_after_successful_both_api_retirement_or_new_process" },
			{ "resources", textures }, { "slots", slots } };
	}

	void Renderer::ConfigurePackedCropDiagnostics(std::optional<bool> enabled, bool clear)
	{
		std::scoped_lock lock(state_->mutex_);
		state_->modelResolution_.CropDiagnostics().Configure(enabled, clear);
	}

	nlohmann::json Renderer::GetPackedCropDiagnostics() const
	{
		std::scoped_lock lock(state_->mutex_);
		return PackedCropDiagnosticsJson(state_->modelResolution_.CropDiagnostics());
	}

	LifetimeSnapshot Renderer::GetLifetimeDiagnostics() const
	{
		std::scoped_lock lock(state_->mutex_);
		return state_->lifetimeDiagnostics_.Snapshot();
	}

	bool Renderer::SimulateTransientStall()
	{
		std::scoped_lock lock(state_->mutex_);
		if (state_->failureLatched_ || state_->quarantined_ ||
			state_->snapshot_.stallRecovery.attempts >= StallRecoveryPolicy::kMaximumAttempts)
			return false;
		const auto failuresBefore = state_->snapshot_.counters.failures;
		try {
			if (!state_->interop_.SimulateBackpressureTimeout())
				return false;
			logger::warn("[DLSSNR][Recovery] DevBench injected a logical timeout; hardware fence completion remains required");
			(void)state_->FailLocked(RendererStage::CommandBegin, HRESULT_FROM_WIN32(ERROR_TIMEOUT),
				"DevBench simulated transient backpressure timeout", Runtime::kFeatureSlotCount, true);
			return true;
		} catch (...) {
			state_->QuarantineAfterUnexpectedFailureLocked(RendererStage::CommandBegin, Runtime::kFeatureSlotCount, true, failuresBefore);
			return false;
		}
	}

	bool Renderer::SimulateMemoryPressure(std::uint32_t a_durationMilliseconds, bool a_conservationOnly)
	{
		if (a_durationMilliseconds > kMaximumMemorySimulationMilliseconds)
			return false;
		std::scoped_lock lock(state_->mutex_);
		if (a_durationMilliseconds && (state_->quarantined_ || state_->failureLatched_))
			return false;
		state_->simulatedPressureUntilMs_ = a_durationMilliseconds ? GetTickCount64() + a_durationMilliseconds : 0;
		state_->simulateConservationOnly_ = a_conservationOnly;
		state_->nextMemorySampleMs_ = 0;
		return true;
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
