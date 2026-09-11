#include "Api/AcceptedDrawService.h"
#include "Api/AcceptedDrawGeometryScope.h"

#include "Api/ServiceRegistry.h"
#include "Features/Upscaling.h"
#include "Globals.h"

#include <wrl/client.h>

namespace
{
	using namespace CSXAcceptedDrawAPI;
	CSX::Api::AcceptedDrawRegistry registry;
	std::atomic_bool ready{ false }, geometryHooksInstalled{ false };
	std::atomic<ID3D11DeviceContext*> immediate{ nullptr };
	std::atomic<DWORD> renderThread{ 0 };
	std::atomic_uint64_t filteredDraws{ 0 }, wrongThreadDraws{ 0 }, geometryScopeErrors{ 0 };
	thread_local CSX::Api::AcceptedDrawGeometryScope geometryScope;
	thread_local uint32_t suppressionDepth = 0;

	uint32_t __cdecl Register(ObserverFn a_observer, void* a_user, uint64_t* a_subscription) noexcept
	{
		if (a_subscription)
			*a_subscription = 0;
		if (!ready.load(std::memory_order_acquire))
			return NotReady;
		try {
			return registry.Register(a_observer, a_user, a_subscription);
		} catch (...) {
			return Failed;
		}
	}

	uint32_t __cdecl Unregister(uint64_t a_subscription) noexcept
	{
		try {
			return registry.Unregister(a_subscription);
		} catch (...) {
			return Failed;
		}
	}
	const API api{ sizeof(API), Version, RequiredCapabilities, &Register, &Unregister };
}

namespace CSX::Api
{
	void InitializeAcceptedDrawService(ID3D11DeviceContext* a_context)
	{
		ready.store(false, std::memory_order_release);
		if (!globals::game::isVR || !geometryHooksInstalled.load(std::memory_order_acquire) ||
			!a_context || a_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			return;
		if (!Upscaling::InstallAcceptedDrawD3DHooks(a_context)) {
			logger::error("Accepted draw API: D3D11 indexed draw coverage could not initialize; registration unavailable");
			return;
		}
		immediate.store(a_context, std::memory_order_release);
		renderThread.store(0, std::memory_order_release);
		ready.store(true, std::memory_order_release);
		logger::info("Accepted draw API v1 ready: native VR lighting/effect geometry, indexed/instanced D3D11 submission, isolated replay");
	}

	void AcceptedDrawGeometryHooksInstalled()
	{
		geometryHooksInstalled.store(true, std::memory_order_release);
	}

	void BeginAcceptedDrawGeometry(RE::BSRenderPass* a_pass)
	{
		if (!geometryScope.Begin(a_pass))
			geometryScopeErrors.fetch_add(1, std::memory_order_relaxed);
	}

	void ActivateAcceptedDrawGeometry(RE::BSRenderPass* a_pass)
	{
		geometryScope.Activate(a_pass, a_pass ? a_pass->geometry : nullptr);
	}

	void SuspendAcceptedDrawGeometry()
	{
		geometryScope.Suspend();
	}

	void EndAcceptedDrawGeometry(RE::BSRenderPass* a_pass)
	{
		if (!geometryScope.End(a_pass))
			geometryScopeErrors.fetch_add(1, std::memory_order_relaxed);
	}

	SuppressAcceptedDraw::SuppressAcceptedDraw() { ++suppressionDepth; }
	SuppressAcceptedDraw::~SuppressAcceptedDraw() { --suppressionDepth; }

	void PublishAcceptedDraw(ID3D11DeviceContext* a_context, const Arguments& a_arguments,
		AcceptedDrawRegistry::NativeReplay a_replay) noexcept
	{
		if (!registry.HasObservers() || !ready.load(std::memory_order_acquire) || AcceptedDrawRegistry::IsDispatching())
			return;
		if (a_context != immediate.load(std::memory_order_acquire) || suppressionDepth ||
			!geometryScope.Current() ||
			!a_arguments.indexCount || !a_arguments.instanceCount) {
			filteredDraws.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		const auto thread = GetCurrentThreadId();
		auto owner = renderThread.load(std::memory_order_acquire);
		if (!owner && renderThread.compare_exchange_strong(owner, thread, std::memory_order_acq_rel))
			owner = thread;
		if (owner != thread) {
			wrongThreadDraws.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		auto* renderer = globals::game::renderer;
		if (!renderer)
			return;
		auto* sceneDepth = reinterpret_cast<ID3D11Texture2D*>(renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture);
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
		a_context->OMGetRenderTargets(0, nullptr, dsv.GetAddressOf());
		Microsoft::WRL::ComPtr<ID3D11Resource> boundDepth;
		if (dsv)
			dsv->GetResource(boundDepth.GetAddressOf());
		if (!sceneDepth || boundDepth.Get() != sceneDepth) {
			filteredDraws.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		D3D11_TEXTURE2D_DESC description{};
		sceneDepth->GetDesc(&description);
		if (description.ArraySize != 1 || description.SampleDesc.Count != 1) {
			filteredDraws.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		registry.Dispatch({ sizeof(Draw), Version, 0, a_context, geometryScope.Current(),
							  sceneDepth, MainScene, PackedStereo2D, a_arguments, nullptr, nullptr },
			a_replay);
	}

	const API* GetAcceptedDrawAPI() { return &api; }

	void RegisterAcceptedDrawService()
	{
		if (!globals::game::isVR)
			return;
		const auto result = GetProcessServiceRegistry().Register({
			.name = "csx.render.accepted_draw",
			.capabilities = ServiceAPI::kCapabilityInspection | ServiceAPI::kCapabilityEventStream,
			.interfacePointer = &api,
		});
		if (result != ServiceAPI::Status::kSuccess && result != ServiceAPI::Status::kAlreadyRegistered)
			logger::error("Accepted draw API service registration failed: {}", static_cast<uint32_t>(result));
	}

	AcceptedDrawStatus InspectAcceptedDrawService()
	{
		return { ready.load(), filteredDraws.load(), wrongThreadDraws.load(), geometryScopeErrors.load(), registry.Inspect() };
	}
}

extern "C" __declspec(dllexport) const CSXAcceptedDrawAPI::API* __cdecl CSX_GetAcceptedDrawAPI(uint32_t a_version, uint32_t a_minimumTableSize) noexcept
{
	if (a_version != CSXAcceptedDrawAPI::Version || a_minimumTableSize > sizeof(CSXAcceptedDrawAPI::API) || !REL::Module::IsVR())
		return nullptr;
	return CSX::Api::GetAcceptedDrawAPI();
}
