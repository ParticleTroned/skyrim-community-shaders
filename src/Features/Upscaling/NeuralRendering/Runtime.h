#pragma once

#include "CapacityFallback.h"
#include "RegionCapacity.h"

#include "ExecutionEvidence.h"

#include "NativeEvaluationLayout.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

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
		CoreUnavailable,
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
		ParameterCore,
		ParameterAllocation,
		FeatureConfiguration,
		FeatureCreate,
		FeatureEvaluate,
		FeatureRelease,
		Shutdown,
		UnsafeAbandon
	};

	enum class ParameterCoreTrust
	{
		Unknown,
		RuntimeIdentityMatched,
		AuthenticodeVerified
	};

	enum class ParameterCoreSource
	{
		None,
		Runtime,
		DriverStore
	};

	struct Tuning
	{
		float intensity = 0.8f;
		float localToneStrength = 0.75f;
		float localStructureStrength = 0.9f;
		float skinStructureStrength = 0.9f;
		std::uint32_t style = 3;
		bool useAutoMask = true;
		bool uiCorrection = false;
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
		static constexpr const char* kMissingRuntimeNotice =
			"Missing DLL: Shaders/Upscaling/Streamline/nvngx_dlssnr.dll";
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

		bool ResetFeature(std::uint32_t a_slot);
		bool ResetFeatures();
		bool Shutdown();
		/** Permanently detaches unsafe NGX ownership without releasing it. */
		void AbandonUnsafe() noexcept;

		[[nodiscard]] RuntimeStatus Status() const;
		[[nodiscard]] RuntimeTrust Trust() const;
		[[nodiscard]] RuntimeFailureStage FailureStage() const;
		[[nodiscard]] std::filesystem::path Path() const;
		[[nodiscard]] std::string Hash() const;
		[[nodiscard]] std::string Version() const;
		[[nodiscard]] std::filesystem::path ParameterCorePath() const;
		[[nodiscard]] std::string ParameterCoreHash() const;
		[[nodiscard]] ParameterCoreTrust CoreTrust() const;
		[[nodiscard]] ParameterCoreSource CoreSource() const;
		[[nodiscard]] std::string Detail() const;
		[[nodiscard]] std::uint32_t NgxResult() const;
		/** Only documented creation failures permit a safely retired smaller retry. */
		[[nodiscard]] CapacityFailure CreationCapacityFailure() const;
		[[nodiscard]] std::uint64_t SuccessfulFrames() const;
		[[nodiscard]] std::uint32_t LastPathProxyHits() const;
		[[nodiscard]] bool LastPathProxyInstalled() const;
		/** Resident handles only; NVIDIA does not expose their allocation byte size. */
		[[nodiscard]] std::uint32_t GetResidentFeatureMask() const;

	private:
		struct FeatureConfiguration
		{
			UpscalingDLSS::Extent colorBacking{};
			NativeCreationExtents creation{};
			bool featureUpscaling = false;
			bool providerBlending = false;
			bool valid = false;

			[[nodiscard]] bool Matches(const NativeEvaluationLayout& a_layout) const;
		};

		struct RuntimeExports
		{
			void* initialize = nullptr;
			void* createFeature = nullptr;
			void* evaluateFeature = nullptr;
			void* releaseFeature = nullptr;
			void* shutdown = nullptr;
		};

		struct ParameterCoreExports
		{
			void* allocateParameters = nullptr;
			void* destroyParameters = nullptr;
		};

		Runtime() = default;

		bool ProbeLocked(const std::filesystem::path& a_explicitPath);
		bool ResetFeatureLocked(std::uint32_t a_slot);
		bool ResetFeaturesLocked();
		bool ShutdownLocked();
		void AbandonLocked() noexcept;
		void LogOnceLocked(bool& a_emitted, const char* a_operation, bool a_succeeded);
		void SetFailureLocked(RuntimeStatus a_status, RuntimeFailureStage a_stage, std::string a_detail, std::uint32_t a_ngxResult = 0);

		mutable std::recursive_mutex mutex_;
		std::filesystem::path path_;
		std::string hash_;
		std::string version_;
		std::filesystem::path corePath_;
		std::string coreHash_;
		ParameterCoreTrust coreTrust_ = ParameterCoreTrust::Unknown;
		ParameterCoreSource coreSource_ = ParameterCoreSource::None;
		std::string detail_;
		void* module_ = nullptr;
		void* coreModule_ = nullptr;
		void* coreFile_ = nullptr;
		void* device_ = nullptr;
		void* parameters_ = nullptr;
		RuntimeExports runtimeExports_{};
		ParameterCoreExports parameterCoreExports_{};
		std::array<void*, kFeatureSlotCount> featureHandles_{};
		std::array<FeatureConfiguration, kFeatureSlotCount> featureConfigurations_{};
		RuntimeStatus status_ = RuntimeStatus::NotProbed;
		RuntimeTrust trust_ = RuntimeTrust::Unknown;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		std::uint32_t ngxResult_ = 0;
		std::uint32_t applicationId_ = 0;
		std::uint32_t apiVersion_ = 0;
		std::uint32_t lastPathProxyHits_ = 0;
		std::uint64_t successfulFrames_ = 0;
		bool lastPathProxyInstalled_ = false;
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
	[[nodiscard]] const char* ToString(ParameterCoreTrust a_trust);
	[[nodiscard]] const char* ToString(ParameterCoreSource a_source);
}
