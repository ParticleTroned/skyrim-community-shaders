#pragma once

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
	enum class RuntimeStatus
	{
		NotProbed,
		NotFound,
		HashFailed,
		TrustRejected,
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
		SignedAllowlisted,
		PatchedAllowlisted
	};

	enum class RuntimeFailureStage
	{
		None,
		Discovery,
		Hash,
		Trust,
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
		RuntimeHashAllowlisted,
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
	};

	class Runtime
	{
	public:
		static constexpr std::size_t kFeatureSlotCount = 4;
		static constexpr std::string_view kPatchedRuntimeSha256 = "8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206";
		static constexpr std::string_view kAlternatePatchedRuntimeSha256 = "CEB6432F6FBDF44D886014BCD47241932BF8B67439FEEF9BBDD0961436662650";
		static constexpr std::string_view kSignedRuntimeSha256 = "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E";

		static Runtime& Instance();

		Runtime(const Runtime&) = delete;
		Runtime& operator=(const Runtime&) = delete;
		~Runtime();

		bool Probe(const std::filesystem::path& a_explicitPath = {});
		bool Initialize(ID3D12Device* a_device, const std::filesystem::path& a_dataPath = {});
		/** Records whether execution reached the Feature 18 vendor evaluation call. */
		bool Execute(
			ID3D12GraphicsCommandList* a_commandList,
			std::uint32_t a_slot,
			ID3D12Resource* a_color,
			ID3D12Resource* a_depth,
			ID3D12Resource* a_motionVectors,
			ID3D12Resource* a_output,
			std::uint32_t a_colorWidth,
			std::uint32_t a_colorHeight,
			std::uint32_t a_guideWidth,
			std::uint32_t a_guideHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			float a_motionVectorScaleX,
			float a_motionVectorScaleY,
			bool a_featureUpscaling,
			const Tuning& a_tuning,
			bool a_reset,
			bool* a_evaluationAttempted = nullptr);

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
		[[nodiscard]] std::uint64_t SuccessfulFrames() const;
		[[nodiscard]] std::uint32_t LastPathProxyHits() const;
		[[nodiscard]] bool LastPathProxyInstalled() const;

	private:
		struct FeatureConfiguration
		{
			std::uint32_t colorWidth = 0;
			std::uint32_t colorHeight = 0;
			std::uint32_t guideWidth = 0;
			std::uint32_t guideHeight = 0;
			std::uint32_t outputWidth = 0;
			std::uint32_t outputHeight = 0;
			bool featureUpscaling = false;
			bool valid = false;

			[[nodiscard]] bool Matches(
				std::uint32_t a_colorWidth,
				std::uint32_t a_colorHeight,
				std::uint32_t a_guideWidth,
				std::uint32_t a_guideHeight,
				std::uint32_t a_outputWidth,
				std::uint32_t a_outputHeight,
				bool a_featureUpscaling) const;
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
