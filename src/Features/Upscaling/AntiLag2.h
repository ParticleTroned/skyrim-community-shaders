#pragma once

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <ffx_antilag2_dx12.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace CSX::AntiLag2
{
	/** Applies driver state only after acknowledgement, keeping rejected changes retryable. */
	inline HRESULT SetEnabled(AMD::AntiLag2DX12::Context& a_context, bool a_enabled, bool a_force = false)
	{
		if (!a_context.m_pAntiLagAPI)
			return E_NOINTERFACE;
		if (!a_force && a_context.m_enabled == a_enabled && a_context.m_maxFPS == 0)
			return S_OK;
		AMD::AntiLag2DX12::APIData_v1 data{};
		data.uiSize = sizeof(data);
		data.uiVersion = 1;
		data.eMode = a_enabled ? 1 : 2;
		const HRESULT result = a_context.m_pAntiLagAPI->UpdateAntiLagState(&data);
		if (result == S_OK) {
			a_context.m_enabled = a_enabled;
			a_context.m_maxFPS = 0;
		}
		return result;
	}

	struct Status
	{
		bool available = false;
		bool requested = false;
		bool active = false;
		bool driverEnabled = false;
		bool suspended = false;
		bool faulted = false;
		bool disableConfirmed = true;
		HRESULT result = S_OK;
		HRESULT disableResult = S_OK;
		const char* operation = "Not initialized: requires an AMD SE/AE FSR Frame Generation session";
	};

	/** Owns a driver context until its FSR presentation consumers have been destroyed. */
	class Session
	{
	public:
		using InitializeFunction = HRESULT (*)(AMD::AntiLag2DX12::Context*, ID3D12Device*);
		/** Retains unretired presentation ownership until process exit. */
		~Session();
		/** Probes the SDK once and retains its D3D12 device on success. */
		HRESULT Initialize(ID3D12Device* a_device, InitializeFunction a_initialize = AMD::AntiLag2DX12::Initialize);
		/** Publishes input-hook readiness before admitting any driver delays. */
		void SetHookReady(bool a_ready);
		/** Queues a live preference; the input thread performs the driver transition. */
		void Request(bool a_enabled);
		/** Applies the requested state and delays without locking out presentation. */
		void BeforeInput();
		/** Publishes FSR frame classification state, including a final disabled handshake. */
		void Publish(IDXGISwapChain* a_swapChain);
		/** Marks submitted rendering before the matching real-frame Present call. */
		void MarkEndOfRendering();
		/** Stops input work before FSR retirement, retaining all presentation references. */
		void Suspend();
		/** Requires successful destruction of all FSR contexts that could hold this pointer. */
		void ReleaseAfterPresentDestruction();
		/** Returns accepted driver state and failures; it does not measure latency. */
		[[nodiscard]] Status GetStatus() const;
		/** Consumes the first failure notification without polling the status mutex each frame. */
		bool TakeFailure() { return failurePending.exchange(false, std::memory_order_acq_rel); }

	private:
		void Fail(HRESULT a_result, const char* a_operation);
		void ReleaseContext();
		mutable std::mutex mutex;
		std::unique_ptr<AMD::AntiLag2DX12::Context> context;
		Microsoft::WRL::ComPtr<ID3D12Device> retainedDevice;
		std::atomic_bool inputWork{ false };
		std::atomic_bool presentationWork{ false };
		std::atomic_bool markerWork{ false };
		std::atomic_bool failurePending{ false };
		Status status;
		bool presentationEnabled = false;
		bool handshakePublished = false;
		static constexpr unsigned MaximumHandshakeAttempts = 3;
		unsigned handshakeRetries = MaximumHandshakeAttempts;
		uint64_t generation = 0;
	};
}
