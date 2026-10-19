#include "Features/Upscaling/NeuralRendering/NativeEvaluationLayout.h"
#include "Features/Upscaling/NeuralRendering/PrewarmPolicy.h"
#include "Features/Upscaling/NeuralRendering/ShaderInvalidation.h"
#include "Features/Upscaling/NeuralRendering/TuningLimits.h"

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
#include <thread>
#include <vector>

namespace Util
{
	struct TestRuntimeVersion
	{
		unsigned majorValue = 310, minorValue = 9;
		unsigned major() const { return majorValue; }
		unsigned minor() const { return minorValue; }
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
	using HMODULE = void*;
	constexpr std::int32_t E_INVALIDARG = -1, E_PENDING = -2;
	constexpr unsigned kFeatureDlssNr = 18;
	constexpr bool NVSDK_NGX_FAILED(int result) { return result != 0; }
	struct NVSDK_NGX_Handle
	{};
	struct NVSDK_NGX_Parameter
	{
		std::map<std::string, std::uint64_t> values;
		void Reset() { values.clear(); }
		void Set(const char* key, std::uint32_t value) { values[key] = value; }
		void Set(const char* key, float value) { values[key] = std::bit_cast<std::uint32_t>(value); }
		void Set(const char* key, ID3D12Resource* value) { values[key] = reinterpret_cast<std::uintptr_t>(value); }
	};
	struct RuntimeCallerPathCall
	{
		static inline bool installed = true;
		explicit RuntimeCallerPathCall(HMODULE) {}
		bool IsInstalled() const { return installed; }
		unsigned Hits() const { return 1; }
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
		MissingExport
	};
	enum class RuntimeFailureStage
	{
		None,
		FeatureConfiguration,
		FeatureCreate,
		FeatureEvaluate,
		Load
	};
	const char* ToString(int) { return "test"; }
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
		float intensity = .8f, localToneStrength = .75f, localStructureStrength = .9f, skinStructureStrength = .9f;
		std::uint32_t style = 3;
		bool useAutoMask = true, uiCorrection = false;
		float singleSubrectScale = 1;
	};
	using CreateFeature = int (*)(ID3D12GraphicsCommandList*, unsigned, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
	using EvaluateFeature = int (*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Handle*, NVSDK_NGX_Parameter*, void*);
	std::vector<std::map<std::string, std::uint64_t>> creates, evaluations;
	int providerCreateResult = 0, providerEvaluateResult = 0;
	bool returnHandle = true;
	NVSDK_NGX_Handle providerHandle;
	int Create(ID3D12GraphicsCommandList*, unsigned feature, NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** output)
	{
		if (feature != kFeatureDlssNr)
			throw std::runtime_error("wrong feature");
		creates.push_back(parameters->values);
		*output = returnHandle ? &providerHandle : nullptr;
		return providerCreateResult;
	}
	int Evaluate(ID3D12GraphicsCommandList*, NVSDK_NGX_Handle*, NVSDK_NGX_Parameter* parameters, void*)
	{
		evaluations.push_back(parameters->values);
		return providerEvaluateResult;
	}
	struct Runtime
	{
		static constexpr unsigned kFeatureSlotCount = 4;
		struct FeatureConfiguration
		{
			UpscalingDLSS::Extent colorBacking{};
			NativeCreationExtents creation{};
			bool featureUpscaling = false, providerBlending = false, valid = false;
			bool Matches(const NativeEvaluationLayout&) const;
		};
		std::optional<TuningLimits> tuningLimits_ = TuningLimits{};
		TuningLimits GetTuningLimits() const;
		mutable std::recursive_mutex mutex_;
		std::atomic<bool> abandonRequested_ = false;
		bool abandoned_ = false;
		RuntimeStatus status_ = RuntimeStatus::Initialized;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		std::array<void*, kFeatureSlotCount> featureHandles_{};
		std::array<FeatureConfiguration, kFeatureSlotCount> featureConfigurations_{};
		std::atomic<std::uint32_t> firstEvaluationResetMask_{ 0 };
		struct Exports
		{
			void* createFeature = reinterpret_cast<void*>(&Create);
			void* evaluateFeature = reinterpret_cast<void*>(&Evaluate);
		} runtimeExports_;
		NVSDK_NGX_Parameter parameterStorage;
		void* parameters_ = &parameterStorage;
		void* module_ = nullptr;
		bool lastPathProxyInstalled_ = false, featureCreateLogEmitted_ = false, featureEvaluateLogEmitted_ = false;
		unsigned lastPathProxyHits_ = 0, ngxResult_ = 0;
		std::uint64_t successfulFrames_ = 0;
		int trust_ = 0;
		std::string detail_;
		void SetFailureLocked(RuntimeStatus status, RuntimeFailureStage stage, std::string detail, unsigned result = 0)
		{
			status_ = status;
			failureStage_ = stage;
			detail_ = std::move(detail);
			ngxResult_ = result;
		}
		void LogOnceLocked(bool& emitted, const char*, bool) { emitted = true; }
		bool PrepareFeature(ID3D12GraphicsCommandList*, unsigned, const NativeEvaluationLayout&, ID3D12Resource* = nullptr, ID3D12Resource* = nullptr, bool = false, RuntimeExecutionEvidence* = nullptr);
		bool PrepareFeatureLocked(ID3D12GraphicsCommandList*, unsigned, const NativeEvaluationLayout&, ID3D12Resource*, ID3D12Resource*, bool, RuntimeExecutionEvidence*);
		void AcknowledgePresentedFeatures(unsigned);
		bool Execute(ID3D12GraphicsCommandList*, unsigned, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, const NativeEvaluationLayout&, const Tuning&, bool, bool* = nullptr, RuntimeExecutionEvidence* = nullptr, D3D12Interop* = nullptr, unsigned = 0, bool = false);
	};
#include "neural_native_creation_under_test.h"
#include "neural_native_key_under_test.h"
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
		Runtime lazy, warm;
		Require(run(lazy, 2, layout), "lazy creation failed");
		const auto lazyParameters = creates.back();
		const auto evaluationsBefore = evaluations.size();
		Require(warm.PrepareFeature(&commands, 2, layout), "prewarm failed");
		Require(creates.back() == lazyParameters, "lazy/prewarm creation parameter mismatch");
		Require(lazyParameters.at("Width") == 320 && lazyParameters.at("DLSSNR.InputHeight") == 192 && lazyParameters.at("DLSSNR.Upscaling") == 0, "creation contract changed");
		Require(warm.successfulFrames_ == 0 && evaluations.size() == evaluationsBefore, "prewarm evaluated a fabricated frame");
		const auto created = creates.size();
		Require(warm.PrepareFeature(&commands, 2, layout) && run(warm, 2, layout), "first use failed");
		Require(creates.size() == created && evaluations.back().at("DLSSNR.Reset") == 1, "first use recreated or lost reset");
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
		RuntimeCallerPathCall::installed = false;
		const auto beforeProxy = creates.size();
		Require(!missing.PrepareFeature(&commands, 2, layout) && creates.size() == beforeProxy, "missing proxy called provider");
		RuntimeCallerPathCall::installed = true;
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
	}
}
int main()
{
	try {
		RuntimeTuningMetadata();
		NativeCreation();
		InvalidationAndLease();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
