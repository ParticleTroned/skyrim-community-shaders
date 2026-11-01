#include "Features/Upscaling/AntiLag2.h"

#include <cstring>
#include <iostream>
#include <functional>
#include <stdexcept>

namespace
{
	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	struct Driver : AMD::AntiLag2DX12::IAmdExtAntiLagApi
	{
		HRESULT enableResult = S_OK, disableResult = S_OK, delayResult = S_OK, markerResult = S_OK;
		unsigned states = 0, delays = 0, markers = 0, releases = 0, references = 1;
		bool enabled = false;
		std::function<void()> onDelay;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			if (--references == 0)
				++releases;
			return references;
		}
		HRESULT UpdateAntiLagState(void* a_data) override
		{
			if (!a_data) {
				++delays;
				if (onDelay)
					onDelay();
				return delayResult;
			}
			const auto& state = *static_cast<AMD::AntiLag2DX12::APIData_v1*>(a_data);
			if (state.uiVersion == 2) {
				++markers;
				return markerResult;
			}
			Check(state.maxFPS == 0 && !state.sControlStr && state.uiSize == sizeof(state), "unexpected SDK limiter/state data");
			++states;
			const bool requested = state.eMode == 1;
			const auto result = requested ? enableResult : disableResult;
			if (result == S_OK)
				enabled = requested;
			return result;
		}
	};

	struct SwapChain : IDXGISwapChain
	{
		struct Data
		{
			AMD::AntiLag2DX12::Context* context;
			bool enabled;
		} data{};
		HRESULT result = S_OK;
		unsigned writes = 0;
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a_key, UINT a_size, const void* a_value) override
		{
			Check(a_key.Data1 == 0x5083ae5b && a_size == sizeof(data), "invalid FSR handshake ABI");
			++writes;
			if (result == S_OK)
				std::memcpy(&data, a_value, sizeof(data));
			return result;
		}
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
		ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
		ULONG STDMETHODCALLTYPE Release() override { return 1; }
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetParent(REFIID, void**) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void**) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE Present(UINT, UINT) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT, REFIID, void**) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL, IDXGIOutput*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL*, IDXGIOutput**) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT, UINT) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput**) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT*) override { return E_NOTIMPL; }
	};

	Driver* driver = nullptr;
	AMD::AntiLag2DX12::Context* lastContext = nullptr;
	HRESULT initializationResult = S_OK;
	HRESULT Initialize(AMD::AntiLag2DX12::Context* a_context, ID3D12Device*)
	{
		lastContext = a_context;
		a_context->m_pAntiLagAPI = driver;
		return initializationResult;
	}

	void Start(CSX::AntiLag2::Session& a_session, Driver& a_driver)
	{
		driver = &a_driver;
		initializationResult = S_OK;
		Check(a_session.Initialize(nullptr, Initialize) == S_OK, "initialization failed");
		a_session.SetHookReady(true);
	}

	void StateAndLifetime()
	{
		Driver fake;
		CSX::AntiLag2::Session session;
		Start(session, fake);
		SwapChain chain;
		session.BeforeInput();
		session.Publish(&chain);
		session.MarkEndOfRendering();
		Check(!session.GetStatus().active && fake.states == 0 && fake.delays == 0 && chain.writes == 0, "default-off path touched the driver");
		session.Request(true);
		Check(!session.GetStatus().active, "request reported active before input");
		session.BeforeInput();
		fake.delayResult = S_FALSE;
		session.BeforeInput();
		fake.delayResult = S_OK;
		session.Publish(&chain);
		session.MarkEndOfRendering();
		Check(session.GetStatus().active && fake.states == 1 && fake.delays == 2 && fake.markers == 1, "active frame sequence incorrect");
		Check(chain.data.context == lastContext && chain.data.enabled, "FSR did not receive the stable context");
		session.Request(false);
		session.BeforeInput();
		session.Publish(&chain);
		session.MarkEndOfRendering();
		const auto writes = chain.writes;
		session.BeforeInput();
		session.Publish(&chain);
		session.MarkEndOfRendering();
		Check(!session.GetStatus().active && !fake.enabled && !chain.data.enabled && fake.states == 2 &&
				  fake.delays == 2 && fake.markers == 1 && chain.writes == writes,
			"disabled path kept issuing work");
		session.Request(true);
		session.BeforeInput();
		session.Suspend();
		session.Request(true);
		session.BeforeInput();
		Check(!session.GetStatus().active && fake.releases == 0, "suspension released pending presentation ownership");
		session.ReleaseAfterPresentDestruction();
		Check(fake.releases == 1 && !session.GetStatus().available, "drained context was not released exactly once");
	}

	void Failures()
	{
		for (const auto result : { S_FALSE, E_FAIL }) {
			Driver fake;
			driver = &fake;
			initializationResult = result;
			CSX::AntiLag2::Session session;
			Check(session.Initialize(nullptr, Initialize) == result && fake.releases == 1, "partial initialization leaked");
			Check(!session.GetStatus().available, "unsupported initialization admitted work");
		}
		{
			Driver fake;
			CSX::AntiLag2::Session session;
			Start(session, fake);
			session.SetHookReady(false);
			Check(fake.releases == 1 && !session.GetStatus().available, "failed hook retained unpublished context");
		}
		for (const bool disableFails : { false, true }) {
			Driver fake;
			CSX::AntiLag2::Session session;
			Start(session, fake);
			session.Request(true);
			session.BeforeInput();
			fake.delayResult = E_FAIL;
			fake.disableResult = disableFails ? E_FAIL : S_OK;
			session.BeforeInput();
			const auto status = session.GetStatus();
			Check(status.faulted && !status.active && status.disableConfirmed != disableFails &&
					  status.driverEnabled == disableFails,
				"fault reported an unacknowledged driver state");
			const auto calls = fake.states + fake.delays;
			session.Request(true);
			session.BeforeInput();
			Check(calls == fake.states + fake.delays && session.TakeFailure() && !session.TakeFailure(), "fault retried delay or repeated notification");
			session.Suspend();
			Check(fake.releases == 0, "failure released an outstanding FSR pointer");
			session.ReleaseAfterPresentDestruction();
		}
		{
			Driver fake;
			CSX::AntiLag2::Session session;
			Start(session, fake);
			session.Request(true);
			session.BeforeInput();
			SwapChain chain;
			session.Publish(&chain);
			chain.result = E_FAIL;
			for (int i = 0; i < 10; ++i)
				session.Publish(&chain);
			Check(chain.writes == 4 && session.GetStatus().faulted && !fake.enabled && fake.releases == 0, "handshake failure was unbounded or released its pointer");
			session.Suspend();
			session.ReleaseAfterPresentDestruction();
		}
	}

	void MarkerFailure()
	{
		Driver fake;
		CSX::AntiLag2::Session session;
		Start(session, fake);
		SwapChain chain;
		session.Request(true);
		session.BeforeInput();
		session.Publish(&chain);
		fake.markerResult = E_FAIL;
		session.MarkEndOfRendering();
		session.Publish(&chain);
		session.MarkEndOfRendering();
		Check(session.GetStatus().faulted && !session.GetStatus().active && !chain.data.enabled &&
				  fake.markers == 1 && fake.releases == 0,
			"marker failure did not disable and retain context");
		session.Suspend();
		session.ReleaseAfterPresentDestruction();
	}

	void TransactionalState()
	{
		Driver fake;
		AMD::AntiLag2DX12::Context context{};
		context.m_pAntiLagAPI = &fake;
		fake.enableResult = E_FAIL;
		Check(CSX::AntiLag2::SetEnabled(context, true) == E_FAIL && !context.m_enabled, "failed enable was cached");
		fake.enableResult = S_OK;
		Check(CSX::AntiLag2::SetEnabled(context, true) == S_OK && context.m_enabled && fake.states == 2, "rejected state was not retryable");
		fake.disableResult = S_FALSE;
		Check(CSX::AntiLag2::SetEnabled(context, false) == S_FALSE && context.m_enabled, "unacknowledged disable was cached");
	}

	void RetirementDuringDelay()
	{
		Driver fake;
		CSX::AntiLag2::Session session;
		Start(session, fake);
		fake.onDelay = [&] {
			session.Suspend();
			session.ReleaseAfterPresentDestruction();
			Check(fake.releases == 0, "input driver lease was released during delay");
		};
		session.Request(true);
		session.BeforeInput();
		Check(fake.releases == 1 && !session.GetStatus().active && !session.GetStatus().available,
			"retired generation published an active delay result");
	}

	void UnprovenRetirement()
	{
		Driver fake;
		{
			CSX::AntiLag2::Session session;
			Start(session, fake);
			session.Request(true);
			session.BeforeInput();
			session.Suspend();
		}
		Check(fake.releases == 0 && lastContext->m_pAntiLagAPI == &fake, "destructor invalidated retained presentation ownership");
		AMD::AntiLag2DX12::DeInitialize(lastContext);
		delete lastContext;
	}
}

int main()
{
	try {
		TransactionalState();
		StateAndLifetime();
		Failures();
		MarkerFailure();
		UnprovenRetirement();
		RetirementDuringDelay();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
