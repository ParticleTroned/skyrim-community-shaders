#include "Features/ScreenshotApiPolicy.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Compile the production source selection and Present handler against capture
// dependencies that record effects without requiring a game or D3D device.
struct IDXGISwapChain
{};
namespace vr
{
	enum EVREye
	{
		Eye_Left,
		Eye_Right
	};
}
namespace globals
{
	struct State
	{
		uint32_t frameCount = 100;
	};
	State testState;
	State* state = &testState;
}
namespace logger
{
	void warn(std::string_view) {}
	template <class... Args>
	void debug(std::string_view, Args&&...)
	{}
}

struct ScreenshotFeature;
struct ScreenshotApi
{
	std::function<void(ScreenshotFeature&)> nextTick;
	std::vector<std::string> terminalErrors;
	std::vector<std::string> cancellationCallbacks;
	void Tick(ScreenshotFeature& feature, uint32_t);
	void OnFeatureDisabled(std::string_view)
	{
		cancellationCallbacks.emplace_back("feature_disabled");
	}
	void OnSourceTerminal(std::string_view, std::string_view, std::string_view error)
	{
		terminalErrors.emplace_back(error);
		cancellationCallbacks.emplace_back("source_terminal");
	}
};

struct ScreenshotFeature
{
#include "screenshot_capture_types_under_test.h"
	struct OutputPlan
	{
		OutputView view;
	};
	struct CaptureOptions
	{
		std::vector<OutputPlan> outputs;
		vr::EVREye framedEye = vr::Eye_Left;
		std::string requestId = "capture-test";
		bool allowDesktopFallback = false;
	};
	struct ActiveCapture
	{
		bool pending = false;
		VRCaptureSource source = VRCaptureSource::HMDSubmission;
		CaptureOptions options;
		uint32_t presentsWaited = 0;
		bool ownsQueueSlot = false;
	};
	ScreenshotApi api;
	ScreenshotApi* screenshotApi = &api;
	ActiveCapture activeCapture;
	std::atomic_bool capturePending = false;
	std::mutex captureStateMutex;
	std::atomic_bool enabled = true;
	uint32_t outstandingSlots = 0;
	uint32_t desktopCopies = 0;
	uint32_t fallbacks = 0;

	void ArmCapture(std::string_view sourceKind, uint8_t requiredEyeMask, CaptureOptions options)
	{
#include "screenshot_capture_selection_under_test.h"
		activeCapture = { true, requestedSource, std::move(options), 0, true };
		capturePending = true;
		++outstandingSlots;
	}
	bool HasPendingCapture() const { return capturePending; }
	bool IsRuntimeEnabled() const { return enabled; }
	void RestoreReadbackContextProtectionIfIdle() {}
	void EnsureScreenshotApi() {}
	void ShowInGameNotification(std::string_view) {}
	void ClearActiveCapture(ActiveCapture& capture)
	{
		if (capture.ownsQueueSlot)
			--outstandingSlots;
		capture = {};
	}
	void FallBackToDesktopCapture(ActiveCapture& capture, std::string_view)
	{
		capture.source = VRCaptureSource::DesktopMirror;
		++fallbacks;
	}
	bool QueueDesktopCapture(IDXGISwapChain*, const CaptureOptions&, bool ownsSlot)
	{
		if (!ownsSlot)
			throw std::runtime_error("desktop capture lost its encoder slot");
		++desktopCopies;
		--outstandingSlots;
		return true;
	}
	void OnBeforePresent(IDXGISwapChain*);
	void SetEnabled(bool);
};

void ScreenshotApi::Tick(ScreenshotFeature& feature, uint32_t)
{
	if (auto dispatch = std::exchange(nextTick, {}))
		dispatch(feature);
}

#include "screenshot_capture_present_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
try {
	using Source = ScreenshotFeature::VRCaptureSource;
	using View = ScreenshotFeature::OutputView;
	using Options = ScreenshotFeature::CaptureOptions;
	using CSX::ScreenshotPolicy::RequiredEyeMask;
	struct EyeCase
	{
		View raw;
		View framed;
		const char* rawName;
		const char* framedName;
		vr::EVREye eye;
	};
	for (const auto& eye : {
			 EyeCase{ View::LeftEye, View::FramedLeft, "left_eye", "framed_left", vr::Eye_Left },
			 EyeCase{ View::RightEye, View::FramedRight, "right_eye", "framed_right", vr::Eye_Right } }) {
		for (const bool frameSecondOutput : { false, true }) {
			ScreenshotFeature feature;
			feature.api.nextTick = [=](ScreenshotFeature& target) {
				Options options;
				options.outputs = { { eye.raw }, { frameSecondOutput ? eye.framed : eye.raw } };
				const auto mask = RequiredEyeMask(eye.rawName) |
				                  RequiredEyeMask(frameSecondOutput ? eye.framedName : eye.rawName);
				target.ArmCapture("hmd_submission", static_cast<uint8_t>(mask), std::move(options));
			};
			for (uint32_t present = 1; present < kCaptureTimeoutPresents; ++present) {
				feature.OnBeforePresent(nullptr);
				Require(feature.HasPendingCapture() && feature.outstandingSlots == 1,
					"same-eye outputs must wait for Submit while retaining the encoder slot");
				Require(feature.activeCapture.source == Source::HMDEye &&
							feature.activeCapture.options.framedEye == eye.eye,
					"same-eye outputs must acquire only their requested eye");
				Require(feature.desktopCopies == 0 && feature.api.terminalErrors.empty(),
					"Present must not acquire a desktop source for HMD eye outputs");
			}
			feature.OnBeforePresent(nullptr);
			Require(!feature.HasPendingCapture() && feature.outstandingSlots == 0 &&
						feature.desktopCopies == 0 && feature.api.terminalErrors == std::vector<std::string>{ "source_timeout" },
				"missing raw-eye Submit must time out and release its encoder slot");
		}
	}

	for (const auto view : { View::FramedLeft, View::FramedRight, View::SideBySide }) {
		ScreenshotFeature feature;
		Options options;
		options.outputs = { { view } };
		feature.ArmCapture("hmd_submission", view == View::SideBySide ? 3 : (view == View::FramedRight ? 2 : 1), options);
		for (uint32_t present = 0; present < kCaptureTimeoutPresents; ++present)
			feature.OnBeforePresent(nullptr);
		Require(!feature.HasPendingCapture() && feature.desktopCopies == 0 && feature.outstandingSlots == 0 &&
					feature.api.terminalErrors == std::vector<std::string>{ "source_timeout" },
			"framed and stereo captures must retain their bounded HMD wait");
	}

	ScreenshotFeature desktop;
	desktop.api.nextTick = [](ScreenshotFeature& target) {
		target.ArmCapture("desktop_mirror", 3, Options{});
	};
	desktop.OnBeforePresent(nullptr);
	Require(desktop.desktopCopies == 1 && !desktop.HasPendingCapture() && desktop.outstandingSlots == 0,
		"desktop capture armed by Tick must still acquire during the same Present");

	ScreenshotFeature fallback;
	Options fallbackOptions;
	fallbackOptions.allowDesktopFallback = true;
	fallbackOptions.outputs = { { View::SourceNative } };
	fallback.ArmCapture("hmd_submission", 3, fallbackOptions);
	for (uint32_t present = 0; present < kCaptureTimeoutPresents; ++present)
		fallback.OnBeforePresent(nullptr);
	Require(fallback.fallbacks == 1 && fallback.HasPendingCapture() && fallback.outstandingSlots == 1,
		"explicit stereo fallback must retain ownership until desktop staging");
	fallback.OnBeforePresent(nullptr);
	Require(fallback.desktopCopies == 1 && !fallback.HasPendingCapture() && fallback.outstandingSlots == 0,
		"explicit desktop fallback must complete on the following Present");

	ScreenshotFeature disabled;
	Options pendingOptions;
	pendingOptions.outputs = { { View::LeftEye } };
	disabled.ArmCapture("hmd_submission", 1, pendingOptions);
	disabled.SetEnabled(false);
	Require(!disabled.IsRuntimeEnabled() && !disabled.HasPendingCapture() && disabled.outstandingSlots == 0,
		"disabling capture must clear acquisition and release its reserved encoder slot");
	Require(disabled.api.cancellationCallbacks == std::vector<std::string>{ "feature_disabled", "source_terminal" },
		"parent cancellation must precede terminal acquisition callbacks that can finalize the sequence");
} catch (const std::exception& error) {
	std::fprintf(stderr, "%s\n", error.what());
	return 1;
}
