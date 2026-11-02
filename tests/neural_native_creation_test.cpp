#include "Features/Upscaling/NeuralRendering/NativeEvaluationLayout.h"
#include "Features/Upscaling/NeuralRendering/NetworkSelection.h"
#include "Features/Upscaling/NeuralRendering/PrewarmPolicy.h"
#include "Features/Upscaling/NeuralRendering/ShaderInvalidation.h"
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
	int providerPopulationResult = 0;
	unsigned providerPopulationCalls = 0;
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
	struct D3D12Interop
	{
		void BeginEvaluationTiming(ID3D12GraphicsCommandList*, unsigned) {}
		void EndEvaluationTiming(ID3D12GraphicsCommandList*, unsigned) {}
	};
	enum class RuntimeStatus
	{
		Initialized,
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
	int NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle*) { return 0; }
	struct Runtime
	{
		static constexpr unsigned kFeatureSlotCount = 4;
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
			struct Provider
			{
				int PopulateParameters(NVSDK_NGX_Parameter* parameters)
				{
					++providerPopulationCalls;
					parameters->Set("provider_defaults", 1u);
					return providerPopulationResult;
				}
				decltype(&NVSDK_NGX_D3D12_CreateFeature) create = &NVSDK_NGX_D3D12_CreateFeature;
				decltype(&NVSDK_NGX_D3D12_EvaluateFeature) evaluate = &NVSDK_NGX_D3D12_EvaluateFeature;
				decltype(&NVSDK_NGX_D3D12_ReleaseFeature) release = &NVSDK_NGX_D3D12_ReleaseFeature;
			} provider;
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
		bool PrepareFeature(ID3D12GraphicsCommandList*, unsigned, const NativeEvaluationLayout&, ID3D12Resource* = nullptr, ID3D12Resource* = nullptr, bool = false, RuntimeExecutionEvidence* = nullptr, std::uint32_t = kNetworkDefault);
		bool PrepareFeatureLocked(ID3D12GraphicsCommandList*, unsigned, const NativeEvaluationLayout&, ID3D12Resource*, ID3D12Resource*, bool, RuntimeExecutionEvidence*, std::uint32_t);
		void AcknowledgePresentedFeatures(unsigned);
		bool Execute(ID3D12GraphicsCommandList*, unsigned, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, const NativeEvaluationLayout&, const Tuning&, bool, bool* = nullptr, RuntimeExecutionEvidence* = nullptr, D3D12Interop* = nullptr, unsigned = 0, bool = false);
	};
#include "neural_native_creation_under_test.h"
#include "neural_native_key_under_test.h"
#include "neural_runtime_detail_under_test.h"
#include "neural_runtime_logging_under_test.h"
#include "neural_runtime_reset_under_test.h"
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
		const UpscalingDLSS::Extent extent{ 320, 192 };
		const auto layout = BuildNativeEvaluationLayout(extent, extent, extent, {}, { 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		Tuning tuning;
		const auto run = [&](Runtime& runtime, unsigned slot, const NativeEvaluationLayout& demand, ID3D12Resource* mask = nullptr, bool provider = false) {
			return runtime.Execute(&commands, slot, &resource, &resource, &resource, &resource, mask, demand, tuning, false, nullptr, nullptr, nullptr, 0, provider);
		};
		Runtime rejectedPopulation;
		const auto createsBeforePopulationFailure = creates.size();
		providerPopulationResult = 7;
		Require(!run(rejectedPopulation, 2, layout) && rejectedPopulation.ngxResult_ == 7 && creates.size() == createsBeforePopulationFailure,
			"Failed provider population reached native feature creation");
		providerPopulationResult = 0;
		Runtime lazy, warm;
		Require(run(lazy, 2, layout), "lazy creation failed");
		const auto lazyParameters = creates.back();
		const auto evaluationsBefore = evaluations.size();
		Require(warm.PrepareFeature(&commands, 2, layout), "prewarm failed");
		Require(creates.back() == lazyParameters, "lazy/prewarm creation parameter mismatch");
		Require(lazyParameters.at("Width") == 320 && lazyParameters.at("DLSSNR.Height") == 192 && lazyParameters.at("provider_defaults") == 1 && !lazyParameters.contains("DLSSNR.Upscaling"), "creation contract changed");
		Require(warm.successfulFrames_ == 0 && evaluations.size() == evaluationsBefore, "prewarm evaluated a fabricated frame");
		const auto created = creates.size();
		const auto populations = providerPopulationCalls;
		Require(warm.PrepareFeature(&commands, 2, layout) && run(warm, 2, layout), "first use failed");
		Require(creates.size() == created && providerPopulationCalls == populations && evaluations.back().at("DLSSNR.Reset") == 1, "first use recreated, repopulated or lost reset");
		Require(run(warm, 2, layout) && evaluations.back().at("DLSSNR.Reset") == 1, "unpresented transaction consumed reset");
		warm.AcknowledgePresentedFeatures(1u << 3);
		Require((warm.firstEvaluationResetMask_.load() & 4u), "peer-only acknowledgement consumed reset");
		warm.AcknowledgePresentedFeatures(1u << 2);
		tuning.intensity = 1.7f;
		Require(run(warm, 2, layout) && evaluations.back().at("DLSSNR.Reset") == 0 && creates.size() == created, "tuning recreated or repeated first-use reset");
		Require(evaluations.back().at("DLSSNR.Intensity") == std::bit_cast<std::uint32_t>(1.7f), "cached feature lost live tuning");
		const auto previousTuning = tuning;
		for (unsigned minor : { 8u, 9u, 10u }) {
			const auto limits = GetTuningLimitsForVersion(310, minor);
			warm.tuningLimits_ = limits;
			tuning.intensity = tuning.localToneStrength = tuning.localStructureStrength = tuning.skinStructureStrength = 1.75f;
			Require(run(warm, 2, layout), "version-specific tuning dispatch failed");
			for (const auto& [key, maximum] : {
					 std::pair{ "DLSSNR.Intensity", limits.intensity },
					 std::pair{ "DLSSNR.LocalToneStrength", limits.localToneStrength },
					 std::pair{ "DLSSNR.LocalStructureStrength", limits.localStructureStrength },
					 std::pair{ "DLSSNR.SkinStructureStrength", limits.skinStructureStrength } })
				Require(evaluations.back().at(key) == std::bit_cast<std::uint32_t>(std::min(1.75f, maximum)), "provider received tuning outside its version-specific range");
		}
		tuning = previousTuning;
		warm.tuningLimits_ = TuningLimits{};
		for (bool upscale : { false, true }) {
			Runtime moving;
			const UpscalingDLSS::Extent backing{ 2048, 1024 }, guides = upscale ? UpscalingDLSS::Extent{ 1365, 683 } : backing;
			const auto demand = [&](ComputeSubrect region) {
				return BuildNativeEvaluationLayout(backing, guides, backing, backing, region,
					{ true, float(guides.width), float(guides.height) }, upscale);
			};
			for (unsigned eye = 0; eye < 2; ++eye)
				Require(run(moving, eye, demand({ 240, 80, 1600, 880 }), &resource, true), "Single ROI creation failed");
			const auto initialCreates = creates.size();
			for (unsigned frame = 0; frame < 32; ++frame) {
				for (unsigned eye = 0; eye < 2; ++eye) {
					const auto rect = (frame + eye) % 2 ? ComputeSubrect{ 0, 0, 640 + (frame % 3) * 64, 512 } :
					                                      ComputeSubrect{ 240 + frame, 80, 1500, 880 };
					const auto projected = demand(rect);
					Require(run(moving, eye, projected, &resource, true) && creates.size() == initialCreates,
						"Moving single/atlas rectangles recreated a native feature");
					Require(evaluations.back().at("DLSSNR.OutputSubrectWidth") == rect.width &&
								evaluations.back().at("DLSSNR.OutputSubrectBaseX") == rect.baseX &&
								evaluations.back().at("DLSSNR.DepthSubrectWidth") == projected.depth.valid.width &&
								evaluations.back().at("DLSSNR.UIAlphaSubrectHeight") == rect.height,
						"Retained native feature ignored changed valid rectangles");
				}
			}
		}
		const auto createsAfterTransitions = creates.size();
		auto changed = BuildNativeEvaluationLayout({ 640, 192 }, { 640, 192 }, { 640, 192 }, {}, { 0, 0, 640, 192 }, { true, 640.f, 192.f }, false);
		Require(!warm.PrepareFeature(&commands, 2, changed) && creates.size() == createsAfterTransitions && warm.featureHandles_[2], "live incompatible feature was replaced");
		Runtime failed;
		providerCreateResult = 1;
		Require(!failed.PrepareFeature(&commands, 2, layout) && failed.featureHandles_[2] && !failed.featureConfigurations_[2].valid, "failed non-null handle was lost or advertised reusable");
		providerCreateResult = 0;
		Runtime missing;
		missing.scratchReady = false;
		const auto beforeScratchFailure = creates.size();
		Require(!missing.PrepareFeature(&commands, 2, layout) && creates.size() == beforeScratchFailure, "scratch failure called provider");
		missing.scratchReady = true;
		Runtime blendLazy, blendWarm, noBindings;
		auto blendLayout = layout;
		blendLayout.controlMask = layout.output;
		Require(!noBindings.PrepareFeature(&commands, 2, blendLayout, nullptr, nullptr, true), "provider prewarm accepted missing resources");
		Require(run(blendLazy, 2, blendLayout, &resource, true), "provider lazy creation failed");
		const auto blendedParameters = creates.back();
		Require(blendWarm.PrepareFeature(&commands, 2, blendLayout, &resource, &resource, true) && creates.back() == blendedParameters, "provider binding parameters differ");
		Runtime failedPair;
		Require(failedPair.PrepareFeature(&commands, 2, layout) && failedPair.PrepareFeature(&commands, 3, layout), "pair preparation failed");
		Require(run(failedPair, 2, layout), "left evaluation failed");
		providerEvaluateResult = 1;
		Require(!run(failedPair, 3, layout) && (failedPair.firstEvaluationResetMask_.load() & 12u) == 12u, "right failure consumed pair reset");
		providerEvaluateResult = 0;
	}
	void DeferredEvaluationDetail()
	{
		using namespace NeuralRendering;
		const UpscalingDLSS::Extent extent{ 320, 192 }, output{ 640, 384 };
		const auto first = BuildNativeEvaluationLayout(extent, extent, extent, {},
			{ 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
		auto latest = BuildNativeEvaluationLayout(output, extent, output, output,
			{ 32, 16, 512, 320 }, { true, 320.f, 192.f }, true);
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		Runtime runtime;
		runtime.device_ = &resource;
		const auto run = [&](unsigned slot, const NativeEvaluationLayout& layout, ID3D12Resource* mask = nullptr) {
			Tuning tuning;
			tuning.useAutoMask = mask == nullptr;
			return runtime.Execute(&commands, slot, &resource, &resource, &resource, &resource, mask, layout, tuning, false);
		};
		const auto successLogBegin = logger::successLogs.size();
		Require(run(0, first) && runtime.detail_.empty(), "successful evaluation eagerly retained a formatted message");
		const auto firstDetail = runtime.Detail();
		Require(firstDetail == "Feature 18 evaluated slot=0 upscaling=false color=320x192 guides=320x192 output=320x192 controlMask=0x0 subrect=(0,0 320x192)",
			"first evaluation lost its detail contract");
		Require(logger::successLogs.back().operation == "evaluate" && logger::successLogs.back().detail == firstDetail,
			"first-success logging did not resolve the evaluation detail");

		Require(run(1, latest, &resource) && runtime.detail_.empty(), "peer evaluation eagerly retained a formatted message");
		latest = {};
		const auto latestDetail = runtime.Detail();
		Require(latestDetail == "Feature 18 evaluated slot=1 upscaling=true color=640x384 guides=320x192 output=640x384 controlMask=640x384 subrect=(32,16 512x320)",
			"on-demand detail did not retain the latest evaluation's own dimensions, slot and subrect");
		Require(std::count_if(logger::successLogs.begin() + successLogBegin, logger::successLogs.end(),
					[](const auto& log) { return log.operation == "evaluate"; }) == 1,
			"a peer success repeated the once-only evaluation log");
		Require(runtime.ResetFeatureLocked(0) && runtime.Detail() == latestDetail,
			"resetting another slot invalidated a retained successful evaluation");
		Require(runtime.ResetFeatureLocked(1) && runtime.Detail().empty() && !runtime.evaluationSuccessDetail_,
			"reset retained a stale evaluation detail");

		Require(run(0, first), "evaluation after reset failed");
		providerEvaluateResult = -7;
		Require(!run(0, first) && !runtime.evaluationSuccessDetail_ &&
					runtime.status_ == RuntimeStatus::FeatureEvaluateFailed && runtime.failureStage_ == RuntimeFailureStage::FeatureEvaluate &&
					runtime.ngxResult_ == static_cast<unsigned>(-7) && runtime.Detail().starts_with("Feature 18 evaluate failed slot=0 "),
			"evaluation failure was hidden by the previous success detail");
		providerEvaluateResult = 0;
		Require(runtime.ResetFeatureLocked(0) && run(0, first) && runtime.Detail() == firstDetail,
			"recovery did not replace the failure detail with the current evaluation");
		Require(runtime.PrepareFeature(&commands, 2, first) && !runtime.evaluationSuccessDetail_ &&
					runtime.Detail().starts_with("Feature 18 created slot=2 "),
			"feature creation retained an earlier evaluation detail");
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
			Require(warm.PrepareFeature(&commands, 2, layout, nullptr, nullptr, false, nullptr, network), "network prewarm failed");
			const auto parameters = creates.back();
			Require(parameters.at("DLSSNR.Hint.Render.Preset") == network &&
						warm.featureConfigurations_[2].networkPreset == network,
				"provider hint and resident identity must match the selected network");
			const auto run = [&](Runtime& runtime) {
				return runtime.Execute(&commands, 2, &resource, &resource, &resource, &resource, nullptr, layout, tuning, false);
			};
			const auto created = creates.size();
			Require(run(warm) && creates.size() == created, "matching prewarm must retain the selected network");
			Require(run(lazy) && creates.back() == parameters, "lazy and prewarmed networks must use identical creation parameters");
			Require(evaluations.back().at("DLSSNR.LocalStructureStrength") == std::bit_cast<std::uint32_t>(network == kNetworkLegacy ? 1.75f : 1.0f),
				"native dispatch must obey the selected network's tuning limits");
			tuning.networkPreset = (network + 1) % 3;
			const auto evaluated = evaluations.size();
			Require(!run(warm) && evaluations.size() == evaluated && warm.featureHandles_[2] &&
						warm.status_ == RuntimeStatus::FeatureConfigurationChanged,
				"a live network change must require renderer retirement before any evaluation");
		}
		Runtime unsupported;
		unsupported.networkSelectionSupported_ = false;
		const auto created = creates.size();
		Require(!unsupported.PrepareFeature(&commands, 2, layout, nullptr, nullptr, false, nullptr, kNetworkNewer) &&
					creates.size() == created && !unsupported.featureHandles_[2] &&
					unsupported.status_ == RuntimeStatus::FeatureCreateFailed &&
					unsupported.failureStage_ == RuntimeFailureStage::FeatureConfiguration,
			"unsupported native networks must fail before calling the provider");
		Require(!unsupported.PrepareFeature(&commands, 2, layout) && creates.size() == created,
			"failed creation must require runtime recovery before another attempt");
		Runtime defaultNetwork;
		defaultNetwork.networkSelectionSupported_ = false;
		Require(defaultNetwork.PrepareFeature(&commands, 2, layout) &&
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
						legacy.PrepareFeature(&commands, 2, layout, nullptr, nullptr, false, nullptr, tuning.networkPreset) &&
						creates.back().at("DLSSNR.Hint.Render.Preset") == kNetworkDefault,
				"A 310.8 DLL must keep native default for every saved network choice");
			const auto createdLegacy = creates.size();
			Require(legacy.Execute(&commands, 2, &resource, &resource, &resource, &resource, nullptr, layout, tuning, false) &&
						creates.size() == createdLegacy &&
						evaluations.back().at("DLSSNR.LocalStructureStrength") == std::bit_cast<std::uint32_t>(1.75f),
				"310.8 defaults must preserve their range and reuse the prepared model");
		}
	}

	void InvalidationAndLease()
	{
		using namespace NeuralRendering;
		ShaderInvalidation invalidation;
		invalidation.Request();
		const auto captured = invalidation.Requested();
		invalidation.Request();
		invalidation.Applied(captured);
		Require(invalidation.Pending(), "request arriving during apply was lost");
		std::vector<std::thread> threads;
		for (unsigned index = 0; index < 4; ++index)
			threads.emplace_back([&] { for (unsigned count = 0; count < 10000; ++count) invalidation.Request(); });
		for (auto& thread : threads)
			thread.join();
		Require(invalidation.Requested() == 40002, "concurrent invalidation was lost");
		invalidation.Applied(invalidation.Requested());
		Require(!invalidation.Pending(), "completed invalidations repeated");
		PrewarmPolicy policy;
		PrewarmKey key;
		key.slotMask = 12;
		policy.Capture(key, 100);
		auto changedNetwork = key;
		changedNetwork.networkPreset = kNetworkNewer;
		Require(changedNetwork != policy.key, "a new network must invalidate the captured prewarm identity");
		Require(policy.BeginOpportunity(8) && !policy.BeginOpportunity(8) && policy.BeginOpportunity(9), "duplicate compositor work was admitted");
		Require(!policy.Expired(30099) && policy.Expired(30100), "unused lease did not expire independently of world frames");
		policy.backendOwned = true;
		policy.Adopt(0, false);
		Require(!policy.backendOwned && !policy.adoptedMask && !policy.missedMask, "gameplay failed to assume a backend without prepared models");
		policy.preparedMask = policy.unusedMask = 12;
		policy.Adopt(2, true);
		Require(policy.unusedMask == 8 && policy.adoptedMask == 4, "partial adoption lost unused peer");
		policy.Adopt(3, false);
		Require(policy.unusedMask == 0 && policy.adoptedMask == 4 && policy.missedMask == 8, "authoritative mismatch advertised reuse");
		policy.Capture(key, 100);
		Require(policy.SuppressRetryAfterRetirement(30100), "expired request restarted optional work");
		policy.backendOwned = policy.failed = true;
		policy.unusedMask = 4;
		policy.CancelRequest();
		Require(policy.cancelled && policy.failed && policy.backendOwned && policy.unusedMask == 4, "cancellation lost unsafe ownership");
		Require(!policy.SuppressRetryAfterRetirement(30100), "explicit cancellation left request permanently suppressed");
		policy = {};
		policy.Capture(key, 30200);
		Require(!policy.failed && !policy.Expired(30200), "new explicit request inherited the retired failure");
		const UpscalingDLSS::Extent native{ 1600, 900 }, source{ 3200, 1800 }, colorWork{ 1600, 960 };
		key.source = source;
		key.modelProjection = true;
		key.layout = BuildNativeEvaluationLayout(native, native, native, {}, { 0, 0, 1600, 900 }, { true, 1600.f, 900.f }, false);
		const auto bytes = PrewarmTransportBytes(key, colorWork);
		const std::uint64_t sourcePixels = 3200u * 1800u, nativePixels = 1600u * 900u;
		const auto privateProxies = 2 * (nativePixels * (16 + 4 + 8 + 16) + sourcePixels * 16);
		const auto sourceAndNative = 2 * (sourcePixels + nativePixels) * (16 + 4 + 8 + 16);
		const auto paddedColor = 2 * 1600u * 960u * 32;
		Require(bytes && *bytes >= privateProxies + sourceAndNative + paddedColor,
			"Prewarm admission omitted source, private proxies or padded colour work");
		auto changedSource = key;
		++changedSource.source.width;
		Require(changedSource != key && PrewarmTransportBytes(changedSource, colorWork) > bytes,
			"A changed source grid must refresh admission even with identical native dimensions");
		key.modelProjection = false;
		Require(PrewarmTransportBytes(key, colorWork) == *bytes - privateProxies,
			"Unscaled prewarm must not reserve unused model proxies");
		key.source.width = kMaximumNeuralImageDimension + 1;
		Require(!PrewarmTransportBytes(key, colorWork), "Oversized source geometry escaped admission bounds");
		key.source = {};
		Require(!PrewarmTransportBytes(key, colorWork), "Missing source geometry escaped admission bounds");
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
		Require(runtime.Execute(&commands, 0, &resource, &resource, &resource, &resource, nullptr, layout, tuning, false, nullptr, nullptr, nullptr, 0, false), "Depth encoding dispatch failed");
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
					layout, tuning, false, nullptr, nullptr, nullptr, 0, true),
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
		NetworkCreation();
		InvalidationAndLease();
		DepthEncoding();
		SdkMaskAndParameterConsistency();
		InvalidSdkInputs();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
