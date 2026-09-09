#include "Features/Upscaling/FSRHostLifecyclePolicy.h"
#include "Features/Upscaling/FSRRuntimeLifecyclePolicy.h"
#include "Features/Upscaling/VRSubmitColorContract.h"
#include "Features/Upscaling/VRSubmitInputFreshnessPolicy.h"
#include "Features/Upscaling/VRSubmitTemporalSnapshot.h"
#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

// Compile the production provider and vendor dispatch functions against effect
// counters, so deferred admission cannot silently become an evaluation failure.
struct D3D11_TEXTURE2D_DESC
{
	uint32_t Width = 1512;
	uint32_t Height = 1680;
};
struct ID3D11Resource
{
	D3D11_TEXTURE2D_DESC desc;
};
struct ID3D11DeviceContext
{};
struct Float2
{
	float x = 0;
	float y = 0;
};
struct Dimensions
{
	uint32_t width = 0;
	uint32_t height = 0;
};
struct FfxFsr3DispatchUpscaleDescription
{
	ID3D11DeviceContext* commandList = nullptr;
	ID3D11Resource* color = nullptr;
	ID3D11Resource* depth = nullptr;
	ID3D11Resource* motionVectors = nullptr;
	ID3D11Resource* exposure = nullptr;
	ID3D11Resource* upscaleOutput = nullptr;
	ID3D11Resource* reactive = nullptr;
	ID3D11Resource* transparencyAndComposition = nullptr;
	Float2 motionVectorScale;
	Float2 jitterOffset;
	Dimensions renderSize;
	Dimensions upscaleSize;
	float frameTimeDelta = 0;
	float cameraFar = 0;
	float cameraNear = 0;
	bool enableSharpening = false;
	float sharpness = 0;
	float cameraFovAngleVertical = 0;
	float viewSpaceToMetersFactor = 0;
	bool reset = false;
	float preExposure = 0;
	uint32_t flags = 0;
};
ID3D11DeviceContext* ffxGetCommandListDX11(ID3D11DeviceContext* a_context) { return a_context; }
ID3D11Resource* ffxGetResource(ID3D11Resource* a_resource, const wchar_t*) { return a_resource; }
#define CS_GPU_PASS_DYNAMIC(name) ((void)(name))
namespace logger
{
	template <class... Args>
	void debug(std::string_view, Args&&...) {}
	template <class... Args>
	void critical(std::string_view, Args&&...) {}
}
namespace Util
{
	float GetVerticalFOVRad() { return 1.0f; }
}
namespace sl
{
	struct Extent
	{
		uint32_t left, top, width, height;
	};
	using ViewportHandle = uint32_t;
}
struct Streamline
{
	enum class DLSSViewportRole
	{
		FullEye,
		FoveatedCenter
	};
	sl::ViewportHandle viewport = 0;
	sl::ViewportHandle viewportRight = 1;
	bool ready = true;
	uint32_t dispatches = 0;
	void ClearLastDLSSFailureState() {}
	template <class... Args>
	bool EvaluateDLSS(Args&&...)
	{
		++dispatches;
		return ready;
	}
};
enum class UpscaleMethod
{
	kNONE,
	kTAA,
	kFSR,
	kDLSS
};
namespace magic_enum
{
	std::string_view enum_name(UpscaleMethod) { return "test vendor"; }
}
struct Upscaling;
namespace globals
{
	struct State
	{
		bool developerMode = true;
		bool IsDeveloperMode() const { return developerMode; }
	};
	State testState;
	State* state = &testState;
	namespace features
	{
		extern Upscaling upscaling;
	}
	namespace d3d
	{
		ID3D11DeviceContext testContext;
		ID3D11DeviceContext* context = &testContext;
	}
	namespace game
	{
		bool isVR = true;
		float testDelta = 1.0f / 90.0f;
		float testFar = 10000;
		float testNear = 1;
		float* deltaTime = &testDelta;
		float* cameraFar = &testFar;
		float* cameraNear = &testNear;
	}
}

struct FidelityFX
{
#include "fsr_eye_dispatch_types_under_test.h"

	bool fsrHostStateQuarantined = false;
	std::array<bool, 2> fsrContextIndeterminate{};
	std::array<bool, 2> runtimeUpscalerContextIndeterminate{};
	bool runtimeUpscalerUsedForFrame = false;
	bool runtimeHostFallbackForFrame = false;
	bool runtimeHostFallbackActive = false;
	uint32_t runtimeFallbackResetDispatchesRemaining = 0;
	uint32_t runtimeResumeResetDispatchesRemaining = 0;
	uint32_t fsrContextCount = 0;
	std::array<bool, 2> fsrContextValid{};
	std::array<uint32_t, 2> fsrContext{ 0, 1 };
	uint32_t scratchStorage = 0;
	uint32_t* fsrScratchBuffer = nullptr;
	uint32_t fsrContextMaxRenderWidth = 0;
	uint32_t fsrContextMaxRenderHeight = 0;
	uint32_t fsrContextDisplayWidth = 0;
	uint32_t fsrContextDisplayHeight = 0;
	bool fsrDispatchCrashLogged = false;
	bool hostSupported = false;
	bool hostDispatchReady = true;
	bool hostDispatchFault = false;
	RuntimeDispatchPlan plan{
		.valid = true,
		.runtimeRequested = true,
		.selected = true,
		.contextCount = 2,
	};
	LifecycleResult runtimeResult = LifecycleResult::Pending;
	uint32_t runtimeCalls = 0;
	uint32_t runtimeEyeMask = 0;
	uint32_t runtimeRegionCount = 0;
	uint32_t hostCalls = 0;
	uint32_t hostEyeMask = 0;
	uint32_t quarantines = 0;
	uint32_t hostQuarantines = 0;
	uint32_t fsr4Failures = 0;
	uint32_t deviceProbes = 0;
	bool lastHostReset = false;
	FfxFsr3DispatchUpscaleDescription lastHostParameters{};
	RuntimeUpscalerFramePath lastFramePath = RuntimeUpscalerFramePath::kInactive;

	RuntimeDispatchPlan ResolveRuntimeDispatchPlan() const { return plan; }
	// Exercise the production gate on an otherwise eligible runtime; provider
	// setup and GPU dispatch remain controlled test dependencies.
	void ResolveEligibleRuntimeShaderGate(bool shaderCompilationActive, bool runtimeContextsCompatible)
	{
		const bool exactCurrentProviderReady = false;
		const bool awaitingInitialVRRenderScaleLatch = false;
		const bool runtimePathEligible = true;
		const bool runtimeUpscalerSessionQuarantined = false;
#include "fsr_runtime_gate_under_test.h"
	}
	LifecycleResult ExecuteRuntimeUpscalerBatch(const RuntimeDispatchPlan&, std::span<const UpscaleRegionParameters> a_regions)
	{
		++runtimeCalls;
		runtimeRegionCount += static_cast<uint32_t>(a_regions.size());
		for (const auto& region : a_regions)
			runtimeEyeMask |= 1u << region.contextIndex;
		return runtimeResult;
	}
	bool IsHostFSR3Supported() const { return hostSupported; }
	bool HasFSRResources() const;
	bool AreFSRResourcesCompatible(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) const;
	void QuarantineRuntimeUpscalerForSession(const char*) { ++quarantines; }
	void QuarantineHostFSRContext(uint32_t, const char*) { ++hostQuarantines; }
	void LatchRuntimeFsr4Failure() { ++fsr4Failures; }
	LifecycleResult ResolveFSRLifecycleFailure(const char*) { return LifecycleResult::Failed; }
	LifecycleResult ProbeFSRDeviceStatus()
	{
		++deviceProbes;
		return LifecycleResult::Ready;
	}
	void RecordRuntimeUpscalerFramePath(RuntimeUpscalerFramePath a_path) { lastFramePath = a_path; }
	struct Sharpening
	{
		bool enabled;
		float sharpness;
	};
	Sharpening ResolveFSRSharpeningSettings(float a_sharpness) { return { a_sharpness > 0, a_sharpness }; }
	void LogFSRSharpeningDispatch(Sharpening, const char*) {}
	bool DispatchHostFsr3UpscaleProtected(uint32_t a_eye, const FfxFsr3DispatchUpscaleDescription& a_params, bool& a_crashed)
	{
		++hostCalls;
		hostEyeMask |= 1u << a_eye;
		lastHostReset = a_params.reset;
		lastHostParameters = a_params;
		a_crashed = hostDispatchFault;
		return hostDispatchReady && !hostDispatchFault;
	}
	void ArmRuntimeHostFallback(uint32_t);
	bool CanDispatchHostFallbackForRegions(std::span<const UpscaleRegionParameters>, const RuntimeDispatchPlan&) const;
	UpscaleResult UpscaleRegion(uint32_t, ID3D11Resource*, ID3D11Resource*, ID3D11Resource*,
		ID3D11Resource*, ID3D11Resource*, ID3D11Resource*, uint32_t, uint32_t,
		uint32_t, uint32_t, float, float, float, bool* = nullptr);
	StereoUpscaleResult UpscaleStereoRegions(const std::array<UpscaleRegionParameters, 2>&);
};

struct Upscaling
{
	static constexpr uint32_t kDLSSPresetK = 1;
#include "fsr_eye_dispatch_params_under_test.h"
	FidelityFX fidelityFX;
	Streamline streamline;
	Float2 jitter;
	VRSubmitTemporalSnapshot::Snapshot<int> temporalSnapshot;
	VRSubmitColorContract::Contract colorContract;
	bool hasSubmitColorContract = false;
	const auto* GetSubmitTemporalSnapshotForDispatch() const { return temporalSnapshot.valid ? &temporalSnapshot : nullptr; }
	const VRSubmitColorContract::Contract* GetSubmitColorContractForDispatch() const { return hasSubmitColorContract ? &colorContract : nullptr; }
	struct Settings
	{
		float sharpnessFSR = 0;
	} settings;
	uint32_t successfulEvaluations = 0;
	uint32_t failedEvaluations = 0;
	uint32_t deviceLossHandlers = 0;
	bool historyResetRequested = false;
	bool ShouldResetHistoryThisFrame() const { return historyResetRequested; }
	bool IsVendorUpscalingMethod(UpscaleMethod a_method) const
	{
		return a_method == UpscaleMethod::kFSR || a_method == UpscaleMethod::kDLSS;
	}
	bool TryGetTexture2DDesc(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_desc)
	{
		a_desc = a_resource->desc;
		return true;
	}
	void RecordVRRenderScaleFullEyeEvaluation(UpscaleMethod, uint32_t, bool a_ready)
	{
		if (a_ready)
			++successfulEvaluations;
		else
			++failedEvaluations;
	}
	void HandleFSRLifecycleDeviceLoss(FidelityFX::LifecycleResult, const char*) { ++deviceLossHandlers; }
	FidelityFX::UpscaleResult DispatchVendorEyeRegion(UpscaleMethod, const VendorEyeDispatchParams&);
};
namespace globals::features
{
	Upscaling upscaling;
}

#include "fsr_eye_dispatch_under_test.h"

namespace
{
	using Result = FidelityFX::UpscaleResult;
	using Lifecycle = FidelityFX::LifecycleResult;

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	Upscaling& Reset()
	{
		globals::features::upscaling = {};
		globals::state = &globals::testState;
		globals::d3d::context = &globals::d3d::testContext;
		return globals::features::upscaling;
	}

	Upscaling::VendorEyeDispatchParams Region(uint32_t a_eye, uint32_t a_width)
	{
		static std::array<ID3D11Resource, 12> resources;
		auto* eyeResources = resources.data() + a_eye * 6;
		return {
			.eyeIndex = a_eye,
			.inputWidth = a_width,
			.inputHeight = a_width == 1284 ? 1428u : 560u,
			.outputWidth = 1512,
			.outputHeight = 1680,
			.colorIn = &eyeResources[0],
			.depth = &eyeResources[1],
			.motionVectors = &eyeResources[2],
			.reactiveMask = &eyeResources[3],
			.transparencyMask = &eyeResources[4],
			.colorOut = &eyeResources[5],
		};
	}

	void EnableHost(FidelityFX& a_provider)
	{
		a_provider.hostSupported = true;
		a_provider.fsrContextCount = 2;
		a_provider.fsrContextValid = { true, true };
		a_provider.fsrScratchBuffer = &a_provider.scratchStorage;
		a_provider.fsrContextMaxRenderWidth = 1512;
		a_provider.fsrContextMaxRenderHeight = 1680;
		a_provider.fsrContextDisplayWidth = 1512;
		a_provider.fsrContextDisplayHeight = 1680;
	}

	void SubmitContractsReachHostDispatch()
	{
		auto& upscaling = Reset();
		auto& provider = upscaling.fidelityFX;
		upscaling.hasSubmitColorContract = true;
		upscaling.colorContract = { VRSubmitColorContract::Transfer::Linear, VRSubmitColorContract::DynamicRange::LDR };
		EnableHost(provider);
		Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(0, 1284)) == Result::Failed &&
					provider.hostCalls == 0 && provider.runtimeCalls == 0,
			"Linear submit input reached a vendor dispatch");
		upscaling.colorContract.transfer = VRSubmitColorContract::Transfer::Gamma;
		provider.plan.runtimeRequested = false;
		upscaling.temporalSnapshot.valid = true;
		upscaling.temporalSnapshot.scalars = { 0.25f, -0.5f, 2.0f, 20000.0f, 1.25f, 8.0f, false };
		upscaling.historyResetRequested = true;
		Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(1, 1284)) == Result::Ready,
			"Gamma submit input could not use the host provider");
		const auto& parameters = provider.lastHostParameters;
		Require(parameters.jitterOffset.x == -0.25f && parameters.jitterOffset.y == 0.5f &&
					parameters.cameraNear == 2.0f && parameters.cameraFar == 20000.0f &&
					parameters.cameraFovAngleVertical == 1.25f && parameters.frameTimeDelta == 8.0f && parameters.reset,
			"Host dispatch did not retain captured scalars and a late history reset");
	}

	void RequireDeferredUntouched(const Upscaling& a_upscaling)
	{
		const auto& provider = a_upscaling.fidelityFX;
		Require(a_upscaling.failedEvaluations == 0 && a_upscaling.successfulEvaluations == 0 &&
				a_upscaling.deviceLossHandlers == 0 && provider.deviceProbes == 0,
			"Deferred eye dispatch recorded an evaluation or device failure");
		Require(!provider.runtimeHostFallbackActive && !provider.runtimeHostFallbackForFrame &&
				provider.runtimeFallbackResetDispatchesRemaining == 0 &&
				provider.runtimeResumeResetDispatchesRemaining == 0 &&
				provider.quarantines == 0 && provider.hostQuarantines == 0 && provider.fsr4Failures == 0 &&
				provider.hostCalls == 0 && !a_upscaling.historyResetRequested,
			"Deferred eye dispatch armed fallback, consumed history, or quarantined a provider");
	}

	void ColdRuntimeWithoutPeerProof()
	{
		using namespace VRSubmitInputFreshnessPolicy;
		ProducerAdmission admission{};
		admission.compositorCycle = 7;
		const auto proof = ResolveProducerProof(admission);
		Require(ResolveProducerRejection(admission) == ProducerRejection::MissingOuterBoundary &&
				!CanConsumePeerInputs(proof, 0) && !CanConsumePeerInputs(proof, 1),
			"The cold-runtime fixture unexpectedly authorized peer inputs");
		for (uint32_t width : { 1284u, 504u }) {
			for (uint32_t eye : { 0u, 1u }) {
				auto& upscaling = Reset();
				const auto params = Region(eye, width);
				Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, params) == Result::Deferred,
					"Cold single-eye runtime Pending became a vendor failure");
				Require(upscaling.fidelityFX.runtimeCalls == 1 && upscaling.fidelityFX.runtimeRegionCount == 1 &&
						upscaling.fidelityFX.runtimeEyeMask == (1u << eye),
					"Single-eye admission consumed its unproven peer");
				RequireDeferredUntouched(upscaling);
				upscaling.fidelityFX.runtimeResult = Lifecycle::Ready;
				Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, params) == Result::Ready &&
						upscaling.successfulEvaluations == 1 && upscaling.failedEvaluations == 0 &&
						upscaling.fidelityFX.runtimeUpscalerUsedForFrame,
					"A deferred runtime eye did not recover on its next ready dispatch");
			}
		}
	}

	void DeferredAdmissionAndHostFallback()
	{
		for (bool setupDeferred : { false, true }) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			provider.plan.selected = false;
			provider.plan.providerSetupDeferred = setupDeferred;
			provider.plan.deferred = !setupDeferred;
			provider.plan.valid = setupDeferred;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(0, 504)) == Result::Deferred,
				"Deferred teardown/provider setup became a vendor failure");
			Require(provider.runtimeCalls == 0, "Deferred admission dispatched into the runtime");
			RequireDeferredUntouched(upscaling);
		}
		for (bool setupDeferred : { false, true }) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			EnableHost(provider);
			provider.plan.selected = !setupDeferred;
			provider.plan.providerSetupDeferred = setupDeferred;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(0, 1284)) == Result::Ready &&
					provider.hostCalls == 1 && provider.hostEyeMask == 1 &&
					provider.runtimeCalls == (setupDeferred ? 0u : 1u) &&
					provider.lastFramePath == FidelityFX::RuntimeUpscalerFramePath::kHostFsr31Fallback &&
					provider.lastHostReset && provider.runtimeFallbackResetDispatchesRemaining == 1 &&
					provider.runtimeResumeResetDispatchesRemaining == 2,
				"Compatible host fallback failed to dispatch/reset only the current eye");
		}
		for (uint32_t blocker = 0; blocker < 3; ++blocker) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			EnableHost(provider);
			if (blocker == 0)
				provider.fsrContextValid[1] = false;
			if (blocker == 1)
				provider.fsrScratchBuffer = nullptr;
			if (blocker == 2)
				provider.runtimeUpscalerUsedForFrame = true;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(0, 1284)) == Result::Deferred,
				"Pending runtime used an incomplete host pair or mixed providers");
			RequireDeferredUntouched(upscaling);
		}
	}

	void GenuineFailuresRemainFailures()
	{
		for (bool fsr4 : { false, true }) {
			for (auto failure : { Lifecycle::Failed, Lifecycle::DeviceLost, Lifecycle::RuntimeDeviceLost }) {
				auto& upscaling = Reset();
				auto& provider = upscaling.fidelityFX;
				provider.plan.runtimeFsr4Requested = fsr4;
				provider.runtimeResult = failure;
				Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(1, 504)) == Result::Failed &&
						upscaling.failedEvaluations == 1 && upscaling.successfulEvaluations == 0 &&
						provider.deviceProbes == 1 && upscaling.deviceLossHandlers == 1 &&
						provider.hostCalls == 0 &&
						provider.quarantines == (failure == Lifecycle::RuntimeDeviceLost ? 0u : 1u) &&
						provider.fsr4Failures == (fsr4 && failure == Lifecycle::Failed ? 1u : 0u),
					"A provider failure lost terminal classification, quarantine, or its FSR4 latch");
			}
		}
		for (bool crashed : { false, true }) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			EnableHost(provider);
			provider.plan.selected = false;
			provider.plan.runtimeRequested = false;
			provider.hostDispatchReady = false;
			provider.hostDispatchFault = crashed;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(1, 504)) == Result::Failed &&
					provider.runtimeCalls == 0 && provider.hostCalls == 1 && provider.hostEyeMask == 2 &&
					provider.hostQuarantines == (crashed ? 1u : 0u) &&
					provider.fsrDispatchCrashLogged == crashed &&
					provider.quarantines == 0 && provider.fsr4Failures == 0 &&
					upscaling.failedEvaluations == 1 && upscaling.successfulEvaluations == 0 &&
					provider.deviceProbes == 1 && upscaling.deviceLossHandlers == 1,
				"A host SDK error/fault was hidden or quarantined the wrong provider");
		}
		for (bool ready : { false, true }) {
			auto& upscaling = Reset();
			upscaling.streamline.ready = ready;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kDLSS, Region(0, 1284)) ==
					(ready ? Result::Ready : Result::Failed) &&
					upscaling.streamline.dispatches == 1 &&
					upscaling.failedEvaluations == (ready ? 0u : 1u),
				"Typed FSR results changed DLSS success/failure classification");
		}
	}

	void HostFallbackPreservesContextBounds()
	{
		for (bool setupDeferred : { false, true }) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			EnableHost(provider);
			provider.plan.selected = !setupDeferred;
			provider.plan.providerSetupDeferred = setupDeferred;
			auto params = Region(0, 504);
			params.outputWidth = 756;
			params.outputHeight = 840;
			params.dlssViewportRole = Streamline::DLSSViewportRole::FoveatedCenter;
			Require(!provider.AreFSRResourcesCompatible(504, 560, 756, 840, 2),
				"Lifecycle compatibility accepted a different configured display size");
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, params) == Result::Ready &&
					provider.hostCalls == 1 && provider.lastHostReset &&
					upscaling.failedEvaluations == 0 && upscaling.successfulEvaluations == 0,
				"A valid foveated subextent could not use a complete host context");
		}
		for (uint32_t blocker = 0; blocker < 8; ++blocker) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			EnableHost(provider);
			auto params = Region(0, 504);
			if (blocker == 0)
				provider.fsrContextMaxRenderWidth = params.inputWidth - 1;
			if (blocker == 1)
				provider.fsrContextMaxRenderHeight = params.inputHeight - 1;
			if (blocker == 2)
				provider.fsrContextDisplayWidth = params.outputWidth - 1;
			if (blocker == 3)
				provider.fsrContextDisplayHeight = params.outputHeight - 1;
			if (blocker == 4)
				provider.fsrContextValid[1] = false;
			if (blocker == 5)
				provider.fsrScratchBuffer = nullptr;
			if (blocker == 6)
				provider.fsrContextCount = 1;
			if (blocker == 7)
				provider.hostSupported = false;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, params) == Result::Deferred,
				"Pending runtime used oversized or incomplete host resources");
			RequireDeferredUntouched(upscaling);
		}
		for (bool subextent : { false, true }) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			EnableHost(provider);
			provider.plan.vendorLifecycleMutationDeferred = true;
			auto params = Region(0, 504);
			if (subextent) {
				params.outputWidth = 756;
				params.outputHeight = 840;
				params.dlssViewportRole = Streamline::DLSSViewportRole::FoveatedCenter;
			}
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, params) ==
					(subextent ? Result::Deferred : Result::Ready),
				"Host fallback admission disagreed with the deferred lifecycle's display contract");
			if (subextent)
				RequireDeferredUntouched(upscaling);
			else
				Require(provider.hostCalls == 1, "An exact existing host contract could not dispatch");
		}
		for (bool indeterminate : { false, true }) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			EnableHost(provider);
			provider.fsrHostStateQuarantined = !indeterminate;
			provider.fsrContextIndeterminate[1] = indeterminate;
			Require(!provider.HasFSRResources() &&
					upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(0, 504)) == Result::Failed &&
					provider.runtimeCalls == 0 && provider.hostCalls == 0,
				"Quarantined or indeterminate host ownership remained dispatchable");
		}
	}

	void GateDeferralDoesNotSelectHostForTheFrame()
	{
		for (bool hostAvailable : { false, true }) {
			auto& upscaling = Reset();
			auto& provider = upscaling.fidelityFX;
			if (hostAvailable)
				EnableHost(provider);
			provider.ResolveEligibleRuntimeShaderGate(true, false);
			Require(provider.plan.providerSetupDeferred && !provider.plan.selected &&
					!provider.runtimeHostFallbackForFrame,
				"An unresolved setup gate selected host before a dispatch decision");
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(0, 1284)) ==
					(hostAvailable ? Result::Ready : Result::Deferred),
				"Setup deferral did not distinguish a complete host provider");
			provider.ResolveEligibleRuntimeShaderGate(false, false);
			Require(!provider.plan.providerSetupDeferred && provider.plan.selected != hostAvailable,
				"Clearing the gate retained an unused host latch or mixed providers after host output");
			provider.runtimeResult = Lifecycle::Ready;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(1, 1284)) == Result::Ready &&
					provider.runtimeCalls == (hostAvailable ? 0u : 1u) &&
					provider.hostCalls == (hostAvailable ? 2u : 0u) &&
					provider.hostEyeMask == (hostAvailable ? 3u : 0u) &&
					provider.runtimeHostFallbackForFrame == hostAvailable,
				"The next eye did not honor the actual provider selected in this frame");
		}

		std::array<FidelityFX::UpscaleRegionParameters, 2> stereo{};
		for (uint32_t eye = 0; eye < stereo.size(); ++eye) {
			const auto params = Region(eye, 1284);
			stereo[eye] = {
				.contextIndex = eye,
				.color = params.colorIn,
				.depth = params.depth,
				.motionVectors = params.motionVectors,
				.reactiveMask = params.reactiveMask,
				.transparencyCompositionMask = params.transparencyMask,
				.output = params.colorOut,
				.renderWidth = params.inputWidth,
				.renderHeight = params.inputHeight,
				.displayWidth = params.outputWidth,
				.displayHeight = params.outputHeight,
			};
		}
		for (bool hostAvailable : { false, true }) {
			auto& provider = Reset().fidelityFX;
			if (hostAvailable)
				EnableHost(provider);
			provider.ResolveEligibleRuntimeShaderGate(true, false);
			Require(provider.UpscaleStereoRegions(stereo) ==
					(hostAvailable ? FidelityFX::StereoUpscaleResult::NotHandled : FidelityFX::StereoUpscaleResult::Deferred),
				"Stereo setup deferral did not retain the host/deferred distinction");
			provider.ResolveEligibleRuntimeShaderGate(false, false);
			provider.runtimeResult = Lifecycle::Ready;
			Require(provider.UpscaleStereoRegions(stereo) ==
					(hostAvailable ? FidelityFX::StereoUpscaleResult::NotHandled : FidelityFX::StereoUpscaleResult::Ready) &&
					provider.runtimeCalls == (hostAvailable ? 0u : 1u) &&
					provider.runtimeHostFallbackForFrame == hostAvailable,
				"Clearing the gate changed an admitted host pair or blocked an unconsumed runtime pair");
		}
	}

	void InvalidInputsFailBeforeProviderDispatch()
	{
		for (uint32_t invalid = 0; invalid < 6; ++invalid) {
			auto& upscaling = Reset();
			auto params = Region(0, 504);
			if (invalid == 0)
				params.eyeIndex = 2;
			if (invalid == 1)
				params.colorIn = nullptr;
			if (invalid == 2)
				params.depth = nullptr;
			if (invalid == 3)
				params.inputWidth = 0;
			if (invalid == 4)
				params.outputHeight = 0;
			if (invalid == 5)
				params.outputWidth = 1513;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, params) == Result::Failed &&
					upscaling.fidelityFX.runtimeCalls == 0 && upscaling.fidelityFX.hostCalls == 0 &&
					upscaling.failedEvaluations == 0 && upscaling.deviceLossHandlers == 0,
				"Invalid eye resources or extents reached provider dispatch");
		}
		for (uint32_t count : { 0u, 3u }) {
			auto& provider = Reset().fidelityFX;
			EnableHost(provider);
			FidelityFX::UpscaleRegionParameters region{};
			region.renderWidth = 504;
			region.renderHeight = 560;
			region.displayWidth = 1512;
			region.displayHeight = 1680;
			provider.plan.contextCount = count;
			Require(!provider.CanDispatchHostFallbackForRegions(std::span{ &region, 1u }, provider.plan),
				"Invalid host context count was accepted");
			provider.fsrContextCount = count;
			Require(!provider.HasFSRResources(), "Invalid host context count indexed the context array");
		}
	}

	struct DeferredPresentation
	{
		enum class VRRenderScalePresentationPath
		{
			PresentationStretch,
			VendorFailureStretch
		};
		struct EyeState
		{
			bool ready = false;
			uint32_t method = static_cast<uint32_t>(UpscaleMethod::kFSR);
			uint32_t generation = 27;
		};
		static constexpr uint32_t kDLSSPresetK = 1;
		uint32_t currentFrame = 126284;
		uint64_t a_compositorCycleToken = 20288;
		uint32_t activeContractGeneration = 27;
		UpscaleMethod upscaleMethod = UpscaleMethod::kFSR;
		uint32_t eyeWidthIn = 1284;
		uint32_t eyeHeightIn = 1428;
		uint32_t submitStageVendorOutputFrame = currentFrame;
		uint64_t submitStageVendorOutputCompositorCycle = a_compositorCycleToken;
		uint32_t submitStageVendorOutputGeneration = activeContractGeneration;
		std::array<EyeState, 2> submitStageVendorEyeState{};
		uint64_t submitStageVendorAdmissionCycle = a_compositorCycleToken;
		uint32_t submitStageVendorAdmissionGeneration = activeContractGeneration;
		uint32_t submitStageVendorAdmissionMethod = static_cast<uint32_t>(upscaleMethod);
		VRSubmitColorContract::Contract sourceColorContract{ VRSubmitColorContract::Transfer::Gamma, VRSubmitColorContract::DynamicRange::LDR };
		VRSubmitColorContract::Contract submitStageVendorAdmissionColorContract = sourceColorContract;
		uint32_t submitStageVendorAdmissionFrame = currentFrame;
		uint32_t submitStageVendorAdmissionEyeMask = 1;
		bool submitStageVendorAdmissionPresentationOnly = false;
		bool submitStageVendorAdmissionExactProviderReady = true;
		bool submitStageVendorAdmissionAuthoritativeDLSSProfile = true;
		uint32_t submitStageVendorAdmissionDLSSQualityMode = 6;
		uint32_t submitStageVendorAdmissionDLSSPreset = 2;
		uint32_t unbinds = 0;
		uint32_t historyResets = 0;
		uint32_t stretches = 0;
		VRRenderScalePresentationPath lastPath = VRRenderScalePresentationPath::VendorFailureStretch;
		bool stretchReady = true;
		void UnbindUpscalingResources() { ++unbinds; }
		void RequestHistoryReset() { ++historyResets; }
		bool presentStretchOutput(uint32_t a_width, uint32_t a_height, VRRenderScalePresentationPath a_path)
		{
			Require(a_width == eyeWidthIn && a_height == eyeHeightIn,
				"Deferred presentation used stale input dimensions");
			++stretches;
			lastPath = a_path;
			return stretchReady;
		}
		bool Present()
		{
#include "fsr_deferred_presentation_under_test.h"
			return presentDeferredVendorOutput();
		}
	};

	void DeferredPresentationRetainsCycleOwnership()
	{
		for (bool admissionWasCleared : { false, true }) {
			DeferredPresentation presentation;
			if (admissionWasCleared) {
				presentation.submitStageVendorAdmissionCycle = 0;
				presentation.submitStageVendorAdmissionColorContract = {};
				presentation.submitStageVendorAdmissionGeneration = 0;
				presentation.submitStageVendorAdmissionMethod = 0;
				presentation.submitStageVendorAdmissionFrame = 0;
			}
			Require(presentation.Present() && presentation.stretches == 1 && presentation.unbinds == 1 &&
						presentation.lastPath == DeferredPresentation::VRRenderScalePresentationPath::PresentationStretch &&
						presentation.historyResets == 1 && presentation.submitStageVendorAdmissionPresentationOnly &&
						presentation.submitStageVendorAdmissionCycle == presentation.a_compositorCycleToken &&
						presentation.submitStageVendorAdmissionGeneration == presentation.activeContractGeneration &&
						presentation.submitStageVendorAdmissionMethod == static_cast<uint32_t>(presentation.upscaleMethod) &&
						presentation.submitStageVendorAdmissionFrame == presentation.currentFrame &&
						presentation.submitStageVendorAdmissionColorContract == presentation.sourceColorContract &&
						!presentation.submitStageVendorAdmissionExactProviderReady &&
						!presentation.submitStageVendorAdmissionAuthoritativeDLSSProfile &&
						presentation.submitStageVendorAdmissionDLSSQualityMode == 0 &&
						presentation.submitStageVendorAdmissionDLSSPreset == DeferredPresentation::kDLSSPresetK,
				"Deferred presentation lost its cycle hold or temporal-history protection");
			if (admissionWasCleared)
				Require(presentation.submitStageVendorAdmissionEyeMask == 0,
					"Restoring cleared admission retained an old eye claim");
		}
		for (uint32_t mismatch = 0; mismatch < 4; ++mismatch) {
			DeferredPresentation presentation;
			if (mismatch == 0)
				++presentation.submitStageVendorAdmissionCycle;
			if (mismatch == 1)
				++presentation.submitStageVendorAdmissionGeneration;
			if (mismatch == 2)
				++presentation.submitStageVendorAdmissionMethod;
			if (mismatch == 3)
				presentation.submitStageVendorAdmissionColorContract.transfer = VRSubmitColorContract::Transfer::Linear;
			Require(!presentation.Present() && presentation.stretches == 0 &&
					!presentation.submitStageVendorAdmissionPresentationOnly,
				"Deferred presentation overwrote another stereo cycle or contract");
		}
		auto& upscaling = Reset();
		upscaling.fidelityFX.runtimeResult = Lifecycle::Ready;
		Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(1, 1284)) == Result::Ready,
			"The right-first fixture did not advance vendor history");
		DeferredPresentation invalidatedOutput;
		invalidatedOutput.submitStageVendorOutputFrame = 0;
		invalidatedOutput.submitStageVendorOutputCompositorCycle = 0;
		invalidatedOutput.submitStageVendorOutputGeneration = 0;
		invalidatedOutput.submitStageVendorEyeState = {};
		upscaling.fidelityFX.runtimeResult = Lifecycle::Pending;
		Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(0, 1284)) == Result::Deferred &&
				invalidatedOutput.Present() && invalidatedOutput.historyResets == 1 &&
				upscaling.successfulEvaluations == 1 && upscaling.failedEvaluations == 0,
			"Invalidating cached output erased an earlier eye's temporal-history protection");
		DeferredPresentation failedStretch;
		failedStretch.stretchReady = false;
		Require(!failedStretch.Present() && failedStretch.submitStageVendorAdmissionPresentationOnly,
			"A failed stretch presentation released the deferred peer-eye hold");
	}
}

int main()
{
	SubmitContractsReachHostDispatch();
	ColdRuntimeWithoutPeerProof();
	DeferredAdmissionAndHostFallback();
	GenuineFailuresRemainFailures();
	HostFallbackPreservesContextBounds();
	GateDeferralDoesNotSelectHostForTheFrame();
	InvalidInputsFailBeforeProviderDispatch();
	DeferredPresentationRetainsCycleOwnership();
}
