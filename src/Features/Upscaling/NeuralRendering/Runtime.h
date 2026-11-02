#pragma once

#include "CapacityFallback.h"
#include "DriverSupport.h"
#include "NetworkSelection.h"
#include "NgxCapabilityInfo.h"
#include "RegionCapacity.h"
#include "Style.h"
#include "TuningLimits.h"

#include "ExecutionEvidence.h"

#include "NativeEvaluationLayout.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace NeuralRendering
{
	class D3D12Interop;
	enum class RuntimeStatus
	{
		NotProbed,
		NotFound,
		HashFailed,
		VersionRejected,
		LoadFailed,
		MissingExport,
		IdentityFailed,
		Ready,
		InitializationFailed,
		ParameterAllocationFailed,
		Initialized,
		FeatureConfigurationChanged,
		FeatureCreateFailed,
		FeatureEvaluateFailed,
		FeatureReleaseFailed,
		ShutdownFailed,
		UnsafeAbandoned
	};

	enum class RuntimeTrust
	{
		Unknown,
		VersionAccepted
	};

	enum class RuntimeFailureStage
	{
		None,
		Discovery,
		Hash,
		Version,
		Load,
		Identity,
		Initialization,
		ParameterAllocation,
		FeatureConfiguration,
		FeatureCreate,
		FeatureEvaluate,
		FeatureRelease,
		Shutdown,
		UnsafeAbandon
	};

	struct Tuning
	{
		std::uint32_t networkPreset = kNetworkDefault;
		float intensity = 0.8f;
		float localToneStrength = 0.75f;
		float localStructureStrength = 0.9f;
		float skinStructureStrength = 0.9f;
		std::uint32_t style = kDefaultStyle;
		bool useAutoMask = true;
		bool uiCorrection = false;
		bool depthInverted = false;
		// Private Feature 18 single-rectangle compute ROI.  One means the
		// full resource and remains the production default.
		float singleSubrectScale = 1.0f;
	};

	class Runtime
	{
	public:
		// Main and submit routes retain one independent feature per eye.
		static constexpr std::size_t kFeatureSlotCount = kLogicalFeatureSlotCount;

		static Runtime& Instance();

		/** Discover the optional provider once per process; installation requires a restart. */
		[[nodiscard]] static bool IsInstalled() noexcept;
		/** Cached rendering-adapter driver check; does not load or initialize NGX. */
		[[nodiscard]] static const DriverSupport& GetDriverSupport() noexcept;
		static constexpr const char* kMissingRuntimeNotice =
			"Missing DLL: Shaders/Upscaling/Streamline/nvngx_dlssnr.dll";
		static constexpr const char* kUserRuntimeNotice =
			"NR runtime is not included. Install your own compatible nvngx_dlssnr.dll (310.8 or newer) "
			"under Shaders/Upscaling/Streamline under its license terms, then restart.";
		static constexpr const char* kUnsupportedHardwareNotice =
			"Incompatible GPU detected. Neural Rendering requires an NVIDIA GPU.";

		Runtime(const Runtime&) = delete;
		Runtime& operator=(const Runtime&) = delete;
		~Runtime();

		bool Probe(const std::filesystem::path& a_explicitPath = {});
		bool Initialize(ID3D12Device* a_device, const std::filesystem::path& a_dataPath = {});
		/** Records one evaluation from explicit creation/valid/storage domains.
		 *  The optional attempt flag distinguishes pre-evaluation failures. */
		bool Execute(
			ID3D12GraphicsCommandList* a_commandList,
			std::uint32_t a_slot,
			ID3D12Resource* a_color,
			ID3D12Resource* a_depth,
			ID3D12Resource* a_motionVectors,
			ID3D12Resource* a_output,
			ID3D12Resource* a_controlMask,
			const NativeEvaluationLayout& a_layout,
			const Tuning& a_tuning,
			bool a_reset,
			bool* a_evaluationAttempted = nullptr,
			RuntimeExecutionEvidence* a_evidence = nullptr,
			D3D12Interop* a_timingInterop = nullptr,
			std::uint32_t a_timingRegion = 0,
			bool a_providerBlending = false);

		/** Record exact feature creation without inference; caller owns submission and retirement. */
		bool PrepareFeature(ID3D12GraphicsCommandList*, std::uint32_t slot,
			const NativeEvaluationLayout&, ID3D12Resource* color = nullptr,
			ID3D12Resource* controlMask = nullptr, bool providerBlending = false,
			RuntimeExecutionEvidence* evidence = nullptr, std::uint32_t networkPreset = kNetworkDefault);
		/** Consume first-use reset only after the renderer commits the complete real transaction. */
		void AcknowledgePresentedFeatures(std::uint32_t slotMask);
		bool ResetFeature(std::uint32_t a_slot);
		bool ResetFeatures();
		/** Releases features and parameter bindings after the caller drains both APIs.
		 *  Retains only a healthy initialized backend; failure forbids warm reuse. */
		[[nodiscard]] bool ReleasePassResources();
		bool Shutdown();
		/** Permanently detaches unsafe NGX ownership without releasing it. */
		void AbandonUnsafe() noexcept;

		[[nodiscard]] RuntimeStatus Status() const;
		[[nodiscard]] RuntimeTrust Trust() const;
		[[nodiscard]] RuntimeFailureStage FailureStage() const;
		[[nodiscard]] std::filesystem::path Path() const;
		[[nodiscard]] std::string Hash() const;
		[[nodiscard]] std::string Version() const;
		/** Cached full provider version for session allocation estimates, including before initialization. */
		[[nodiscard]] std::uint64_t AllocationVersion() const;
		/** Resolve network limits from probed metadata, or cached installed metadata before initialization. */
		[[nodiscard]] TuningLimits GetTuningLimits(std::uint32_t selection = kNetworkDefault) const;
		/** Whether the probed provider, or installed metadata before probing, supports explicit networks. */
		[[nodiscard]] bool SupportsNetworkSelection() const;
		/** Resolves a saved selection without changing provider ownership or loading another DLL. */
		[[nodiscard]] std::uint32_t GetNetworkPreset(std::uint32_t selection) const;
		/** Formats the latest successful evaluation on demand, or returns the current lifecycle/failure detail. */
		[[nodiscard]] std::string Detail() const;
		[[nodiscard]] std::uint32_t NgxResult() const;
		/** Last provider-reported driver requirements, including failed query results. */
		[[nodiscard]] NgxDriverRequirements DriverRequirements() const;
		/** Only documented creation failures permit a safely retired smaller retry. */
		[[nodiscard]] CapacityFailure CreationCapacityFailure() const;
		[[nodiscard]] std::uint64_t SuccessfulFrames() const;
		/** Query only at safe lifecycle boundaries; warm evaluations never collect statistics. */
		[[nodiscard]] NativeMemorySnapshot QueryMemorySnapshot() const;

#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Selected initialization route and whether SDK discovery was attempted. */
		[[nodiscard]] std::pair<std::string, std::string> GetInitializationRoute() const;
		/** Explicit diagnostic query only; does not create features or change memory admission. */
		[[nodiscard]] NgxAllocationStatistics QueryAllocationStatistics() const;
		struct LifecycleCounts
		{
			std::uint64_t creates = 0, releases = 0;
		};
		/** Monotonic native-call counts for cache/prewarm comparisons. */
		[[nodiscard]] LifecycleCounts GetLifecycleCounts() const;
#endif
		/** Resident handles only; provider allocation statistics are aggregate. */
		[[nodiscard]] std::uint32_t GetResidentFeatureMask() const;
		/** Checks creation compatibility without changing a resident handle. */
		[[nodiscard]] bool HasCompatibleFeature(std::uint32_t slot, const NativeEvaluationLayout&, bool providerBlending,
			std::uint32_t networkPreset = kNetworkDefault) const;
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Actual creation presets by logical slot; UINT32_MAX denotes no valid configured feature. */
		[[nodiscard]] std::array<std::uint32_t, kFeatureSlotCount> GetFeatureNetworkPresets() const;
#endif

	private:
		struct FeatureConfiguration
		{
			std::uint32_t networkPreset = kNetworkDefault;
			UpscalingDLSS::Extent colorBacking{};
			NativeCreationExtents creation{};
			bool featureUpscaling = false;
			bool providerBlending = false;
			bool nativeControlMask = false;
			bool valid = false;

			[[nodiscard]] bool Matches(const NativeEvaluationLayout& a_layout) const;
		};

		struct EvaluationSuccessDetail
		{
			std::uint32_t slot = 0;
			bool featureUpscaling = false;
			UpscalingDLSS::Extent color{}, guides{}, output{}, controlMask{};
			ComputeSubrect outputSubrect{};
		};

		struct Backend;

		Runtime();

		bool PrepareScratchLocked(std::uint32_t slot);
		bool PrepareFeatureLocked(ID3D12GraphicsCommandList*, std::uint32_t,
			const NativeEvaluationLayout&, ID3D12Resource*, ID3D12Resource*, bool,
			RuntimeExecutionEvidence*, std::uint32_t);
		bool ProbeLocked(const std::filesystem::path& a_explicitPath);
		bool ResetFeatureLocked(std::uint32_t a_slot);
		bool ResetFeaturesLocked(bool a_stopOnFailure = false);
		bool ShutdownLocked();
		void AbandonLocked() noexcept;
		void LogOnceLocked(bool& a_emitted, const char* a_operation, bool a_succeeded);
		void SetFailureLocked(RuntimeStatus a_status, RuntimeFailureStage a_stage, std::string a_detail, std::uint32_t a_ngxResult = 0);
		[[nodiscard]] std::string DetailLocked() const;
		[[nodiscard]] NgxAllocationStatistics QueryAllocationStatisticsLocked() const;

		mutable std::recursive_mutex mutex_;
		std::filesystem::path path_;
		std::string hash_;
		std::string version_;
		std::uint64_t allocationVersion_ = 0;
		std::optional<TuningLimits> tuningLimits_;
		std::optional<bool> networkSelectionSupported_;
		NgxDriverRequirements driverRequirements_;
		std::string initializationRoute_ = "NGX_D3D12_DIRECT_PROVIDER";
		std::string discoveryDetail_;
		std::string detail_;
		std::optional<EvaluationSuccessDetail> evaluationSuccessDetail_;
		std::unique_ptr<Backend> backend_;
		void* device_ = nullptr;
		void* parameters_ = nullptr;
		std::array<void*, kFeatureSlotCount> featureHandles_{};
		std::array<FeatureConfiguration, kFeatureSlotCount> featureConfigurations_{};
		std::atomic<std::uint32_t> firstEvaluationResetMask_{ 0 };
#ifdef DEVBENCH_BRIDGE_ENABLED
		LifecycleCounts lifecycleCounts_{};
#endif
		RuntimeStatus status_ = RuntimeStatus::NotProbed;
		RuntimeTrust trust_ = RuntimeTrust::Unknown;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		std::uint32_t ngxResult_ = 0;
		std::uint64_t successfulFrames_ = 0;
		bool probeLogEmitted_ = false;
		bool initializationLogEmitted_ = false;
		bool featureCreateLogEmitted_ = false;
		bool featureEvaluateLogEmitted_ = false;
		std::atomic_bool abandonRequested_{ false };
		bool abandoned_ = false;
	};

	[[nodiscard]] const char* ToString(RuntimeStatus a_status);
	[[nodiscard]] const char* ToString(RuntimeTrust a_trust);
	[[nodiscard]] const char* ToString(RuntimeFailureStage a_stage);
}
