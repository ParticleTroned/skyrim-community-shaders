#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"
#include "Features/Upscaling/NeuralRendering/NativeEvaluationLayout.h"
#include "Features/Upscaling/NeuralRendering/NetworkSelection.h"
#include "Features/Upscaling/NeuralRendering/RegionCapacity.h"
#include "Features/Upscaling/NeuralRendering/Style.h"
#include "Features/Upscaling/NeuralRendering/TuningLimits.h"
#include "neural_sdk_parameters_under_test.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Util
{
	struct TestRuntimeVersion
	{
		unsigned majorValue = 310, minorValue = 9;
		unsigned major() const { return majorValue; }
		unsigned minor() const { return minorValue; }
		unsigned patch() const { return 0; }
		unsigned build() const { return 0; }
	};
	std::optional<TestRuntimeVersion> installedVersion = TestRuntimeVersion{};
	bool versionReadFails = false;
	unsigned versionReads = 0;
	std::optional<TestRuntimeVersion> GetDllVersion(const std::wstring&)
	{
		++versionReads;
		if (versionReadFails)
			throw std::runtime_error("version resource unavailable");
		return installedVersion;
	}
}
namespace logger
{
	unsigned discoveryWarnings = 0;
	struct SuccessLog
	{
		std::string operation, detail;
	};
	std::vector<SuccessLog> successLogs;
	void info(std::string_view, const char* operation, std::string detail)
	{
		successLogs.push_back({ operation, std::move(detail) });
	}
	template <class... Args>
	void error(Args&&...)
	{}
	template <class... Args>
	void warn(Args&&...)
	{
		++discoveryWarnings;
	}
}

namespace NeuralRendering
{
	bool runtimePresent = true, discoveryFails = false;
	unsigned discoveryCalls = 0;
	std::filesystem::path ResolveRuntimePath(const std::filesystem::path&)
	{
		++discoveryCalls;
		if (discoveryFails)
			throw std::runtime_error("runtime path unavailable");
		return runtimePresent ? std::filesystem::path("nvngx_dlssnr.dll") : std::filesystem::path{};
	}
#include "neural_installed_metadata_under_test.h"

	struct ID3D12GraphicsCommandList
	{};
	struct ID3D12Resource
	{};
	constexpr std::int32_t E_INVALIDARG = -1, E_PENDING = -2;
	constexpr unsigned kFeatureDlssNr = 18;
	constexpr int NVSDK_NGX_DLSSNR_Mask_Blend_Mode_None = 0, NVSDK_NGX_DLSSNR_Mask_Blend_Mode_Min = 2;
	constexpr bool NVSDK_NGX_FAILED(int result) { return result != 0; }
	struct NVSDK_NGX_Handle
	{};
	struct NVSDK_NGX_Parameter
	{
		std::map<std::string, std::uint64_t> values;
		void Reset() { values.clear(); }
		void Set(const char* key, int value) { values[key] = static_cast<std::uint64_t>(value); }
		void Set(const char* key, unsigned long long value) { values[key] = value; }
		void Set(const char* key, std::uint32_t value) { values[key] = value; }
		void Set(const char* key, float value) { values[key] = std::bit_cast<std::uint32_t>(value); }
		void Set(const char* key, ID3D12Resource* value) { values[key] = reinterpret_cast<std::uintptr_t>(value); }
	};
	struct MonoInterop
	{
		void BeginGpuTiming(std::uint64_t, std::uint32_t) {}
		void EndGpuTiming() {}
	};
	enum class RuntimeStatus
	{
		Initialized,
		InitializationFailed,
		ShutdownFailed,
		FeatureConfigurationChanged,
		FeatureCreateFailed,
		FeatureEvaluateFailed,
		FeatureReleaseFailed,
		MissingExport
	};
	enum class RuntimeFailureStage
	{
		None,
		Discovery,
		Hash,
		Version,
		Identity,
		Initialization,
		ParameterAllocation,
		FeatureConfiguration,
		FeatureCreate,
		FeatureEvaluate,
		FeatureRelease,
		Shutdown,
		Load
	};
	template <class T>
	const char* ToString(T)
	{
		return "test";
	}
	auto DiagnosticNow() { return std::chrono::steady_clock::now(); }
	struct RuntimeExecutionEvidence
	{
		bool createAttempted = false, createSucceeded = false, evaluateAttempted = false, evaluateSucceeded = false;
		const char* createReason = nullptr;
		std::optional<std::uint32_t> createResult, evaluateResult;
		std::optional<std::uint64_t> createCpuMicroseconds, evaluateCpuMicroseconds;
	};
	struct Tuning
	{
		std::uint32_t networkPreset = kNetworkDefault;
		float intensity = .8f, localToneStrength = .75f, localStructureStrength = .9f, skinStructureStrength = .9f;
		std::uint32_t style = kDefaultStyle;
		bool useAutoMask = true, uiCorrection = false, depthInverted = false;
		float singleSubrectScale = 1;
	};
	std::vector<std::map<std::string, std::uint64_t>> creates, evaluations;
	int providerCreateResult = 0, providerEvaluateResult = 0;
	bool returnHandle = true;
	NVSDK_NGX_Handle providerHandle;
	int NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList*, unsigned feature, NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** output)
	{
		if (feature != kFeatureDlssNr)
			throw std::runtime_error("wrong feature");
		creates.push_back(parameters->values);
		*output = returnHandle ? &providerHandle : nullptr;
		return providerCreateResult;
	}
	int NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList*, NVSDK_NGX_Handle*, NVSDK_NGX_Parameter* parameters, void*)
	{
		evaluations.push_back(parameters->values);
		return providerEvaluateResult;
	}
	int providerReleaseResult = 0;
	unsigned featureReleases = 0;
	int NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle*)
	{
		++featureReleases;
		return providerReleaseResult;
	}
	struct Runtime
	{
		static constexpr unsigned kFeatureSlotCount = kLogicalFeatureSlotCount;
		struct FeatureConfiguration
		{
			std::uint32_t networkPreset = kNetworkDefault;
			UpscalingDLSS::Extent colorBacking{};
			NativeCreationExtents creation{};
			bool featureUpscaling = false, providerBlending = false, nativeControlMask = false, valid = false;
			bool Matches(const NativeEvaluationLayout&) const;
		};
#include "neural_evaluation_success_detail_under_test.h"
		std::optional<TuningLimits> tuningLimits_ = TuningLimits{};
		std::optional<bool> networkSelectionSupported_ = true;
		TuningLimits GetTuningLimits(std::uint32_t = kNetworkDefault) const;
		bool SupportsNetworkSelection() const;
		std::uint32_t GetNetworkPreset(std::uint32_t) const;
		mutable std::recursive_mutex mutex_;
		std::atomic<bool> abandonRequested_ = false;
		bool abandoned_ = false;
		RuntimeStatus status_ = RuntimeStatus::Initialized;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		std::array<void*, kFeatureSlotCount> featureHandles_{};
		std::array<FeatureConfiguration, kFeatureSlotCount> featureConfigurations_{};
		std::atomic<std::uint32_t> firstEvaluationResetMask_{ 0 };
		struct Scratch
		{
			ID3D12Resource* Get() const { return nullptr; }
			void Reset() {}
		};
		struct Backend
		{
			NVSDK_NGX_Parameter* capabilities = nullptr;
			std::array<Scratch, kFeatureSlotCount> scratch;
			std::array<std::size_t, kFeatureSlotCount> scratchBytes{};
		} backendStorage;
		Backend* backend_ = &backendStorage;
		bool scratchReady = true;
		bool PrepareScratchLocked(unsigned) { return scratchReady; }
		NVSDK_NGX_Parameter parameterStorage;
		void* parameters_ = &parameterStorage;
		void* device_ = nullptr;
		bool probeLogEmitted_ = false, initializationLogEmitted_ = false;
		bool featureCreateLogEmitted_ = false, featureEvaluateLogEmitted_ = false;
		unsigned ngxResult_ = 0;
		std::uint64_t successfulFrames_ = 0;
		int trust_ = 0;
		std::string detail_;
		std::optional<EvaluationSuccessDetail> evaluationSuccessDetail_;
		void SetFailureLocked(RuntimeStatus status, RuntimeFailureStage stage, std::string detail, unsigned result = 0);
		void LogOnceLocked(bool& emitted, const char* operation, bool succeeded);
		std::string DetailLocked() const;
		std::string Detail() const;
		bool ResetFeatureLocked(std::uint32_t);
		bool ResetFeaturesLocked(bool = false);
		bool ResetFeatures();
		bool ReleasePassResources();
		bool PrepareFeature(ID3D12GraphicsCommandList*, unsigned, const NativeEvaluationLayout&, ID3D12Resource* = nullptr, ID3D12Resource* = nullptr, bool = false, RuntimeExecutionEvidence* = nullptr, std::uint32_t = kNetworkDefault);
		bool PrepareFeatureLocked(ID3D12GraphicsCommandList*, unsigned, const NativeEvaluationLayout&, ID3D12Resource*, ID3D12Resource*, bool, RuntimeExecutionEvidence*, std::uint32_t);
		void AcknowledgePresentedFeatures(unsigned);
		bool Execute(ID3D12GraphicsCommandList*, unsigned, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, const NativeEvaluationLayout&, const Tuning&, bool, bool* = nullptr, RuntimeExecutionEvidence* = nullptr, MonoInterop* = nullptr, std::uint64_t = 0, unsigned = 0, bool = false);
	};
#include "neural_native_creation_under_test.h"
#include "neural_native_key_under_test.h"
#include "neural_runtime_detail_under_test.h"
#include "neural_runtime_logging_under_test.h"
#include "neural_runtime_reset_under_test.h"
#include "neural_runtime_retirement_under_test.h"
#include "neural_runtime_tuning_under_test.h"
}

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void RuntimeTuningMetadata()
	{
		using namespace NeuralRendering;
		runtimePresent = false;
		const auto absent = DiscoverInstalledRuntimeMetadata();
		Require(!absent.installed && absent.tuningLimits.intensity == 2 && Util::versionReads == 0,
			"missing providers must retain the original ranges without reading version metadata");
		discoveryFails = true;
		Require(!DiscoverInstalledRuntimeMetadata().installed && logger::discoveryWarnings == 1,
			"discovery failures must remain observable and leave the optional provider unavailable");
		discoveryFails = false;
		runtimePresent = true;
		Util::installedVersion.reset();
		const auto unreadable = DiscoverInstalledRuntimeMetadata();
		Require(unreadable.installed && unreadable.tuningLimits.localStructureStrength == 2,
			"unreadable metadata must not infer a restricted version");
		Util::versionReadFails = true;
		Require(DiscoverInstalledRuntimeMetadata().installed && logger::discoveryWarnings == 2,
			"version-resource failures must preserve file presence and report discovery failure");
		Util::versionReadFails = false;
		for (const auto& [major, minor, intensity, structure] : {
				 std::array{ 310u, 8u, 1u, 2u }, std::array{ 310u, 9u, 1u, 1u },
				 std::array{ 310u, 90u, 2u, 2u }, std::array{ 311u, 9u, 2u, 2u } }) {
			Util::installedVersion = Util::TestRuntimeVersion{ major, minor };
			const auto metadata = DiscoverInstalledRuntimeMetadata();
			Require(metadata.networkSelectionSupported == (major == 310 && minor == 9),
				"explicit networks must be restricted to the known runtime family");
			Require(ResolveNetworkPreset(kNetworkDefault, metadata.networkSelectionSupported) ==
						(major == 310 && minor == 9 ? kNetworkNewer : kNetworkDefault),
				"Default must follow the installed DLL family without selecting another DLL");
			Require(metadata.installed && metadata.tuningLimits.intensity == intensity &&
						metadata.tuningLimits.localToneStrength == 2 && metadata.tuningLimits.localStructureStrength == structure &&
						metadata.tuningLimits.skinStructureStrength == structure,
				"numeric major/minor metadata must select only the matching provider family");
		}
		Util::installedVersion = Util::TestRuntimeVersion{ 310, 9 };
		Runtime runtime;
		runtime.tuningLimits_.reset();
		Require(GetInstalledRuntimeMetadata().installed && runtime.GetTuningLimits().localStructureStrength == 1,
			"availability and pre-initialization limits must share installed metadata");
		const auto reads = Util::versionReads;
		const auto calls = discoveryCalls;
		Util::installedVersion = Util::TestRuntimeVersion{ 310, 8 };
		Require(runtime.GetTuningLimits().localStructureStrength == 1 && Util::versionReads == reads && discoveryCalls == calls,
			"installed metadata must remain cached until restart");
		runtime.tuningLimits_ = GetTuningLimitsForVersion(310, 8);
		Require(runtime.GetTuningLimits().localStructureStrength == 2 && runtime.GetTuningLimits().intensity == 1,
			"probed metadata must override the installed-file snapshot");
		runtime.tuningLimits_ = GetTuningLimitsForVersion(310, 10);
		Require(runtime.GetTuningLimits().intensity == 2, "a probed unknown version must not inherit the installed version's restrictions");
		runtime.tuningLimits_.reset();
		Require(runtime.GetTuningLimits().localStructureStrength == 1 && Util::versionReads == reads,
			"retirement must restore cached installation limits without another filesystem query");
		runtime.networkSelectionSupported_.reset();
		Require(runtime.SupportsNetworkSelection(), "unprobed network support must use cached installed metadata");
		Require(runtime.GetNetworkPreset(kNetworkDefault) == kNetworkNewer,
			"Default (310.9) must explicitly select the newer network");
		runtime.networkSelectionSupported_ = false;
		Require(!runtime.SupportsNetworkSelection() && runtime.GetNetworkPreset(kNetworkLegacy) == kNetworkDefault,
			"probed unsupported networks must resolve to default even with supported installed metadata");
	}
	void NativeCreation()
	{
		using namespace NeuralRendering;
		static_assert(Runtime::kFeatureSlotCount == 1);
		const UpscalingDLSS::Extent extent{ 320, 192 };
		const auto layout = BuildNativeEvaluationLayout(extent, extent, extent, {}, { 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		Tuning tuning;
		const auto run = [&](Runtime& runtime, const NativeEvaluationLayout& demand) {
			return runtime.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, demand, tuning, false);
		};
		Runtime lazy, warm;
		Require(run(lazy, layout), "lazy mono creation failed");
		const auto lazyParameters = creates.back();
		const auto evaluationsBefore = evaluations.size();
		Require(warm.PrepareFeature(&commands, 0, layout), "mono prewarm failed");
		Require(creates.back() == lazyParameters && warm.successfulFrames_ == 0 && evaluations.size() == evaluationsBefore,
			"prewarm changed creation parameters or evaluated a fabricated frame");
		Require(lazyParameters.at("Width") == 320 && lazyParameters.at("DLSSNR.Height") == 192 && !lazyParameters.contains("DLSSNR.Upscaling"), "creation contract changed");
		const auto created = creates.size();
		Require(warm.PrepareFeature(&commands, 0, layout) && run(warm, layout) && creates.size() == created,
			"first mono use recreated the feature");
		Require(evaluations.back().at("DLSSNR.Reset") == 1 && run(warm, layout) && evaluations.back().at("DLSSNR.Reset") == 1,
			"unpresented evaluation consumed the first-use reset");
		warm.AcknowledgePresentedFeatures(2u);
		Require(warm.firstEvaluationResetMask_.load() == 1u, "invalid slot acknowledgement consumed reset");
		warm.AcknowledgePresentedFeatures(1u);
		tuning.intensity = 1.7f;
		Require(run(warm, layout) && evaluations.back().at("DLSSNR.Reset") == 0 && creates.size() == created,
			"tuning recreated a feature or repeated first-use reset");
		Require(evaluations.back().at("DLSSNR.Intensity") == std::bit_cast<std::uint32_t>(1.7f), "cached feature lost tuning");
		Runtime moving;
		const UpscalingDLSS::Extent backing{ 2048, 1024 };
		const auto demand = [&](ComputeSubrect region) {
			return BuildNativeEvaluationLayout(backing, backing, backing, {}, region, { true, 2048.f, 1024.f }, false);
		};
		Require(run(moving, demand({ 240, 80, 1600, 880 })), "mono ROI creation failed");
		const auto initialCreates = creates.size();
		for (unsigned frame = 0; frame < 32; ++frame) {
			const auto rect = frame % 2 ? ComputeSubrect{ 0, 0, 640 + (frame % 3) * 64, 512 } : ComputeSubrect{ 240 + frame, 80, 1500, 880 };
			Require(run(moving, demand(rect)) && creates.size() == initialCreates, "moving mono rectangles recreated capacity");
			Require(evaluations.back().at("DLSSNR.OutputSubrectWidth") == rect.width && evaluations.back().at("DLSSNR.OutputSubrectBaseX") == rect.baseX,
				"retained feature ignored live valid rectangles");
		}
		auto changed = BuildNativeEvaluationLayout({ 640, 192 }, { 640, 192 }, { 640, 192 }, {}, { 0, 0, 640, 192 }, { true, 640.f, 192.f }, false);
		Require(!warm.PrepareFeature(&commands, 0, changed) && warm.featureHandles_[0], "live incompatible capacity was replaced");
		Runtime failed;
		providerCreateResult = 1;
		Require(!failed.PrepareFeature(&commands, 0, layout) && failed.featureHandles_[0] && !failed.featureConfigurations_[0].valid,
			"failed non-null handle was lost or advertised reusable");
		providerCreateResult = 0;
		Runtime missing;
		missing.scratchReady = false;
		const auto beforeScratchFailure = creates.size();
		Require(!missing.PrepareFeature(&commands, 0, layout) && creates.size() == beforeScratchFailure, "scratch failure called provider");
		Runtime failedEvaluation;
		Require(failedEvaluation.PrepareFeature(&commands, 0, layout), "failure fixture preparation failed");
		providerEvaluateResult = 1;
		Require(!run(failedEvaluation, layout) && failedEvaluation.firstEvaluationResetMask_.load() == 1u, "evaluation failure consumed reset");
		providerEvaluateResult = 0;
		Runtime invalidSlot;
		Require(!invalidSlot.PrepareFeature(&commands, 1, layout), "mono runtime accepted a second view");
	}

	void DeferredEvaluationDetail()
	{
		using namespace NeuralRendering;
		const UpscalingDLSS::Extent extent{ 320, 192 };
		const auto layout = BuildNativeEvaluationLayout(extent, extent, extent, {}, { 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		Runtime runtime;
		runtime.device_ = &resource;
		const auto run = [&] { return runtime.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, layout, Tuning{}, false); };
		Require(run() && runtime.detail_.empty(), "successful evaluation eagerly formatted its detail");
		const auto detail = runtime.Detail();
		Require(detail == "Feature 18 evaluated slot=0 upscaling=false color=320x192 guides=320x192 output=320x192 controlMask=0x0 subrect=(0,0 320x192)",
			"on-demand detail lost mono evaluation geometry");
		Require(logger::successLogs.back().operation == "evaluate" && logger::successLogs.back().detail == detail, "first success logging lost detail");
		providerEvaluateResult = -7;
		Require(!run() && !runtime.evaluationSuccessDetail_ && runtime.Detail().starts_with("Feature 18 evaluate failed slot=0 "),
			"evaluation failure was hidden behind success evidence");
		providerEvaluateResult = 0;
		Require(runtime.ResetFeatureLocked(0) && run() && runtime.Detail() == detail, "recovery retained stale failure evidence");
	}

	void RetiredFailureRecovery()
	{
		using namespace NeuralRendering;
		const UpscalingDLSS::Extent extent{ 320, 192 };
		const auto layout = BuildNativeEvaluationLayout(extent, extent, extent, {}, { 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		for (const bool returnsHandle : { false, true }) {
			Runtime runtime;
			runtime.device_ = &resource;
			returnHandle = returnsHandle;
			providerCreateResult = -7;
			Require(!runtime.PrepareFeature(&commands, 0, layout) && runtime.failureStage_ == RuntimeFailureStage::FeatureCreate,
				"create failure did not establish the retirement fixture");
			const auto releases = featureReleases;
			providerCreateResult = 0;
			returnHandle = true;
			Require(runtime.ReleasePassResources() && !runtime.featureHandles_[0] && !runtime.featureConfigurations_[0].valid &&
						runtime.parameterStorage.values.empty() && runtime.status_ == RuntimeStatus::Initialized &&
						runtime.failureStage_ == RuntimeFailureStage::None && runtime.firstEvaluationResetMask_.load() == 0 &&
						featureReleases == releases + unsigned(returnsHandle),
				"a safely retired failed create could not recover without abandoning the runtime");
			Require(runtime.PrepareFeature(&commands, 0, layout), "retired failed creation could not retry");
		}
		Runtime evaluation;
		evaluation.device_ = &resource;
		providerEvaluateResult = -9;
		Require(!evaluation.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, layout, Tuning{}, false),
			"evaluate failure did not establish the retirement fixture");
		providerEvaluateResult = 0;
		Require(evaluation.ReleasePassResources() && evaluation.PrepareFeature(&commands, 0, layout),
			"a safely retired failed evaluation could not release its handle and recover");

		Runtime release;
		release.device_ = &resource;
		Require(release.PrepareFeature(&commands, 0, layout), "release-failure fixture preparation failed");
		providerReleaseResult = -11;
		Require(!release.ReleasePassResources() && release.featureHandles_[0] &&
					release.failureStage_ == RuntimeFailureStage::FeatureRelease,
			"failed native release discarded owned handle or reported successful retirement");
		const auto releases = featureReleases;
		providerReleaseResult = 0;
		Require(!release.ReleasePassResources() && featureReleases == releases && release.featureHandles_[0],
			"failed release was retried without the owner's quarantine decision");

		Runtime rollback;
		rollback.parameters_ = nullptr;
		rollback.status_ = RuntimeStatus::InitializationFailed;
		rollback.failureStage_ = RuntimeFailureStage::Initialization;
		Require(rollback.ReleasePassResources() && featureReleases == releases &&
					rollback.failureStage_ == RuntimeFailureStage::Initialization,
			"an empty initialization rollback was quarantined or lost its original diagnostic");
		rollback.backendStorage.capabilities = &rollback.parameterStorage;
		Require(!rollback.ReleasePassResources(), "partial initialization ownership was treated as an empty rollback");

		Runtime shutdown;
		shutdown.device_ = &resource;
		shutdown.status_ = RuntimeStatus::ShutdownFailed;
		shutdown.failureStage_ = RuntimeFailureStage::Shutdown;
		Require(!shutdown.ReleasePassResources() && featureReleases == releases && shutdown.device_,
			"partial SDK shutdown was advertised as a reusable runtime");
		Runtime abandoned;
		abandoned.abandonRequested_.store(true);
		Require(!abandoned.ReleasePassResources(), "unsafe ownership was released through the warm cleanup path");
	}

	void NetworkCreation()
	{
		using namespace NeuralRendering;
		const UpscalingDLSS::Extent extent{ 320, 192 };
		const auto layout = BuildNativeEvaluationLayout(extent, extent, extent, {}, { 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		for (const auto selection : { kNetworkDefault, kNetworkLegacy, kNetworkNewer }) {
			Runtime warm, lazy;
			const auto network = warm.GetNetworkPreset(selection);
			warm.tuningLimits_ = lazy.tuningLimits_ = GetTuningLimitsForVersion(310, 9);
			Tuning tuning;
			tuning.networkPreset = network;
			tuning.localStructureStrength = 1.75f;
			Require(warm.PrepareFeature(&commands, 0, layout, nullptr, nullptr, false, nullptr, network), "network prewarm failed");
			const auto parameters = creates.back();
			Require(parameters.at("DLSSNR.Hint.Render.Preset") == network &&
						warm.featureConfigurations_[0].networkPreset == network,
				"provider hint and resident identity must match the selected network");
			const auto run = [&](Runtime& runtime) {
				return runtime.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, layout, tuning, false);
			};
			const auto created = creates.size();
			Require(run(warm) && creates.size() == created, "matching prewarm must retain the selected network");
			Require(run(lazy) && creates.back() == parameters, "lazy and prewarmed networks must use identical creation parameters");
			Require(evaluations.back().at("DLSSNR.LocalStructureStrength") == std::bit_cast<std::uint32_t>(network == kNetworkLegacy ? 1.75f : 1.0f),
				"native dispatch must obey the selected network's tuning limits");
			tuning.networkPreset = (network + 1) % 3;
			const auto evaluated = evaluations.size();
			Require(!run(warm) && evaluations.size() == evaluated && warm.featureHandles_[0] &&
						warm.status_ == RuntimeStatus::FeatureConfigurationChanged,
				"a live network change must require renderer retirement before any evaluation");
		}
		Runtime unsupported;
		unsupported.networkSelectionSupported_ = false;
		const auto created = creates.size();
		Require(!unsupported.PrepareFeature(&commands, 0, layout, nullptr, nullptr, false, nullptr, kNetworkNewer) &&
					creates.size() == created && !unsupported.featureHandles_[0] &&
					unsupported.status_ == RuntimeStatus::FeatureCreateFailed &&
					unsupported.failureStage_ == RuntimeFailureStage::FeatureConfiguration,
			"unsupported native networks must fail before calling the provider");
		Require(!unsupported.PrepareFeature(&commands, 0, layout) && creates.size() == created,
			"failed creation must require runtime recovery before another attempt");
		Runtime defaultNetwork;
		defaultNetwork.networkSelectionSupported_ = false;
		Require(defaultNetwork.PrepareFeature(&commands, 0, layout) &&
					creates.back().at("DLSSNR.Hint.Render.Preset") == kNetworkDefault,
			"initialized unsupported families must retain default creation");
		for (const auto selection : { kNetworkDefault, kNetworkLegacy, kNetworkNewer }) {
			Runtime legacy;
			legacy.networkSelectionSupported_ = false;
			legacy.tuningLimits_ = GetTuningLimitsForVersion(310, 8);
			Tuning tuning;
			tuning.networkPreset = legacy.GetNetworkPreset(selection);
			tuning.localStructureStrength = 1.75f;
			Require(tuning.networkPreset == kNetworkDefault &&
						legacy.PrepareFeature(&commands, 0, layout, nullptr, nullptr, false, nullptr, tuning.networkPreset) &&
						creates.back().at("DLSSNR.Hint.Render.Preset") == kNetworkDefault,
				"A 310.8 DLL must keep native default for every saved network choice");
			const auto createdLegacy = creates.size();
			Require(legacy.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, layout, tuning, false) &&
						creates.size() == createdLegacy &&
						evaluations.back().at("DLSSNR.LocalStructureStrength") == std::bit_cast<std::uint32_t>(1.75f),
				"310.8 defaults must preserve their range and reuse the prepared model");
		}
	}

}
void DepthEncoding()
{
	using namespace NeuralRendering;
	const UpscalingDLSS::Extent extent{ 320, 192 };
	const auto layout = BuildNativeEvaluationLayout(extent, extent, extent, {}, { 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
	ID3D12GraphicsCommandList commands;
	ID3D12Resource resource;
	for (bool reverse : { false, true }) {
		Runtime runtime;
		Tuning tuning;
		tuning.depthInverted = reverse;
		Require(runtime.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, layout, tuning, false, nullptr, nullptr, nullptr, 0, 0, false), "Depth encoding dispatch failed");
		Require(evaluations.back().at("DLSSNR.DepthInverted") == unsigned(reverse), "NR provider received the wrong guide encoding");
	}
}

namespace
{
	void SdkMaskAndParameterConsistency()
	{
		using namespace NeuralRendering;
		Runtime runtime;
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		auto layout = BuildNativeEvaluationLayout({ 640, 384 }, { 320, 192 }, { 640, 384 }, { 640, 384 },
			{ 0, 0, 640, 384 }, { true, 320.f, 192.f }, true);
		layout.nativeControlMask = true;
		layout.jitterPixels = { 0.25f, -0.375f };
		Tuning tuning;
		tuning.useAutoMask = true;
		Require(runtime.PrepareFeature(&commands, 0, layout, &resource, &resource, true), "Native mask preparation failed");
		const auto creation = creates.back();
		Require(runtime.Execute(&commands, 0, &resource, &resource, &resource, &resource, &resource,
					layout, tuning, false, nullptr, nullptr, nullptr, 0, 0, true),
			"Native mask evaluation failed");
		const auto& evaluation = evaluations.back();
		Require(evaluation.at("DLSSNR.ControlMask") == reinterpret_cast<std::uintptr_t>(&resource) &&
					evaluation.at("DLSSNR.MaskBlendMode") == 2 && evaluation.at("DLSSNR.UseAutoMask") == 1,
			"RGB strength mask did not combine with the skin automask using Min");
		Require(!evaluation.contains("DLSSNR.UIAlpha") && evaluation.at("DLSSNR.UICorrection") == 0,
			"Native strength masking leaked into UI protection");
		Require(evaluation.at("DLSSNR.ScalingRatio") == creation.at("DLSSNR.ScalingRatio") &&
					evaluation.at("DLSSNR.ScalingRatio") == std::bit_cast<std::uint32_t>(1.0f),
			"Guide resolution changed the inference budget or parameter reset changed its ratio");
		Require(evaluation.at(NVSDK_NGX_Parameter_Jitter_Offset_X) == std::bit_cast<std::uint32_t>(0.25f) &&
					evaluation.at(NVSDK_NGX_Parameter_Jitter_Offset_Y) == std::bit_cast<std::uint32_t>(-0.375f),
			"Explicit native jitter was lost before NGX evaluation");
		layout.nativeControlMask = false;
		Require(!runtime.PrepareFeature(&commands, 0, layout, &resource, &resource, true),
			"A live mask-ABI change skipped safe feature retirement");
	}

	void MonoGuideCapacity()
	{
		using namespace NeuralRendering;
		const UpscalingDLSS::Extent source{ 1919, 1079 }, model{ 960, 540 };
		for (const auto guides : { UpscalingDLSS::Extent{ 1279, 719 }, UpscalingDLSS::Extent{ 2879, 1619 } }) {
			Runtime runtime;
			ID3D12GraphicsCommandList commands;
			ID3D12Resource resource;
			const auto nativeGuides = BuildMonoGuideExtent(source, guides, model);
			const auto layout = BuildNativeEvaluationLayout(model, nativeGuides, model, {}, { 0, 0, model.width, model.height },
				{ true, float(nativeGuides.width), float(nativeGuides.height) }, nativeGuides != model);
			Require(runtime.PrepareFeature(&commands, 0, layout), "Mono scaled guides failed to prewarm");
			const auto count = creates.size();
			Require(runtime.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, layout, Tuning{}, true),
				"Mono scaled guides failed to evaluate");
			Require(creates.size() == count, "Mono prewarm and evaluation disagreed on guide capacity");
			const auto& parameters = evaluations.back();
			Require(parameters.at("DLSSNR.ColorSubrectWidth") == model.width && parameters.at("DLSSNR.OutputSubrectWidth") == model.width &&
						parameters.at("DLSSNR.DepthSubrectWidth") == nativeGuides.width && parameters.at("DLSSNR.DepthSubrectHeight") == nativeGuides.height &&
						parameters.at("DLSSNR.MVecSubrectWidth") == nativeGuides.width,
				"SDK guide rectangles followed the color grid instead of their retained mono capacity");
			Require(parameters.at("DLSSNR.ScalingRatio") == std::bit_cast<std::uint32_t>(1.0f),
				"SE render scale changed the SDK color inference ratio");
		}
	}

	void InvalidSdkInputs()
	{
		using namespace NeuralRendering;
		const UpscalingDLSS::Extent extent{ 320, 192 };
		const auto layout = BuildNativeEvaluationLayout(extent, extent, extent, {}, { 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		for (unsigned field = 0; field < 6; ++field) {
			Runtime runtime;
			Tuning tuning;
			const auto nan = std::bit_cast<float>(0x7fc00000u);
			switch (field) {
			case 0:
				tuning.intensity = nan;
				break;
			case 1:
				tuning.localToneStrength = nan;
				break;
			case 2:
				tuning.localStructureStrength = nan;
				break;
			case 3:
				tuning.skinStructureStrength = nan;
				break;
			case 4:
				tuning.singleSubrectScale = nan;
				break;
			case 5:
				tuning.style = kMaximumStyle + 1;
				break;
			}
			const auto before = creates.size();
			Require(!runtime.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, layout, tuning, false),
				"Invalid tuning reached the SDK");
			Require(creates.size() == before, "Invalid tuning created a native feature");
		}
	}
}

int main()
{
	try {
		RuntimeTuningMetadata();
		NativeCreation();
		DeferredEvaluationDetail();
		RetiredFailureRecovery();
		NetworkCreation();
		DepthEncoding();
		SdkMaskAndParameterConsistency();
		MonoGuideCapacity();
		InvalidSdkInputs();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
