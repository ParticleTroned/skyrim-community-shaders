#include "AntiLag2.h"

#include <utility>

namespace CSX::AntiLag2
{
	Session::~Session()
	{
		// Unproven presentation retirement must retain both the context and its device until process exit.
		if (context) {
			(void)context.release();
			(void)retainedDevice.Detach();
		}
	}

	HRESULT Session::Initialize(ID3D12Device* a_device, InitializeFunction a_initialize)
	{
		std::lock_guard lock(mutex);
		if (context)
			return E_UNEXPECTED;
		++generation;
		status = {};
		handshakeRetries = MaximumHandshakeAttempts;
		failurePending.store(false, std::memory_order_release);
		auto candidate = std::make_unique<AMD::AntiLag2DX12::Context>();
		HRESULT result = a_initialize(candidate.get(), a_device);
		if (result == S_OK && !candidate->m_pAntiLagAPI)
			result = E_NOINTERFACE;
		status.result = result;
		status.operation = "Driver initialization";
		if (result != S_OK) {
			AMD::AntiLag2DX12::DeInitialize(candidate.get());
			return result;
		}
		context = std::move(candidate);
		retainedDevice = a_device;
		return S_OK;
	}

	void Session::SetHookReady(bool a_ready)
	{
		std::lock_guard lock(mutex);
		status.available = context && a_ready;
		if (!status.available && context) {
			status.result = E_FAIL;
			status.operation = "Pre-input hook installation";
			ReleaseContext();
		}
	}

	void Session::Request(bool a_enabled)
	{
		std::lock_guard lock(mutex);
		status.requested = a_enabled;
		inputWork.store(status.available && !status.suspended && !status.faulted &&
							(a_enabled || status.driverEnabled),
			std::memory_order_release);
	}

	void Session::Fail(HRESULT a_result, const char* a_operation)
	{
		if (!status.faulted) {
			status.result = a_result;
			status.operation = a_operation;
			status.faulted = true;
			failurePending.store(true, std::memory_order_release);
			status.disableResult = context ? SetEnabled(*context, false, true) : S_OK;
		}
		status.disableConfirmed = status.disableResult == S_OK;
		status.active = false;
		markerWork.store(false, std::memory_order_release);
		status.driverEnabled = context && context->m_enabled;
		presentationWork.store(handshakeRetries != 0 && (status.driverEnabled || handshakePublished), std::memory_order_release);
		inputWork.store(false, std::memory_order_release);
	}

	void Session::BeforeInput()
	{
		if (!inputWork.load(std::memory_order_acquire))
			return;
		Microsoft::WRL::ComPtr<AMD::AntiLag2DX12::IAmdExtAntiLagApi> driver;
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		uint64_t observedGeneration = 0;
		{
			std::lock_guard lock(mutex);
			if (!status.available || status.suspended || status.faulted)
				return;
			const HRESULT result = SetEnabled(*context, status.requested);
			if (result != S_OK) {
				Fail(result, "Driver enable/disable");
				return;
			}
			status.disableConfirmed = !status.requested;
			status.driverEnabled = context->m_enabled;
			presentationWork.store(status.driverEnabled || handshakePublished, std::memory_order_release);
			if (!status.requested) {
				status.active = false;
				inputWork.store(false, std::memory_order_release);
				return;
			}
			driver = context->m_pAntiLagAPI;
			device = retainedDevice;
			observedGeneration = generation;
		}
		// Driver pacing can wait for render-thread work; never hold the presentation mutex while delaying.
		const HRESULT result = driver->UpdateAntiLagState(nullptr);
		std::lock_guard lock(mutex);
		if (generation != observedGeneration || !status.available || status.suspended || status.faulted)
			return;
		if (result != S_OK && result != S_FALSE) {
			Fail(result, "Pre-input delay");
			return;
		}
		status.active = status.requested;
	}

	void Session::Publish(IDXGISwapChain* a_swapChain)
	{
		if (!presentationWork.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(mutex);
		presentationEnabled = context && context->m_enabled && !status.faulted && !status.suspended;
		markerWork.store(presentationEnabled, std::memory_order_release);
		if (!context || handshakeRetries == 0 || (!presentationEnabled && !handshakePublished)) {
			presentationWork.store(false, std::memory_order_release);
			return;
		}
		struct Data
		{
			AMD::AntiLag2DX12::Context* context;
			bool enabled;
		};
		static constexpr GUID key{ 0x5083ae5b, 0x8070, 0x4fca, { 0x8e, 0xe5, 0x35, 0x82, 0xdd, 0x36, 0x7d, 0x13 } };
		const Data data{ context.get(), presentationEnabled };
		const HRESULT result = a_swapChain->SetPrivateData(key, sizeof(data), &data);
		if (result != S_OK) {
			Fail(result, "FSR presentation handshake");
			// A failed write may leave the previous pointer published; retain it until FSR destruction.
			handshakePublished = true;
			--handshakeRetries;
		} else {
			handshakePublished = presentationEnabled;
		}
		presentationWork.store(handshakeRetries != 0 && (presentationEnabled || handshakePublished), std::memory_order_release);
	}

	void Session::MarkEndOfRendering()
	{
		if (!markerWork.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(mutex);
		if (!context || !presentationEnabled)
			return;
		const HRESULT result = AMD::AntiLag2DX12::MarkEndOfFrameRendering(context.get());
		if (result != S_OK)
			Fail(result, "End-of-render marker");
	}

	void Session::Suspend()
	{
		std::lock_guard lock(mutex);
		status.suspended = true;
		status.active = false;
		markerWork.store(false, std::memory_order_release);
		inputWork.store(false, std::memory_order_release);
		if (context) {
			const HRESULT result = SetEnabled(*context, false, !status.disableConfirmed);
			if (result != S_OK) {
				status.disableResult = result;
				Fail(result, "Driver disable during retirement");
			} else {
				status.disableResult = S_OK;
				status.disableConfirmed = true;
			}
			status.driverEnabled = context->m_enabled;
		}
	}

	void Session::ReleaseContext()
	{
		++generation;
		if (context)
			AMD::AntiLag2DX12::DeInitialize(context.get());
		context.reset();
		retainedDevice.Reset();
		status.available = false;
		status.active = false;
		status.driverEnabled = false;
		presentationEnabled = false;
		handshakePublished = false;
		inputWork.store(false, std::memory_order_release);
		presentationWork.store(false, std::memory_order_release);
		markerWork.store(false, std::memory_order_release);
	}

	void Session::ReleaseAfterPresentDestruction()
	{
		std::lock_guard lock(mutex);
		ReleaseContext();
	}

	Status Session::GetStatus() const
	{
		std::lock_guard lock(mutex);
		return status;
	}
}
