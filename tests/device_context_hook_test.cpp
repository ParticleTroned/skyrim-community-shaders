#include "Utils/DeviceContextState.h"
#include "Utils/VirtualFunctionHook.cpp"
#include "d3d_resource_naming.h"
#include "render_map_context_com_fixture.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace
{
	ID3D11DeviceContext* expectedContext = nullptr;
	bool canonicalReceiver = true;
	unsigned clears = 0, viewportSets = 0, viewportGets = 0, stateSwaps = 0;

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

	struct SwapStateHook
	{
		static void Call(ID3D11DeviceContext1* context, ID3DDeviceContextState* state, ID3DDeviceContextState** previous)
		{
			canonicalReceiver &= context == expectedContext;
			++stateSwaps;
			original(context, state, previous);
		}
		static inline decltype(&Call) original = nullptr;
	};

	Util::DetourAttachResult RejectDetour(void**, void*) { return { ERROR_INVALID_BLOCK, true }; }
	BOOL WINAPI RejectProtection(LPVOID, SIZE_T, DWORD, PDWORD)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}
	struct ContextView : CSX::RenderMap::ContextHookFixture::Context
	{
		explicit ContextView(IUnknown* a_identity) : identity(a_identity) {}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override
		{
			if (!output)
				return E_POINTER;
			*output = nullptr;
			if (iid == __uuidof(IUnknown)) {
				identity->AddRef();
				*output = identity;
			} else if (iid == __uuidof(ID3D11DeviceContext)) {
				AddRef();
				*output = static_cast<ID3D11DeviceContext*>(this);
			} else if (iid == __uuidof(ID3D11DeviceContext1)) {
				AddRef();
				*output = static_cast<ID3D11DeviceContext1*>(this);
			} else {
				return E_NOINTERFACE;
			}
			return S_OK;
		}

		void STDMETHODCALLTYPE SwapDeviceContextState(ID3DDeviceContextState*, ID3DDeviceContextState** previous) override
		{
			if (previous)
				*previous = nullptr;
		}

		IUnknown* identity;
	};

	void Context1FallbackHook(ID3D11DeviceContext1*, ID3DDeviceContextState*, ID3DDeviceContextState**) {}

	void TestContext1FallbackIdentity()
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		auto identity = context.as<IUnknown>();
		Util::RegisterDeviceContextStateForHooks(context.get());
		HookState hookState;
		void* original = nullptr;
		ContextView hooked(identity.get()), alternate(identity.get());
		winrt::com_ptr<ID3D11DeviceContext1> context1;
		std::unique_ptr<Util::IsolatedDeviceContextState> isolated;
		Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "ContextHookTest::DistinctView"),
			"Could not establish native isolation before Context1 fallback");

		const auto result = CompleteContextHook(&hooked,
			InstallHook(hookState, &hooked, 131, kContext1Slots, ContextSlots, &original,
				reinterpret_cast<void*>(Context1FallbackHook), RejectDetour, RejectProtection));
		Require(result.error == NO_ERROR && result.method == Util::VirtualHookMethod::Clone && original,
			"Expected a cloned Context1 hook on a distinct interface view");

		auto* cached = isolated.get();
		Require(!Util::CreateIsolatedDeviceContextState(device.get(), &alternate, context1, isolated, "Invalid") && isolated.get() == cached,
			"Context1 fallback admitted new native state through a different view");
		{
			const Util::ScopedDeviceContextState guard(&alternate, cached);
			Require(!guard, "Context1 fallback admitted a cached native state through a different view");
		}

		struct RestoreRegistration
		{
			ID3D11DeviceContext* context;
			~RestoreRegistration() { Util::RegisterDeviceContextStateForHooks(context); }
		} restoreRegistration{ context.get() };
		Util::RegisterDeviceContextStateForHooks(&alternate);
		Require(!Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "Invalid") && isolated.get() == cached,
			"Re-registering a context view admitted new native state through the original view");
		{
			const Util::ScopedDeviceContextState guard(context1.get(), cached);
			Require(!guard, "Re-registering a context view admitted cached native state through the original view");
		}

		winrt::com_ptr<ID3D11Device> otherDevice;
		winrt::com_ptr<ID3D11DeviceContext> otherContext;
		winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, otherDevice.put(), nullptr, otherContext.put()));
		winrt::com_ptr<ID3D11DeviceContext1> otherContext1;
		std::unique_ptr<Util::IsolatedDeviceContextState> otherIsolated;
		Require(Util::CreateIsolatedDeviceContextState(otherDevice.get(), otherContext.get(), otherContext1, otherIsolated, "ContextHookTest::OtherContext"),
			"A disabled renderer context blocked native isolation on an unrelated context");
		const Util::ScopedDeviceContextState otherGuard(otherContext1.get(), otherIsolated.get());
		Require(static_cast<bool>(otherGuard), "A disabled renderer context blocked a native scope on an unrelated context");
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
		std::unique_ptr<Util::IsolatedDeviceContextState> isolated;
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

	template <unsigned Slot, class Hook, class Interface>
	void Install(Interface* context, Util::VirtualHookMethod method)
	{
		// Hook storage must outlive every native table that can still reference it.
		static auto& state = *new HookState;
		auto** original = reinterpret_cast<void**>(&Hook::original);
		auto* hook = reinterpret_cast<void*>(Hook::Call);
		const auto slots = std::is_same_v<Interface, ID3D11DeviceContext1> ? kContext1Slots : kContextSlots;
		const auto result = method == Util::VirtualHookMethod::Detour ? Util::InstallVirtualFunctionHook(context, Slot, original, hook) :
		                                                                CompleteContextHook(context, InstallHook(state, context, Slot, slots, ContextSlots, original, hook,
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
		std::unique_ptr<Util::IsolatedDeviceContextState> isolated;
		Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "ContextHookTest::Isolated"),
			"New renderer context did not permit isolated state creation");
		Install<110, ClearStateHook>(context.get(), method);
		Install<44, SetViewportsHook>(context.get(), method);
		Install<95, GetViewportsHook>(context.get(), method);
		void* rejectedOriginal = nullptr;
		Require(Util::InstallVirtualFunctionHook(context.get(), 131, &rejectedOriginal, reinterpret_cast<void*>(SwapStateHook::Call)).error == ERROR_INVALID_PARAMETER,
			"Base context accepted an extended interface slot");
		Require(Util::InstallVirtualFunctionHook(context1.get(), 134, &rejectedOriginal, reinterpret_cast<void*>(SwapStateHook::Call)).error == ERROR_INVALID_PARAMETER && !rejectedOriginal,
			"Extended context accepted a slot beyond its interface");
		Install<131, SwapStateHook>(context1.get(), method);
		const auto originalSwap = SwapStateHook::original;
		Install<131, SwapStateHook>(context1.get(), method);
		Require(SwapStateHook::original == originalSwap, "Repeated extended context installation replaced its original target");
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
		// Native state swaps can replace dispatch tables, so exercise them after scene interception.
		const unsigned swapsBefore = stateSwaps;
		winrt::com_ptr<ID3DDeviceContextState> previous;
		context1->SwapDeviceContextState(isolated->native.get(), previous.put());
		context1->SwapDeviceContextState(previous.get(), nullptr);
		Require(stateSwaps == swapsBefore + 2 && canonicalReceiver, "Extended context state swaps bypassed their hook");
	}
}

int main(int argc, char** argv)
{
	try {
		const std::string_view mode = argc == 2 ? argv[1] : "detour";
		if (argc == 2 && mode == "publication_failure") {
			TestContext1FallbackIdentity();
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
