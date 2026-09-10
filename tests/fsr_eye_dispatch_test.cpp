#include "Features/Upscaling/FSRHostLifecyclePolicy.h"
#include "Features/Upscaling/FSRRuntimeLifecyclePolicy.h"
#include "Features/Upscaling/VRSubmitInputFreshnessPolicy.h"
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
	bool fsrDispatchCrashLogged = false;
	bool hostSupported = false;
	bool hostResourcesCompatible = false;
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
	RuntimeUpscalerFramePath lastFramePath = RuntimeUpscalerFramePath::kInactive;

	RuntimeDispatchPlan ResolveRuntimeDispatchPlan() const { return plan; }
	LifecycleResult ExecuteRuntimeUpscalerBatch(const RuntimeDispatchPlan&, std::span<const UpscaleRegionParameters> a_regions)
	{
		++runtimeCalls;
		runtimeRegionCount += static_cast<uint32_t>(a_regions.size());
		for (const auto& region : a_regions)
			runtimeEyeMask |= 1u << region.contextIndex;
		return runtimeResult;
	}
	bool IsHostFSR3Supported() const { return hostSupported; }
	bool HasFSRResources() const { return fsrContextCount != 0; }
	bool AreFSRResourcesCompatible(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t a_count) const
	{
		return hostResourcesCompatible && fsrContextCount == a_count &&
		       std::all_of(fsrContextValid.begin(), fsrContextValid.begin() + a_count,
			       [](bool a_valid) { return a_valid; });
	}
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
		a_crashed = hostDispatchFault;
		return hostDispatchReady && !hostDispatchFault;
	}
	void ArmRuntimeHostFallback(uint32_t);
	bool CanDispatchHostFallbackForRegions(std::span<const UpscaleRegionParameters>, uint32_t) const;
	UpscaleResult UpscaleRegion(uint32_t, ID3D11Resource*, ID3D11Resource*, ID3D11Resource*,
		ID3D11Resource*, ID3D11Resource*, ID3D11Resource*, uint32_t, uint32_t,
		uint32_t, uint32_t, float, float, float, bool* = nullptr);
};

struct Upscaling
{
	static constexpr uint32_t kDLSSPresetK = 1;
#include "fsr_eye_dispatch_params_under_test.h"
	FidelityFX fidelityFX;
	Streamline streamline;
	Float2 jitter;
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
		a_provider.hostResourcesCompatible = true;
		a_provider.fsrContextCount = 2;
		a_provider.fsrContextValid = { true, true };
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
				provider.hostResourcesCompatible = false;
			if (blocker == 2)
				provider.runtimeUpscalerUsedForFrame = true;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(0, 1284)) == Result::Deferred,
				"Pending runtime used an incomplete host pair or mixed providers");
			RequireDeferredUntouched(upscaling);
		}
	}

	void GenuineFailuresRemainFailures()
	{
		for (auto failure : { Lifecycle::Failed, Lifecycle::DeviceLost, Lifecycle::RuntimeDeviceLost }) {
			auto& upscaling = Reset();
			upscaling.fidelityFX.runtimeResult = failure;
			Require(upscaling.DispatchVendorEyeRegion(UpscaleMethod::kFSR, Region(1, 504)) == Result::Failed &&
					upscaling.failedEvaluations == 1 && upscaling.successfulEvaluations == 0 &&
					upscaling.fidelityFX.deviceProbes == 1 && upscaling.deviceLossHandlers == 1 &&
					upscaling.fidelityFX.hostCalls == 0,
				"A genuine provider failure was hidden as deferred or ready");
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
				presentation.submitStageVendorAdmissionGeneration = 0;
				presentation.submitStageVendorAdmissionMethod = 0;
				presentation.submitStageVendorAdmissionFrame = 0;
			}
			Require(presentation.Present() && presentation.stretches == 1 && presentation.unbinds == 1 &&
					presentation.lastPath == DeferredPresentation::VRRenderScalePresentationPath::PresentationStretch &&
					presentation.historyResets == 0 && presentation.submitStageVendorAdmissionPresentationOnly &&
					presentation.submitStageVendorAdmissionCycle == presentation.a_compositorCycleToken &&
					presentation.submitStageVendorAdmissionGeneration == presentation.activeContractGeneration &&
					presentation.submitStageVendorAdmissionMethod == static_cast<uint32_t>(presentation.upscaleMethod) &&
					presentation.submitStageVendorAdmissionFrame == presentation.currentFrame &&
					!presentation.submitStageVendorAdmissionExactProviderReady &&
					!presentation.submitStageVendorAdmissionAuthoritativeDLSSProfile &&
					presentation.submitStageVendorAdmissionDLSSQualityMode == 0 &&
					presentation.submitStageVendorAdmissionDLSSPreset == DeferredPresentation::kDLSSPresetK,
				"Deferred first-eye presentation lost its cycle hold or recorded failure recovery");
			if (admissionWasCleared)
				Require(presentation.submitStageVendorAdmissionEyeMask == 0,
					"Restoring cleared admission retained an old eye claim");
		}
		for (uint32_t mismatch = 0; mismatch < 3; ++mismatch) {
			DeferredPresentation presentation;
			if (mismatch == 0)
				++presentation.submitStageVendorAdmissionCycle;
			if (mismatch == 1)
				++presentation.submitStageVendorAdmissionGeneration;
			if (mismatch == 2)
				++presentation.submitStageVendorAdmissionMethod;
			Require(!presentation.Present() && presentation.stretches == 0 &&
					!presentation.submitStageVendorAdmissionPresentationOnly,
				"Deferred presentation overwrote another stereo cycle or contract");
		}
		for (uint32_t staleField = 0; staleField < 6; ++staleField) {
			DeferredPresentation presentation;
			presentation.submitStageVendorEyeState[0].ready = true;
			if (staleField == 1)
				--presentation.submitStageVendorOutputFrame;
			if (staleField == 2)
				--presentation.submitStageVendorOutputCompositorCycle;
			if (staleField == 3)
				--presentation.submitStageVendorOutputGeneration;
			if (staleField == 4)
				++presentation.submitStageVendorEyeState[0].method;
			if (staleField == 5)
				--presentation.submitStageVendorEyeState[0].generation;
			Require(presentation.Present() && presentation.historyResets == (staleField == 0 ? 1u : 0u),
				"Deferred presentation reset history without a completed eye from this cycle");
		}
		DeferredPresentation failedStretch;
		failedStretch.stretchReady = false;
		Require(!failedStretch.Present() && failedStretch.submitStageVendorAdmissionPresentationOnly,
			"A failed stretch presentation released the deferred peer-eye hold");
	}
}

int main()
{
	ColdRuntimeWithoutPeerProof();
	DeferredAdmissionAndHostFallback();
	GenuineFailuresRemainFailures();
	DeferredPresentationRetainsCycleOwnership();
}
