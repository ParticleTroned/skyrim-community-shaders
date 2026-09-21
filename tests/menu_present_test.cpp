#include <dxgi.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

namespace
{
	unsigned effects = 0;
	unsigned presentCalls = 0;
	UINT forwardedFlags = 0;
	UINT forwardedInterval = 0;
	IDXGISwapChain* forwardedChain = nullptr;
	HRESULT presentResult = S_OK;
	bool diagnostics = false;
	bool frameGeneration = false;
	bool flatBoundaryOwned = true;
	bool timingSupported = true;
	unsigned flatBegins = 0, flatCompletions = 0, timingCapabilityQueries = 0;
	uint32_t timingFrame = 0;
	HRESULT timingResult = S_OK;
	std::string renderOrder;
}
namespace globals
{
	struct State
	{
		bool startupMenuBlurSourceReady = false;
		std::atomic<bool> startupMenuInitializationComplete{ true };
		uint32_t frameCount = 0;
		int tracyCtx = 0;
		void Reset()
		{
			renderOrder += "reset;";
			++effects;
			++frameCount;
		}
	} stateValue;
	State* state = &stateValue;
	struct Menu
	{
		void DrawOverlay()
		{
			++effects;
			renderOrder += "overlay;";
		}
	} menuValue;
	Menu* menu = &menuValue;
	struct Profiler
	{
		bool BeginFlatPresent(uint32_t frame, UINT, bool supported)
		{
			++flatBegins;
			timingFrame = frame;
			timingSupported = supported;
			renderOrder += "timing-end;";
			return flatBoundaryOwned;
		}
		void CompleteFlatPresent(HRESULT result)
		{
			++flatCompletions;
			timingResult = result;
			renderOrder += "timing-begin;";
		}
	} profilerValue;
	Profiler* profiler = &profilerValue;
	namespace game
	{
		bool isVR = false;
	}
	namespace d3d
	{
		void* context = nullptr;
	}
	namespace features
	{
		struct
		{
			bool IsFrameGenerationDx12PathActive()
			{
				++timingCapabilityQueries;
				return frameGeneration;
			}
			void PresentVRMenuDesktopMirror(IDXGISwapChain*)
			{
				++effects;
				renderOrder += "mirror;";
			}
		} upscaling;
		struct
		{
			void OnBeforePresent(IDXGISwapChain*)
			{
				++effects;
				renderOrder += "capture;";
			}
			void DrawPostCaptureIndicator()
			{
				++effects;
				renderOrder += "indicator;";
			}
		} screenshotFeature;
	}
}
namespace CSX::Api
{
	void AdvanceAcceptedDrawFrame(void*)
	{
		++effects;
		renderOrder += "accepted;";
	}
}
namespace REL
{
	template <class Function>
	struct Relocation
	{
		Function* target = nullptr;
		template <class... Args>
		auto operator()(Args... args)
		{
			return target(args...);
		}
	};
}
bool ShouldRecordCSFramePhaseDiag()
{
	++effects;
	return diagnostics;
}
uint64_t ReadFrameDiagCounterTicks()
{
	++effects;
	return 1;
}
double ConvertFrameDiagTicksToMilliseconds(uint64_t)
{
	++effects;
	return 1;
}
void FlushCSFrameHookPhaseDiag(uint32_t, double) { ++effects; }
void TracyD3D11Collect(int)
{
	++effects;
	renderOrder += "collect;";
}
void LogFrameIntervalSpikeIfNeeded(uint64_t, uint64_t, uint64_t, uint64_t, HRESULT) { ++effects; }

#include "menu_present_under_test.h"

int main()
{
	IDXGISwapChain_Present::func.target = [](IDXGISwapChain* chain, UINT interval, UINT flags) -> HRESULT {
		++presentCalls;
		renderOrder += "present;";
		forwardedChain = chain;
		forwardedInterval = interval;
		forwardedFlags = flags;
		return presentResult;
	};
	bool passed = true;
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		for (bool diag : { false, true }) {
			diagnostics = diag;
			for (UINT flags : { UINT(DXGI_PRESENT_TEST), UINT(DXGI_PRESENT_TEST | DXGI_PRESENT_DO_NOT_WAIT), UINT(0) }) {
				for (HRESULT result : { S_OK, DXGI_STATUS_OCCLUDED, E_FAIL }) {
					for (bool initialized : { false, true }) {
						for (bool blurReady : { false, true }) {
							effects = presentCalls = 0;
							flatBegins = flatCompletions = timingCapabilityQueries = 0;
							renderOrder.clear();
							globals::stateValue.frameCount = 0;
							globals::stateValue.startupMenuInitializationComplete = initialized;
							globals::stateValue.startupMenuBlurSourceReady = blurReady;
							presentResult = result;
							auto* chain = reinterpret_cast<IDXGISwapChain*>(uintptr_t(1));
							const auto actual = IDXGISwapChain_Present::thunk(chain, 2, flags);
							const bool test = !vr && (flags & DXGI_PRESENT_TEST) != 0;
							const char* expectedOrder = test ? "present;" :
							                                   (vr ? "mirror;reset;accepted;overlay;capture;indicator;present;collect;" :
																	 "mirror;reset;overlay;capture;indicator;timing-end;present;timing-begin;collect;");
							const bool expectedBlurReady = blurReady || (!test && initialized && SUCCEEDED(result));
							if (actual != result || presentCalls != 1 || forwardedChain != chain || forwardedFlags != flags || forwardedInterval != 2 ||
								flatBegins != unsigned(!vr && !test) || flatCompletions != flatBegins || timingCapabilityQueries != flatBegins ||
								(!vr && !test && (timingFrame != 0 || timingResult != result || !timingSupported)) ||
								renderOrder != expectedOrder || globals::stateValue.startupMenuBlurSourceReady != expectedBlurReady ||
								(test && (effects != 0 || globals::stateValue.frameCount != 0)) ||
								(!test && (effects == 0 || globals::stateValue.frameCount != 1))) {
								std::printf("FAIL: Present vr=%d diag=%d flags=%u result=%ld effects=%u\n", vr, diag, flags, result, effects);
								passed = false;
							}
						}
					}
				}
			}
		}
	}
	// Only the boundary owner may complete; DX12 support is forwarded before acquisition.
	globals::game::isVR = false;
	frameGeneration = true;
	for (bool owned : { false, true }) {
		flatBoundaryOwned = owned;
		flatBegins = flatCompletions = timingCapabilityQueries = 0;
		IDXGISwapChain_Present::thunk(nullptr, 0, 0);
		passed = passed && flatBegins == 1 && flatCompletions == unsigned(owned) &&
		         timingCapabilityQueries == 1 && !timingSupported;
	}
	// A flat status probe must be safe before rendering state is available.
	globals::game::isVR = false;
	globals::state = nullptr;
	globals::menu = nullptr;
	globals::profiler = nullptr;
	effects = presentCalls = 0;
	renderOrder.clear();
	presentResult = S_OK;
	const auto probeResult = IDXGISwapChain_Present::thunk(nullptr, 0, DXGI_PRESENT_TEST);
	passed = passed && probeResult == S_OK && presentCalls == 1 && effects == 0 && renderOrder == "present;";
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
