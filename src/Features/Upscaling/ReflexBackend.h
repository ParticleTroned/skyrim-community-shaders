#pragma once

#include <d3d11.h>
#include <nvapi.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <optional>

namespace Reflex
{
	enum class Mode : uint8_t
	{
		Off,
		On,
		Boost
	};

	struct Options
	{
		Mode mode = Mode::Off;
		uint32_t frameLimitUs = 0;
		bool useMarkersToOptimize = false;
		bool operator==(const Options&) const = default;
		[[nodiscard]] bool NeedsSleep() const { return mode != Mode::Off || frameLimitUs != 0; }
	};

	enum class Health
	{
		Unavailable,
		Ready,
		Healthy,
		Recovering,
		Exhausted,
		Unsupported,
		InvalidDevice,
		Failed
	};
	enum class Operation
	{
		Bind,
		Settings,
		Sleep
	};
	enum class Notice
	{
		None,
		Configured,
		Recovered,
		Failure,
		Exhausted
	};

	struct Status
	{
		Health health = Health::Unavailable;
		Operation operation = Operation::Bind;
		NvAPI_Status nativeStatus = NVAPI_API_NOT_INITIALIZED;
		Operation failureOperation = Operation::Bind;
		NvAPI_Status failureStatus = NVAPI_OK;
		HRESULT deviceStatus = S_OK;
		bool deviceBound = false;
		bool desiredValid = false;
		bool acceptedValid = false;
		bool pending = false;
		Options desired{};
		Options accepted{};
		uint32_t failures = 0;
		uint64_t retryAtMs = 0;
		uint64_t deviceGeneration = 0;
	};

	struct Outcome
	{
		Status status{};
		Notice notice = Notice::None;
		bool disabled = false;
	};

	/// Official native entry points, injectable without replacing recovery logic.
	struct NativeCalls
	{
		decltype(&NvAPI_D3D_SetSleepMode) setSleepMode = nullptr;
		decltype(&NvAPI_D3D_Sleep) sleep = nullptr;
	};

	/// Owns a native device and one serialized settings/sleep recovery state.
	class Backend
	{
	public:
		Backend() = default;
		explicit Backend(NativeCalls a_calls) : calls(a_calls), injected(true) {}
		~Backend();
		Backend(const Backend&) = delete;
		Backend& operator=(const Backend&) = delete;

		/// Bind only an unwrapped rendering device; identical healthy bindings are idempotent.
		NvAPI_Status BindDevice(ID3D11Device* a_device, bool a_supported);
		/// Release our COM, NVAPI initialization and loader references together.
		NvAPI_Status Shutdown();
		/// Apply pending options and attempt at most one sleep per logical frame.
		Outcome Update(Options a_desired, uint32_t a_frame, uint64_t a_nowMs);
		/// Confirm native acceptance of Off, zero cap and disabled marker optimization.
		Outcome Disable(uint64_t a_nowMs);
		[[nodiscard]] Status GetStatus() const;

	private:
		NvAPI_Status InitializeApi();
		Outcome Process(Options a_desired, std::optional<uint32_t> a_frame, uint64_t a_nowMs);
		void Fail(NvAPI_Status a_result, Operation a_operation, uint64_t a_nowMs);
		[[nodiscard]] bool CanApply() const;
		[[nodiscard]] bool IsDisabled() const;

		mutable std::mutex mutex;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		NativeCalls calls{};
		HMODULE module = nullptr;
		decltype(&NvAPI_Unload) unload = nullptr;
		bool initializedApi = false;
		bool retainedModule = false;
		bool injected = false;
		bool supportedDevice = false;
		bool possiblyActive = false;
		Status status{};
		// Sleep exclusion follows the logical game frame across device changes.
		std::optional<uint32_t> lastSleepFrame;
		std::optional<uint64_t> lastFailureNoticeMs;
		std::optional<uint64_t> lastRecoveryNoticeMs;
	};
}
