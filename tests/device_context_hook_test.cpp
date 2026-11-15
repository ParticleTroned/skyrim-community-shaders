#include "Utils/DeviceContextState.h"
#include "Utils/VirtualFunctionHook.cpp"
#include "d3d_resource_naming.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
	ID3D11DeviceContext* expectedContext = nullptr;
	bool canonicalReceiver = true;
	unsigned clears = 0, viewportSets = 0, viewportGets = 0;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct ClearStateHook
	{
		static void Call(ID3D11DeviceContext* context)
		{
			canonicalReceiver &= context == expectedContext;
			++clears;
			original(context);
		}
		static inline decltype(&Call) original = nullptr;
	};

	struct SetViewportsHook
	{
		static void Call(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
		{
			canonicalReceiver &= context == expectedContext;
			++viewportSets;
			original(context, count, viewports);
		}
		static inline decltype(&Call) original = nullptr;
	};

	struct GetViewportsHook
	{
		static void Call(ID3D11DeviceContext* context, UINT* count, D3D11_VIEWPORT* viewports)
		{
			canonicalReceiver &= context == expectedContext;
			++viewportGets;
			original(context, count, viewports);
		}
		static inline decltype(&Call) original = nullptr;
	};

	Util::DetourAttachResult RejectDetour(void**, void*) { return { ERROR_INVALID_BLOCK, true }; }
	BOOL WINAPI RejectProtection(LPVOID, SIZE_T, DWORD, PDWORD)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}
	unsigned protectionCalls = 0;
	DWORD tableProtection = 0;
	BOOL WINAPI FailProtectionRestore(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous)
	{
		if (++protectionCalls == 1) {
			const auto result = VirtualProtect(address, size, protection, previous);
			tableProtection = *previous;
			return result;
		}
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	void TestPublishedHookFailure()
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		expectedContext = context.get();
		Util::RegisterDeviceContextStateForHooks(context.get());
		winrt::com_ptr<ID3D11DeviceContext1> context1;
		winrt::com_ptr<ID3DDeviceContextState> isolated;
		Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "ContextHookTest::BeforePublicationFailure"),
			"Initial isolated state creation failed");
		static auto& state = *new HookState;
		const auto result = CompleteContextHook(context.get(), InstallHook(state, context.get(), 110, kContextSlots, ContextSlots,
																   reinterpret_cast<void**>(&ClearStateHook::original), reinterpret_cast<void*>(ClearStateHook::Call), RejectDetour, FailProtectionRestore));
		DWORD ignored = 0;
		auto** table = *reinterpret_cast<void***>(context.get());
		Require(VirtualProtect(table + 110, sizeof(void*), tableProtection, &ignored) != FALSE, "Test table protection cleanup failed");
		Require(result.error == ERROR_ACCESS_DENIED && result.method == Util::VirtualHookMethod::VTable && protectionCalls == 2,
			"Expected a published table hook with a protection-restoration error");
		context->ClearState();
		Require(clears == 1 && canonicalReceiver, "The reported failure did not retain its published hook");
		auto* original = isolated.get();
		Require(!Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "Invalid") && isolated.get() == original,
			"Published table-hook failure admitted new isolated state");
		const Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
		Require(!guard, "Published table-hook failure admitted a cached isolated state");
	}

	template <unsigned Slot, class Hook>
	void Install(ID3D11DeviceContext* context, Util::VirtualHookMethod method)
	{
		// Hook storage must outlive every native table that can still reference it.
		static auto& state = *new HookState;
		auto** original = reinterpret_cast<void**>(&Hook::original);
		auto* hook = reinterpret_cast<void*>(Hook::Call);
		const auto result = method == Util::VirtualHookMethod::Detour ? Util::InstallVirtualFunctionHook(context, Slot, original, hook) :
		                                                                CompleteContextHook(context, InstallHook(state, context, Slot, kContextSlots, ContextSlots, original, hook,
																										 RejectDetour, method == Util::VirtualHookMethod::Clone ? RejectProtection : VirtualProtect));
		Require(result.error == NO_ERROR, "Native context hook installation failed");
		Require(result.method == method,
			"Hook installation did not exercise the selected method");
	}

	void TestContext(Util::VirtualHookMethod method)
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		expectedContext = context.get();
		canonicalReceiver = true;
		Util::RegisterDeviceContextStateForHooks(context.get());
		winrt::com_ptr<ID3D11DeviceContext1> context1;
		winrt::com_ptr<ID3DDeviceContextState> isolated;
		Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "ContextHookTest::Isolated"),
			"New renderer context did not permit isolated state creation");
		Install<110, ClearStateHook>(context.get(), method);
		Install<44, SetViewportsHook>(context.get(), method);
		Install<95, GetViewportsHook>(context.get(), method);
		Util::RegisterDeviceContextStateForHooks(context.get());
		const bool swapsAllowed = method == Util::VirtualHookMethod::Detour;
		auto* originalIsolated = isolated.get();
		Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "ContextHookTest::Replacement") == swapsAllowed,
			"Context isolation did not follow the installed native hook method");
		Require(swapsAllowed || isolated.get() == originalIsolated, "Rejected creation replaced an existing isolated state");
		for (unsigned iteration = 0; iteration < 3; ++iteration) {
			const D3D11_VIEWPORT sceneViewport{ 1, 2, 32, 32, .2f, .7f };
			const unsigned setsBefore = viewportSets;
			context->RSSetViewports(1, &sceneViewport);
			Require(viewportSets == setsBefore + 1, "Scene viewport setter bypassed its hook");
			{
				Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
				Require(static_cast<bool>(guard) == swapsAllowed, "Previously created isolation ignored native hook compatibility");
				if (guard) {
					context->ClearState();
					const D3D11_VIEWPORT overlayViewport{ 0, 0, 16, 16, 0, 1 };
					context->RSSetViewports(1, &overlayViewport);
				}
			}
			D3D11_VIEWPORT restored{};
			UINT count = 1;
			const unsigned getsBefore = viewportGets;
			context->RSGetViewports(&count, &restored);
			Require(viewportGets == getsBefore + 1, "Restored viewport getter bypassed its hook");
			Require(count == 1 && std::memcmp(&restored, &sceneViewport, sizeof(restored)) == 0,
				"Intercepted isolation changed the saved native scene state");
			const unsigned clearsBefore = clears;
			context->ClearState();
			Require(clears == clearsBefore + 1, "Restored ClearState bypassed its hook");
			Require(canonicalReceiver, "Native dispatch changed the context identity seen by hooks");
		}
	}
}

int main(int argc, char** argv)
{
	try {
		const std::string_view mode = argc == 2 ? argv[1] : "detour";
		if (argc == 2 && mode == "publication_failure") {
			TestPublishedHookFailure();
			std::cout << "D3D11 published table-hook failure retained interception and disabled isolation\n";
			return 0;
		}
		Require(argc <= 2 && (mode == "detour" || mode == "vtable" || mode == "clone"), "Expected detour, vtable or clone");
		const auto method = mode == "detour" ? Util::VirtualHookMethod::Detour :
		                    mode == "vtable" ? Util::VirtualHookMethod::VTable :
		                                       Util::VirtualHookMethod::Clone;
		TestContext(method);
		TestContext(method);
		std::cout << "D3D11 WARP " << mode
				  << " hooks preserved scene interception, context identity and state across isolation admission and context replacement\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
