#include "d3d_resource_naming.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <format>
#include <iostream>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>
#include <wrl/implements.h>

using json = nlohmann::json;

namespace REL
{
	template <class Function>
	using Relocation = Function*;
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
		kMAIN_DOWNSAMPLE
	};
}
struct DepthTarget
{
	ID3D11Texture2D* texture = nullptr;
	ID3D11DepthStencilView* views[1]{};
	ID3D11DepthStencilView* readOnlyViews[1]{};
	ID3D11ShaderResourceView* depthSRV = nullptr;
	ID3D11ShaderResourceView* stencilSRV = nullptr;
	~DepthTarget()
	{
		if (texture)
			texture->Release();
		if (views[0])
			views[0]->Release();
		if (readOnlyViews[0])
			readOnlyViews[0]->Release();
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
		DepthTarget depthStencils[6];
	} data;
	auto& GetDepthStencilData() { return data; }
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
#include "reverse_z_state_storage.h"
#include "reverse_z_state_targets.h"
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
					Require(reverse.PrepareDepthTargets() && reverse.generation == 2 && globals::cache.clears == clears,
						"Warm recreation changed shader ownership or failed to advance the target generation");
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

	void CheckBoundStates(ID3D11DeviceContext* context, ID3D11DepthStencilState* depth, ID3D11RasterizerState* raster, UINT expectedRef)
	{
		winrt::com_ptr<ID3D11DepthStencilState> actualDepth;
		winrt::com_ptr<ID3D11RasterizerState> actualRaster;
		UINT actualRef = 0;
		context->OMGetDepthStencilState(actualDepth.put(), &actualRef);
		context->RSGetState(actualRaster.put());
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
		ID3D11DeviceContext_OMSetDepthStencilState::func = [](ID3D11DeviceContext* ctx, ID3D11DepthStencilState* state, UINT ref) { ctx->OMSetDepthStencilState(state, ref); };
		ID3D11DeviceContext_RSSetState::func = [](ID3D11DeviceContext* ctx, ID3D11RasterizerState* state) { ctx->RSSetState(state); };
		ID3D11DeviceContext_ClearState::func = [](ID3D11DeviceContext* ctx) { ctx->ClearState(); };
		UseNative<ID3D11DeviceContext_RSSetViewports, 44>(context.get());
		UseNative<ID3D11DeviceContext_ClearDepthStencilView, 53>(context.get());
		UseNative<ID3D11DeviceContext_OMGetDepthStencilState, 92>(context.get());
		UseNative<ID3D11DeviceContext_RSGetState, 94>(context.get());
		UseNative<ID3D11DeviceContext_RSGetViewports, 95>(context.get());
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

		rasterDesc.DepthBias = std::numeric_limits<INT>::min();
		requestedRaster = nullptr;
		Require(SUCCEEDED(device->CreateRasterizerState(&rasterDesc, requestedRaster.put())), "Extreme rasterizer fixture creation failed");
		Util::SetResourceName(requestedRaster.get(), "ReverseZStateTest::UnrepresentableBias");
		ID3D11DeviceContext_RSSetState::thunk(context.get(), requestedRaster.get());
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
