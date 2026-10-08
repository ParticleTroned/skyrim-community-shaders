#include "Features/Upscaling/NvidiaPipelinePolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

using UINT = unsigned;
using uint = unsigned;
using HRESULT = std::int32_t;
constexpr HRESULT S_OK = 0;
constexpr HRESULT E_FAIL = -1;
constexpr HRESULT E_POINTER = -2;
constexpr HRESULT DXGI_ERROR_INVALID_CALL = -3;
constexpr HRESULT DXGI_ERROR_WAS_STILL_DRAWING = -4;
constexpr UINT DXGI_PRESENT_TEST = 1;
constexpr int DXGI_FORMAT_UNKNOWN = 0;
constexpr int D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4;
constexpr int D3D12_RESOURCE_STATE_COMMON = 0;
constexpr int D3D12_RESOURCE_STATE_COPY_SOURCE = 1;
constexpr int D3D12_RESOURCE_STATE_COPY_DEST = 2;
constexpr int D3D12_RESOURCE_STATE_PRESENT = 3;
constexpr bool FAILED(HRESULT result) { return result < 0; }
#define ARRAYSIZE(value) std::size(value)
#define CS_GPU_PASS(name) static_cast<void>(0)

namespace SKSE::stl
{
	template <class Fn>
	struct scope_exit
	{
		Fn fn;
		explicit scope_exit(Fn action) : fn(std::move(action)) {}
		~scope_exit() { fn(); }
	};
}

namespace logger
{
	template <class... Args>
	void error(Args&&...)
	{}
}

struct DXGI_PRESENT_PARAMETERS
{};
struct D3D11_VIEWPORT
{
	float TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth;
};

// Simulated engine and D3D boundaries expose copy, draw and presentation events.
struct Gpu
{
	int copies = 0;
	int draws = 0;
	int clears = 0;
	HRESULT closeResult = S_OK;
	template <class... Args>
	HRESULT Signal(Args...)
	{
		return S_OK;
	}
	template <class... Args>
	HRESULT Wait(Args...)
	{
		return S_OK;
	}
	template <class... Args>
	HRESULT SetEventOnCompletion(Args...)
	{
		return S_OK;
	}
	template <class... Args>
	HRESULT Reset(Args...)
	{
		return S_OK;
	}
	HRESULT Close() { return closeResult; }
	template <class... Args>
	void ResourceBarrier(Args...)
	{}
	template <class... Args>
	void ExecuteCommandLists(Args...)
	{}
	template <class... Args>
	void CopyResource(Args...)
	{
		++copies;
	}
	template <class... Args>
	void ClearRenderTargetView(Args...)
	{
		++clears;
	}
	template <class... Args>
	void Draw(Args...)
	{
		++draws;
	}
	template <class... Args>
	void RSSetViewports(Args...)
	{}
	template <class... Args>
	void IASetInputLayout(Args...)
	{}
	template <class... Args>
	void IASetVertexBuffers(Args...)
	{}
	template <class... Args>
	void IASetIndexBuffer(Args...)
	{}
	template <class... Args>
	void IASetPrimitiveTopology(Args...)
	{}
	template <class... Args>
	void VSSetShader(Args...)
	{}
	template <class... Args>
	void RSSetState(Args...)
	{}
	template <class... Args>
	void OMSetBlendState(Args...)
	{}
	template <class... Args>
	void PSSetShaderResources(Args...)
	{}
	template <class... Args>
	void OMSetRenderTargets(Args...)
	{}
	template <class... Args>
	void PSSetShader(Args...)
	{}
};
using ID3D11ShaderResourceView = Gpu;
using ID3D11RenderTargetView = Gpu;
using ID3D12CommandList = Gpu;

struct CD3DX12_RESOURCE_BARRIER
{
	static int Transition(Gpu*, int, int) { return 0; }
};

template <class T>
struct Handle
{
	T* value = nullptr;
	T* get() const { return value; }
	T* operator->() const { return value; }
	explicit operator bool() const { return value != nullptr; }
};

struct WrappedResource
{
	Gpu gpu;
	Handle<Gpu> resource{ &gpu }, resource11{ &gpu }, rtv{ &gpu };
};

struct SwapChain
{
	HRESULT result = S_OK;
	int presents = 0;
	bool throwOnPresent = false;
	HRESULT Present(UINT, UINT)
	{
		++presents;
		if (throwOnPresent)
			throw std::runtime_error("injected present failure");
		return result;
	}
	HRESULT Present1(UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS*) { return Present(interval, flags); }
	UINT GetCurrentBackBufferIndex() const { return 0; }
};

struct DX12SwapChain
{
	Gpu gpu;
	SwapChain backend;
	SwapChain* swapChain = &backend;
	Handle<Gpu> d3d11Context{ &gpu }, d3d11Fence{ &gpu }, d3d12Fence{ &gpu }, commandQueue{ &gpu };
	std::array<Handle<Gpu>, 2> commandAllocators{ Handle<Gpu>{ &gpu }, Handle<Gpu>{ &gpu } };
	std::array<Handle<Gpu>, 2> commandLists = commandAllocators;
	std::array<Handle<Gpu>, 2> swapChainBuffers = commandAllocators;
	std::array<std::uint64_t, 2> allocatorFenceValues{};
	CSX::NvidiaPipelinePolicy::InteropFenceSequence fenceSequence;
	std::unique_ptr<WrappedResource> swapChainBufferWrapped = std::make_unique<WrappedResource>();
	std::unique_ptr<WrappedResource> uiBufferWrapped = std::make_unique<WrappedResource>();
	std::unique_ptr<WrappedResource> depthBufferShared12 = std::make_unique<WrappedResource>();
	std::unique_ptr<WrappedResource> motionVectorBufferShared12 = std::make_unique<WrappedResource>();
	UINT frameIndex = 0;
	bool runtimeQuarantined = false;
	HRESULT Present(UINT, UINT);
	HRESULT Present1(UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT PresentInternal(UINT, UINT, const DXGI_PRESENT_PARAMETERS*) noexcept;
};

namespace RE
{
	namespace RENDER_TARGETS
	{
		constexpr int kMOTION_VECTOR = 0;
	}
	namespace RENDER_TARGETS_DEPTHSTENCIL
	{
		constexpr int kMAIN = 0;
	}
}

struct UI
{
	bool paused = false;
	bool GameIsPaused() const { return paused; }
};
struct State
{
	bool loading = false;
	struct
	{
		float x = 1920, y = 1080;
	} screenSize;
	bool IsMainOrLoadingMenuOpen(UI*) const { return loading; }
};
struct Renderer
{
	Gpu texture;
	struct Target
	{
		Gpu* texture;
	};
	struct Depth
	{
		Gpu* depthSRV;
	};
	struct
	{
		Target renderTargets[1];
	} runtime{ { { &texture } } };
	struct
	{
		Depth depthStencils[1];
	} depth{ { { &texture } } };
	auto& GetRuntimeData() { return runtime; }
	auto& GetDepthStencilData() { return depth; }
};

struct Upscaling
{
#include "frame_generation_members.h"
	struct
	{
		bool ready = true, disableConfirmed = true, presentResult = true;
		bool lastUse = false;
		int presents = 0;
		bool IsFrameGenerationRuntimeReady() const { return ready; }
		bool IsFrameGenerationDisableConfirmed() const { return disableConfirmed; }
		bool Present(bool use)
		{
			++presents;
			lastUse = use;
			return presentResult;
		}
	} fidelityFX;
	struct
	{
		bool quarantined = false, disableSucceeds = true;
		bool IsFrameGenerationQuarantinedByReflex() const { return quarantined; }
		bool EnsureReflexDisabledForFrameGeneration()
		{
			quarantined = quarantined || !disableSucceeds;
			return !quarantined;
		}
	} streamline;
	Gpu shader;
	bool vertexShaderReady = true;
	struct LazyShader
	{
		Gpu shader;
		bool ready = true;
		Gpu* Get(const wchar_t*, std::initializer_list<std::pair<const char*, const char*>>, const char*, const char*, const char*) { return ready ? &shader : nullptr; }
	} copyDepthToSharedBufferPS;
	Handle<Gpu> upscaleRasterizerState{ &shader }, upscaleBlendState{ &shader };
	Gpu* GetUpscaleVS() { return vertexShaderReady ? &shader : nullptr; }
	DX12SwapChain dx12SwapChain;
	std::atomic_bool d3d12SwapChainActive{ true };
	bool limiterUsedGeneration = false;
	void FrameLimiter() { limiterUsedGeneration = ShouldUseFrameGenerationThisFrame(); }
	void PrepareFrameGenerationInputs();
	bool CopySharedD3D12Resources();
	bool IsFrameGenerationDx12PathActive() const;
	bool ShouldPrepareFrameGeneration() const;
	bool ShouldUseFrameGenerationThisFrame() const;
	void InvalidateFrameGenerationInputs() noexcept;
};

namespace globals
{
	State stateStorage;
	State* state = &stateStorage;
	namespace game
	{
		bool isVR = false;
		UI uiStorage;
		UI* ui = &uiStorage;
		Renderer rendererStorage;
		Renderer* renderer = &rendererStorage;
	}
	namespace d3d
	{
		Gpu contextStorage;
		Gpu* context = &contextStorage;
	}
	namespace features
	{
		Upscaling upscaling;
	}
}

namespace REL
{
	struct Module
	{
		static bool IsVR() { return globals::game::isVR; }
	};
}

#include "frame_generation_under_test.h"

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	Upscaling& Reset()
	{
		auto& upscaling = globals::features::upscaling;
		std::destroy_at(&upscaling);
		std::construct_at(&upscaling);
		upscaling.settings.frameGenerationMode = 1;
		globals::stateStorage = {};
		globals::state = &globals::stateStorage;
		globals::game::isVR = false;
		globals::game::uiStorage = {};
		globals::game::ui = &globals::game::uiStorage;
		std::destroy_at(&globals::game::rendererStorage);
		std::construct_at(&globals::game::rendererStorage);
		globals::game::renderer = &globals::game::rendererStorage;
		globals::d3d::contextStorage = {};
		globals::d3d::context = &globals::d3d::contextStorage;
		return upscaling;
	}

	void RuntimeSettings()
	{
		const Upscaling::Settings defaults{};
		Check(defaults.frameGenerationMode == 0 && defaults.frameGenerationForceEnable == 0,
			"frame generation defaults must remain disabled");
		for (const bool isVR : { false, true }) {
			globals::game::isVR = isVR;
			for (const uint requested : { 0u, 1u, 2u, UINT32_MAX }) {
				Upscaling::Settings settings{};
				settings.frameGenerationMode = requested;
				settings.frameGenerationForceEnable = requested;
				SanitizeFrameGenerationSettings(settings);
				const uint expected = isVR ? 0u : std::min(requested, 1u);
				Check(settings.frameGenerationMode == expected && settings.frameGenerationForceEnable == expected,
					"runtime normalization accepted VR frame generation or changed flat clamping");
				SanitizeFrameGenerationSettings(settings);
				Check(settings.frameGenerationMode == expected && settings.frameGenerationForceEnable == expected,
					"frame-generation normalization must be idempotent");
			}
		}
	}

	void MenuTransitions()
	{
		for (bool paused : { false, true }) {
			auto& u = Reset();
			globals::game::ui->paused = paused;
			globals::state->loading = !paused;
			u.PrepareFrameGenerationInputs();
			globals::game::ui->paused = false;
			globals::state->loading = false;
			Check(!u.ShouldUseFrameGenerationThisFrame(), "menu close enabled unprepared generation");
			Check(u.dx12SwapChain.Present(0, 0) == S_OK && !u.fidelityFX.lastUse,
				"unprepared frame did not use base presentation");
			Check(globals::d3d::context->copies == 0 && globals::d3d::context->draws == 0,
				"blocked frame copied inputs");

			u.PrepareFrameGenerationInputs();
			globals::state->loading = true;
			globals::game::ui->paused = true;
			Check(u.ShouldUseFrameGenerationThisFrame(), "menu opening changed an already prepared frame");
			u.PrepareFrameGenerationInputs();
			Check(!u.ShouldUseFrameGenerationThisFrame(), "next blocked frame retained old inputs");
			u.settings.frameGenerationAllowInMenus = true;
			u.PrepareFrameGenerationInputs();
			Check(u.ShouldUseFrameGenerationThisFrame(), "allow-in-menus setting was ignored");
		}
	}

	void MissingInputs()
	{
		for (int failure = 0; failure < 10; ++failure) {
			auto& u = Reset();
			u.PrepareFrameGenerationInputs();
			Check(u.ShouldUseFrameGenerationThisFrame(), "valid inputs were not prepared");
			globals::d3d::contextStorage.copies = 0;
			globals::d3d::contextStorage.draws = 0;
			switch (failure) {
			case 0:
				u.vertexShaderReady = false;
				break;
			case 1:
				u.copyDepthToSharedBufferPS.ready = false;
				break;
			case 2:
				u.dx12SwapChain.depthBufferShared12.reset();
				break;
			case 3:
				u.dx12SwapChain.motionVectorBufferShared12.reset();
				break;
			case 4:
				globals::game::renderer = nullptr;
				break;
			case 5:
				globals::d3d::context = nullptr;
				break;
			case 6:
				globals::state = nullptr;
				break;
			case 7:
				globals::game::renderer->runtime.renderTargets[0].texture = nullptr;
				break;
			case 8:
				globals::game::renderer->depth.depthStencils[0].depthSRV = nullptr;
				break;
			case 9:
				u.upscaleBlendState.value = nullptr;
				break;
			}
			u.PrepareFrameGenerationInputs();
			Check(!u.ShouldUseFrameGenerationThisFrame(), "failed copy retained readiness");
			Check(globals::d3d::contextStorage.copies == 0 && globals::d3d::contextStorage.draws == 0,
				"missing input allowed a partial copy");
		}
	}

	void SafetyGates()
	{
		for (int gate = 0; gate < 5; ++gate) {
			auto& u = Reset();
			u.PrepareFrameGenerationInputs();
			switch (gate) {
			case 0:
				globals::game::isVR = true;
				break;
			case 1:
				u.d3d12SwapChainActive = false;
				break;
			case 2:
				u.settings.frameGenerationMode = 0;
				break;
			case 3:
				u.fidelityFX.ready = false;
				break;
			case 4:
				u.streamline.quarantined = true;
				break;
			}
			Check(!u.ShouldUseFrameGenerationThisFrame(), "latched inputs bypassed a live safety gate");
			globals::d3d::context->copies = 0;
			u.PrepareFrameGenerationInputs();
			Check(!u.ShouldUseFrameGenerationThisFrame() && globals::d3d::context->copies == 0,
				"disabled runtime prepared frame generation");
		}
	}

	void PresentLifetime()
	{
		for (bool present1 : { false, true }) {
			auto& u = Reset();
			auto& chain = u.dx12SwapChain;
			DXGI_PRESENT_PARAMETERS parameters;
			auto present = [&](UINT flags) {
				return present1 ? chain.Present1(0, flags, &parameters) : chain.Present(0, flags);
			};
			u.PrepareFrameGenerationInputs();
			Check(globals::d3d::context->copies == 1 && globals::d3d::context->draws == 1,
				"successful preparation did not copy both inputs");
			Check(present(DXGI_PRESENT_TEST) == S_OK && u.ShouldUseFrameGenerationThisFrame(),
				"test present consumed inputs");
			Check(u.fidelityFX.presents == 0 && chain.gpu.clears == 0, "test present performed frame work");
			chain.backend.result = DXGI_ERROR_WAS_STILL_DRAWING;
			Check(present(0) == DXGI_ERROR_WAS_STILL_DRAWING && u.ShouldUseFrameGenerationThisFrame(),
				"retryable present consumed inputs");
			Check(chain.gpu.clears == 0, "retryable present cleared UI");
			chain.backend.result = S_OK;
			Check(present(0) == S_OK && u.fidelityFX.lastUse, "prepared frame did not generate");
			Check(u.limiterUsedGeneration, "readiness was consumed before the frame limiter");
			Check(!u.ShouldUseFrameGenerationThisFrame() && chain.gpu.clears == 1,
				"completed present retained readiness or missed UI clearing");
			Check(present(0) == S_OK && !u.fidelityFX.lastUse, "second present reused consumed inputs");
			u.PrepareFrameGenerationInputs();
			u.InvalidateFrameGenerationInputs();
			Check(present(0) == S_OK && !u.fidelityFX.lastUse, "invalidated resources retained readiness");
		}
	}

	void FailedPresentation()
	{
		for (int failure = 0; failure < 6; ++failure) {
			auto& u = Reset();
			auto& chain = u.dx12SwapChain;
			u.PrepareFrameGenerationInputs();
			switch (failure) {
			case 0:
				chain.swapChain = nullptr;
				break;
			case 1:
				chain.runtimeQuarantined = true;
				break;
			case 2:
				chain.backend.result = E_FAIL;
				break;
			case 3:
				chain.gpu.closeResult = E_FAIL;
				break;
			case 4:
				chain.backend.throwOnPresent = true;
				break;
			case 5:
				u.fidelityFX.presentResult = false;
				u.fidelityFX.disableConfirmed = false;
				break;
			}
			Check(FAILED(chain.Present(0, 0)), "injected failure did not fail presentation");
			Check(!u.ShouldUseFrameGenerationThisFrame(), "failed presentation retained readiness");
		}
		auto& u = Reset();
		u.PrepareFrameGenerationInputs();
		u.streamline.disableSucceeds = false;
		Check(u.dx12SwapChain.Present(0, 0) == S_OK && !u.fidelityFX.lastUse,
			"failed Reflex disable did not fall back to base presentation");
		Check(!u.ShouldUseFrameGenerationThisFrame(), "Reflex quarantine retained generation");
	}
}

int main()
{
	try {
		RuntimeSettings();
		MenuTransitions();
		MissingInputs();
		SafetyGates();
		PresentLifetime();
		FailedPresentation();
		std::cout << "Frame-generation input readiness checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
