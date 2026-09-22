#include "Features/FoveatedCommon.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace globals::game
{
	bool isVR = true;
}

struct Upscaling
{
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kDLSS,
		kFSR
	};
	enum class DLSSSharpenerMode
	{
		RCAS
	};
	enum class VRUpscalingTransitionOrigin
	{
		CSMenu
	};
	static constexpr uint32_t kDLSSPresetK = 3, kQualityModeMaxIndex = 6;
	struct Settings
	{
		bool neuralRenderingEnabled = true, neuralRenderingFovOnly = false;
		bool neuralRenderingRenderscaleFov = false, foveatedVendorDispatch = false;
		bool periphery_taa_enable = false;
		uint32_t neuralRenderingMode = 2;
		float foveatedCenterArea = 0.6f, periphery_taa_center_area = 0.3f;
		float foveatedCenterHorizontalScale = 1.1f;
		float foveatedLeftEyeMaskOffsetX = 0, foveatedLeftEyeMaskOffsetY = 0;
		float foveatedRightEyeMaskOffsetX = 0, foveatedRightEyeMaskOffsetY = 0;
	} settings;
#include "neural_resource_key_types.h"
	struct FidelityFXFixture
	{
		bool runtime = false, failed = false, fsr4 = false, fsr4Failed = false;
		bool IsRuntimeUpscalerFailureLatched() const { return failed; }
		bool ShouldUseRuntimeUpscalerForFSR() const { return runtime; }
		bool IsRuntimeFsr4FailureLatched() const { return fsr4Failed; }
		bool IsRuntimeFsr4Available() const { return fsr4; }
	} fidelityFX;
	bool neuralRenderingFeatureAvailable = true;
	bool requested = true, latched = true, active = true;
	mutable uint32_t liveRequestReads = 0;
	std::mutex pendingRequestMutex;
	// This boundary records reentry without aborting inside the noexcept caller.
	bool GetVRRenderScaleModeRequested() const
	{
		++liveRequestReads;
		return requested;
	}
	bool IsVRRenderScaleModeLatched() const { return latched; }
	bool IsVRRenderScaleModeActive() const { return active; }
	bool IsNeuralRenderingFovConfigurationAvailable() const { return settings.foveatedVendorDispatch && settings.foveatedCenterArea < 0.999f; }
	auto GetNeuralRenderingMode() const { return NeuralRendering::ClampRenderingMode(settings.neuralRenderingMode); }
	static uint32_t ClampDLSSPresetUInt(uint32_t value) { return std::min(value, 5u); }
	bool IsNeuralRenderingRenderScaleRequired() const noexcept;
	bool IsNeuralRenderingRenderScaleAvailable() const noexcept;
	bool IsNeuralRenderingRequested() const noexcept;
	bool IsFoveatedVendorDispatchEnabled(UpscaleMethod) const;
	bool IsPeripheryTAAEnabled(UpscaleMethod) const;
	VRRenderScaleResourceKey BuildVRRenderScaleResourceKey(const VRRenderScaleProfileSnapshot&) const;
};

float ClampFoveatedCenterScale(float value) { return FoveatedCommon::ClampCenterScale(value); }
float ClampFoveatedCenterHorizontalScale(float value) { return FoveatedCommon::ClampCenterHorizontalScale(value); }
float ClampFoveatedMaskOffsetAdjustment(float value) { return value; }
#include "neural_resource_key_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
{
	try {
		using Method = Upscaling::UpscaleMethod;
		using Mode = NeuralRendering::RenderingMode;
		using Backend = Upscaling::VRRenderScaleBackendKind;
		Upscaling upscaling;
		Upscaling::VRRenderScaleProfileSnapshot profile{};
		profile.valid = profile.active = true;
		profile.method = Method::kDLSS;
		profile.displayEyeWidth = 2000;
		profile.displayEyeHeight = 1800;
		profile.renderEyeWidth = 1000;
		profile.renderEyeHeight = 900;
		{
			std::scoped_lock requestLock(upscaling.pendingRequestMutex);
			const auto key = upscaling.BuildVRRenderScaleResourceKey(profile);
			Require(upscaling.liveRequestReads == 0, "Resource-key construction reentered the live pending-request query under its lock");
			Require(key.foveatedVendorDispatch && !key.peripheryTAA, "Full-image reduced NR must reserve its VR dispatch resources");
		}
		// Desired resources must exist before the incoming mode has latched.
		upscaling.requested = upscaling.latched = upscaling.active = false;
		Require(upscaling.BuildVRRenderScaleResourceKey(profile).foveatedVendorDispatch, "Target resources must not depend on the previous live latch");
		Require(!upscaling.IsNeuralRenderingRequested(), "Resource planning must not bypass runtime NR admission");
		upscaling.requested = upscaling.latched = upscaling.active = true;
		for (const bool vr : { false, true })
			for (const auto method : { Method::kNONE, Method::kTAA, Method::kDLSS, Method::kFSR })
				for (const auto mode : { Mode::FullResolution, Mode::Foveated, Mode::ReducedResolution })
					for (const bool enabled : { false, true })
						for (const bool available : { false, true })
							for (const bool masked : { false, true })
								for (const bool fov : { false, true })
									for (const bool taa : { false, true }) {
										globals::game::isVR = vr;
										profile.method = method;
										upscaling.settings.neuralRenderingMode = static_cast<uint32_t>(mode);
										upscaling.settings.neuralRenderingEnabled = enabled;
										upscaling.neuralRenderingFeatureAvailable = available;
										upscaling.settings.neuralRenderingRenderscaleFov = masked;
										upscaling.settings.foveatedVendorDispatch = fov;
										upscaling.settings.periphery_taa_enable = taa;
										const bool expectedDispatch = upscaling.IsFoveatedVendorDispatchEnabled(method);
										const bool expectedTAA = upscaling.IsPeripheryTAAEnabled(method);
										const auto reads = upscaling.liveRequestReads;
										std::scoped_lock requestLock(upscaling.pendingRequestMutex);
										const auto key = upscaling.BuildVRRenderScaleResourceKey(profile);
										Require(upscaling.liveRequestReads == reads, "Resource keys must not read unrelated pending state");
										Require(key.foveatedVendorDispatch == expectedDispatch && key.peripheryTAA == expectedTAA, "Settled resource keys must match runtime dispatch for every mode");
										Require(key.valid && key.contextCount == (vr ? 2u : 1u), "Runtime-specific resource identity must remain intact");
									}
		profile.active = false;
		const auto inactive = upscaling.BuildVRRenderScaleResourceKey(profile);
		Require(inactive.backend == Backend::None && !inactive.foveatedVendorDispatch && !inactive.peripheryTAA, "Native recovery must not request scaled vendor resources");
		profile.active = true;
		profile.method = Method::kFSR;
		Require(upscaling.BuildVRRenderScaleResourceKey(profile).backend == Backend::FSRHost, "Host FSR selection changed");
		upscaling.fidelityFX.runtime = true;
		Require(upscaling.BuildVRRenderScaleResourceKey(profile).backend == Backend::FSRRuntime, "Runtime FSR selection changed");
		profile.fsr4RuntimeEnabled = upscaling.fidelityFX.fsr4 = true;
		Require(upscaling.BuildVRRenderScaleResourceKey(profile).backend == Backend::FSR4Runtime, "FSR4 selection changed");
		upscaling.fidelityFX.fsr4Failed = true;
		Require(upscaling.BuildVRRenderScaleResourceKey(profile).backend == Backend::FSRRuntime, "FSR4 fallback changed");
		upscaling.fidelityFX.failed = true;
		Require(upscaling.BuildVRRenderScaleResourceKey(profile).backend == Backend::FSRHost, "Runtime FSR fallback changed");
		std::cout << "NR resource-key lock, target identity, runtime parity and fallback checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
