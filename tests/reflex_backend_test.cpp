#include "Features/Upscaling/ReflexBackend.h"

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace
{
	using namespace Reflex;
	using Microsoft::WRL::ComPtr;

	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::fprintf(stderr, "%s\n", a_message);
			std::exit(EXIT_FAILURE);
		}
	}

	namespace spdlog::level
	{
		enum level_enum
		{
			info,
			warn,
			off
		};
	}

	struct LoggingState
	{
		spdlog::level::level_enum minimum = spdlog::level::info;
		unsigned levelChecks = 0;
		unsigned loggerCalls = 0;
		unsigned emissions = 0;
		unsigned argumentConversions = 0;
	} logging;

	namespace spdlog
	{
		bool should_log(level::level_enum a_level)
		{
			++logging.levelChecks;
			return a_level >= logging.minimum;
		}
	}

	namespace magic_enum
	{
		template <class T>
		const char* enum_name(T)
		{
			++logging.argumentConversions;
			return "";
		}
	}

	void RecordLog(spdlog::level::level_enum a_level)
	{
		++logging.loggerCalls;
		if (spdlog::should_log(a_level))
			++logging.emissions;
	}

	namespace logger
	{
		template <class... Args>
		void warn(const char*, const Args&...)
		{
			RecordLog(spdlog::level::warn);
		}
		template <class... Args>
		void info(const char*, const Args&...)
		{
			RecordLog(spdlog::level::info);
		}
	}

#include "reflex_logging_under_test.h"

	struct NativeState
	{
		NvAPI_Status settingsResult = NVAPI_OK;
		NvAPI_Status sleepResult = NVAPI_OK;
		std::vector<NV_SET_SLEEP_MODE_PARAMS> writes;
		std::vector<IUnknown*> devices;
		unsigned sleeps = 0;
	};
	NativeState* current = nullptr;

	NvAPI_Status __cdecl SetSleepMode(IUnknown* a_device, NV_SET_SLEEP_MODE_PARAMS* a_parameters)
	{
		current->writes.push_back(*a_parameters);
		current->devices.push_back(a_device);
		return current->settingsResult;
	}

	NvAPI_Status __cdecl Sleep(IUnknown* a_device)
	{
		Check(!current->devices.empty() && current->devices.back() == a_device, "Sleep must use the configured native device");
		++current->sleeps;
		return current->sleepResult;
	}

	const NativeCalls kCalls{ SetSleepMode, Sleep };
	constexpr Options kOn{ Mode::On, 0, false };
	constexpr Options kBoost{ Mode::Boost, 50000, true };

	ComPtr<ID3D11Device> MakeDevice()
	{
		ComPtr<ID3D11Device> device;
		Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				  nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr)),
			"Offline tests require the Windows WARP D3D11 device");
		return device;
	}

	ULONG References(ID3D11Device* a_device)
	{
		a_device->AddRef();
		return a_device->Release();
	}

	void SettingsRetry(ID3D11Device* a_device)
	{
		NativeState native;
		current = &native;
		Backend backend(kCalls);
		Check(backend.BindDevice(a_device, true) == NVAPI_OK, "Native binding failed");
		Check(backend.Update({}, 1, 0).disabled, "Initial Off must be accepted");
		native.settingsResult = NVAPI_ERROR;
		auto result = backend.Update(kOn, 2, 10);
		Check(result.status.pending && result.status.accepted == Options{} &&
				  result.status.desired == kOn && result.status.health == Health::Recovering &&
				  result.status.nativeStatus == NVAPI_ERROR && !result.disabled,
			"Settings failure must retain historical acceptance, pending desire and actual failure");
		Check(native.writes.size() == 2 && native.sleeps == 0, "Failed configuration must not sleep");
		for (uint32_t frame = 3; frame < 100; ++frame)
			backend.Update(kOn, frame, 50);
		Check(native.writes.size() == 2, "Backoff must not rewrite settings each frame");
		result = backend.Update(kOn, 100, 110);
		Check(native.writes.size() == 3 && result.status.retryAtMs == 360, "Unchanged settings must retry with increasing delay");
		native.settingsResult = NVAPI_OK;
		result = backend.Update(kOn, 101, 360);
		Check(result.status.accepted == kOn && !result.status.pending &&
				  result.status.health == Health::Healthy && result.notice == Notice::Recovered,
			"Successful native reapply and sleep must recover");
		Check(native.writes.back().bLowLatencyMode && !native.writes.back().bLowLatencyBoost,
			"On must enable low latency without requesting Boost");
		backend.Update(kOn, 102, 2000);
		Check(native.writes.size() == 4 && native.sleeps == 2, "Healthy accepted options must remain cached");
	}

	void SleepRecovery(ID3D11Device* a_device)
	{
		NativeState native;
		current = &native;
		Backend backend(kCalls);
		backend.BindDevice(a_device, true);
		native.sleepResult = NVAPI_TIMEOUT;
		auto result = backend.Update(kOn, 42, 0);
		Check(result.status.accepted == kOn && result.status.pending &&
				  result.status.health == Health::Recovering &&
				  result.status.failureOperation == Operation::Sleep &&
				  result.status.failureStatus == NVAPI_TIMEOUT,
			"Sleep failure must retain preference and report native status");
		backend.Update(kOn, 42, 1000);
		Check(native.sleeps == 1 && native.writes.size() == 1, "Failed sleep consumes the entire logical frame");
		native.sleepResult = NVAPI_OK;
		result = backend.Update(kOn, 43, 1001);
		Check(native.sleeps == 2 && native.writes.size() == 2 &&
				  result.status.health == Health::Healthy && result.notice == Notice::Recovered,
			"Later frame must reapply and resume native sleep");
		backend.Update(kBoost, 43, 1002);
		Check(native.sleeps == 2, "Settings changes cannot permit a second sleep in one frame");
		backend.Update(kBoost, 44, 1003);
		Check(native.sleeps == 3, "Next frame must sleep normally");
	}

	void Exhaustion(ID3D11Device* a_device)
	{
		for (const bool failSleep : { false, true }) {
			NativeState native;
			current = &native;
			Backend backend(kCalls);
			backend.BindDevice(a_device, true);
			(failSleep ? native.sleepResult : native.settingsResult) = NVAPI_ERROR;
			Outcome result;
			uint32_t frame = 0;
			for (const uint64_t now : { 0, 100, 350, 850, 1850 })
				result = backend.Update(kOn, ++frame, now);
			Check(result.status.health == Health::Exhausted && result.status.pending &&
					  result.status.failures == 5 && result.notice == Notice::Exhausted,
				"Repeated settings or sleep failures must exhaust a bounded budget");
			const auto writes = native.writes.size();
			const auto sleeps = native.sleeps;
			backend.Update(kOn, ++frame, 100000);
			backend.BindDevice(a_device, true);
			backend.Update(kOn, ++frame, 100001);
			Check(native.writes.size() == writes && native.sleeps == sleeps, "Same options or duplicate binding must not rearm exhaustion");
			native.settingsResult = native.sleepResult = NVAPI_OK;
			result = backend.Update(kBoost, ++frame, 100002);
			Check(result.status.health == Health::Healthy && result.status.accepted == kBoost,
				"A meaningful settings change must rearm recovery");
		}
	}

	void OptionsAndOncePerFrame(ID3D11Device* a_device)
	{
		NativeState native;
		current = &native;
		Backend backend(kCalls);
		backend.BindDevice(a_device, true);
		backend.Update({}, 1, 0);
		backend.Update({}, 2, 1);
		Check(native.sleeps == 0 && native.writes.size() == 1, "Off without cap never sleeps or repeats accepted settings");
		const Options capOnly{ Mode::Off, 50000, false };
		backend.Update(capOnly, 3, 2);
		Check(native.sleeps == 1 && !native.writes.back().bLowLatencyMode &&
				  !native.writes.back().bLowLatencyBoost && native.writes.back().minimumIntervalUs == 50000,
			"Off with cap must keep pacing and exact interval");
		backend.Update(kBoost, 4, 3);
		const auto& parameters = native.writes.back();
		Check(parameters.version == NV_SET_SLEEP_MODE_PARAMS_VER &&
				  parameters.bLowLatencyMode && parameters.bLowLatencyBoost &&
				  parameters.bUseMarkersToOptimize && !parameters.bUseMinQueueTime,
			"Native fields and official structure version must match option semantics");
		for (const auto value : parameters.rsvd)
			Check(value == 0, "Native reserved fields must be zero initialized");
		std::vector<std::thread> workers;
		for (int index = 0; index < 8; ++index)
			workers.emplace_back([&] { backend.Update(kBoost, 5, 4); });
		for (auto& worker : workers)
			worker.join();
		Check(native.sleeps == 3 && native.writes.size() == 3, "Concurrent execution points must serialize one sleep and one settings owner");
		Check(backend.Disable(5).disabled && native.sleeps == 3, "Confirmed disable clears cap and markers without sleeping");
		const auto& disabled = native.writes.back();
		Check(!disabled.bLowLatencyMode && !disabled.bLowLatencyBoost &&
				  !disabled.bUseMarkersToOptimize && !disabled.minimumIntervalUs,
			"Frame Generation disable must clear every active native field");
	}

	void DisableAndDeviceLifetime(ID3D11Device* a_device, ID3D11Device* a_other)
	{
		NativeState native;
		current = &native;
		const auto originalRefs = References(a_device);
		const auto otherRefs = References(a_other);
		Backend backend(kCalls);
		Check(backend.Disable(0).disabled, "Never-active unavailable backend is a legitimate exclusion case");
		backend.BindDevice(a_device, true);
		Check(References(a_device) == originalRefs + 1, "Backend must own one native COM reference");
		backend.Update(kOn, 1, 1);
		native.settingsResult = NVAPI_ERROR;
		auto result = backend.Disable(2);
		Check(!result.disabled && result.status.pending && result.status.accepted == kOn,
			"Failed native disable must reject Frame Generation");
		native.settingsResult = NVAPI_OK;
		Check(backend.Disable(102).disabled, "Disable can recover only with native acceptance");
		const auto generation = backend.GetStatus().deviceGeneration;
		backend.BindDevice(a_other, true);
		Check(References(a_device) == originalRefs && References(a_other) == otherRefs + 1,
			"Device replacement must release the old reference and retain the new one");
		auto state = backend.GetStatus();
		Check(!state.acceptedValid && !state.desiredValid && !state.pending &&
				  !state.failures && !state.retryAtMs && state.deviceGeneration > generation,
			"Device replacement must invalidate acceptance and recovery state");
		native.settingsResult = NVAPI_ERROR;
		Check(!backend.Disable(103).disabled, "Old device's accepted Off cannot satisfy new device disable");
		backend.BindDevice(nullptr, false);
		Check(References(a_other) == otherRefs && !backend.Disable(104).disabled,
			"Lost availability after prior activity must release COM and fail closed");
		Check(backend.Shutdown() == NVAPI_OK && !backend.Disable(105).disabled, "Teardown must not claim that driver state was cleared");
	}

	void HistoricalOffDoesNotConfirmDisable(ID3D11Device* a_device)
	{
		NativeState native;
		current = &native;
		Backend backend(kCalls);
		backend.BindDevice(a_device, true);
		Check(backend.Disable(0).disabled, "Initial native Off must be accepted");
		native.settingsResult = NVAPI_ERROR;
		backend.Update(kBoost, 1, 1);
		const auto result = backend.Disable(2);
		Check(!result.disabled && result.status.acceptedValid && result.status.accepted == Options{} &&
				  result.status.pending && result.status.desired == Options{} && native.writes.size() == 3,
			"Historical Off cannot confirm disable after an indeterminate active request");
		native.settingsResult = NVAPI_OK;
		Check(backend.Disable(102).disabled && native.writes.size() == 4,
			"A failed disable needs fresh native acceptance even when historical Off matches");
	}

	void DeviceNotificationIsolation(ID3D11Device* a_device, ID3D11Device* a_other)
	{
		for (const bool failSleep : { false, true }) {
			NativeState native;
			current = &native;
			Backend backend(kCalls);
			backend.BindDevice(a_device, true);
			auto& failure = failSleep ? native.sleepResult : native.settingsResult;
			failure = NVAPI_ERROR;
			Check(backend.Update(kOn, 1, 0).notice == Notice::Failure, "First native failure must be reported");
			backend.BindDevice(a_device, true);
			Check(backend.Update(kOn, 2, 100).notice == Notice::None,
				"Duplicate binding must preserve the repeated-failure throttle");
			failure = NVAPI_OK;
			Check(backend.Update(kOn, 3, 350).notice == Notice::Recovered, "First device recovery must be reported");
			backend.BindDevice(a_other, true);
			failure = NVAPI_ERROR;
			Check(backend.Update(kOn, 4, 351).notice == Notice::Failure,
				"A new device's first failure cannot be hidden by the old device's throttle");
			failure = NVAPI_OK;
			Check(backend.Update(kOn, 5, 451).notice == Notice::Recovered,
				"A new device's recovery cannot be hidden by the old device's throttle");
		}
	}

	void DeviceReplacementPreservesFrameExclusion(ID3D11Device* a_device, ID3D11Device* a_other)
	{
		NativeState native;
		current = &native;
		Backend backend(kCalls);
		backend.BindDevice(a_device, true);
		backend.Update(kOn, 50, 0);
		backend.BindDevice(a_other, true);
		const auto result = backend.Update(kOn, 50, 1);
		Check(result.status.acceptedValid && result.status.accepted == kOn && native.writes.size() == 2 && native.sleeps == 1,
			"Device replacement must configure the new device without another sleep in the same frame");
		backend.Update(kOn, 51, 2);
		Check(native.sleeps == 2 && native.writes.size() == 2, "The new device must sleep normally on the next frame");
	}

	void ErrorClassesAndDeviceRearm(ID3D11Device* a_device, ID3D11Device* a_other)
	{
		for (const auto error : { NVAPI_NOT_SUPPORTED, NVAPI_NO_IMPLEMENTATION,
				 NVAPI_D3D_DEVICE_LOST, NVAPI_HANDLE_INVALIDATED, NVAPI_INVALID_ARGUMENT }) {
			NativeState native;
			current = &native;
			Backend backend(kCalls);
			backend.BindDevice(a_device, true);
			native.sleepResult = error;
			const auto result = backend.Update(kOn, 1, 0);
			const auto expected = error == NVAPI_NOT_SUPPORTED || error == NVAPI_NO_IMPLEMENTATION ? Health::Unsupported :
			                      error == NVAPI_INVALID_ARGUMENT                                  ? Health::Failed :
			                                                                                         Health::InvalidDevice;
			Check(result.status.health == expected && result.status.failureStatus == error,
				"Permanent, unsupported and invalid-device failures need distinct health");
			backend.Update(kOn, 2, 100000);
			Check(native.writes.size() == 1 && native.sleeps == 1, "Nonretryable failures cannot retry on every frame");
			Check(backend.Disable(100001).disabled == (error == NVAPI_INVALID_ARGUMENT),
				"Unsupported/lost backends must reject disable; a rearmed permanent error requires native acceptance");
			backend.BindDevice(a_other, true);
			native.sleepResult = NVAPI_OK;
			Check(backend.Update(kOn, 3, 100002).status.health == Health::Healthy,
				"New valid device must rearm failure recovery");
		}
		NativeState native;
		current = &native;
		Backend unsupported(NativeCalls{});
		Check(unsupported.BindDevice(a_device, true) == NVAPI_NO_IMPLEMENTATION &&
				  unsupported.Disable(0).disabled && native.writes.empty(),
			"Missing entry points on a never-active backend are legitimately unsupported");
	}

	void LoggingCost(ID3D11Device* a_device)
	{
		for (const auto options : { Options{}, kOn }) {
			NativeState native;
			current = &native;
			Backend backend(kCalls);
			backend.BindDevice(a_device, true);
			logging = {};
			LogReflexOutcome(backend.Update(options, 0, 0));
			Check(logging.emissions == 1, "Accepted configuration changes must remain observable");
			logging = {};
			for (uint32_t frame = 1; frame <= 10000; ++frame)
				LogReflexOutcome(backend.Update(options, frame, frame));
			Check(!logging.levelChecks && !logging.loggerCalls && !logging.emissions && !logging.argumentConversions,
				"Healthy frames must bypass level checks, argument conversion and logger calls");
			Check(native.writes.size() == 1 && native.sleeps == (options.NeedsSleep() ? 10001u : 0u),
				"Silent healthy frames must preserve cached settings and normal pacing");
		}

		logging = { .minimum = spdlog::level::off };
		for (const auto notice : { Notice::Configured, Notice::Recovered, Notice::Failure, Notice::Exhausted })
			LogReflexOutcome({ .notice = notice });
		Check(logging.levelChecks == 4 && !logging.argumentConversions && !logging.loggerCalls && !logging.emissions,
			"Disabled log levels must skip argument conversion and logger calls even for events");

		for (const bool failSleep : { false, true }) {
			NativeState native;
			current = &native;
			Backend backend(kCalls);
			backend.BindDevice(a_device, true);
			(failSleep ? native.sleepResult : native.settingsResult) = NVAPI_ERROR;
			logging = {};
			for (uint32_t frame = 0; frame < 10000; ++frame)
				LogReflexOutcome(backend.Update(kOn, frame, frame));
			Check(logging.emissions == 2 && logging.loggerCalls == 2 && native.writes.size() == 5,
				"Persistent failures must emit only the first warning and exhaustion, with bounded native retries");
		}
	}
}

int main()
{
	const auto first = MakeDevice();
	const auto second = MakeDevice();
	SettingsRetry(first.Get());
	SleepRecovery(first.Get());
	Exhaustion(first.Get());
	OptionsAndOncePerFrame(first.Get());
	DisableAndDeviceLifetime(first.Get(), second.Get());
	HistoricalOffDoesNotConfirmDisable(first.Get());
	DeviceNotificationIsolation(first.Get(), second.Get());
	DeviceReplacementPreservesFrameExclusion(first.Get(), second.Get());
	ErrorClassesAndDeviceRearm(first.Get(), second.Get());
	LoggingCost(first.Get());
	std::puts("Native Reflex settings, sleep, retry, exclusion and device-lifetime tests passed");
}
