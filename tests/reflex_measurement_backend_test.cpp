#include "Diagnostics/ReflexMeasurement.h"
#include "Features/Upscaling/ReflexBackend.h"
#include <cstdio>
#include <cstdlib>

namespace CSX::Diagnostics::ReflexMeasurement
{
	Tick Clock() noexcept
	{
		LARGE_INTEGER now{};
		QueryPerformanceCounter(&now);
		return now.QuadPart;
	}
	Journal& Records()
	{
		static Journal journal;
		return journal;
	}
	Journal::Ticket BeginNativeOperation() { return Records().BeginTimed(Clock, true); }
	bool NativeTracingActive() { return Records().nativeEnabled; }
}
namespace
{
	using namespace Reflex;
	using namespace CSX::Diagnostics::ReflexMeasurement;
	NvAPI_Status settingsResult = NVAPI_OK, sleepResult = NVAPI_OK, getterResult = NVAPI_OK;
	IUnknown* bound{};
	NV_SET_SLEEP_MODE_PARAMS submitted{};
	void Check(bool condition, const char* reason)
	{
		if (!condition) {
			std::fprintf(stderr, "%s\n", reason);
			std::exit(EXIT_FAILURE);
		}
	}
	NvAPI_Status __cdecl Settings(IUnknown* device, NV_SET_SLEEP_MODE_PARAMS* p)
	{
		bound = device;
		submitted = *p;
		p->minimumIntervalUs = 123;
		return settingsResult;
	}
	NvAPI_Status __cdecl Sleep(IUnknown* device)
	{
		Check(bound == device, "native device mismatch");
		return sleepResult;
	}
	NvAPI_Status __cdecl Getter(IUnknown* device, NV_GET_SLEEP_STATUS_PARAMS* p)
	{
		Check(bound == device, "getter used a different native device");
		Check(p->version == NV_GET_SLEEP_STATUS_PARAMS_VER, "getter structure version missing");
		p->bLowLatencyMode = 1;
		p->sleepIntervalUs = 7;
		return getterResult;
	}
}
int main()
{
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			  D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr)),
		"WARP device unavailable");
	Backend backend({ Settings, Sleep, Getter });
	Check(backend.BindDevice(device.Get(), true) == NVAPI_OK, "injected native binding failed");
	auto& j = Records();
	j.Start(Mode::Passive, 16, Clock());
	sleepResult = NVAPI_ERROR;
	const auto failed = backend.Update({ Mode::Boost, 33333, false }, 10, 100);
	j.Close(Clock());
	j.Freeze(Clock(), true, false);
	Check(j.native.size() == 2 && j.native[0].settings && !j.native[1].settings, "settings/sleep streams conflated");
	Check(j.native[0].result == NVAPI_OK && j.native[1].result == NVAPI_ERROR, "separate results missing");
	Check(j.native[0].intervalUs == 33333 && submitted.minimumIntervalUs == 33333, "journal used post-call mutation or desired options instead of actual submitted structure");
	const auto binding = backend.InspectDiagnosticBinding();
	Check(binding.submitted.minimumIntervalUs == 33333 && binding.submissionResult == NVAPI_OK, "settled settings receipt missing actual submission");
	for (const auto result : { NVAPI_OK, NVAPI_ERROR, NVAPI_NOT_SUPPORTED }) {
		getterResult = result;
		const auto readback = backend.ReadSleepStatus();
		const auto after = backend.GetStatus();
		Check(readback.result == result && readback.generation == failed.status.deviceGeneration && readback.enter <= readback.exit, "readback receipt invalid");
		Check(after.health == failed.status.health && after.failures == failed.status.failures && after.retryAtMs == failed.status.retryAtMs &&
				  after.pending == failed.status.pending && after.accepted == failed.status.accepted && after.desired == failed.status.desired,
			"diagnostic query changed recovery/health/preferences");
	}
	j.Start(Mode::Passive, 16, Clock());
	const auto sameFailure = backend.Update({ Mode::Boost, 33333, false }, 10, 101);
	Check(sameFailure.attemptsObserved && !sameFailure.sleepAttempted && sameFailure.sleepSkipReason == SleepSkip::SameFrame, "same-frame sleep exclusion not distinguished");
	const auto backoff = backend.Update({ Mode::Boost, 33333, false }, 11, 150);
	Check(!backoff.settingsAttempted && !backoff.sleepAttempted && backoff.sleepSkipReason == SleepSkip::Backoff, "backoff was presented as a fresh native attempt");
	sleepResult = NVAPI_OK;
	const auto recovered = backend.Update({ Mode::Boost, 33333, false }, 12, 200);
	Check(recovered.settingsAttempted && recovered.sleepAttempted && recovered.sleepSkipReason == SleepSkip::Attempted, "native recovery attempts not reported");
	const auto off = backend.Update({}, 13, 210);
	Check(off.settingsAttempted && !off.sleepAttempted && off.sleepSkipReason == SleepSkip::NotRequested, "Off/zero was not distinguished from failed pacing");
	Check(backend.Disable(220).sleepSkipReason == SleepSkip::NoFrame, "non-frame disable was labelled as a frame sleep");
	Backend unavailable(NativeCalls{});
	unavailable.BindDevice(device.Get(), true);
	Check(unavailable.Update({ Mode::On, 0, false }, 20, 300).sleepSkipReason == SleepSkip::BackendUnavailable, "unavailable backend skip missing");
	settingsResult = NVAPI_ERROR;
	Backend rejected({ Settings, Sleep });
	rejected.BindDevice(device.Get(), true);
	const auto unaccepted = rejected.Update({ Mode::On, 0, false }, 21, 400);
	Check(unaccepted.settingsAttempted && !unaccepted.sleepAttempted && unaccepted.sleepSkipReason == SleepSkip::SettingsPending, "failed settings was confused with successful sleep");
	settingsResult = NVAPI_OK;
	j.Close(Clock());
	j.Freeze(Clock(), true, false);
	Backend unsupported({ Settings, Sleep });
	unsupported.BindDevice(device.Get(), true);
	Check(unsupported.ReadSleepStatus().result == NVAPI_NO_IMPLEMENTATION, "missing optional getter not unsupported");
	j.Start(Mode::Minimal, 16, Clock());
	unsupported.Update({}, 11, 110);
	j.Close(Clock());
	j.Freeze(Clock(), true, false);
	Check(j.native.empty(), "minimal app observer traced native calls");
	return EXIT_SUCCESS;
}
