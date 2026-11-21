#include "Features/FurShells/DepthBinding.h"
#include "Utils/DeviceContextState.h"
#include "commonlib_d3d_bridge_fake.h"
#include "d3d_resource_naming.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <format>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <winrt/base.h>
#include <wrl/implements.h>

using json = nlohmann::json;

namespace
{
	thread_local bool rejectHeapAllocations = false;
	struct RejectHeapAllocations
	{
		RejectHeapAllocations() { rejectHeapAllocations = true; }
		~RejectHeapAllocations() { rejectHeapAllocations = false; }
	};
}

void* operator new(std::size_t size)
{
	if (!rejectHeapAllocations)
		if (void* allocation = std::malloc(size ? size : 1))
			return allocation;
	throw std::bad_alloc{};
}
void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }

namespace REL
{
	template <class Function>
	using Relocation = Function*;
}
namespace CSX::Diagnostics::PerformanceCosts
{
	enum class Point
	{
		ReverseZBindings
	};
	bool referenceBindings = false;
	bool ReferenceEnabled(Point) { return referenceBindings; }
}
namespace logger
{
	unsigned errors = 0;
	template <class... Args>
	void error(const char*, const Args&...)
	{
		++errors;
	}
}
struct ReverseZ
{
	bool loaded = true, bootLatched = true, requestedThisBoot = true, activationRejectedThisBoot = false;
	struct
	{
		bool EnableReverseZBuffer = true;
	} settings;
	std::atomic_bool activeThisBoot{ false };
	bool depthTargetsConverted = false, runtimeHooksInstalled = true, cameraHooksInstalled = true;
	uint32_t convertedTargetMask = 0;
	std::atomic_uint64_t generation{ 0 };
	std::atomic<double> extraDepthBytesPerPixel{ 0 };
	mutable std::mutex failureMutex;
	std::string failure;
	const char* failurePhase = "none";
	uint64_t failureGeneration = 0;
	bool nativeShadersReady = true;
	unsigned nativePreparations = 0;
	bool IsActive() const { return activeThisBoot.load(); }
	bool IsEnabledAtStartup() const { return settings.EnableReverseZBuffer; }
	uint64_t GetGeneration() const { return generation.load(); }
	double GetAdditionalDepthBytesPerPixel() const { return extraDepthBytesPerPixel.load(); }
	void LatchBootState() {}
	void InstallRuntimeHooks() {}
	bool PrepareNativeShaders()
	{
		++nativePreparations;
		return nativeShadersReady;
	}
	json GetDiagnostics() const;
	bool HasPendingBootChange() const;
	bool PrepareDepthTargets();
	bool IsConvertedDepthTarget(uint32_t) const;
	bool IsReverseDepthView(ID3D11DepthStencilView*) const;
	void RegisterSceneDepthView(ID3D11DepthStencilView*);
	void UnregisterSceneDepthView(ID3D11DepthStencilView*);
	void RecordFailure(const char*, const char*, uint64_t);
	bool RejectDepthPreparation(const char*);
	void RejectRendering(const char* reason);
	ID3D11DepthStencilState* GetReversedState(ID3D11DepthStencilState*);
	ID3D11RasterizerState* GetReversedRasterizerState(ID3D11RasterizerState*);
	unsigned depthMappings = 0, rasterMappings = 0;
	ID3D11DepthStencilState* CountedGetReversedState(ID3D11DepthStencilState* state)
	{
		++depthMappings;
		return GetReversedState(state);
	}
	ID3D11RasterizerState* CountedGetReversedRasterizerState(ID3D11RasterizerState* state)
	{
		++rasterMappings;
		return GetReversedRasterizerState(state);
	}
};
namespace RE::RENDER_TARGETS_DEPTHSTENCIL
{
	enum RENDER_TARGET_DEPTHSTENCIL
	{
		kMAIN,
		kMAIN_COPY,
		kDECAL_OCCLUSION,
		kPOST_ZPREPASS_COPY,
		kPOST_WATER_COPY,
		kMAIN_DOWNSAMPLE,
		kSHADOWMAPS,
		kCUBEMAP_REFLECTIONS,
		kHUDMENU,
		kTOTAL,
		kNONE = -1
	};
}
struct DepthTarget
{
	ID3D11Texture2D* texture = nullptr;
	ID3D11DepthStencilView* views[8]{};
	ID3D11DepthStencilView* readOnlyViews[8]{};
	ID3D11ShaderResourceView* depthSRV = nullptr;
	ID3D11ShaderResourceView* stencilSRV = nullptr;
	~DepthTarget()
	{
		if (texture)
			texture->Release();
		for (auto* view : views)
			if (view)
				view->Release();
		for (auto* view : readOnlyViews)
			if (view)
				view->Release();
		if (depthSRV)
			depthSRV->Release();
		if (stencilSRV)
			stencilSRV->Release();
	}
};
struct Renderer
{
	struct
	{
		DepthTarget depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL];
	} data;
	auto& GetDepthStencilData() { return data; }
};
namespace RE::BSGraphics
{
	struct ViewData
	{
		struct Range
		{
			float x = 0, y = 1;
		} viewDepthRange;
	};
}
struct ShadowState
{
	struct EngineViewport
	{
		float topLeftX = 0, topLeftY = 0, width = 0, height = 0, minDepth = 0, maxDepth = 0;
		EngineViewport& operator=(const D3D11_VIEWPORT& viewport)
		{
			topLeftX = viewport.TopLeftX;
			topLeftY = viewport.TopLeftY;
			width = viewport.Width;
			height = viewport.Height;
			minDepth = viewport.MinDepth;
			maxDepth = viewport.MaxDepth;
			return *this;
		}
		operator D3D11_VIEWPORT() const { return { topLeftX, topLeftY, width, height, minDepth, maxDepth }; }
	};
	std::array<RE::BSGraphics::ViewData, 2> eyes;
	struct
	{
		EngineViewport viewPort{};
		uint32_t rasterStateDepthBiasMode = 0;
	} data;
	auto& GetVRRuntimeData() { return data; }
};
namespace globals
{
	struct ShaderCache
	{
		bool enabled = true;
		unsigned clears = 0;
		bool IsEnabled() const { return enabled; }
		void Clear() { ++clears; }
	};
	ShaderCache cache;
	ShaderCache* shaderCache = &cache;
	struct State
	{
		bool enableVShaders = true, enablePShaders = true;
	} stateStorage;
	State* state = &stateStorage;
	bool uploadHookReady = true;
	bool IsVRFrameBufferUploadHookReady() { return uploadHookReady; }
	namespace game
	{
		Renderer* renderer;
		ShadowState* shadowState = nullptr;
		bool isVR = false;
	}
	namespace d3d
	{
		ID3D11Device* device;
		ID3D11DeviceContext* context;
	}
	namespace features
	{
		ReverseZ reverseZ;
	}
}
namespace VRDepthCullingTemporal
{
	void InvalidateDepthGeneration() {}
}
namespace
{
#include "reverse_z_state_comparison.h"
#include "reverse_z_state_resources.h"
#include "reverse_z_state_targets.h"

#include "reverse_z_state_storage.h"
	uint32_t publishedDepthStencilTarget = static_cast<uint32_t>(RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE);
	uint32_t PublishedDepthStencilTarget() { return publishedDepthStencilTarget; }
	std::array nativeViewportBiases{ 0.0f, .000015f, 0.0f, 0.0f, 0.0f, 0.0f,
		.000026f, .000015f, .000028f, .000017f, .000030f, .000019f };
	unsigned nativeViewportBiasReads = 0, publishedViewportReads = 0;
	std::span<const float> NativeViewportDepthBiases()
	{
		++nativeViewportBiasReads;
		return nativeViewportBiases;
	}
	RE::BSGraphics::ViewData* PublishedViewData(uint32_t eye)
	{
		++publishedViewportReads;
		return globals::game::shadowState ? &globals::game::shadowState->eyes.at(eye) : nullptr;
	}
#include "reverse_z_state_views.h"
	void ReapplyNearPlaneShader(ID3D11DeviceContext*) {}
#include "reverse_z_state_getters.h"
// Count slow-path calls without replacing the production hook or mapping bodies.
#define GetReversedState CountedGetReversedState
#define GetReversedRasterizerState CountedGetReversedRasterizerState
#include "reverse_z_state_hooks.h"
#undef GetReversedRasterizerState
#undef GetReversedState
	ID3D11RenderTargetView* g_lastClearedCubeFace = nullptr;
	ID3D11VertexShader* g_boundVertexShader = nullptr;
#include "reverse_z_state_clear.h"

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	std::array<unsigned, 3> stateForwards{};

	void TestMenuTargetRefresh(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 2;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		winrt::com_ptr<ID3D11Texture2D> color, readback, depth;
		Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, color.put())), "Menu target creation failed");
		Util::SetResourceName(color.get(), "ReverseZStateTest::MenuColor");
		winrt::com_ptr<ID3D11RenderTargetView> colorView;
		Require(SUCCEEDED(device->CreateRenderTargetView(color.get(), nullptr, colorView.put())), "Menu color view creation failed");
		Util::SetResourceName(colorView.get(), "ReverseZStateTest::MenuColor RTV");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, readback.put())), "Menu readback creation failed");
		Util::SetResourceName(readback.get(), "ReverseZStateTest::MenuReadback");
		desc.Format = DXGI_FORMAT_D32_FLOAT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, depth.put())), "Menu depth creation failed");
		Util::SetResourceName(depth.get(), "ReverseZStateTest::MenuDepth");
		winrt::com_ptr<ID3D11DepthStencilView> depthView;
		Require(SUCCEEDED(device->CreateDepthStencilView(depth.get(), nullptr, depthView.put())), "Menu depth view creation failed");
		Util::SetResourceName(depthView.get(), "ReverseZStateTest::MenuDepth DSV");
		const char* program =
			"float4 vs(uint id:SV_VertexID):SV_Position {"
			"float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0.5,1); }"
			"float4 ps():SV_Target {return float4(0,1,0,1);}";
		winrt::com_ptr<ID3DBlob> vertexCode, pixelCode, errors;
		Require(SUCCEEDED(D3DCompile(program, std::strlen(program), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, vertexCode.put(), errors.put())), "Menu vertex compilation failed");
		errors = nullptr;
		Require(SUCCEEDED(D3DCompile(program, std::strlen(program), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, pixelCode.put(), errors.put())), "Menu pixel compilation failed");
		winrt::com_ptr<ID3D11VertexShader> vertex;
		winrt::com_ptr<ID3D11PixelShader> pixel;
		Require(SUCCEEDED(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.put())), "Menu vertex creation failed");
		Util::SetResourceName(vertex.get(), "ReverseZStateTest::MenuVS");
		Require(SUCCEEDED(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, pixel.put())), "Menu pixel creation failed");
		Util::SetResourceName(pixel.get(), "ReverseZStateTest::MenuPS");
		D3D11_RASTERIZER_DESC rasterDesc{};
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.CullMode = D3D11_CULL_NONE;
		rasterDesc.DepthClipEnable = TRUE;
		winrt::com_ptr<ID3D11RasterizerState> raster;
		Require(SUCCEEDED(device->CreateRasterizerState(&rasterDesc, raster.put())), "Menu rasterizer creation failed");
		Util::SetResourceName(raster.get(), "ReverseZStateTest::MenuRasterizer");
		globals::features::reverseZ.activeThisBoot = true;
		globals::features::reverseZ.generation = 1;
		g_reversalActive = true;
		g_viewIsReverse.emplace(depthView.get(), ViewVerdict{ depthView, true });
		for (bool reversed : { false, true }) {
			ID3D11DeviceContext_ClearState::thunk(context);
			g_reverseTargetBound = false;
			g_projectionReversed = reversed;
			auto* target = colorView.get();
			context->OMSetRenderTargets(1, &target, depthView.get());
			ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, nullptr, 0);
			RefreshBoundDepthTarget();
			Require(g_reverseTargetBound, "World activation did not reclassify the already bound depth view");
			const float staleColor[]{ 1, 0, 0, 1 };
			context->ClearRenderTargetView(target, staleColor);
			ID3D11DeviceContext_ClearDepthStencilView::thunk(context, depthView.get(), D3D11_CLEAR_DEPTH, 1, 0);
			const D3D11_VIEWPORT viewport{ 0, 0, 2, 2, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->RSSetState(raster.get());
			context->VSSetShader(vertex.get(), nullptr, 0);
			context->PSSetShader(pixel.get(), nullptr, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->Draw(3, 0);
			context->OMSetRenderTargets(0, nullptr, nullptr);
			context->CopyResource(readback.get(), color.get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Require(SUCCEEDED(context->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped)), "Menu readback failed");
			const auto result = *static_cast<const uint32_t*>(mapped.pData);
			context->Unmap(readback.get(), 0);
			Require(result == 0xff00ff00u, reversed ? "Reversed scene retained stale color" : "Standard menu retained stale color after depth clear reversal");
		}
		g_reversalActive = g_reverseTargetBound = false;
		context->OMSetRenderTargets(0, nullptr, depthView.get());
		auto& reverse = globals::features::reverseZ;
		const auto errorsBeforeFailure = logger::errors;
		reverse.RejectRendering("Invalid camera frustum");
		winrt::com_ptr<ID3D11DepthStencilState> blockedState;
		context->OMGetDepthStencilState(blockedState.put(), nullptr);
		Require(g_reverseTargetBound && blockedState.get() == g_rejectedDepthState.get(),
			"First world-camera failure left the bound scene target drawing");
		const auto firstFailure = reverse.GetDiagnostics();
		Require(firstFailure.at("failurePhase") == "rendering" && firstFailure.at("failureGeneration") == 1 &&
					firstFailure.at("restartRequired") == true && firstFailure.at("active") == true,
			"Camera failure lost the active convention or generation");
		for (unsigned repeat = 0; repeat < 100; ++repeat)
			reverse.RejectRendering("Published scene camera unavailable");
		Require(reverse.GetDiagnostics() == firstFailure && logger::errors == errorsBeforeFailure + 1,
			"Repeated camera failures changed the first reason or flooded the log");
		// Reset the fixture between independent boot scenarios.
		reverse.failure.clear();
		reverse.failurePhase = "none";
		reverse.failureGeneration = 0;
		g_renderingRejected = false;
		g_reversalActive = true;
		ID3D11DeviceContext_ClearState::thunk(context);
		g_viewIsReverse.clear();
		g_reverseTargetBound = g_projectionReversed = false;
	}
	template <class Hook, size_t Slot>
	void UseNative(ID3D11DeviceContext* context)
	{
		Hook::func = reinterpret_cast<decltype(Hook::func)>((*reinterpret_cast<void***>(context))[Slot]);
	}
}
#include "reverse_z_state_creation.h"
#include "reverse_z_state_diagnostics.h"
#include "reverse_z_state_preparation.h"

namespace
{
	void CreateSceneTargets(Renderer& renderer, ID3D11Device* device)
	{
		for (auto index : { RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN, RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY,
				 RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_DOWNSAMPLE }) {
			auto& target = renderer.data.depthStencils[index];
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 4;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = kStandardDepthTypeless;
			desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &target.texture)), "Scene fixture allocation failed");
			Util::SetResourceName(target.texture, "ReverseZStateTest::SceneDepth");
			D3D11_DEPTH_STENCIL_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			Require(SUCCEEDED(device->CreateDepthStencilView(target.texture, &view, &target.views[0])), "Scene DSV fixture failed");
			Util::SetResourceName(target.views[0], "ReverseZStateTest::SceneDepth DSV");
			view.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
			Require(SUCCEEDED(device->CreateDepthStencilView(target.texture, &view, &target.readOnlyViews[0])), "Scene read-only fixture failed");
			Util::SetResourceName(target.readOnlyViews[0], "ReverseZStateTest::SceneDepth RODSV");
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			Require(SUCCEEDED(device->CreateShaderResourceView(target.texture, &srv, &target.depthSRV)), "Scene SRV fixture failed");
			Util::SetResourceName(target.depthSRV, "ReverseZStateTest::SceneDepth SRV");
		}
	}

	struct ViewLifetime final : Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IUnknown>
	{
		explicit ViewLifetime(unsigned* count) : destroyed(count) {}
		~ViewLifetime() { ++*destroyed; }
		unsigned* destroyed;
	};

	void TestFeatureViewRetirement(ID3D11Device* device)
	{
		ReverseZ reverse;
		reverse.activeThisBoot = true;
		g_reversalActive = true;
		g_viewIsReverse.clear();
		unsigned destroyed = 0;
		for (unsigned generation = 0; generation < 32; ++generation) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 4;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_D32_FLOAT;
			desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11DepthStencilView> view;
			Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.put())), "Feature depth allocation failed");
			Util::SetResourceName(texture.get(), "ReverseZStateTest::FeatureDepth");
			Require(SUCCEEDED(device->CreateDepthStencilView(texture.get(), nullptr, view.put())), "Feature DSV creation failed");
			Util::SetResourceName(view.get(), "ReverseZStateTest::FeatureDepth DSV");
			auto lifetime = Microsoft::WRL::Make<ViewLifetime>(&destroyed);
			Require(SUCCEEDED(view->SetPrivateDataInterface(__uuidof(IUnknown), lifetime.Get())), "Feature lifetime marker failed");
			lifetime.Reset();
			reverse.RegisterSceneDepthView(view.get());
			reverse.RegisterSceneDepthView(view.get());
			Require(reverse.IsReverseDepthView(view.get()) && g_viewIsReverse.size() == 1, "Feature view registration lost its identity");
			// Retirement must work even if rendering has stopped using the depth domain.
			g_reversalActive = false;
			reverse.UnregisterSceneDepthView(view.get());
			reverse.UnregisterSceneDepthView(view.get());
			reverse.UnregisterSceneDepthView(nullptr);
			Require(g_viewIsReverse.empty(), "Feature recreation retained a stale classification");
			view = nullptr;
			Require(destroyed == generation + 1, "Feature recreation retained the old depth view");
			g_reversalActive = true;
		}
	}

	void CheckFailure(ReverseZ& reverse, bool active, const char* phase, uint64_t failedGeneration)
	{
		const auto status = reverse.GetDiagnostics();
		Require(status.at("active") == active && status.at("renderingBlocked") == active &&
					status.at("activationRejected") == !active && status.at("restartRequired") == true &&
					status.at("failurePhase") == phase && status.at("failureGeneration") == failedGeneration &&
					!status.at("failure").get<std::string>().empty(),
			"Failure status lost its convention, generation or recovery contract");
	}

	bool PreparationThrows(ReverseZ& reverse)
	{
		bool prepared = false;
		try {
			prepared = reverse.PrepareDepthTargets();
		} catch (const std::runtime_error& error) {
			Require(reverse.failure == error.what(), "Preparation exception lost the original failure reason");
			return true;
		}
		Require(prepared, "Preparation returned without a safe depth convention");
		return false;
	}

	void TestPreparationAdmission(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		for (bool active : { false, true }) {
			for (unsigned fault = 0; fault < 12; ++fault) {
				Renderer renderer;
				CreateSceneTargets(renderer, device);
				ReverseZ reverse;
				reverse.activeThisBoot = active;
				reverse.generation = active ? 7 : 0;
				globals::game::renderer = &renderer;
				globals::game::isVR = true;
				g_renderingRejected = false;
				switch (fault) {
				case 0:
					globals::game::renderer = nullptr;
					break;
				case 1:
					globals::d3d::device = nullptr;
					break;
				case 2:
					globals::d3d::context = nullptr;
					break;
				case 3:
					globals::state = nullptr;
					break;
				case 4:
					globals::shaderCache = nullptr;
					break;
				case 5:
					globals::cache.enabled = false;
					break;
				case 6:
					globals::stateStorage.enableVShaders = false;
					break;
				case 7:
					globals::stateStorage.enablePShaders = false;
					break;
				case 8:
					globals::uploadHookReady = false;
					break;
				case 9:
					reverse.cameraHooksInstalled = false;
					break;
				case 10:
					reverse.nativeShadersReady = false;
					break;
				case 11:
					reverse.runtimeHooksInstalled = false;
					break;
				}
				const auto errors = logger::errors;
				const bool threw = PreparationThrows(reverse);
				Require(threw == active && reverse.generation == (active ? 7u : 0u), "Preparation failure changed generation or exception policy");
				CheckFailure(reverse, active, active ? "target_recreation" : "activation", active ? 8 : 1);
				Require(reverse.nativePreparations == (fault == 10 ? 1u : 0u), "Admission failure performed native shader work");
				const auto first = reverse.GetDiagnostics();
				const auto preparations = reverse.nativePreparations;
				globals::game::renderer = &renderer;
				globals::d3d::device = device;
				globals::d3d::context = context;
				globals::state = &globals::stateStorage;
				globals::shaderCache = &globals::cache;
				globals::cache.enabled = globals::stateStorage.enableVShaders = globals::stateStorage.enablePShaders = true;
				globals::uploadHookReady = true;
				reverse.cameraHooksInstalled = reverse.runtimeHooksInstalled = reverse.nativeShadersReady = true;
				Require(PreparationThrows(reverse) == active, "Repaired prerequisites bypassed the boot failure latch");
				Require(logger::errors == errors + 1 && reverse.GetDiagnostics() == first &&
							reverse.nativePreparations == preparations,
					"Retry overwrote the first failure or repeated preparation work");
			}
		}
		g_renderingRejected = false;
	}

	void TestPreparationTransaction(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			for (bool active : { false, true }) {
				ReverseZ reverse;
				Renderer preceding, recreated, replacement;
				CreateSceneTargets(preceding, device);
				globals::game::renderer = &preceding;
				g_renderingRejected = false;
				g_viewIsReverse.clear();
				if (active) {
					context->OMSetRenderTargets(0, nullptr, preceding.data.depthStencils[0].views[0]);
					Require(reverse.PrepareDepthTargets() && reverse.IsActive() && reverse.generation == 1, "Cold preparation did not activate");
					winrt::com_ptr<ID3D11DepthStencilView> bound;
					context->OMGetRenderTargets(0, nullptr, bound.put());
					Require(bound.get() == preceding.data.depthStencils[0].views[0], "Successful preparation retained the old depth binding");
					Require(!reverse.GetDiagnostics().at("restartRequired").get<bool>(), "Healthy activation requested a restart");
					const auto clears = globals::cache.clears;
					CreateSceneTargets(recreated, device);
					globals::game::renderer = &recreated;
					context->OMSetRenderTargets(0, nullptr, recreated.data.depthStencils[0].views[0]);
					g_preparedOcclusionDepthRange = { .2f, .7f };
					Require(reverse.PrepareDepthTargets() && reverse.generation == 2 && globals::cache.clears == clears,
						"Warm recreation changed shader ownership or failed to advance the target generation");
					Require(g_preparedOcclusionDepthRange.minDepth == 1 && g_preparedOcclusionDepthRange.maxDepth == 1,
						"Target recreation retained a stale occlusion producer range");
					bound = nullptr;
					context->OMGetRenderTargets(0, nullptr, bound.put());
					Require(bound.get() == recreated.data.depthStencils[0].views[0] &&
								!g_viewIsReverse.contains(preceding.data.depthStencils[0].views[0]),
						"Warm recreation retained the previous generation's binding or registration");
				}
				context->OMSetRenderTargets(0, nullptr, nullptr);
				CreateSceneTargets(replacement, device);
				globals::game::renderer = &replacement;
				// A valid DSV from an incompatible format must fail after the main target has staged.
				auto& broken = replacement.data.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
				D3D11_TEXTURE2D_DESC desc{};
				broken.texture->GetDesc(&desc);
				desc.Format = DXGI_FORMAT_D32_FLOAT;
				desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
				winrt::com_ptr<ID3D11Texture2D> incompatible;
				Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, incompatible.put())), "Incompatible fixture allocation failed");
				Util::SetResourceName(incompatible.get(), "ReverseZStateTest::IncompatibleDepth");
				broken.views[0]->Release();
				broken.views[0] = nullptr;
				Require(SUCCEEDED(device->CreateDepthStencilView(incompatible.get(), nullptr, &broken.views[0])), "Incompatible fixture DSV failed");
				Util::SetResourceName(broken.views[0], "ReverseZStateTest::IncompatibleDepth DSV");
				const auto* original = replacement.data.depthStencils[0].texture;
				const auto mask = reverse.convertedTargetMask;
				const auto viewCount = g_viewIsReverse.size();
				const auto clears = globals::cache.clears;
				const bool threw = PreparationThrows(reverse);
				Require(threw == active && replacement.data.depthStencils[0].texture == original &&
							reverse.convertedTargetMask == mask && g_viewIsReverse.size() == viewCount && globals::cache.clears == clears,
					"Failed staging published partial resources or invalidated shader ownership");
				CheckFailure(reverse, active, active ? "target_recreation" : "activation", active ? 3 : 1);
				Require(reverse.failure == "Scene depth view preparation failed", "Failure injection missed the D3D view path");
			}
		}
		g_renderingRejected = false;
		g_viewIsReverse.clear();
		globals::game::renderer = nullptr;
	}

	void CheckBoundStates(ID3D11DeviceContext* context, ID3D11DepthStencilState* depth, ID3D11RasterizerState* raster, UINT expectedRef,
		std::source_location caller = std::source_location::current())
	{
		winrt::com_ptr<ID3D11DepthStencilState> actualDepth;
		winrt::com_ptr<ID3D11RasterizerState> actualRaster;
		UINT actualRef = 0;
		context->OMGetDepthStencilState(actualDepth.put(), &actualRef);
		context->RSGetState(actualRaster.put());
		if (actualDepth.get() != depth || actualRaster.get() != raster || actualRef != expectedRef)
			std::cerr << "State mismatch at line " << caller.line() << '\n';
		Require(actualDepth.get() == depth && actualRaster.get() == raster && actualRef == expectedRef,
			"Cached mapping changed the physical state or stencil reference");
	}

	void TestRequestedStateMappingCache(ID3D11Device* device, ID3D11DeviceContext* context,
		ID3D11DepthStencilState* depth, ID3D11RasterizerState* raster)
	{
		auto& reverse = globals::features::reverseZ;
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 21);
		ID3D11DeviceContext_RSSetState::thunk(context, raster);
		const auto mappedDepth = g_cachedReversedState;
		const auto mappedRaster = g_cachedReversedRasterState;
		Require(mappedDepth && mappedRaster, "Reversed mappings were not retained");
		const auto depthMappings = reverse.depthMappings, rasterMappings = reverse.rasterMappings;
		for (UINT ref = 1; ref <= 64; ++ref) {
			ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, ref);
			ID3D11DeviceContext_RSSetState::thunk(context, raster);
			CheckBoundStates(context, mappedDepth, mappedRaster, ref);
			winrt::com_ptr<ID3D11DepthStencilState> logicalDepth;
			winrt::com_ptr<ID3D11RasterizerState> logicalRaster;
			UINT logicalRef = 0;
			ID3D11DeviceContext_OMGetDepthStencilState::thunk(context, logicalDepth.put(), &logicalRef);
			ID3D11DeviceContext_RSGetState::thunk(context, logicalRaster.put());
			Require(logicalDepth.get() == depth && logicalRaster.get() == raster && logicalRef == ref,
				"Cached bind changed logical state ownership");
		}
		struct Domain
		{
			bool target, projection, nearMask, reversed;
		};
		for (const auto domain : { Domain{ true, false, false, false }, Domain{ false, true, false, false },
				 Domain{ true, false, true, true }, Domain{ false, false, true, false }, Domain{ true, true, false, true } }) {
			g_reverseTargetBound = domain.target;
			g_projectionReversed = domain.projection;
			g_nearMaskBound = domain.nearMask;
			ReapplyMappedStates(context);
			CheckBoundStates(context, domain.reversed ? mappedDepth : depth, domain.reversed ? mappedRaster : raster, 64);
		}
		Require(reverse.depthMappings == depthMappings && reverse.rasterMappings == rasterMappings,
			"Repeated binds or convention transitions repeated the mapping lookup");
		const auto repeatedForwards = stateForwards;
		ReapplyMappedStates(context);
		Require(stateForwards == repeatedForwards, "Unchanged mapped states were rebound");
		const D3D11_VIEWPORT fullDepthViewport{ 0, 0, 4, 4, 0, 1 };
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &fullDepthViewport);
		const auto viewportForwards = stateForwards[2];
		g_reverseTargetBound = false;
		ReapplyMappedStates(context);
		CheckBoundStates(context, depth, raster, 64);
		g_reverseTargetBound = true;
		ReapplyMappedStates(context);
		CheckBoundStates(context, mappedDepth, mappedRaster, 64);
		Require(stateForwards[2] == viewportForwards, "A full-depth viewport was rebound across conventions");
		const D3D11_VIEWPORT partialDepthViewport{ 0, 0, 4, 4, .2f, .7f };
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &partialDepthViewport);
		const auto partialForwards = stateForwards[2];
		g_reverseTargetBound = false;
		ReapplyMappedStates(context);
		UINT viewportCount = 1;
		D3D11_VIEWPORT physicalViewport{};
		context->RSGetViewports(&viewportCount, &physicalViewport);
		Require(stateForwards[2] == partialForwards + 1 && std::memcmp(&physicalViewport, &partialDepthViewport, sizeof(physicalViewport)) == 0,
			"Changed depth convention did not restore a partial-depth viewport");
		g_reverseTargetBound = true;
		ReapplyMappedStates(context);
		Require(stateForwards[2] == partialForwards + 2, "Partial-depth viewport reversal was not rebound");
		const auto stencilForwards = stateForwards[0];
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 129);
		Require(stateForwards[0] == stencilForwards + 1, "An explicit stencil-only change was skipped");
		ReapplyMappedStates(context);
		CheckBoundStates(context, mappedDepth, mappedRaster, 129);
		Require(stateForwards[0] == stencilForwards + 1, "An unchanged stencil-only state was rebound");
		const std::array viewports{ fullDepthViewport, partialDepthViewport };
		ID3D11DeviceContext_RSSetViewports::thunk(context, UINT(viewports.size()), viewports.data());
		g_reverseTargetBound = false;
		ReapplyMappedStates(context);
		std::array<D3D11_VIEWPORT, 2> actualViewports{};
		viewportCount = UINT(actualViewports.size());
		context->RSGetViewports(&viewportCount, actualViewports.data());
		Require(viewportCount == viewports.size() && std::memcmp(actualViewports.data(), viewports.data(), sizeof(viewports)) == 0,
			"A changed secondary viewport was skipped when the first viewport stayed unchanged");
		const auto arrayForwards = stateForwards;
		CSX::Diagnostics::PerformanceCosts::referenceBindings = true;
		ReapplyMappedStates(context);
		CSX::Diagnostics::PerformanceCosts::referenceBindings = false;
		Require(stateForwards[0] == arrayForwards[0] + 1 && stateForwards[1] == arrayForwards[1] + 1 &&
					stateForwards[2] == arrayForwards[2] + 1,
			"Reference reapply skipped an unchanged synthetic binding");
		ID3D11DeviceContext_RSSetViewports::thunk(context, 0, nullptr);
		const auto emptyForwards = stateForwards[2];
		g_reverseTargetBound = true;
		ReapplyMappedStates(context);
		viewportCount = 0;
		context->RSGetViewports(&viewportCount, nullptr);
		Require(viewportCount == 0 && stateForwards[2] == emptyForwards, "Synthetic reapply resurrected empty viewports");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 64);
		const auto beforeReference = stateForwards;
		CSX::Diagnostics::PerformanceCosts::referenceBindings = true;
		ReapplyMappedStates(context);
		CSX::Diagnostics::PerformanceCosts::referenceBindings = false;
		CheckBoundStates(context, mappedDepth, mappedRaster, 64);
		Require(stateForwards[0] == beforeReference[0] + 1 && stateForwards[1] == beforeReference[1] + 1 &&
					stateForwards[2] == beforeReference[2],
			"Reference reapply failed to bind depth/raster states or resurrected empty viewports");

		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, nullptr, 9);
		ID3D11DeviceContext_RSSetState::thunk(context, nullptr);
		const auto defaultDepth = g_cachedReversedState;
		Require(defaultDepth && !g_cachedReversedRasterState, "Default state reused a preceding mapping");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, nullptr, 10);
		ID3D11DeviceContext_RSSetState::thunk(context, nullptr);
		CheckBoundStates(context, defaultDepth, nullptr, 10);
		Require(reverse.depthMappings == depthMappings + 1 && reverse.rasterMappings == rasterMappings,
			"Default state lookup was repeated or inherited the preceding cache entry");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 21);
		ID3D11DeviceContext_RSSetState::thunk(context, raster);
		Require(reverse.depthMappings == depthMappings + 2 && reverse.rasterMappings == rasterMappings + 1,
			"Changing the requested state did not invalidate its mapping");
		CheckBoundStates(context, mappedDepth, mappedRaster, 21);

		D3D11_RASTERIZER_DESC desc{};
		raster->GetDesc(&desc);
		desc.DepthBias = 0;
		desc.SlopeScaledDepthBias = desc.DepthBiasClamp = 0;
		winrt::com_ptr<ID3D11RasterizerState> unbiased;
		Require(SUCCEEDED(device->CreateRasterizerState(&desc, unbiased.put())), "Unbiased rasterizer creation failed");
		Util::SetResourceName(unbiased.get(), "ReverseZStateTest::UnbiasedRasterizer");
		ID3D11DeviceContext_RSSetState::thunk(context, unbiased.get());
		const auto unbiasedMappings = reverse.rasterMappings;
		ID3D11DeviceContext_RSSetState::thunk(context, unbiased.get());
		CheckBoundStates(context, mappedDepth, unbiased.get(), 21);
		Require(reverse.rasterMappings == unbiasedMappings, "Unchanged rasterizer mapping was not cached");
		const auto unbiasedForwards = stateForwards[1];
		g_reverseTargetBound = false;
		ReapplyMappedStates(context);
		g_reverseTargetBound = true;
		ReapplyMappedStates(context);
		Require(stateForwards[1] == unbiasedForwards, "An unbiased rasterizer was rebound across conventions");
		ID3D11DeviceContext_RSSetState::thunk(context, raster);

		winrt::com_ptr<ID3D11DeviceContext> other;
		Require(SUCCEEDED(device->CreateDeferredContext(0, other.put())), "Other context creation failed");
		Util::SetResourceName(other.get(), "ReverseZStateTest::OtherContext");
		const auto beforeClearDepth = reverse.depthMappings, beforeClearRaster = reverse.rasterMappings;
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(other.get(), nullptr, 3);
		ID3D11DeviceContext_RSSetState::thunk(other.get(), nullptr);
		ID3D11DeviceContext_ClearState::thunk(other.get());
		ReapplyMappedStates(context);
		CheckBoundStates(context, mappedDepth, mappedRaster, 21);
		Require(reverse.depthMappings == beforeClearDepth && reverse.rasterMappings == beforeClearRaster,
			"Other context changed the immediate-context mapping cache");
		ID3D11DeviceContext_ClearState::thunk(context);
		Require(!g_cachedReversedState && !g_cachedReversedRasterState && !g_requestedState && !g_requestedRasterState,
			"ClearState retained requested mappings");
		g_reverseTargetBound = g_projectionReversed = true;
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 21);
		ID3D11DeviceContext_RSSetState::thunk(context, raster);
		Require(reverse.depthMappings == beforeClearDepth + 1 && reverse.rasterMappings == beforeClearRaster + 1,
			"ClearState did not invalidate both mapping entries");
		CheckBoundStates(context, mappedDepth, mappedRaster, 21);

		g_renderingRejected = true;
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 7);
		ID3D11DeviceContext_RSSetState::thunk(context, raster);
		CheckBoundStates(context, g_rejectedDepthState.get(), nullptr, 7);
		CSX::Diagnostics::PerformanceCosts::referenceBindings = true;
		ReapplyMappedStates(context);
		CSX::Diagnostics::PerformanceCosts::referenceBindings = false;
		CheckBoundStates(context, g_rejectedDepthState.get(), nullptr, 7);
		g_reverseTargetBound = false;
		ReapplyMappedStates(context);
		CheckBoundStates(context, depth, raster, 7);
		Require(reverse.depthMappings == beforeClearDepth + 1 && reverse.rasterMappings == beforeClearRaster + 1,
			"Failure handling reused or refreshed a normal mapping");
		g_renderingRejected = false;
		g_reverseTargetBound = true;
		ReapplyMappedStates(context);

		const auto beforeStandardDepth = reverse.depthMappings, beforeStandardRaster = reverse.rasterMappings;
		g_reverseTargetBound = false;
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, nullptr, 13);
		ID3D11DeviceContext_RSSetState::thunk(context, unbiased.get());
		Require(!g_cachedReversedState && !g_cachedReversedRasterState,
			"A requested-state change in the standard domain retained a preceding mapping");
		CheckBoundStates(context, nullptr, unbiased.get(), 13);
		g_reverseTargetBound = true;
		ReapplyMappedStates(context);
		CheckBoundStates(context, defaultDepth, unbiased.get(), 13);
		Require(reverse.depthMappings == beforeStandardDepth + 1 && reverse.rasterMappings == beforeStandardRaster + 1,
			"Entering the reversed domain did not resolve requests changed in the standard domain");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 7);
		ID3D11DeviceContext_RSSetState::thunk(context, raster);
	}

	void TestNativeViewportBiasMapping(ID3D11DeviceContext* context)
	{
		ShadowState shadow;
		const D3D11_VIEWPORT scene{ 0, 0, 8, 8, 0, .999998f };
		shadow.data.viewPort = scene;
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { scene.MinDepth, scene.MaxDepth };
		globals::game::shadowState = &shadow;
		const bool previousVR = globals::game::isVR;
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			for (bool target : { false, true }) {
				g_reverseTargetBound = target;
				for (bool projection : { false, true }) {
					g_projectionReversed = projection;
					for (bool nearMask : { false, true }) {
						g_nearMaskBound = nearMask;
						for (uint32_t mode = 0; mode < nativeViewportBiases.size(); ++mode) {
							shadow.data.rasterStateDepthBiasMode = mode;
							auto requested = scene;
							requested.MaxDepth -= nativeViewportBiases[mode];
							shadow.data.viewPort = requested;
							ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
							ReapplyMappedStates(context);
							const RejectHeapAllocations rejectAllocations;
							ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
							const auto expected = ShouldFlip() ? MirrorViewportDepth(vr && projection && !nearMask ? scene : requested) : requested;
							auto expectedLayered = expected;
							if (vr && target && projection && !nearMask)
								expectedLayered.MaxDepth -= .000030f - nativeViewportBiases[mode];
							D3D11_VIEWPORT actual{};
							UINT count = 1;
							context->RSGetViewports(&count, &actual);
							Require(std::memcmp(&actual, &expectedLayered, sizeof(actual)) == 0, "Native bias correction escaped its depth domain or missed a mode");
							if (g_requestedRasterState) {
								D3D11_RASTERIZER_DESC source{}, mapped{};
								g_requestedRasterState->GetDesc(&source);
								g_boundRasterState->GetDesc(&mapped);
								Require(mapped.DepthBias == (ShouldFlip() ? -source.DepthBias : source.DepthBias),
									"Native viewport layers changed rasterizer bias");
							}
							ID3D11DeviceContext_RSGetViewports::thunk(context, &count, &actual);
							Require(std::memcmp(&actual, &requested, sizeof(actual)) == 0, "Native correction changed logical viewport readback");
							shadow.data.rasterStateDepthBiasMode = 0;
							const auto reads = std::pair{ nativeViewportBiasReads, publishedViewportReads };
							ReapplyMappedStates(context);
							Require(reads == std::pair{ nativeViewportBiasReads, publishedViewportReads }, "Reapplication reread native viewport metadata");
							context->RSGetViewports(&count, &actual);
							Require(std::memcmp(&actual, &expectedLayered, sizeof(actual)) == 0, "Reapplication read stale native bias state");
						}
					}
				}
			}
		}
		globals::game::isVR = g_reverseTargetBound = g_projectionReversed = true;
		g_nearMaskBound = false;
		shadow.data.rasterStateDepthBiasMode = 6;
		auto requested = scene;
		requested.MaxDepth -= nativeViewportBiases[6];
		shadow.data.viewPort = requested;
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
		CaptureOcclusionDepthRange();
		Require(g_preparedOcclusionDepthRange.minDepth == 1.0f - scene.MaxDepth,
			"Occlusion producer capture ignored the corrected scene viewport");
		const std::array multiple{ requested, requested };
		ID3D11DeviceContext_RSSetViewports::thunk(context, 2, multiple.data());
		Require(!g_nativeViewportCorrection, "Multiple viewports inherited a single native correction");
		std::array<D3D11_VIEWPORT, 2> actualMultiple;
		UINT multipleCount = 2;
		context->RSGetViewports(&multipleCount, actualMultiple.data());
		for (const auto& actual : actualMultiple) {
			const auto expected = MirrorViewportDepth(requested);
			Require(std::memcmp(&actual, &expected, sizeof(actual)) == 0, "Native correction changed a custom viewport array");
		}
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
		ID3D11DeviceContext_RSSetViewports::thunk(context, 0, nullptr);
		Require(!g_nativeViewportCorrection && g_requestedViewportCount == 0, "Unbinding viewports retained a native correction");
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
		ID3D11DeviceContext_ClearState::thunk(context);
		Require(!g_nativeViewportCorrection, "ClearState retained a native viewport correction");
		for (unsigned component = 0; component < 6; ++component) {
			auto custom = requested;
			switch (component) {
			case 0:
				custom.TopLeftX = 1;
				break;
			case 1:
				custom.TopLeftY = 1;
				break;
			case 2:
				custom.Width = 4;
				break;
			case 3:
				custom.Height = 4;
				break;
			case 4:
				custom.MinDepth = .1f;
				break;
			case 5:
				custom.MaxDepth = .8f;
				break;
			}
			nativeViewportBiasReads = publishedViewportReads = 0;
			Require(!ResolveNativeViewportCorrection(custom), "Custom viewport matched the native bias contract");
			Require(nativeViewportBiasReads == 0 && publishedViewportReads == 0, "Custom viewport queried native bias or camera data");
		}
		for (uint32_t mode : { uint32_t(nativeViewportBiases.size()), UINT32_MAX }) {
			shadow.data.rasterStateDepthBiasMode = mode;
			Require(!ResolveNativeViewportCorrection(requested), "Unknown native mode accessed its bias table");
		}
		shadow.data.rasterStateDepthBiasMode = 6;
		const float savedBias = nativeViewportBiases[6];
		for (float bias : { -1.0f, 0.0f, 1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			nativeViewportBiases[6] = bias;
			Require(!ResolveNativeViewportCorrection(requested), "Invalid native bias was accepted");
		}
		nativeViewportBiases[6] = savedBias;
		const float otherBias = nativeViewportBiases[10];
		for (float bias : { -.01f, 1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			nativeViewportBiases[10] = bias;
			const RejectHeapAllocations rejectAllocations;
			Require(!ResolveNativeViewportCorrection(requested), "Invalid sibling layer exhausted the shared viewport headroom");
		}
		nativeViewportBiases[10] = otherBias;
		shadow.eyes[1].viewDepthRange.y = .8f;
		Require(!ResolveNativeViewportCorrection(requested), "Mismatched stereo depth ranges were normalized");
		for (const auto range : { RE::BSGraphics::ViewData::Range{ -1, 1 }, { 0, 2 }, { .8f, .2f }, { 0, savedBias },
				 { 0, std::numeric_limits<float>::quiet_NaN() }, { 0, std::numeric_limits<float>::infinity() } }) {
			for (auto& eye : shadow.eyes)
				eye.viewDepthRange = range;
			shadow.data.viewPort.minDepth = range.x;
			shadow.data.viewPort.maxDepth = range.y - savedBias;
			Require(!ResolveNativeViewportCorrection(shadow.data.viewPort), "Invalid or exhausted camera range was normalized");
		}
		globals::game::shadowState = nullptr;
		Require(!ResolveNativeViewportCorrection(requested), "Missing native state was dereferenced");
		globals::game::isVR = previousVR;
		g_reverseTargetBound = g_projectionReversed = true;
	}

	void TestStandardViewportMapping(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		const bool previousActive = globals::features::reverseZ.IsActive();
		const bool previousVR = globals::game::isVR;
		globals::features::reverseZ.activeThisBoot = false;
		g_reverseTargetBound = g_projectionReversed = g_nearMaskBound = false;
		Renderer renderer;
		CreateSceneTargets(renderer, device);
		auto* previousRenderer = std::exchange(globals::game::renderer, &renderer);
		auto* view = renderer.data.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
		context->OMSetRenderTargets(0, nullptr, view);
		ShadowState shadow;
		globals::game::shadowState = &shadow;
		const D3D11_VIEWPORT scene{ 0, 0, 8, 8, 0, .999998f };
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { scene.MinDepth, scene.MaxDepth };
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			UpdateBoundDepthTarget(context, view);
			for (uint32_t target = 0; target <= RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL; ++target) {
				publishedDepthStencilTarget = target;
				for (uint32_t mode = 0; mode < nativeViewportBiases.size(); ++mode) {
					auto requested = scene;
					requested.MaxDepth -= nativeViewportBiases[mode];
					shadow.data.viewPort = requested;
					shadow.data.rasterStateDepthBiasMode = mode;
					const RejectHeapAllocations rejectAllocations;
					ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
					auto expected = requested;
					if (vr && target < RE::RENDER_TARGETS_DEPTHSTENCIL::kSHADOWMAPS) {
						expected.MaxDepth = scene.MaxDepth;
						expected.MinDepth += .000030f - nativeViewportBiases[mode];
					}
					D3D11_VIEWPORT actual{};
					UINT count = 1;
					context->RSGetViewports(&count, &actual);
					Require(std::memcmp(&actual, &expected, sizeof(actual)) == 0, "Standard VR layer correction escaped its scene domain or missed a mode");
					ID3D11DeviceContext_RSGetViewports::thunk(context, &count, &actual);
					Require(std::memcmp(&actual, &requested, sizeof(actual)) == 0, "Standard correction changed logical viewport readback");
					if (target >= RE::RENDER_TARGETS_DEPTHSTENCIL::kSHADOWMAPS) {
						nativeViewportBiasReads = publishedViewportReads = 0;
						ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
						Require(nativeViewportBiasReads == 0 && publishedViewportReads == 0, "Non-scene viewport scanned native bias metadata");
					}
				}
			}
		}
		publishedDepthStencilTarget = RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN;
		shadow.data.viewPort = scene;
		shadow.data.rasterStateDepthBiasMode = 0;
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
		{
			Util::DeviceContextStateSnapshot snapshot;
			CaptureReverseContextState(context, snapshot);
			ID3D11DeviceContext_ClearState::thunk(context);
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
			Require(!g_nativeViewportCorrection, "Isolated pass inherited standard scene layering after ClearState");
		}
		Require(!g_isolatedContext && g_nativeViewportCorrection && g_nativeViewportCorrection->standardScene,
			"Isolated pass did not restore standard scene metadata");
		auto custom = scene;
		custom.Width = 4;
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &custom);
		Require(!g_nativeViewportCorrection, "Custom pass inherited standard scene layering");
		globals::game::shadowState = nullptr;
		globals::game::isVR = previousVR;
		globals::features::reverseZ.activeThisBoot = previousActive;
		globals::game::renderer = previousRenderer;
		publishedDepthStencilTarget = static_cast<uint32_t>(RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE);
		ID3D11DeviceContext_ClearState::thunk(context);
		g_reverseTargetBound = g_projectionReversed = true;
	}

	void TestFurDepthDomain(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		auto& reverse = globals::features::reverseZ;
		const bool previousActive = reverse.IsActive();
		const bool previousVR = std::exchange(globals::game::isVR, true);
		Renderer renderer;
		CreateSceneTargets(renderer, device);
		auto* previousRenderer = std::exchange(globals::game::renderer, &renderer);
		ShadowState shadow;
		auto* previousShadow = std::exchange(globals::game::shadowState, &shadow);
		const D3D11_VIEWPORT scene{ 0, 0, 4, 4, 0, .999998f };
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { scene.MinDepth, scene.MaxDepth };
		shadow.data.rasterStateDepthBiasMode = 6;
		auto requested = scene;
		requested.MaxDepth -= nativeViewportBiases[6];
		shadow.data.viewPort = requested;
		publishedDepthStencilTarget = RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN;
		auto& target = renderer.data.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		D3D11_DEPTH_STENCIL_DESC state{};
		state.DepthEnable = TRUE;
		state.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		state.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		winrt::com_ptr<ID3D11DepthStencilState> prepass;
		Require(SUCCEEDED(device->CreateDepthStencilState(&state, prepass.put())), "Fur prepass state creation failed");
		Util::SetResourceName(prepass.get(), "ReverseZStateTest::FurPrepass");
		for (bool runtimeVR : { false, true })
			for (bool reversed : { false, true }) {
				globals::game::isVR = runtimeVR;
				ID3D11DeviceContext_ClearState::thunk(context);
				reverse.activeThisBoot = reversed;
				g_projectionReversed = reversed;
				if (reversed) {
					reverse.RegisterSceneDepthView(target.views[0]);
					reverse.RegisterSceneDepthView(target.readOnlyViews[0]);
				}
				context->OMSetRenderTargets(0, nullptr, target.readOnlyViews[0]);
				RefreshBoundDepthTarget();
				ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
				D3D11_VIEWPORT before{};
				UINT count = 1;
				context->RSGetViewports(&count, &before);
				FurRendering::DepthViewCache cache;
				{
					FurRendering::ScopedDepthWrite fur(context, cache, [&](auto* source) {
						return FurRendering::FindWritableDepthView(source,
							std::span{ &renderer.data.depthStencils[0], std::size(renderer.data.depthStencils) }, [](auto* view) { return view; });
					});
					Require(SUCCEEDED(fur.Status()), "Fur native depth lookup failed");
					RefreshBoundDepthTarget();
					Require(g_reverseTargetBound == reversed && g_standardSceneDepthBound == (runtimeVR && !reversed), "Fur view substitution changed the depth domain");
					ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, prepass.get(), 0);
					winrt::com_ptr<ID3D11DepthStencilState> actual;
					context->OMGetDepthStencilState(actual.put(), nullptr);
					D3D11_DEPTH_STENCIL_DESC desc{};
					actual->GetDesc(&desc);
					Require(desc.DepthFunc == (reversed ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL), "Fur prepass lost its mapped comparison");
					D3D11_VIEWPORT after{};
					count = 1;
					context->RSGetViewports(&count, &after);
					Require(std::memcmp(&before, &after, sizeof(before)) == 0, "Fur prepass changed the VR scene depth interval");
				}
				RefreshBoundDepthTarget();
				Require(g_reverseTargetBound == reversed && g_standardSceneDepthBound == (runtimeVR && !reversed), "Fur restoration changed the depth domain");
				if (reversed) {
					reverse.UnregisterSceneDepthView(target.views[0]);
					reverse.UnregisterSceneDepthView(target.readOnlyViews[0]);
				}
			}
		ID3D11DeviceContext_ClearState::thunk(context);
		globals::game::renderer = previousRenderer;
		globals::game::shadowState = previousShadow;
		globals::game::isVR = previousVR;
		reverse.activeThisBoot = previousActive;
		publishedDepthStencilTarget = static_cast<uint32_t>(RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE);
		g_reverseTargetBound = g_projectionReversed = true;
	}

	void TestStandardViewportTargets(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		const bool previousActive = globals::features::reverseZ.IsActive();
		const bool previousVR = globals::game::isVR;
		globals::features::reverseZ.activeThisBoot = false;
		globals::game::isVR = true;
		ID3D11DeviceContext_ClearState::thunk(context);
		g_projectionReversed = false;
		Renderer renderer, other;
		CreateSceneTargets(renderer, device);
		CreateSceneTargets(other, device);
		auto* previousRenderer = std::exchange(globals::game::renderer, &renderer);
		auto& target = renderer.data.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto* customView = other.data.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
		std::swap(target.readOnlyViews[0], target.readOnlyViews[7]);
		ShadowState shadow;
		const D3D11_VIEWPORT scene{ 0, 0, 4, 4, 0, .999998f };
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { scene.MinDepth, scene.MaxDepth };
		shadow.data.rasterStateDepthBiasMode = 6;
		auto requested = scene;
		requested.MaxDepth -= nativeViewportBiases[6];
		shadow.data.viewPort = requested;
		globals::game::shadowState = &shadow;
		publishedDepthStencilTarget = RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN;
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
		auto verify = [&](bool corrected) {
			D3D11_VIEWPORT actual{}, expected = requested;
			UINT count = 1;
			if (corrected) {
				expected.MaxDepth = scene.MaxDepth;
				expected.MinDepth = .000030f - nativeViewportBiases[6];
			}
			context->RSGetViewports(&count, &actual);
			Require(count == 1 && std::memcmp(&actual, &expected, sizeof(actual)) == 0, "Bound depth target lost its viewport domain");
			ID3D11DeviceContext_RSGetViewports::thunk(context, &count, &actual);
			Require(std::memcmp(&actual, &requested, sizeof(actual)) == 0, "Target transition changed the logical viewport");
		};
		auto bind = [&](ID3D11DepthStencilView* view) {
			context->OMSetRenderTargets(0, nullptr, view);
			UpdateBoundDepthTarget(context, view);
		};
		verify(false);
		bind(target.views[0]);
		verify(true);
		const auto forwards = stateForwards;
		const auto reads = std::pair{ nativeViewportBiasReads, publishedViewportReads };
		{
			const RejectHeapAllocations rejectAllocations;
			for (unsigned repeat = 0; repeat < 100; ++repeat) {
				bind(target.views[0]);
				bind(target.readOnlyViews[7]);
			}
		}
		Require(stateForwards == forwards && reads == std::pair{ nativeViewportBiasReads, publishedViewportReads },
			"Repeated scene bindings repeated viewport state or metadata work");
		verify(true);
		bind(customView);
		verify(false);
		bind(nullptr);
		verify(false);
		bind(target.views[0]);
		verify(true);
		for (bool manual : { false, true }) {
			Util::UseManualDeviceContextState(manual);
			winrt::com_ptr<ID3D11DeviceContext1> context1;
			std::unique_ptr<Util::IsolatedDeviceContextState> isolated;
			Require(Util::CreateIsolatedDeviceContextState(device, context, context1, isolated, "ReverseZStateTest::StandardIsolated"), "Standard isolation creation failed");
			Util::captureDeviceContextState = CaptureReverseContextState;
			{
				const RejectHeapAllocations rejectAllocations;
				Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
				Require(static_cast<bool>(guard), "Standard isolation was unavailable");
				ID3D11DeviceContext_ClearState::thunk(context);
				bind(target.views[0]);
				ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
				verify(false);
			}
			Require(g_standardSceneDepthBound && !g_isolatedContext, "Standard isolation lost the scene domain");
			verify(true);
		}
		Util::captureDeviceContextState = nullptr;
		Util::UseManualDeviceContextState(false);
		globals::game::renderer = &other;
		bind(target.views[0]);
		verify(false);
		bind(customView);
		verify(true);
		ID3D11DeviceContext_ClearState::thunk(context);
		Require(!g_standardSceneDepthBound && !g_nativeViewportCorrection, "ClearState retained standard scene ownership");
		globals::game::shadowState = nullptr;
		globals::game::renderer = previousRenderer;
		globals::game::isVR = previousVR;
		globals::features::reverseZ.activeThisBoot = previousActive;
		publishedDepthStencilTarget = static_cast<uint32_t>(RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE);
		g_reverseTargetBound = g_projectionReversed = true;
	}

	void TestNativeViewportDomainTransitions(ID3D11DeviceContext* context)
	{
		ShadowState shadow;
		const D3D11_VIEWPORT scene{ 0, 0, 8, 8, 0, .999998f };
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { scene.MinDepth, scene.MaxDepth };
		auto requested = scene;
		requested.MaxDepth -= nativeViewportBiases[6];
		shadow.data.viewPort = requested;
		shadow.data.rasterStateDepthBiasMode = 6;
		const bool previousVR = globals::game::isVR;
		globals::game::isVR = true;
		globals::game::shadowState = &shadow;
		g_reverseTargetBound = g_projectionReversed = true;
		g_nearMaskBound = false;
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
		g_reverseTargetBound = g_projectionReversed = g_nearMaskBound = false;
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
		// A cached viewport can enter several depth domains before the engine publishes another one.
		for (const auto domain : { std::array{ true, false, false }, { true, true, false }, { true, true, true },
				 std::array{ false, true, false }, { true, false, true }, { true, true, false } }) {
			g_reverseTargetBound = domain[0];
			g_projectionReversed = domain[1];
			g_nearMaskBound = domain[2];
			const RejectHeapAllocations rejectAllocations;
			ReapplyMappedStates(context);
			D3D11_VIEWPORT actual{};
			UINT count = 1;
			context->RSGetViewports(&count, &actual);
			const auto expected = ShouldFlip() ? MirrorViewportDepth(domain[1] && !domain[2] ? scene : requested) : requested;
			auto expectedLayered = expected;
			if (domain[0] && domain[1] && !domain[2])
				expectedLayered.MaxDepth -= .000030f - nativeViewportBiases[6];
			Require(std::memcmp(&actual, &expectedLayered, sizeof(actual)) == 0, "Native viewport correction did not follow a depth-domain transition");
			CaptureOcclusionDepthRange();
			Require(g_preparedOcclusionDepthRange.minDepth == (ShouldFlip() ? expected.MinDepth : 1.0f) &&
						g_preparedOcclusionDepthRange.maxDepth == (ShouldFlip() ? expected.MaxDepth : 1.0f),
				"Occlusion producer range diverged from the scene during a depth-domain transition");
			ID3D11DeviceContext_RSGetViewports::thunk(context, &count, &actual);
			Require(std::memcmp(&actual, &requested, sizeof(actual)) == 0, "Depth-domain transition changed logical viewport readback");
		}
		globals::game::shadowState = nullptr;
		globals::game::isVR = previousVR;
		ID3D11DeviceContext_ClearState::thunk(context);
		g_reverseTargetBound = g_projectionReversed = true;
	}

	template <class SetDepth, class VisibleSamples>
	void TestStandardViewportDepthOrdering(ID3D11Device* device, ID3D11DeviceContext* context,
		SetDepth setDepth, VisibleSamples visibleSamples)
	{
		const bool previousActive = globals::features::reverseZ.IsActive();
		const bool previousVR = globals::game::isVR;
		globals::features::reverseZ.activeThisBoot = false;
		globals::game::isVR = true;
		g_reverseTargetBound = g_projectionReversed = g_nearMaskBound = false;
		publishedDepthStencilTarget = RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN;
		Renderer renderer;
		CreateSceneTargets(renderer, device);
		auto* previousRenderer = std::exchange(globals::game::renderer, &renderer);
		auto* view = renderer.data.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
		context->OMSetRenderTargets(0, nullptr, view);
		UpdateBoundDepthTarget(context, view);
		D3D11_DEPTH_STENCIL_DESC desc{};
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		winrt::com_ptr<ID3D11DepthStencilState> write, read, equal;
		Require(SUCCEEDED(device->CreateDepthStencilState(&desc, write.put())), "Standard layer write state creation failed");
		Util::SetResourceName(write.get(), "ReverseZStateTest::StandardLayerWrite");
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Require(SUCCEEDED(device->CreateDepthStencilState(&desc, read.put())), "Standard layer read state creation failed");
		Util::SetResourceName(read.get(), "ReverseZStateTest::StandardLayerRead");
		desc.DepthFunc = D3D11_COMPARISON_EQUAL;
		Require(SUCCEEDED(device->CreateDepthStencilState(&desc, equal.put())), "Standard layer equality state creation failed");
		Util::SetResourceName(equal.get(), "ReverseZStateTest::StandardLayerEqual");
		D3D11_RASTERIZER_DESC rasterDesc = CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);
		rasterDesc.CullMode = D3D11_CULL_NONE;
		winrt::com_ptr<ID3D11RasterizerState> raster;
		Require(SUCCEEDED(device->CreateRasterizerState(&rasterDesc, raster.put())), "Standard layer rasterizer creation failed");
		Util::SetResourceName(raster.get(), "ReverseZStateTest::StandardLayerRasterizer");
		context->RSSetState(raster.get());
		const D3D11_VIEWPORT scene{ 0, 0, 2, 2, 0, .999998f };
		ShadowState shadow;
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { scene.MinDepth, scene.MaxDepth };
		auto setMode = [&](uint32_t mode) {
			auto requested = scene;
			requested.MaxDepth -= nativeViewportBiases[mode];
			shadow.data.viewPort = requested;
			shadow.data.rasterStateDepthBiasMode = mode;
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
		};
		// Projected depths from overlapping opaque and decal surfaces retain their native layer intervals.
		const std::array samples{
			std::array{ .9968489408493042f / scene.MaxDepth, .99684077501297f / (scene.MaxDepth - .000026f) },
			std::array{ .9964895844459534f / scene.MaxDepth, .9964855313301086f / (scene.MaxDepth - .000026f) }
		};
		for (const auto& sample : samples) {
			for (bool corrected : { false, true }) {
				globals::game::shadowState = corrected ? &shadow : nullptr;
				context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, 1, 0);
				setMode(0);
				setDepth(sample[0]);
				context->OMSetDepthStencilState(write.get(), 0);
				Require(visibleSamples() == 4, "Standard wall prepass failed");
				context->OMSetDepthStencilState(equal.get(), 0);
				Require(visibleSamples() == 4, "Standard correction broke equal-depth shading");
				context->OMSetDepthStencilState(read.get(), 0);
				setMode(6);
				setDepth(sample[1]);
				Require(visibleSamples() == (corrected ? 0 : 4), "Standard decal bleed-through control or correction failed");
				if (!corrected)
					continue;
				for (uint32_t mode = 0; mode < nativeViewportBiases.size(); ++mode) {
					setMode(mode);
					Require(visibleSamples() == 0, "Standard viewport layer admitted a hidden decal");
					setDepth(sample[0]);
					Require(visibleSamples() == 4, "Standard viewport correction lost a coplanar decal");
					setDepth(sample[1]);
				}
			}
		}
		globals::game::shadowState = nullptr;
		globals::game::isVR = previousVR;
		globals::features::reverseZ.activeThisBoot = previousActive;
		publishedDepthStencilTarget = static_cast<uint32_t>(RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE);
		globals::game::renderer = previousRenderer;
	}

	template <class SetDepth, class VisibleSamples>
	void TestNativeViewportDepthOrdering(ID3D11DeviceContext* context, ID3D11DepthStencilView* view,
		const D3D11_VIEWPORT& scene, SetDepth setDepth, VisibleSamples visibleSamples)
	{
		ShadowState shadow;
		shadow.data.viewPort = scene;
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { scene.MinDepth, scene.MaxDepth };
		const bool previousVR = globals::game::isVR;
		globals::game::isVR = true;
		const auto range = MirrorViewportDepth(scene);
		for (float depth : { .00003f, .0006f, .02f, .5f }) {
			const float gap = std::max(8.0f * (std::nextafter(depth, 1.0f) - depth), depth * depth * .1f);
			for (uint32_t mode = 0; mode < nativeViewportBiases.size(); ++mode) {
				context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, range.MinDepth + (range.MaxDepth - range.MinDepth) * depth, 0);
				shadow.data.rasterStateDepthBiasMode = mode;
				auto requested = scene;
				requested.MaxDepth -= nativeViewportBiases[mode];
				shadow.data.viewPort = requested;
				setDepth(depth - gap);
				if (depth < .001f && nativeViewportBiases[mode] > 0) {
					globals::game::shadowState = nullptr;
					ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
					Require(visibleSamples() == 4, "Control did not reproduce distant hidden-surface bleed-through");
				}
				globals::game::shadowState = &shadow;
				ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
				Require(visibleSamples() == 0, "Native viewport bias pulled a hidden surface through its occluder");
				D3D11_VIEWPORT layer{};
				UINT count = 1;
				context->RSGetViewports(&count, &layer);
				context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, layer.MinDepth + (layer.MaxDepth - layer.MinDepth) * depth, 0);
				setDepth(depth);
				Require(visibleSamples() == 4, "Viewport correction lost coplanar surface layering");
			}
		}
		globals::game::shadowState = nullptr;
		globals::game::isVR = previousVR;
	}

	template <class SetDepth, class VisibleSamples>
	void TestNativeViewportLayering(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11DepthStencilView* view,
		SetDepth setDepth, VisibleSamples visibleSamples)
	{
		const D3D11_VIEWPORT scene{ 0, 0, 2, 2, 0, 1 };
		ShadowState shadow;
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { 0, 1 };
		const bool previousVR = globals::game::isVR;
		globals::game::isVR = true;
		g_reverseTargetBound = g_projectionReversed = true;
		D3D11_RASTERIZER_DESC desc = CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);
		desc.CullMode = D3D11_CULL_NONE;
		desc.DepthBiasClamp = -100;
		winrt::com_ptr<ID3D11RasterizerState> unbiased;
		Require(SUCCEEDED(device->CreateRasterizerState(&desc, unbiased.put())), "Layer rasterizer creation failed");
		Util::SetResourceName(unbiased.get(), "ReverseZStateTest::NativeLayerRasterizer");
		ID3D11DeviceContext_RSSetState::thunk(context, unbiased.get());
		auto setMode = [&](uint32_t mode) {
			shadow.data.rasterStateDepthBiasMode = mode;
			auto requested = scene;
			requested.MaxDepth -= nativeViewportBiases[mode];
			shadow.data.viewPort = requested;
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &requested);
		};
		// Distinct native layers can differ by several floating-point steps after projection.
		const std::array samples{
			std::array{ .039658550173044205f, .039658550173044205f },
			std::array{ .039653241634368896f, .03965326026082039f },
			std::array{ .03965268284082413f, .03965267911553383f },
			std::array{ .03965165466070175f, .03965163975954056f },
			std::array{ .039652757346630096f, .03965276852250099f }
		};
		for (const auto& sample : samples) {
			globals::game::shadowState = nullptr;
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
			context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, sample[0], 0);
			setDepth(sample[1]);
			Require(visibleSamples() == (sample[1] >= sample[0] ? 4 : 0), "Unbiased layer control did not reproduce rounding-dependent rejection");
			globals::game::shadowState = &shadow;
			context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, sample[0] * (1 - .000030f), 0);
			for (uint32_t mode = 0; mode < nativeViewportBiases.size(); ++mode) {
				if (nativeViewportBiases[mode] <= 0)
					continue;
				setMode(mode);
				Require(visibleSamples() == 4, "A native layer still failed because of projection rounding");
			}
		}
		D3D11_DEPTH_STENCIL_DESC depthDesc{};
		depthDesc.DepthEnable = TRUE;
		depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		winrt::com_ptr<ID3D11DepthStencilState> write, equal, read;
		Require(SUCCEEDED(device->CreateDepthStencilState(&depthDesc, write.put())), "Layer prepass state creation failed");
		Util::SetResourceName(write.get(), "ReverseZStateTest::LayerWrite");
		depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthDesc.DepthFunc = D3D11_COMPARISON_EQUAL;
		Require(SUCCEEDED(device->CreateDepthStencilState(&depthDesc, equal.put())), "Layer equality state creation failed");
		Util::SetResourceName(equal.get(), "ReverseZStateTest::LayerEqual");
		depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		Require(SUCCEEDED(device->CreateDepthStencilState(&depthDesc, read.put())), "Layer read state creation failed");
		Util::SetResourceName(read.get(), "ReverseZStateTest::LayerRead");
		setMode(6);
		context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, 0, 0);
		setDepth(samples[3][0]);
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, write.get(), 0);
		Require(visibleSamples() == 4, "Native layer prepass failed");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, equal.get(), 0);
		Require(visibleSamples() == 4, "Native layer changed depth between prepass and equality shading");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, read.get(), 0);
		setMode(1);
		Require(visibleSamples() == 0, "Smaller native layer bias reversed layer ordering");
		setMode(8);
		setDepth(samples[3][1]);
		Require(visibleSamples() == 4, "Distinct biased layers lost their ordering after projection rounding");
		setMode(6);
		context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, 0, 0);
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, write.get(), 0);
		setDepth(.045f, .03f, false);
		Require(visibleSamples() == 4, "Sloped wall fixture did not cover the viewport");
		setMode(8);
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, read.get(), 0);
		setDepth(.045f, .03f, true);
		Require(visibleSamples(12) == 4, "Native layer ordering depended on triangle depth range");
		for (uint32_t baseMode = 0; baseMode < nativeViewportBiases.size(); ++baseMode) {
			setMode(baseMode);
			context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, 0, 0);
			ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, write.get(), 0);
			setDepth(.045f, .03f, false);
			Require(visibleSamples() == 4, "Native base layer was clipped");
			ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, read.get(), 0);
			setDepth(.045f, .03f, true);
			for (uint32_t layerMode = 0; layerMode < nativeViewportBiases.size(); ++layerMode) {
				if (nativeViewportBiases[layerMode] == nativeViewportBiases[baseMode])
					continue;
				setMode(layerMode);
				Require(visibleSamples(12) == (nativeViewportBiases[layerMode] > nativeViewportBiases[baseMode] ? 4 : 0),
					"Native layer ordering changed across differently tessellated surfaces");
			}
		}
		setDepth(samples[0][0]);
		context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, samples[0][0] * (1 - .000030f), 0);
		for (uint32_t mode = 0; mode < nativeViewportBiases.size(); ++mode) {
			setMode(mode);
			CaptureOcclusionDepthRange(nativeViewportBiases);
			const OcclusionViewportScope scope(context);
			Require(visibleSamples() == 4, "Producer material biased a visible occlusion bound");
		}
		const auto raster = g_boundRasterState;
		const auto mappings = globals::features::reverseZ.rasterMappings;
		const auto rasterForwards = stateForwards[1];
		for (int iteration = 0; iteration < 3; ++iteration) {
			const RejectHeapAllocations rejectAllocations;
			for (uint32_t mode = 0; mode < nativeViewportBiases.size(); ++mode) {
				setMode(mode);
				Require(g_boundRasterState == raster && stateForwards[1] == rasterForwards &&
							globals::features::reverseZ.rasterMappings == mappings,
					"Layer changes allocated or rebound a rasterizer state");
				const auto forwards = stateForwards;
				ReapplyMappedStates(context);
				Require(stateForwards == forwards, "Unchanged layer bindings were redundantly reapplied");
			}
		}
		ID3D11DeviceContext_RSSetState::thunk(context, nullptr);
		Require(!g_boundRasterState, "Viewport layering replaced the default rasterizer");
		globals::game::shadowState = nullptr;
		globals::game::isVR = previousVR;
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
		Require(!g_boundRasterState, "Unbiased viewport replaced the default rasterizer");
	}

	void TestOcclusionViewportRanges(ID3D11DeviceContext* context)
	{
		const D3D11_VIEWPORT bounds{ 0, 0, 2, 2, 0, 1 };
		for (bool reversedTarget : { false, true }) {
			for (bool reversedProjection : { false, true }) {
				g_reverseTargetBound = reversedTarget;
				g_projectionReversed = reversedProjection;
				for (const auto interval : { std::array{ 0.0f, 0.999998f }, std::array{ .2f, .7f }, std::array{ 0.0f, 1.0f } }) {
					const D3D11_VIEWPORT scene{ 0, 0, 8, 8, interval[0], interval[1] };
					ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
					CaptureOcclusionDepthRange();
					ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
					const RejectHeapAllocations rejectAllocations;
					const OcclusionViewportScope scope(context);
					D3D11_VIEWPORT actual{};
					UINT count = 1;
					context->RSGetViewports(&count, &actual);
					const auto expected = reversedTarget && reversedProjection ? MirrorViewportDepth(scene) : bounds;
					Require(actual.MinDepth == expected.MinDepth && actual.MaxDepth == expected.MaxDepth,
						"Occlusion mapping changed an inactive depth domain or ignored its source range");
				}
			}
		}
		g_reverseTargetBound = g_projectionReversed = true;
		for (const auto interval : { std::array{ -1.0f, 1.0f }, std::array{ 0.0f, 2.0f }, std::array{ .8f, .2f },
				 std::array{ std::numeric_limits<float>::quiet_NaN(), 1.0f } }) {
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
			g_mappedViewports[0].MinDepth = interval[0];
			g_mappedViewports[0].MaxDepth = interval[1];
			CaptureOcclusionDepthRange();
			Require(g_preparedOcclusionDepthRange.minDepth == 1 && g_preparedOcclusionDepthRange.maxDepth == 1,
				"Invalid producer range did not select conservative visibility");
		}
	}

	void TestOcclusionViewportBiases(ID3D11DeviceContext* context)
	{
		const auto& biases = nativeViewportBiases;
		const D3D11_VIEWPORT scene{ 0, 0, 8, 8, 0, 0.999998f };
		const D3D11_VIEWPORT bounds{ 0, 0, 2, 2, 0, 1 };
		for (bool reversed : { false, true }) {
			g_reverseTargetBound = g_projectionReversed = reversed;
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
			{
				const RejectHeapAllocations rejectAllocations;
				CaptureOcclusionDepthRange(biases);
			}
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
			const OcclusionViewportScope scope(context);
			UINT count = 1;
			D3D11_VIEWPORT actual{};
			context->RSGetViewports(&count, &actual);
			Require(actual.MinDepth == (reversed ? 1.0f - (scene.MaxDepth - .000030f) : 0.0f) && actual.MaxDepth == 1.0f,
				"Occlusion viewport did not include native depth bias or changed standard rendering");
		}
		g_reverseTargetBound = g_projectionReversed = true;
		for (const auto interval : { std::array{ -0.01f, scene.MaxDepth }, std::array{ 0.0f, 1.01f },
				 std::array{ .8f, .2f }, std::array{ 0.0f, std::numeric_limits<float>::quiet_NaN() },
				 std::array{ std::numeric_limits<float>::infinity(), scene.MaxDepth }, std::array{ 0.0f, 1.0f } }) {
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
			g_requestedViewports[0].MinDepth = interval[0];
			g_requestedViewports[0].MaxDepth = interval[1];
			CaptureOcclusionDepthRange(biases);
			Require(g_preparedOcclusionDepthRange.minDepth == 1 && g_preparedOcclusionDepthRange.maxDepth == 1,
				"Invalid or mismatched logical viewport did not retain visibility");
		}
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
		g_mappedViewports[0].MinDepth = .000028f;
		CaptureOcclusionDepthRange(biases);
		Require(g_preparedOcclusionDepthRange.minDepth == 1 && g_preparedOcclusionDepthRange.maxDepth == 1,
			"Divergent mapped viewport did not retain visibility");
		for (float bias : { -0.01f, 0.0f, .000026f }) {
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
			CaptureOcclusionDepthRange({ &bias, 1 });
			Require(g_preparedOcclusionDepthRange.minDepth == 1.0f - (scene.MaxDepth - std::max(bias, 0.0f)),
				"Signed viewport bias did not preserve conservative bounds");
		}
		for (float bias : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
				 -std::numeric_limits<float>::infinity(), 1.01f, -1.01f, 1.0f }) {
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
			CaptureOcclusionDepthRange({ &bias, 1 });
			Require(g_preparedOcclusionDepthRange.minDepth == 1 && g_preparedOcclusionDepthRange.maxDepth == 1,
				"Invalid or exhausted viewport bias did not retain visibility");
		}
		const D3D11_VIEWPORT narrow{ 0, 0, 8, 8, .99999f, .999998f };
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &narrow);
		CaptureOcclusionDepthRange(biases);
		Require(g_preparedOcclusionDepthRange.minDepth == 1 && g_preparedOcclusionDepthRange.maxDepth == 1,
			"Bias exhausted a partial viewport without retaining visibility");
		for (const auto interval : { std::array{ 0.0f, scene.MaxDepth }, std::array{ .2f, .7f }, std::array{ 0.0f, 1.0f } }) {
			const D3D11_VIEWPORT source{ 0, 0, 8, 8, interval[0], interval[1] };
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &source);
			CaptureOcclusionDepthRange(biases);
			for (const float bias : biases) {
				auto layer = source;
				layer.MaxDepth -= bias;
				const auto range = MirrorViewportDepth(layer);
				for (const double clipDepth : { 0.0, .0008, .5, 1.0 }) {
					const double boundDepth = g_preparedOcclusionDepthRange.minDepth * (1.0 - clipDepth) +
					                          g_preparedOcclusionDepthRange.maxDepth * clipDepth;
					const double layerDepth = range.MinDepth * (1.0 - clipDepth) + range.MaxDepth * clipDepth;
					Require(boundDepth >= layerDepth, "Occlusion range excluded a native biased layer");
				}
			}
		}
	}

	void TestOcclusionViewport(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		g_reverseTargetBound = g_projectionReversed = true;
		const D3D11_VIEWPORT scene{ 0, 0, 8, 8, 0, 0.999998f };
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
		CaptureOcclusionDepthRange();
		const auto sourceRange = g_preparedOcclusionDepthRange;
		const D3D11_VIEWPORT bounds{ 0, 0, 2, 2, 0, 1 };
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
		auto verifyRange = [&](float minimum, float maximum) {
			UINT count = 1;
			D3D11_VIEWPORT actual{};
			context->RSGetViewports(&count, &actual);
			Require(count == 1 && actual.MinDepth == minimum && actual.MaxDepth == maximum &&
						actual.Width == bounds.Width && actual.Height == bounds.Height,
				"Occlusion viewport lost the source depth range or changed raster dimensions");
			ID3D11DeviceContext_RSGetViewports::thunk(context, &count, &actual);
			Require(std::memcmp(&actual, &bounds, sizeof(bounds)) == 0, "Occlusion mapping changed the logical viewport");
		};
		{
			const OcclusionViewportScope scope(context);
			verifyRange(sourceRange.minDepth, sourceRange.maxDepth);
			Require(g_preparedOcclusionDepthRange.minDepth == 1 && g_preparedOcclusionDepthRange.maxDepth == 1,
				"Occlusion submission did not consume its producer range");
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
			verifyRange(sourceRange.minDepth, sourceRange.maxDepth);
			ID3D11DeviceContext_ClearState::thunk(context);
			Require(g_occlusionTesting, "ClearState ended an enclosing occlusion pass");
			g_reverseTargetBound = true;
			ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
			verifyRange(sourceRange.minDepth, sourceRange.maxDepth);
			{
				const OcclusionViewportScope nested(context);
			}
			verifyRange(sourceRange.minDepth, sourceRange.maxDepth);
			g_reverseTargetBound = false;
			ReapplyMappedStates(context);
			verifyRange(0, 1);
			g_reverseTargetBound = true;
		}
		verifyRange(0, 1);
		{
			const OcclusionViewportScope missingProducer(context);
			verifyRange(1, 1);
		}
		try {
			const OcclusionViewportScope scope(context);
			throw std::runtime_error("Occlusion scope fixture");
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()) == "Occlusion scope fixture", "Unexpected occlusion scope failure");
		}
		Require(!g_occlusionTesting, "Occlusion scope survived an exceptional exit");
		verifyRange(0, 1);

		const char* shaderSource = R"(
cbuffer TestDepth : register(b0) { float boundDepth; float depthSlope; float quad; };
float4 VS(uint i : SV_VertexID) : SV_Position {
    float2 p = float2((i << 1) & 2, i & 2) * float2(2, -2) + float2(-1, 1);
    const float2 corners[12] = {
        float2(-1, 1), float2(0, 1), float2(-1, -1), float2(-1, -1), float2(0, 1), float2(0, -1),
        float2(0, 1), float2(1, 1), float2(0, -1), float2(0, -1), float2(1, 1), float2(1, -1) };
    if (quad > 0) p = corners[i];
    return float4(p, boundDepth + p.x * depthSlope, 1);
}
float4 PS() : SV_Target { return 1; }
)";
		winrt::com_ptr<ID3DBlob> code, errors;
		winrt::com_ptr<ID3D11VertexShader> vertex;
		winrt::com_ptr<ID3D11PixelShader> pixel;
		Require(SUCCEEDED(D3DCompile(shaderSource, std::strlen(shaderSource), nullptr, nullptr, nullptr,
					"VS", "vs_5_0", 0, 0, code.put(), errors.put())),
			"Occlusion vertex fixture did not compile");
		Require(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vertex.put())), "Occlusion vertex fixture failed");
		Util::SetResourceName(vertex.get(), "ReverseZStateTest::Occlusion VS");
		code = nullptr;
		errors = nullptr;
		Require(SUCCEEDED(D3DCompile(shaderSource, std::strlen(shaderSource), nullptr, nullptr, nullptr,
					"PS", "ps_5_0", 0, 0, code.put(), errors.put())),
			"Occlusion pixel fixture did not compile");
		Require(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, pixel.put())), "Occlusion pixel fixture failed");
		Util::SetResourceName(pixel.get(), "ReverseZStateTest::Occlusion PS");
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = textureDesc.Height = 2;
		textureDesc.MipLevels = textureDesc.ArraySize = textureDesc.SampleDesc.Count = 1;
		textureDesc.Format = DXGI_FORMAT_D32_FLOAT;
		textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		winrt::com_ptr<ID3D11Texture2D> texture;
		winrt::com_ptr<ID3D11DepthStencilView> view;
		Require(SUCCEEDED(device->CreateTexture2D(&textureDesc, nullptr, texture.put())), "Occlusion depth fixture failed");
		Util::SetResourceName(texture.get(), "ReverseZStateTest::OcclusionDepth");
		Require(SUCCEEDED(device->CreateDepthStencilView(texture.get(), nullptr, view.put())), "Occlusion depth view fixture failed");
		Util::SetResourceName(view.get(), "ReverseZStateTest::OcclusionDepth DSV");
		D3D11_DEPTH_STENCIL_DESC depthDesc{};
		depthDesc.DepthEnable = TRUE;
		depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		winrt::com_ptr<ID3D11DepthStencilState> depth;
		Require(SUCCEEDED(device->CreateDepthStencilState(&depthDesc, depth.put())), "Occlusion state fixture failed");
		Util::SetResourceName(depth.get(), "ReverseZStateTest::OcclusionDepthState");
		D3D11_RASTERIZER_DESC rasterDesc{};
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.CullMode = D3D11_CULL_NONE;
		rasterDesc.DepthClipEnable = TRUE;
		rasterDesc.DepthBias = -1;
		rasterDesc.SlopeScaledDepthBias = -0.65f;
		rasterDesc.DepthBiasClamp = -100.0f;
		winrt::com_ptr<ID3D11RasterizerState> raster;
		Require(SUCCEEDED(device->CreateRasterizerState(&rasterDesc, raster.put())), "Occlusion raster fixture failed");
		Util::SetResourceName(raster.get(), "ReverseZStateTest::OcclusionRasterizer");
		winrt::com_ptr<ID3D11Query> query;
		const D3D11_QUERY_DESC queryDesc{ D3D11_QUERY_OCCLUSION, 0 };
		Require(SUCCEEDED(device->CreateQuery(&queryDesc, query.put())), "Occlusion query fixture failed");
		Util::SetResourceName(query.get(), "ReverseZStateTest::OcclusionQuery");
		winrt::com_ptr<ID3D11Buffer> input;
		D3D11_BUFFER_DESC inputDesc{};
		inputDesc.ByteWidth = 16;
		inputDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Require(SUCCEEDED(device->CreateBuffer(&inputDesc, nullptr, input.put())), "Occlusion input fixture failed");
		Util::SetResourceName(input.get(), "ReverseZStateTest::OcclusionInput");
		auto setBoundDepth = [&](float depthValue, float slope = 0, bool quad = false) {
			const std::array values{ depthValue, slope, quad ? 1.0f : 0.0f, 0.0f };
			context->UpdateSubresource(input.get(), 0, nullptr, values.data(), 0, 0);
		};
		setBoundDepth(0.000601f);
		auto* inputBuffer = input.get();
		context->VSSetConstantBuffers(0, 1, &inputBuffer);
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
		CaptureOcclusionDepthRange();
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
		context->OMSetRenderTargets(0, nullptr, view.get());
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth.get(), 0);
		ID3D11DeviceContext_RSSetState::thunk(context, raster.get());
		context->ClearDepthStencilView(view.get(), D3D11_CLEAR_DEPTH,
			sourceRange.minDepth + (sourceRange.maxDepth - sourceRange.minDepth) * 0.0006f, 0);
		context->VSSetShader(vertex.get(), nullptr, 0);
		context->PSSetShader(pixel.get(), nullptr, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		auto visibleSamples = [&](UINT vertices = 3) {
			context->Begin(query.get());
			context->Draw(vertices, 0);
			context->End(query.get());
			UINT64 samples = 0;
			HRESULT result;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			do {
				result = context->GetData(query.get(), &samples, sizeof(samples), 0);
			} while (result == S_FALSE && std::chrono::steady_clock::now() < deadline);
			Require(result == S_OK, "Occlusion query readback failed or timed out");
			return samples;
		};
		Require(visibleSamples() == 0, "Unmatched viewport did not reproduce thin-bound self-occlusion");
		{
			const OcclusionViewportScope scope(context);
			Require(visibleSamples() == 4, "Aligned viewport still culled a thin visible bound");
			setBoundDepth(0.000599f);
			Require(visibleSamples() == 0, "Aligned viewport admitted a fully occluded bound");
		}
		context->ClearDepthStencilView(view.get(), D3D11_CLEAR_DEPTH,
			sourceRange.minDepth + (sourceRange.maxDepth - sourceRange.minDepth) * 0.00085f, 0);
		setBoundDepth(0.000825f);
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
		CaptureOcclusionDepthRange();
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
		{
			const OcclusionViewportScope scope(context);
			Require(visibleSamples() == 0, "Unbiased bounds did not reproduce decal-layer occlusion");
		}
		const std::array biases{ 0.0f, .000026f, .000030f };
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &scene);
		CaptureOcclusionDepthRange(biases);
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
		{
			const OcclusionViewportScope scope(context);
			Require(visibleSamples() == 4, "Biased visible layer was still culled");
			setBoundDepth(0.00075f);
			Require(visibleSamples() == 0, "Conservative viewport admitted a deeply occluded bound");
		}
		ID3D11DeviceContext_RSSetViewports::thunk(context, 0, nullptr);
		CaptureOcclusionDepthRange();
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &bounds);
		{
			const OcclusionViewportScope scope(context);
			Require(visibleSamples() == 4, "Missing producer viewport did not retain visible bounds");
		}
		TestNativeViewportDepthOrdering(context, view.get(), scene, setBoundDepth, visibleSamples);
		TestNativeViewportLayering(device, context, view.get(), setBoundDepth, visibleSamples);
		TestStandardViewportDepthOrdering(device, context, setBoundDepth, visibleSamples);
		context->OMSetRenderTargets(0, nullptr, nullptr);
		ID3D11DeviceContext_ClearState::thunk(context);
	}

	void TestIsolatedContextState(ID3D11Device* device, ID3D11DeviceContext* context,
		ID3D11DepthStencilState* depth, ID3D11RasterizerState* raster)
	{
		winrt::com_ptr<ID3D11DeviceContext1> context1, nestedContext;
		std::unique_ptr<Util::IsolatedDeviceContextState> isolated, nested;
		Require(Util::CreateIsolatedDeviceContextState(device, context, context1, isolated, "ReverseZStateTest::Isolated"), "Isolated context creation failed");
		Require(Util::CreateIsolatedDeviceContextState(device, context, nestedContext, nested, "ReverseZStateTest::Nested"), "Nested context creation failed");
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 4;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		winrt::com_ptr<ID3D11Texture2D> texture;
		winrt::com_ptr<ID3D11DepthStencilView> view;
		Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.put())), "Isolated scene depth creation failed");
		Util::SetResourceName(texture.get(), "ReverseZStateTest::IsolatedSceneDepth");
		Require(SUCCEEDED(device->CreateDepthStencilView(texture.get(), nullptr, view.put())), "Isolated scene view creation failed");
		Util::SetResourceName(view.get(), "ReverseZStateTest::IsolatedSceneDepth DSV");
		auto& reverse = globals::features::reverseZ;
		reverse.activeThisBoot = true;
		g_reversalActive = g_projectionReversed = true;
		reverse.RegisterSceneDepthView(view.get());
		context->OMSetRenderTargets(0, nullptr, view.get());
		UpdateBoundDepthTarget(context, view.get());
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 21);
		ID3D11DeviceContext_RSSetState::thunk(context, raster);
		ShadowState shadow;
		shadow.data.viewPort = D3D11_VIEWPORT{ 0, 0, 4, 4, .2f, .7f };
		for (auto& eye : shadow.eyes)
			eye.viewDepthRange = { .2f, .7f };
		shadow.data.rasterStateDepthBiasMode = 6;
		const bool previousVR = globals::game::isVR;
		globals::game::isVR = true;
		globals::game::shadowState = &shadow;
		D3D11_VIEWPORT viewport = shadow.data.viewPort;
		viewport.MaxDepth -= nativeViewportBiases[6];
		shadow.data.viewPort = viewport;
		ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &viewport);
		CaptureOcclusionDepthRange();
		auto sourceRange = g_preparedOcclusionDepthRange;
		const auto mappedDepth = g_boundState;
		const auto mappedRaster = g_boundRasterState;
		auto verify = [&] {
			CheckBoundStates(context, mappedDepth, mappedRaster, 21);
			winrt::com_ptr<ID3D11DepthStencilState> logicalDepth;
			winrt::com_ptr<ID3D11RasterizerState> logicalRaster;
			ID3D11DeviceContext_OMGetDepthStencilState::thunk(context, logicalDepth.put(), nullptr);
			ID3D11DeviceContext_RSGetState::thunk(context, logicalRaster.put());
			Require(logicalDepth.get() == depth && logicalRaster.get() == raster, "Context restoration lost logical state ownership");
			UINT count = 1;
			D3D11_VIEWPORT logicalViewport{};
			ID3D11DeviceContext_RSGetViewports::thunk(context, &count, &logicalViewport);
			Require(count == 1 && std::memcmp(&logicalViewport, &viewport, sizeof(viewport)) == 0,
				"Context restoration lost the logical viewport");
			Require(g_reverseTargetBound && g_projectionReversed && !g_nearMaskBound,
				"Context restoration lost the depth domain");
			Require(g_nativeViewportCorrection && g_nativeViewportCorrection->maxDepth == shadow.eyes[0].viewDepthRange.y, "Context restoration lost native viewport normalization");
			Require(g_preparedOcclusionDepthRange.minDepth == sourceRange.minDepth && g_preparedOcclusionDepthRange.maxDepth == sourceRange.maxDepth,
				"Context restoration lost the occlusion producer depth range");
			const auto restoredForwards = stateForwards;
			ReapplyMappedStates(context);
			Require(stateForwards == restoredForwards, "Restored physical states were redundantly rebound");
			ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 21);
			ID3D11DeviceContext_RSSetState::thunk(context, raster);
			CheckBoundStates(context, mappedDepth, mappedRaster, 21);
		};
		Util::captureDeviceContextState = CaptureReverseContextState;
		{
			const RejectHeapAllocations rejectAllocations;
			Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
			Require(static_cast<bool>(guard), "Allocation-free isolation was unavailable");
			ID3D11DeviceContext_ClearState::thunk(context);
			{
				Util::ScopedDeviceContextState nestedGuard(nestedContext.get(), nested.get());
				ID3D11DeviceContext_ClearState::thunk(context);
			}
			guard.Restore();
		}
		verify();
		{
			const OcclusionViewportScope occlusion(context);
			sourceRange = {};
			{
				Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
				Require(!g_occlusionTesting, "Isolated context inherited an occlusion pass");
			}
			Require(g_occlusionTesting, "Context restoration lost the active occlusion pass");
			verify();
		}
		for (int iteration = 0; iteration < 3; ++iteration) {
			try {
				Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
				ID3D11DeviceContext_ClearState::thunk(context);
				const D3D11_VIEWPORT overlayViewport{ 0, 0, 2, 2, 0, 1 };
				ID3D11DeviceContext_RSSetViewports::thunk(context, 1, &overlayViewport);
				ID3D11DeviceContext_OMSetDepthStencilState::thunk(context, depth, 9);
				{
					Util::ScopedDeviceContextState nestedGuard(nestedContext.get(), nested.get());
					ID3D11DeviceContext_ClearState::thunk(context);
				}
				Require(!g_reverseTargetBound && g_requestedState.get() == depth && g_requestedStencilRef == 9,
					"Nested isolation did not restore the outer logical state");
				if (iteration == 1) {
					guard.Restore();
					verify();
					guard.Restore();
				}
				if (iteration == 2)
					throw std::runtime_error("Isolated draw aborted");
			} catch (const std::runtime_error& error) {
				if (std::string_view(error.what()) != "Isolated draw aborted")
					throw;
			}
			verify();
		}
		{
			const RejectHeapAllocations rejectAllocations;
			Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
			g_renderingRejected = true;
		}
		CheckBoundStates(context, g_rejectedDepthState.get(), nullptr, 21);
		Require(g_renderingRejected, "Context restoration cleared a rendering failure");
		g_renderingRejected = false;
		ReapplyMappedStates(context);
		verify();
		winrt::com_ptr<ID3D11DeviceContext> other;
		Require(SUCCEEDED(device->CreateDeferredContext(0, other.put())), "Observer other-context creation failed");
		Util::SetResourceName(other.get(), "ReverseZStateTest::ObserverOtherContext");
		Util::DeviceContextStateSnapshot unrelatedSnapshot;
		const D3D11_VIEWPORT otherViewport{ 0, 0, 2, 2, 0, 1 };
		ID3D11DeviceContext_RSSetViewports::thunk(other.get(), 1, &otherViewport);
		CaptureReverseContextState(other.get(), unrelatedSnapshot);
		Require(!unrelatedSnapshot, "Observer captured an unrelated context");
		verify();
		Util::captureDeviceContextState = nullptr;
		globals::game::shadowState = nullptr;
		globals::game::isVR = previousVR;
		reverse.UnregisterSceneDepthView(view.get());
		context->OMSetRenderTargets(0, nullptr, nullptr);
	}
}

int main()
{
	try {
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
					D3D11_SDK_VERSION, device.put(), nullptr, context.put())),
			"WARP device creation failed");
		globals::d3d::device = device.get();
		globals::d3d::context = context.get();
		ID3D11DeviceContext_OMSetDepthStencilState::func = [](ID3D11DeviceContext* ctx, ID3D11DepthStencilState* state, UINT ref) { ++stateForwards[0]; ctx->OMSetDepthStencilState(state, ref); };
		ID3D11DeviceContext_RSSetState::func = [](ID3D11DeviceContext* ctx, ID3D11RasterizerState* state) { ++stateForwards[1]; ctx->RSSetState(state); };
		ID3D11DeviceContext_ClearState::func = [](ID3D11DeviceContext* ctx) { ctx->ClearState(); };
		ID3D11DeviceContext_RSSetViewports::func = [](ID3D11DeviceContext* ctx, UINT count, const D3D11_VIEWPORT* viewports) { ++stateForwards[2]; ctx->RSSetViewports(count, viewports); };
		UseNative<ID3D11DeviceContext_ClearDepthStencilView, 53>(context.get());
		// Native dispatch can change when the first isolated context state is activated.
		ID3D11DeviceContext_OMGetDepthStencilState::func = [](ID3D11DeviceContext* ctx, ID3D11DepthStencilState** state, UINT* ref) { ctx->OMGetDepthStencilState(state, ref); };
		ID3D11DeviceContext_RSGetState::func = [](ID3D11DeviceContext* ctx, ID3D11RasterizerState** state) { ctx->RSGetState(state); };
		ID3D11DeviceContext_RSGetViewports::func = [](ID3D11DeviceContext* ctx, UINT* count, D3D11_VIEWPORT* viewports) { ctx->RSGetViewports(count, viewports); };
		Require(PrepareRejectedDepthState(device.get()), "Rejection fallback preparation failed");
		TestPreparationAdmission(device.get(), context.get());
		TestPreparationTransaction(device.get(), context.get());
		TestFeatureViewRetirement(device.get());
		TestMenuTargetRefresh(device.get(), context.get());

		D3D11_DEPTH_STENCIL_DESC depthDesc{};
		depthDesc.DepthEnable = TRUE;
		depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		depthDesc.StencilEnable = TRUE;
		depthDesc.StencilReadMask = depthDesc.StencilWriteMask = 0x5a;
		depthDesc.FrontFace = depthDesc.BackFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_EQUAL };
		winrt::com_ptr<ID3D11DepthStencilState> requestedDepth, actualDepth, logicalDepth;
		Require(SUCCEEDED(device->CreateDepthStencilState(&depthDesc, requestedDepth.put())), "Depth state creation failed");
		Util::SetResourceName(requestedDepth.get(), "ReverseZStateTest::DepthState");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context.get(), requestedDepth.get(), 21);
		context->OMGetDepthStencilState(actualDepth.put(), nullptr);
		Require(actualDepth.get() == requestedDepth.get(), "Standard domain changed requested state");
		actualDepth = nullptr;
		g_reverseTargetBound = g_projectionReversed = true;
		ReapplyMappedStates(context.get());
		UINT stencilRef = 0;
		context->OMGetDepthStencilState(actualDepth.put(), &stencilRef);
		D3D11_DEPTH_STENCIL_DESC mappedDepth{};
		actualDepth->GetDesc(&mappedDepth);
		Require(mappedDepth.DepthFunc == D3D11_COMPARISON_GREATER_EQUAL && stencilRef == 21 &&
					mappedDepth.StencilEnable && mappedDepth.StencilWriteMask == depthDesc.StencilWriteMask &&
					mappedDepth.FrontFace.StencilPassOp == D3D11_STENCIL_OP_REPLACE,
			"Reversal changed stencil or comparison contract");
		ID3D11DeviceContext_OMGetDepthStencilState::thunk(context.get(), logicalDepth.put(), &stencilRef);
		Require(logicalDepth.get() == requestedDepth.get() && stencilRef == 21, "Depth getter lost logical ownership");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context.get(), nullptr, 4);
		logicalDepth = nullptr;
		ID3D11DeviceContext_OMGetDepthStencilState::thunk(context.get(), logicalDepth.put(), &stencilRef);
		Require(!logicalDepth && stencilRef == 4, "Default depth getter did not return null");

		D3D11_VIEWPORT viewport{ 0, 0, 2, 2, 0.2f, 0.7f };
		ID3D11DeviceContext_RSSetViewports::thunk(context.get(), 1, &viewport);
		UINT count = 1;
		D3D11_VIEWPORT observed{};
		context->RSGetViewports(&count, &observed);
		Require(std::abs(observed.MinDepth - 0.3f) < 1e-6f && std::abs(observed.MaxDepth - 0.8f) < 1e-6f, "Viewport depth interval was not mirrored");
		ID3D11DeviceContext_RSGetViewports::thunk(context.get(), &count, &observed);
		Require(std::memcmp(&observed, &viewport, sizeof(viewport)) == 0, "Viewport getter lost requested interval");
		ID3D11DeviceContext_RSSetViewports::thunk(context.get(), 0, nullptr);
		count = 1;
		ID3D11DeviceContext_RSGetViewports::thunk(context.get(), &count, &observed);
		Require(count == 0, "Empty viewport binding retained stale entries");

		D3D11_RASTERIZER_DESC rasterDesc{};
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.CullMode = D3D11_CULL_NONE;
		rasterDesc.DepthClipEnable = TRUE;
		rasterDesc.DepthBias = 7;
		rasterDesc.SlopeScaledDepthBias = 2;
		rasterDesc.DepthBiasClamp = 0.25f;
		winrt::com_ptr<ID3D11RasterizerState> requestedRaster, actualRaster, logicalRaster;
		Require(SUCCEEDED(device->CreateRasterizerState(&rasterDesc, requestedRaster.put())), "Rasterizer creation failed");
		Util::SetResourceName(requestedRaster.get(), "ReverseZStateTest::Rasterizer");
		ID3D11DeviceContext_RSSetState::thunk(context.get(), requestedRaster.get());
		context->RSGetState(actualRaster.put());
		D3D11_RASTERIZER_DESC mappedRaster{};
		actualRaster->GetDesc(&mappedRaster);
		Require(mappedRaster.DepthBias == -7 && mappedRaster.SlopeScaledDepthBias == -2 && mappedRaster.DepthBiasClamp == -0.25f, "Rasterizer bias was not mirrored");
		ID3D11DeviceContext_RSGetState::thunk(context.get(), logicalRaster.put());
		Require(logicalRaster.get() == requestedRaster.get(), "Rasterizer getter lost logical ownership");
		TestRequestedStateMappingCache(device.get(), context.get(), requestedDepth.get(), requestedRaster.get());
		TestNativeViewportBiasMapping(context.get());
		TestStandardViewportMapping(device.get(), context.get());
		TestStandardViewportTargets(device.get(), context.get());
		TestFurDepthDomain(device.get(), context.get());
		TestNativeViewportDomainTransitions(context.get());
		TestOcclusionViewportRanges(context.get());
		TestOcclusionViewportBiases(context.get());
		TestOcclusionViewport(device.get(), context.get());
		TestIsolatedContextState(device.get(), context.get(), requestedDepth.get(), requestedRaster.get());

		rasterDesc.DepthBias = std::numeric_limits<INT>::min();
		requestedRaster = nullptr;
		Require(SUCCEEDED(device->CreateRasterizerState(&rasterDesc, requestedRaster.put())), "Extreme rasterizer fixture creation failed");
		Util::SetResourceName(requestedRaster.get(), "ReverseZStateTest::UnrepresentableBias");
		g_reverseTargetBound = false;
		ReapplyMappedStates(context.get());
		ID3D11DeviceContext_RSSetState::thunk(context.get(), requestedRaster.get());
		g_reverseTargetBound = true;
		ReapplyMappedStates(context.get());
		logicalRaster = nullptr;
		ID3D11DeviceContext_RSGetState::thunk(context.get(), logicalRaster.put());
		Require(logicalRaster.get() == requestedRaster.get(), "Rejected rasterizer lost its logical getter state");
		actualDepth = nullptr;
		context->OMGetDepthStencilState(actualDepth.put(), nullptr);
		actualDepth->GetDesc(&mappedDepth);
		Require(g_renderingRejected && !globals::features::reverseZ.failure.empty() && mappedDepth.DepthFunc == D3D11_COMPARISON_NEVER && !mappedDepth.StencilEnable,
			"Invalid mirrored state did not fail closed");
		ID3D11DeviceContext_OMSetDepthStencilState::thunk(context.get(), requestedDepth.get(), 21);
		actualDepth = nullptr;
		context->OMGetDepthStencilState(actualDepth.put(), nullptr);
		Require(actualDepth.get() == g_rejectedDepthState.get(), "A later state bind bypassed the failure latch");
		g_reverseTargetBound = false;
		ReapplyMappedStates(context.get());
		actualDepth = nullptr;
		context->OMGetDepthStencilState(actualDepth.put(), nullptr);
		Require(actualDepth.get() == requestedDepth.get(), "Failure latch changed standard render domains");
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
