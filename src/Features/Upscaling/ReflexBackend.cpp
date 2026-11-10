#include "ReflexBackend.h"
#include "NvidiaComIdentity.h"

#include <array>
#include <string_view>

namespace
{
#include <nvapi_interface.h>

	using QueryInterface = void*(__cdecl*)(unsigned int);

	template <class T>
	T Resolve(QueryInterface a_query, std::string_view a_name)
	{
		for (const auto& entry : nvapi_interface_table) {
			if (entry.func == a_name)
				return reinterpret_cast<T>(a_query(entry.id));
		}
		return nullptr;
	}

	constexpr std::array<uint64_t, 4> kRetryDelayMs{ 100, 250, 500, 1000 };

	bool Unsupported(NvAPI_Status a_status)
	{
		return a_status == NVAPI_NOT_SUPPORTED ||
		       a_status == NVAPI_NO_IMPLEMENTATION ||
		       a_status == NVAPI_FUNCTION_NOT_FOUND ||
		       a_status == NVAPI_NVIDIA_DEVICE_NOT_FOUND;
	}

	bool InvalidDevice(NvAPI_Status a_status)
	{
		return a_status == NVAPI_D3D_DEVICE_LOST ||
		       a_status == NVAPI_INVALID_HANDLE ||
		       a_status == NVAPI_HANDLE_INVALIDATED;
	}

	bool Retryable(NvAPI_Status a_status)
	{
		return a_status == NVAPI_ERROR || a_status == NVAPI_TIMEOUT ||
		       a_status == NVAPI_DEVICE_BUSY;
	}
}

namespace Reflex
{
	Backend::~Backend()
	{
		(void)Shutdown();
	}

	NvAPI_Status Backend::InitializeApi()
	{
		if (injected)
			return calls.setSleepMode && calls.sleep ? NVAPI_OK : NVAPI_NO_IMPLEMENTATION;
		if (retainedModule)
			return NVAPI_API_IN_USE;
		if (initializedApi)
			return NVAPI_OK;

		// LoadLibrary owns a reference even when Streamline already loaded NVAPI.
		if (!module)
			module = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!module)
			return NVAPI_LIBRARY_NOT_FOUND;
		const auto query = reinterpret_cast<QueryInterface>(GetProcAddress(module, "nvapi_QueryInterface"));
		if (!query)
			return NVAPI_NO_IMPLEMENTATION;
		const auto initialize = Resolve<decltype(&NvAPI_Initialize)>(query, "NvAPI_Initialize");
		unload = Resolve<decltype(&NvAPI_Unload)>(query, "NvAPI_Unload");
		calls.setSleepMode = Resolve<decltype(calls.setSleepMode)>(query, "NvAPI_D3D_SetSleepMode");
		calls.sleep = Resolve<decltype(calls.sleep)>(query, "NvAPI_D3D_Sleep");
		if (!initialize || !unload || !calls.setSleepMode || !calls.sleep)
			return NVAPI_NO_IMPLEMENTATION;
		const auto result = initialize();
		initializedApi = result == NVAPI_OK;
		return result;
	}

	NvAPI_Status Backend::BindDevice(ID3D11Device* a_device, bool a_supported)
	{
		std::scoped_lock lock(mutex);
		if (device && CSX::NvidiaComIdentity::IsSame(device.Get(), a_device) &&
			supportedDevice == a_supported &&
			status.health != Health::InvalidDevice && status.health != Health::Unavailable)
			return status.health == Health::Unsupported ? NVAPI_NOT_SUPPORTED : NVAPI_OK;

		const auto generation = status.deviceGeneration + 1;
		device.Reset();
		supportedDevice = a_supported;
		status = {};
		lastFailureNoticeMs.reset();
		lastRecoveryNoticeMs.reset();
		status.deviceGeneration = generation;
		if (!a_device) {
			status.nativeStatus = NVAPI_INVALID_ARGUMENT;
			return status.nativeStatus;
		}
		device = a_device;
		status.deviceBound = true;
		status.deviceStatus = device->GetDeviceRemovedReason();
		if (FAILED(status.deviceStatus)) {
			status.nativeStatus = NVAPI_D3D_DEVICE_LOST;
			status.health = Health::InvalidDevice;
			return status.nativeStatus;
		}
		status.nativeStatus = a_supported ? InitializeApi() : NVAPI_NOT_SUPPORTED;
		status.health = status.nativeStatus == NVAPI_OK  ? Health::Ready :
		                Unsupported(status.nativeStatus) ? Health::Unsupported :
		                                                   Health::Unavailable;
		return status.nativeStatus;
	}

	NvAPI_Status Backend::Shutdown()
	{
		std::scoped_lock lock(mutex);
		device.Reset();
		const auto generation = status.deviceGeneration;
		status = {};
		status.deviceGeneration = generation;
		if (retainedModule)
			return NVAPI_API_IN_USE;
		if (initializedApi) {
			const auto result = unload();
			if (result != NVAPI_OK) {
				// An indeterminate NVAPI lifetime must retain our loader reference.
				retainedModule = true;
				return result;
			}
			initializedApi = false;
		}
		if (module) {
			FreeLibrary(module);
			module = nullptr;
		}
		if (!injected)
			calls = {};
		unload = nullptr;
		return NVAPI_OK;
	}

	Status Backend::GetStatus() const
	{
		std::scoped_lock lock(mutex);
		return status;
	}

	bool Backend::CanApply() const
	{
		return device && calls.setSleepMode && calls.sleep &&
		       (status.health == Health::Ready || status.health == Health::Healthy ||
				   status.health == Health::Recovering);
	}

	bool Backend::IsDisabled() const
	{
		if (!possiblyActive && !status.deviceBound && !status.acceptedValid)
			return true;
		if (!possiblyActive && status.health == Health::Unsupported)
			return true;
		return status.deviceBound && status.acceptedValid && !status.pending &&
		       status.health == Health::Healthy && status.accepted == Options{};
	}

	void Backend::Fail(NvAPI_Status a_result, Operation a_operation, uint64_t a_nowMs)
	{
		status.operation = status.failureOperation = a_operation;
		status.nativeStatus = status.failureStatus = a_result;
		status.pending = true;
		++status.failures;
		status.retryAtMs = 0;
		status.deviceStatus = device ? device->GetDeviceRemovedReason() : E_POINTER;
		if (FAILED(status.deviceStatus) || InvalidDevice(a_result)) {
			status.health = Health::InvalidDevice;
		} else if (Unsupported(a_result)) {
			status.health = Health::Unsupported;
		} else if (!Retryable(a_result)) {
			status.health = Health::Failed;
		} else if (status.failures > kRetryDelayMs.size()) {
			status.health = Health::Exhausted;
		} else {
			status.health = Health::Recovering;
			status.retryAtMs = a_nowMs + kRetryDelayMs[status.failures - 1];
		}
	}

	Outcome Backend::Process(Options a_desired, std::optional<uint32_t> a_frame, uint64_t a_nowMs)
	{
		Notice notice = Notice::None;
		const bool changed = !status.desiredValid || status.desired != a_desired;
		if (changed) {
			status.desired = a_desired;
			status.desiredValid = true;
			status.pending = true;
			if (status.health == Health::Exhausted || status.health == Health::Failed ||
				status.health == Health::Recovering || status.health == Health::Healthy) {
				status.health = Health::Ready;
				status.failures = 0;
				status.retryAtMs = 0;
			}
		}

		// Recovery from a sleep failure waits for another logical frame.
		const bool sameFailedFrame = a_frame && lastSleepFrame == a_frame &&
		                             status.pending && status.failureOperation == Operation::Sleep;
		if (CanApply() && status.pending && !sameFailedFrame && a_nowMs >= status.retryAtMs) {
			NV_SET_SLEEP_MODE_PARAMS parameters{};
			parameters.version = NV_SET_SLEEP_MODE_PARAMS_VER;
			parameters.bLowLatencyMode = a_desired.mode != Mode::Off;
			parameters.bLowLatencyBoost = a_desired.mode == Mode::Boost;
			parameters.minimumIntervalUs = a_desired.frameLimitUs;
			parameters.bUseMarkersToOptimize = a_desired.useMarkersToOptimize;
			// Preserve the SDK's zero default for minimum queue time and reserved fields.
			possiblyActive |= a_desired.NeedsSleep() || a_desired.useMarkersToOptimize;
			const auto result = calls.setSleepMode(device.Get(), &parameters);
			status.operation = Operation::Settings;
			status.nativeStatus = result;
			if (result == NVAPI_OK) {
				status.accepted = a_desired;
				status.acceptedValid = true;
				status.pending = false;
				status.retryAtMs = 0;
				const bool recovering = status.failures != 0;
				status.health = recovering && a_desired.NeedsSleep() ? Health::Recovering : Health::Healthy;
				if (!a_desired.NeedsSleep())
					status.failures = 0;
				notice = recovering && a_desired.NeedsSleep() ? Notice::None :
				         recovering                           ? Notice::Recovered :
				                                                Notice::Configured;
			} else {
				Fail(result, Operation::Settings, a_nowMs);
				notice = status.health == Health::Exhausted ? Notice::Exhausted : Notice::Failure;
			}
		}

		if (a_frame && lastSleepFrame != a_frame && CanApply() &&
			!status.pending && a_desired.NeedsSleep()) {
			lastSleepFrame = a_frame;
			const auto result = calls.sleep(device.Get());
			status.operation = Operation::Sleep;
			status.nativeStatus = result;
			if (result == NVAPI_OK) {
				if (status.health == Health::Recovering)
					notice = Notice::Recovered;
				status.health = Health::Healthy;
				status.failures = 0;
			} else {
				Fail(result, Operation::Sleep, a_nowMs);
				notice = status.health == Health::Exhausted ? Notice::Exhausted : Notice::Failure;
			}
		}

		auto rateLimit = [&](std::optional<uint64_t>& a_lastNotice) {
			if (a_lastNotice && a_nowMs - *a_lastNotice < 1000)
				notice = Notice::None;
			else
				a_lastNotice = a_nowMs;
		};
		if (notice == Notice::Failure && status.health == Health::Recovering)
			rateLimit(lastFailureNoticeMs);
		else if (notice == Notice::Recovered)
			rateLimit(lastRecoveryNoticeMs);
		return { status, notice, IsDisabled() };
	}

	Outcome Backend::Update(Options a_desired, uint32_t a_frame, uint64_t a_nowMs)
	{
		std::scoped_lock lock(mutex);
		return Process(a_desired, a_frame, a_nowMs);
	}

	Outcome Backend::Disable(uint64_t a_nowMs)
	{
		std::scoped_lock lock(mutex);
		return Process({}, std::nullopt, a_nowMs);
	}
}
