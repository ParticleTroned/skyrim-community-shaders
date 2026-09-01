#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "Features/FoveatedCommon.h"

namespace
{
	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << a_message << '\n';
			std::exit(1);
		}
	}

	struct State
	{
		bool pendingPostLoadRuntimeReset = false;
	};
	struct float2
	{
		float x = 0, y = 0;
	};
	struct Texture
	{
		bool resource = true;
		std::unique_ptr<int> uav = std::make_unique<int>(1);
		struct
		{
			uint32_t Width = 1280, Height = 1440;
		} desc;
	};
	template <class... Args>
	void LogWarnOnce(Args&&...)
	{}
	template <class F>
	struct ScopeExit
	{
		F fn;
		~ScopeExit() { fn(); }
	};
}

namespace globals
{
	State* state = nullptr;
	namespace game
	{
		bool isVR = true;
	}
}

#define CS_GPU_PASS(name) (void)0

struct Upscaling
{
	enum class UpscaleMethod
	{
		None,
		TAA,
		DLSS,
		FSR
	};
	struct Settings
	{
		bool foveatedVendorDispatch = true;
		bool foveatedPeripheryMaskVisualization = true;
		bool periphery_taa_enable = false;
		float center = 1.0f;
		float taaCenter = 0.4f;
	} settings;
	struct
	{
		bool knownMenuContextActive = false;
		bool menuContextActive = false;
		bool loadingMenuActive = false;
	} runtimeResolutionPlan;
	struct Profile
	{
		float centerScale = 0, centerHorizontalScale = 1.25f;
	};
	bool mainMenu = false, loadingMenu = false, nativeMenu = false, loadingProtection = false;
	bool deferred = false, saveReuse = false, deviceLost = false, dispatchReady = true;
	bool throwDispatch = false;
	bool foveatedPeripheryCS = true, foveatedPeripheryCB = true;
	std::array<std::unique_ptr<Texture>, 2> vrIntermediateColorOut{
		std::make_unique<Texture>(), std::make_unique<Texture>()
	};
	uint32_t ensures = 0, creations = 0, dispatches = 0, unbinds = 0;
	float dispatchedScale = 0;
	float2 dispatchedOffset{};
	bool usedTaaOffsets = false;
	bool IsMainMenuContextActive() const { return mainMenu; }
	bool IsLoadingMenuContextActive() const { return loadingMenu; }
	bool IsVRMenuPresentationContextActive() const { return nativeMenu; }
	bool ShouldDeferVRVendorLifecycleMutation() const { return deferred; }
	bool IsSubmitStageDeviceLost() const { return deviceLost; }
	bool MarkSubmitStageDeviceLostIfDeviceRemoved(const char*) const { return deviceLost; }
	bool MarkSubmitStageDeviceLostIfNeeded(const std::exception&, const char*) const { return deviceLost; }
	void UnbindUpscalingResources() { ++unbinds; }
	bool EnsureFoveatedDispatchShaders(bool a_taa, bool a_visualize, const char*, const char*)
	{
		Check(!a_taa && a_visualize, "Preview must not prepare temporal shaders");
		++ensures;
		if (saveReuse)
			return foveatedPeripheryCS && foveatedPeripheryCB;
		if (!foveatedPeripheryCS) {
			++creations;
			foveatedPeripheryCS = true;
		}
		return foveatedPeripheryCB;
	}
	std::array<float2, 2> GetResolvedFoveatedMaskCenterOffsets(bool a_taa)
	{
		usedTaaOffsets = a_taa;
		return { float2{ -0.1f, 0.2f }, float2{ 0.3f, -0.4f } };
	}
	bool DispatchFoveatedPeripheryPass(void* a_source, int* a_output, uint32_t a_sourceWidth, uint32_t a_sourceHeight,
		uint32_t a_width, uint32_t a_height, uint32_t a_x, uint32_t a_y, uint32_t a_dispatchWidth, uint32_t a_dispatchHeight,
		float a_scale, float a_horizontalScale, bool a_keepBindings, float, float, float, float, float a_offsetX, float a_offsetY, bool a_visualize)
	{
		Check(!a_source && !a_sourceWidth && !a_sourceHeight, "Preview must not require vendor inputs");
		Check(a_output && a_width == 1280 && a_height == 1440, "Preview must use the actual eye output");
		Check(!a_x && !a_y && a_dispatchWidth == a_width && a_dispatchHeight == a_height,
			"Preview must cover the entire eye, including full coverage");
		Check(a_visualize && !a_keepBindings && a_horizontalScale == 1.25f, "Preview must explicitly select mask drawing");
		++dispatches;
		if (throwDispatch)
			throw std::runtime_error("simulated constant buffer update failure");
		dispatchedScale = a_scale;
		dispatchedOffset = { a_offsetX, a_offsetY };
		return dispatchReady;
	}
	bool IsFoveatedMaskVisualizationEnabled(UpscaleMethod) const;
	bool DispatchFoveatedMaskVisualization(uint32_t);
};

bool IsFoveatedVendorDispatchRequested(const Upscaling::Settings& a_settings, Upscaling::UpscaleMethod a_method)
{
	return globals::game::isVR && a_settings.foveatedVendorDispatch &&
	       (a_method == Upscaling::UpscaleMethod::DLSS || a_method == Upscaling::UpscaleMethod::FSR);
}
bool IsVRLoadingSubmitProtectionContextActive(const Upscaling& a_upscaling, State*)
{
	return a_upscaling.loadingProtection;
}
Upscaling::Profile GetFoveatedMaskProfileParams(const Upscaling::Settings& a_settings, bool a_taa)
{
	return { FoveatedCommon::ClampCenterScale(a_taa ? a_settings.taaCenter : a_settings.center), 1.25f };
}

#include "foveated_mask_visualization_under_test.h"

int main()
{
	State state;
	globals::state = &state;
	Upscaling upscaling;
	using Method = Upscaling::UpscaleMethod;
	for (const auto method : { Method::DLSS, Method::FSR }) {
		for (float scale : { 0.25f, 0.999f, 1.0f }) {
			upscaling.settings.center = scale;
			Check(upscaling.IsFoveatedMaskVisualizationEnabled(method), "Full coverage must not suppress preview");
			Check(upscaling.DispatchFoveatedMaskVisualization(0), "Left eye preview must draw");
			Check(upscaling.dispatchedScale == scale && upscaling.dispatchedOffset.x == -0.1f, "Left profile must match");
		}
	}
	upscaling.settings.periphery_taa_enable = true;
	Check(upscaling.DispatchFoveatedMaskVisualization(1) && upscaling.dispatchedScale == 0.4f,
		"TAA preview must select its own center scale");
	upscaling.settings.taaCenter = 1.0f;
	Check(upscaling.DispatchFoveatedMaskVisualization(1), "Right eye full-coverage TAA profile must draw");
	Check(upscaling.dispatchedScale == 1.0f && upscaling.usedTaaOffsets && upscaling.dispatchedOffset.y == -0.4f,
		"Full coverage must retain the selected TAA profile and eye offsets");
	for (auto* gate : { &upscaling.mainMenu, &upscaling.loadingMenu, &upscaling.nativeMenu,
			 &upscaling.loadingProtection, &state.pendingPostLoadRuntimeReset,
			 &upscaling.runtimeResolutionPlan.knownMenuContextActive,
			 &upscaling.runtimeResolutionPlan.menuContextActive, &upscaling.runtimeResolutionPlan.loadingMenuActive }) {
		*gate = true;
		Check(!upscaling.IsFoveatedMaskVisualizationEnabled(Method::DLSS), "Native menu/load protection must prevail");
		*gate = false;
	}
	Check(!upscaling.IsFoveatedMaskVisualizationEnabled(Method::None) && !upscaling.IsFoveatedMaskVisualizationEnabled(Method::TAA), "Unsupported methods must remain inactive");
	globals::game::isVR = false;
	Check(!upscaling.IsFoveatedMaskVisualizationEnabled(Method::FSR) && !upscaling.DispatchFoveatedMaskVisualization(0), "SE/AE must remain inactive");
	globals::game::isVR = true;
	upscaling.settings.foveatedVendorDispatch = false;
	Check(!upscaling.IsFoveatedMaskVisualizationEnabled(Method::FSR), "Master switch must remain authoritative");
	upscaling.settings.foveatedVendorDispatch = true;
	upscaling.settings.foveatedPeripheryMaskVisualization = false;
	Check(!upscaling.IsFoveatedMaskVisualizationEnabled(Method::FSR) && !upscaling.DispatchFoveatedMaskVisualization(0), "Disabled preview must not draw");
	upscaling.settings.foveatedPeripheryMaskVisualization = true;
	globals::state = nullptr;
	Check(!upscaling.IsFoveatedMaskVisualizationEnabled(Method::FSR), "Missing renderer state must fail closed");
	globals::state = &state;
	upscaling.deferred = upscaling.saveReuse = true;
	Check(upscaling.DispatchFoveatedMaskVisualization(0), "Existing resources must draw during save protection");
	upscaling.foveatedPeripheryCS = false;
	Check(!upscaling.DispatchFoveatedMaskVisualization(0) && !upscaling.creations, "Lifecycle protection must prohibit preview creation");
	upscaling.deferred = false;
	Check(!upscaling.DispatchFoveatedMaskVisualization(0) && !upscaling.creations, "Save protection must prohibit preview creation");
	upscaling.saveReuse = false;
	Check(upscaling.DispatchFoveatedMaskVisualization(0) && upscaling.creations == 1, "Preview must recover after protection clears");
	upscaling.dispatchReady = false;
	Check(!upscaling.DispatchFoveatedMaskVisualization(0), "Failed drawing must not be reported as a completed preview");
	upscaling.throwDispatch = true;
	Check(!upscaling.DispatchFoveatedMaskVisualization(0), "Drawing exceptions must fall back safely");
	upscaling.throwDispatch = false;
	Check(upscaling.dispatches == upscaling.unbinds, "Every dispatched preview must release bindings");
	Check(!upscaling.DispatchFoveatedMaskVisualization(2), "Invalid eyes must fail closed");
	upscaling.vrIntermediateColorOut[0]->uav.reset();
	Check(!upscaling.DispatchFoveatedMaskVisualization(0), "Missing output view must fail closed");
	upscaling.deviceLost = true;
	Check(!upscaling.DispatchFoveatedMaskVisualization(1), "Device loss must fail closed");
	std::cout << "FOV mask preview admission and dispatch checks passed\n";
}
