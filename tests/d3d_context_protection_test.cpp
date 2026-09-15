#include "Utils/D3DContextProtection.h"
#include "Utils/RendererContextAccess.h"
#include "Utils/VRLoadingMenuClear.h"

#include <d3d11_4.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	using Microsoft::WRL::ComPtr;

	void Require(bool a_condition, const std::source_location& a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("D3D context protection check failed at line " + std::to_string(a_location.line()));
	}

	void Check(HRESULT a_result, const std::source_location& a_location = std::source_location::current())
	{
		Require(SUCCEEDED(a_result), a_location);
	}

	struct Device
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;

		explicit Device(UINT a_flags = 0)
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, a_flags,
				nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		}
	};

	struct HiddenWindow
	{
		HWND handle = CreateWindowExW(0, L"STATIC", L"D3D context protection test",
			WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

		HiddenWindow() { Require(handle != nullptr); }
		~HiddenWindow() { DestroyWindow(handle); }
	};

	struct ContextTransaction
	{
		ID3D11Multithread* multithread;

		explicit ContextTransaction(ID3D11Multithread* a_multithread) : multithread(a_multithread)
		{
			multithread->Enter();
		}
		~ContextTransaction() { multithread->Leave(); }
		ContextTransaction(const ContextTransaction&) = delete;
		ContextTransaction& operator=(const ContextTransaction&) = delete;
	};

	struct NativeRenderer
	{
		CRITICAL_SECTION lock{};
		struct RuntimeData
		{
			ID3D11DeviceContext* context = nullptr;
		} data;
		CRITICAL_SECTION& GetLock() { return lock; }
		RuntimeData& GetRuntimeData() { return data; }
		NativeRenderer() { InitializeCriticalSection(&lock); }
		~NativeRenderer() { DeleteCriticalSection(&lock); }
	};

	void TestPublishedContextOwnership()
	{
		Device current;
		Device replaced;
		NativeRenderer renderer;
		renderer.data.context = current.context.Get();
		Require(Util::GetRendererContextLock(&renderer, current.context.Get()) == &renderer.lock);
		Require(Util::GetRendererContextLock(&renderer, replaced.context.Get()) == nullptr);
		renderer.data.context = nullptr;
		Require(Util::GetRendererContextLock(&renderer, current.context.Get()) == nullptr);
		Require(Util::GetRendererContextLock<NativeRenderer>(nullptr, current.context.Get()) == nullptr);
	}

	void TestValidationPreservesProtectionState()
	{
		Device fixture(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
		const auto before = Util::InspectImmediateContextProtection(fixture.context.Get());
		Require(before.contextAvailable && before.immediateContext && before.multithreadAvailable);
		Require(before.status == S_OK && !before.multithreadProtected);
		Require(before.deviceFlags == D3D11_CREATE_DEVICE_BGRA_SUPPORT);
		for (unsigned user = 0; user < 4; ++user) {
			ComPtr<ID3D11DeviceContext> temporaryUser = fixture.context;
			Check(Util::ValidateImmediateContext(temporaryUser.Get()));
		}
		const auto after = Util::InspectImmediateContextProtection(fixture.context.Get());
		Require(after.status == S_OK && !after.multithreadProtected);
		Require(after.deviceFlags == before.deviceFlags);
		ComPtr<ID3D11Multithread> externalOwner;
		Check(fixture.context.As(&externalOwner));
		externalOwner->SetMultithreadProtected(TRUE);
		Check(Util::ValidateImmediateContext(fixture.context.Get()));
		Require(Util::InspectImmediateContextProtection(fixture.context.Get()).multithreadProtected);
	}

	void TestCreationDoesNotEnablePerCallLocking()
	{
		Device fixture;
		Check(Util::ValidateDeviceCreation(S_OK, fixture.device.GetAddressOf(), fixture.context.GetAddressOf(), nullptr));
		Require(!Util::InspectImmediateContextProtection(fixture.context.Get()).multithreadProtected);
	}

	void TestRejectedContexts()
	{
		Require(Util::ValidateImmediateContext(nullptr) == E_POINTER);
		const auto absent = Util::InspectImmediateContextProtection(nullptr);
		Require(!absent.contextAvailable && !absent.immediateContext && !absent.multithreadAvailable);
		Require(!absent.multithreadProtected && absent.status == E_POINTER);
		Device ordinary;
		ComPtr<ID3D11DeviceContext> deferred;
		Check(ordinary.device->CreateDeferredContext(0, deferred.GetAddressOf()));
		Require(Util::ValidateImmediateContext(deferred.Get()) == E_INVALIDARG);
		Require(!Util::InspectImmediateContextProtection(deferred.Get()).immediateContext);
		Device singleThreaded(D3D11_CREATE_DEVICE_SINGLETHREADED);
		const auto rejected = Util::InspectImmediateContextProtection(singleThreaded.context.Get());
		Require(rejected.contextAvailable && rejected.immediateContext);
		Require((rejected.deviceFlags & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0);
		Require(rejected.status == DXGI_ERROR_INVALID_CALL);
		Require(Util::ValidateImmediateContext(singleThreaded.context.Get()) == DXGI_ERROR_INVALID_CALL);
	}

	void TestCreationOutputs()
	{
		const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG |
		                   D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY |
		                   D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT;
		Require(Util::ThreadSafeDeviceFlags(flags) == flags);
		Require(Util::ThreadSafeDeviceFlags(flags | D3D11_CREATE_DEVICE_SINGLETHREADED) == flags);
		Require(Util::ThreadSafeDeviceFlags(D3D11_CREATE_DEVICE_SINGLETHREADED) == 0);

		Device complete;
		Check(Util::ValidateDeviceCreation(S_OK, complete.device.GetAddressOf(), complete.context.GetAddressOf(), nullptr));
		Require(!Util::InspectImmediateContextProtection(complete.context.Get()).multithreadProtected);
		Device deviceOnly;
		deviceOnly.context.Reset();
		Check(Util::ValidateDeviceCreation(S_OK, deviceOnly.device.GetAddressOf(), nullptr, nullptr));
		deviceOnly.device->GetImmediateContext(deviceOnly.context.GetAddressOf());
		Require(!Util::InspectImmediateContextProtection(deviceOnly.context.Get()).multithreadProtected);
		Device contextOnly;
		contextOnly.device.Reset();
		Check(Util::ValidateDeviceCreation(S_OK, nullptr, contextOnly.context.GetAddressOf(), nullptr));
		Require(!Util::InspectImmediateContextProtection(contextOnly.context.Get()).multithreadProtected);

		Device failedCreation;
		auto* originalDevice = failedCreation.device.Get();
		auto* originalContext = failedCreation.context.Get();
		Require(Util::ValidateDeviceCreation(E_OUTOFMEMORY, failedCreation.device.GetAddressOf(),
					failedCreation.context.GetAddressOf(), nullptr) == E_OUTOFMEMORY);
		Require(failedCreation.device.Get() == originalDevice && failedCreation.context.Get() == originalContext);
		Require(!Util::InspectImmediateContextProtection(originalContext).multithreadProtected);

		Device rejected(D3D11_CREATE_DEVICE_SINGLETHREADED);
		Require(Util::ValidateDeviceCreation(S_OK, rejected.device.GetAddressOf(),
					rejected.context.GetAddressOf(), nullptr) == DXGI_ERROR_INVALID_CALL);
		Require(!rejected.device && !rejected.context);
		Device deferred;
		deferred.context.Reset();
		Check(deferred.device->CreateDeferredContext(0, deferred.context.GetAddressOf()));
		Require(Util::ValidateDeviceCreation(S_OK, deferred.device.GetAddressOf(),
					deferred.context.GetAddressOf(), nullptr) == E_INVALIDARG);
		Require(!deferred.device && !deferred.context);

		D3D_FEATURE_LEVEL featureLevel{};
		const auto validation = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
			nullptr, 0, D3D11_SDK_VERSION, nullptr, &featureLevel, nullptr);
		Require(validation == S_FALSE);
		Require(Util::ValidateDeviceCreation(validation, nullptr, nullptr, nullptr) == validation);
	}

	void TestSwapChainOutputs()
	{
		HiddenWindow window;
		DXGI_SWAP_CHAIN_DESC description{};
		description.BufferDesc.Width = 64;
		description.BufferDesc.Height = 64;
		description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		description.BufferCount = 1;
		description.OutputWindow = window.handle;
		description.Windowed = TRUE;
		description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		ComPtr<IDXGISwapChain> swapChain;
		Check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
			nullptr, 0, D3D11_SDK_VERSION, &description, swapChain.GetAddressOf(), nullptr, nullptr, nullptr));
		Check(Util::ValidateDeviceCreation(S_OK, nullptr, nullptr, swapChain.GetAddressOf()));
		ComPtr<ID3D11Device> device;
		Check(swapChain->GetDevice(IID_PPV_ARGS(device.GetAddressOf())));
		ComPtr<ID3D11DeviceContext> context;
		device->GetImmediateContext(context.GetAddressOf());
		Require(!Util::InspectImmediateContextProtection(context.Get()).multithreadProtected);
		swapChain.Reset();
		context.Reset();
		device.Reset();

		Check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
			D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0, D3D11_SDK_VERSION, &description,
			swapChain.GetAddressOf(), device.GetAddressOf(), nullptr, context.GetAddressOf()));
		Require(Util::ValidateDeviceCreation(S_OK, device.GetAddressOf(), context.GetAddressOf(),
					swapChain.GetAddressOf()) == DXGI_ERROR_INVALID_CALL);
		Require(!swapChain && !device && !context);
	}

	void TestConcurrentStateTransactions()
	{
		Device fixture;
		Check(Util::ValidateImmediateContext(fixture.context.Get()));
		NativeRenderer renderer;
		std::atomic<bool> start = false;
		std::atomic<unsigned> mismatches = 0;
		std::array<std::thread, 2> writers;
		for (unsigned writer = 0; writer < writers.size(); ++writer) {
			writers[writer] = std::thread([&, writer]() {
				while (!start.load(std::memory_order_acquire))
					std::this_thread::yield();
				for (unsigned iteration = 0; iteration < 1000; ++iteration) {
					const D3D11_VIEWPORT expected{ 0, 0, static_cast<float>(32 + writer), 32, 0, 1 };
					const Util::RendererOwnership transaction(&renderer.lock, true);
					fixture.context->RSSetViewports(1, &expected);
					std::this_thread::yield();
					UINT count = 1;
					D3D11_VIEWPORT observed{};
					fixture.context->RSGetViewports(&count, &observed);
					if (count != 1 || observed.Width != expected.Width || observed.Height != expected.Height)
						mismatches.fetch_add(1, std::memory_order_relaxed);
				}
			});
		}
		start.store(true, std::memory_order_release);
		for (auto& writer : writers)
			writer.join();
		Require(mismatches.load() == 0);
		Require(!Util::InspectImmediateContextProtection(fixture.context.Get()).multithreadProtected);
	}

	void TestDeferredCommandListRestoration()
	{
		Device fixture;
		NativeRenderer renderer;
		Check(Util::ValidateImmediateContext(fixture.context.Get()));
		ComPtr<ID3D11DeviceContext> deferred;
		Check(fixture.device->CreateDeferredContext(0, deferred.GetAddressOf()));
		const D3D11_VIEWPORT recorded{ 7, 11, 19, 23, 0.125f, 0.875f };
		deferred->RSSetViewports(1, &recorded);
		ComPtr<ID3D11CommandList> commands;
		Check(deferred->FinishCommandList(FALSE, commands.GetAddressOf()));

		const D3D11_VIEWPORT initial{ 3, 5, 31, 37, 0.25f, 0.75f };
		fixture.context->RSSetViewports(1, &initial);
		fixture.context->ExecuteCommandList(commands.Get(), FALSE);
		UINT count = 1;
		D3D11_VIEWPORT observed{};
		fixture.context->RSGetViewports(&count, &observed);
		Require(count == 0);

		fixture.context->RSSetViewports(1, &initial);
		// Renderer transactions serialize private command execution with the
		// native state owner while API locking remains disabled.
		std::atomic<bool> start = false;
		std::thread executor([&]() {
			while (!start.load(std::memory_order_acquire))
				std::this_thread::yield();
			for (unsigned iteration = 0; iteration < 1000; ++iteration) {
				const Util::RendererOwnership ownership(&renderer.lock, true);
				fixture.context->ExecuteCommandList(commands.Get(), TRUE);
				std::this_thread::yield();
			}
		});
		unsigned mismatches = 0;
		auto expected = initial;
		start.store(true, std::memory_order_release);
		for (unsigned iteration = 0; iteration < 1000; ++iteration) {
			const Util::RendererOwnership ownership(&renderer.lock, true);
			expected.Width = static_cast<float>(32 + iteration);
			fixture.context->RSSetViewports(1, &expected);
			std::this_thread::yield();
			count = 1;
			fixture.context->RSGetViewports(&count, &observed);
			if (count != 1 || observed.TopLeftX != expected.TopLeftX || observed.TopLeftY != expected.TopLeftY ||
				observed.Width != expected.Width || observed.Height != expected.Height ||
				observed.MinDepth != expected.MinDepth || observed.MaxDepth != expected.MaxDepth)
				++mismatches;
		}
		executor.join();
		Require(mismatches == 0);
		count = 1;
		fixture.context->RSGetViewports(&count, &observed);
		Require(count == 1 && observed.Width == expected.Width && observed.Height == expected.Height);
		Require(!Util::InspectImmediateContextProtection(fixture.context.Get()).multithreadProtected);
	}

	void VerifyAutomaticCallProtection(ID3D11DeviceContext* a_context, bool a_expectBlocked)
	{
		struct CallObservation
		{
			std::promise<void> aboutToCall;
			std::promise<void> completed;
		};
		const auto observation = std::make_shared<CallObservation>();
		auto aboutToCall = observation->aboutToCall.get_future();
		auto completed = observation->completed.get_future();
		ComPtr<ID3D11DeviceContext> context = a_context;
		ComPtr<ID3D11Multithread> multithread;
		Check(context.As(&multithread));
		const D3D11_VIEWPORT expected{ 3, 5, 31, 37, 0.25f, 0.75f };
		bool observedAttempt = false;
		bool completedWhileLocked = false;
		std::thread caller;
		{
			const ContextTransaction transaction(multithread.Get());
			caller = std::thread([observation, context, expected]() {
				observation->aboutToCall.set_value();
				context->RSSetViewports(1, &expected);
				observation->completed.set_value();
			});
			observedAttempt = aboutToCall.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
			if (observedAttempt) {
				const auto wait = a_expectBlocked ? std::chrono::milliseconds(100) : std::chrono::milliseconds(2000);
				completedWhileLocked = completed.wait_for(wait) == std::future_status::ready;
			}
		}
		const bool completedAfterRelease = completed.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
		if (completedAfterRelease)
			caller.join();
		else
			caller.detach();
		Require(observedAttempt && completedAfterRelease);
		Require(completedWhileLocked != a_expectBlocked);
		UINT count = 1;
		D3D11_VIEWPORT observed{};
		context->RSGetViewports(&count, &observed);
		Require(count == 1 && observed.TopLeftX == expected.TopLeftX && observed.TopLeftY == expected.TopLeftY);
		Require(observed.Width == expected.Width && observed.Height == expected.Height);
		Require(observed.MinDepth == expected.MinDepth && observed.MaxDepth == expected.MaxDepth);
	}

	void TestAutomaticCallProtectionAndContextIsolation()
	{
		Device protectedDevice;
		Device independentDevice;
		Require(!Util::InspectImmediateContextProtection(protectedDevice.context.Get()).multithreadProtected);
		Require(!Util::InspectImmediateContextProtection(independentDevice.context.Get()).multithreadProtected);
		Check(Util::ValidateImmediateContext(protectedDevice.context.Get()));
		VerifyAutomaticCallProtection(protectedDevice.context.Get(), false);
		ComPtr<ID3D11Multithread> externalOwner;
		Check(protectedDevice.context.As(&externalOwner));
		externalOwner->SetMultithreadProtected(TRUE);
		Check(Util::ValidateImmediateContext(protectedDevice.context.Get()));
		Require(!Util::InspectImmediateContextProtection(independentDevice.context.Get()).multithreadProtected);
		VerifyAutomaticCallProtection(independentDevice.context.Get(), false);
		VerifyAutomaticCallProtection(protectedDevice.context.Get(), true);
		Require(Util::InspectImmediateContextProtection(protectedDevice.context.Get()).multithreadProtected);
		Require(!Util::InspectImmediateContextProtection(independentDevice.context.Get()).multithreadProtected);
	}

	void TestReadbackOwnershipAndRelease()
	{
		Device fixture;
		NativeRenderer renderer;
		Check(Util::ValidateImmediateContext(fixture.context.Get()));
		D3D11_TEXTURE2D_DESC description{};
		description.Width = 4;
		description.Height = 4;
		description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
		description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.Usage = D3D11_USAGE_STAGING;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		const std::array<unsigned, 16> pixels{ 0x12345678 };
		const D3D11_SUBRESOURCE_DATA initial{ pixels.data(), 16, 64 };
		ComPtr<ID3D11Texture2D> staging;
		Check(fixture.device->CreateTexture2D(&description, &initial, staging.GetAddressOf()));
		unsigned copied = 0;
		const auto copy = [&](const D3D11_MAPPED_SUBRESOURCE& mapped) -> HRESULT {
			Require(mapped.pData && mapped.RowPitch >= 16);
			Require(*static_cast<const unsigned*>(mapped.pData) == pixels.front());
			++copied;
			return S_OK;
		};
		bool clearPending = true;
		Check(Util::TryReadbackWithRendererOwnership(fixture.context.Get(), staging.Get(), &renderer.lock,
			[&](const D3D11_MAPPED_SUBRESOURCE&) -> HRESULT {
				Util::VRLoadingMenuClear::Result result{};
				std::thread loadingMessage([&]() {
					result = Util::VRLoadingMenuClear::TryExecute(&renderer.lock, [&]() { clearPending = false; });
				});
				loadingMessage.join();
				Require(result == Util::VRLoadingMenuClear::Result::Deferred && clearPending);
				return S_OK;
			}));
		Require(Util::VRLoadingMenuClear::TryExecute(&renderer.lock, [&]() { clearPending = false; }) ==
				Util::VRLoadingMenuClear::Result::Executed);
		Require(!clearPending);
		Require(Util::TryReadbackWithRendererOwnership(fixture.context.Get(), staging.Get(), &renderer.lock,
					[](const D3D11_MAPPED_SUBRESOURCE&) -> HRESULT { return E_FAIL; }) == E_FAIL);
		Require(Util::TryReadbackWithRendererOwnership(fixture.context.Get(), staging.Get(), nullptr, copy) == E_POINTER);
		HRESULT contended = S_OK;
		{
			const Util::RendererOwnership nativeFrame(&renderer.lock, true);
			std::thread worker([&]() {
				contended = Util::TryReadbackWithRendererOwnership(fixture.context.Get(), staging.Get(), &renderer.lock, copy);
			});
			worker.join();
			Require(contended == DXGI_ERROR_WAS_STILL_DRAWING && copied == 0);
			Check(Util::TryReadbackWithRendererOwnership(fixture.context.Get(), staging.Get(), &renderer.lock, copy));
		}
		Check(Util::TryReadbackWithRendererOwnership(fixture.context.Get(), staging.Get(), &renderer.lock, copy));
		bool propagated = false;
		try {
			(void)Util::TryReadbackWithRendererOwnership(fixture.context.Get(), staging.Get(), &renderer.lock,
				[](const D3D11_MAPPED_SUBRESOURCE&) -> HRESULT { throw std::runtime_error("readback copy"); });
		} catch (const std::runtime_error&) {
			propagated = true;
		}
		Require(propagated);
		std::thread afterException([&]() {
			Check(Util::TryReadbackWithRendererOwnership(fixture.context.Get(), staging.Get(), &renderer.lock, copy));
		});
		afterException.join();
		Require(copied == 3);
		Require(!Util::InspectImmediateContextProtection(fixture.context.Get()).multithreadProtected);
	}
}

int main()
{
	try {
		TestCreationDoesNotEnablePerCallLocking();
		TestPublishedContextOwnership();
		TestValidationPreservesProtectionState();
		TestRejectedContexts();
		TestCreationOutputs();
		TestSwapChainOutputs();
		TestConcurrentStateTransactions();
		TestDeferredCommandListRestoration();
		TestAutomaticCallProtectionAndContextIsolation();
		TestReadbackOwnershipAndRelease();
		std::cout << "WARP D3D11 ownership: unchanged API flags, creation outputs, rejected contexts, "
					 "concurrent renderer transactions, state restoration, readback contention and exception release passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
