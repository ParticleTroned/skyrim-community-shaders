#include "Features/Upscaling/FSRSharedGuidePolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
	struct Effects
	{
		uint32_t graphicsCreates = 0, wrappedCreates = 0, wrappedDestroys = 0;
		uint32_t sharedHandles = 0, retainedSources = 0, drainInvalidations = 0, idlePolls = 0;
		bool Empty() const
		{
			return !graphicsCreates && !wrappedCreates && !wrappedDestroys &&
			       !sharedHandles && !retainedSources && !drainInvalidations && !idlePolls;
		}
	} effects;
}

using UINT = uint32_t;
constexpr uint32_t D3D11_USAGE_DEFAULT = 0;
constexpr uint32_t D3D11_BIND_UNORDERED_ACCESS = 8;
constexpr uint32_t D3D11_RESOURCE_MISC_SHARED = 1;
constexpr uint32_t D3D11_RESOURCE_MISC_SHARED_NTHANDLE = 2;
constexpr uint32_t D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX = 4;
constexpr uint32_t D3D12_COMMAND_LIST_TYPE_DIRECT = 0;
constexpr uint32_t D3D12_FENCE_FLAG_SHARED = 1;
constexpr uint32_t GENERIC_ALL = 0;
#define IID_PPV_ARGS(pointer) pointer

struct D3D11_TEXTURE2D_DESC
{
	uint32_t Width = 100, Height = 80, MipLevels = 1, ArraySize = 1, Format = 28;
	struct Sample
	{
		uint32_t Count = 1, Quality = 0;
	} SampleDesc;
	uint32_t Usage = 0, BindFlags = 0, CPUAccessFlags = 0, MiscFlags = 0;
};

struct GraphicsObject
{
	int CreateCommandAllocator(uint32_t, GraphicsObject** a_result) { return Create(a_result); }
	int CreateCommandList(uint32_t, uint32_t, GraphicsObject*, void*, GraphicsObject** a_result) { return Create(a_result); }
	int CreateFence(uint32_t, uint32_t, GraphicsObject** a_result) { return Create(a_result); }
	int OpenSharedFence(void*, GraphicsObject** a_result) { return Create(a_result); }
	int CreateSharedHandle(GraphicsObject*, void*, uint32_t, void*, void** a_result)
	{
		++effects.graphicsCreates;
		*a_result = this;
		return 0;
	}
	int QueryInterface(GraphicsObject** a_result)
	{
		*a_result = this;
		return 0;
	}
	int GetAdapter(GraphicsObject** a_result)
	{
		*a_result = this;
		return 0;
	}
	void GetDevice(GraphicsObject** a_result);
	int Close() { return 0; }
	int Create(GraphicsObject** a_result)
	{
		++effects.graphicsCreates;
		*a_result = this;
		return 0;
	}
};
using ID3D11Resource = GraphicsObject;
using ID3D11Texture2D = GraphicsObject;
using ID3D11Device = GraphicsObject;
using IDXGIDevice = GraphicsObject;
using IDXGIAdapter = GraphicsObject;
using ID3D12CommandAllocator = GraphicsObject;
using ID3D12GraphicsCommandList4 = GraphicsObject;
GraphicsObject graphicsDevice, graphicsContext;
void GraphicsObject::GetDevice(GraphicsObject** a_result) { *a_result = &graphicsDevice; }

namespace winrt
{
	template <class T>
	struct com_ptr
	{
		T* value = nullptr;
		T* get() const { return value; }
		T** put() { return &value; }
		T** operator&() { return &value; }
		T* operator->() const { return value; }
		explicit operator bool() const { return value != nullptr; }
		bool operator!=(std::nullptr_t) const { return value != nullptr; }
		com_ptr& operator=(std::nullptr_t)
		{
			value = nullptr;
			return *this;
		}
		void copy_from(T* a_value)
		{
			++effects.retainedSources;
			value = a_value;
		}
	};
	struct handle
	{
		void* value = nullptr;
		void* get() const { return value; }
		void** put() { return &value; }
	};
	struct hresult_error : std::runtime_error
	{
		using std::runtime_error::runtime_error;
		uint32_t code() const { return 1; }
	};
}
namespace DX
{
	void ThrowIfFailed(int a_result)
	{
		if (a_result)
			throw std::runtime_error("Graphics failure");
	}
}
namespace logger
{
	template <class... Args>
	void error(std::string_view, Args&&...)
	{}
	template <class... Args>
	void warn(std::string_view, Args&&...)
	{}
}
namespace CSX::NvidiaComIdentity
{
	bool IsSame(const GraphicsObject* a_left, const GraphicsObject* a_right) { return a_left == a_right; }
}

struct WrappedResource
{
	GraphicsObject storage11, storage12;
	winrt::com_ptr<GraphicsObject> resource11{ &storage11 }, resource{ &storage12 };
	WrappedResource(const D3D11_TEXTURE2D_DESC&, GraphicsObject*, GraphicsObject*) { ++effects.wrappedCreates; }
	WrappedResource(GraphicsObject*, GraphicsObject*, void*) { ++effects.wrappedCreates; }
	~WrappedResource() { ++effects.wrappedDestroys; }
};
struct Texture2D
{
	GraphicsObject storage;
	winrt::com_ptr<GraphicsObject> resource{ &storage };
	void* GetOrCreateSharedHandle()
	{
		++effects.sharedHandles;
		return this;
	}
};
struct Upscaling
{
	struct SwapChain
	{
		winrt::com_ptr<GraphicsObject> d3d11Device, d3d11Context, d3d12Device, commandQueue;
		void SetD3D11Device(GraphicsObject* a_device)
		{
			++effects.graphicsCreates;
			d3d11Device.value = a_device;
		}
		void SetD3D11DeviceContext(GraphicsObject* a_context)
		{
			++effects.graphicsCreates;
			d3d11Context.value = a_context;
		}
		void CreateD3D12Device(GraphicsObject* a_device)
		{
			++effects.graphicsCreates;
			d3d12Device.value = a_device;
			commandQueue.value = a_device;
		}
	} dx12SwapChain;
	bool reuseOnly = false;
	bool ShouldReuseOrdinarySaveResources() const { return reuseOnly; }
	std::array<std::unique_ptr<Texture2D>, 2> vrIntermediateLinearDepth, vrIntermediateMotionVectors;
	std::array<std::unique_ptr<Texture2D>, 2> vrIntermediateReactiveMask, vrIntermediateTransparencyMask;
};
namespace globals
{
	namespace features
	{
		Upscaling upscaling;
	}
	namespace d3d
	{
		GraphicsObject* device = &graphicsDevice;
		GraphicsObject* context = &graphicsContext;
	}
	namespace game
	{
		bool isVR = true;
	}
}

struct FidelityFX
{
	enum class LifecycleResult
	{
		Ready,
		Pending,
		Failed
	};
	struct RuntimeCommandContext
	{
		winrt::com_ptr<GraphicsObject> commandAllocator, commandList;
		uint64_t fenceValue = 0;
	};
	std::array<RuntimeCommandContext, 3> runtimeCommandContexts;
	winrt::com_ptr<GraphicsObject> runtimeD3D11Fence, runtimeD3D12Fence;
	uint64_t runtimeFenceValue = 0;
	uint32_t runtimeCommandContextCursor = 0;
	using RuntimeWrappedResources = std::array<std::unique_ptr<WrappedResource>, 2>;
	RuntimeWrappedResources runtimeColorShared, runtimeDepthShared, runtimeMotionShared;
	RuntimeWrappedResources runtimeReactiveShared, runtimeTransparencyShared, runtimeOutputShared;
	D3D11_TEXTURE2D_DESC runtimeColorSharedDesc, runtimeDepthSharedDesc, runtimeMotionSharedDesc;
	D3D11_TEXTURE2D_DESC runtimeReactiveSharedDesc, runtimeTransparencySharedDesc, runtimeOutputSharedDesc;
	struct ImportedGuide
	{
		winrt::com_ptr<GraphicsObject> source;
		std::unique_ptr<WrappedResource> imported;
	};
	std::array<std::array<ImportedGuide, FSRSharedGuidePolicy::kGuideCount>, 2> runtimeSharedGuideImports;
	uint32_t runtimeUpscalerMaxRenderWidth = 100, runtimeUpscalerMaxRenderHeight = 80;
	std::array<bool, 2> runtimeUpscalerContexts{ true, true };
	struct Module
	{
		bool CreateContext = true, DestroyContext = true;
	} ffxModule;
	bool contextsCompatible = true;
	uint32_t contextRecreationAdmissions = 0;
	LifecycleResult pendingFenceResponse = LifecycleResult::Ready;
	bool AreRuntimeSharedGuideInputsEnabled() const { return true; }
	void InvalidateFSRRelatchDrain() { ++effects.drainInvalidations; }
	LifecycleResult ResolveRuntimeUpscalerLifecycleFailure(const char*) { return LifecycleResult::Failed; }
	LifecycleResult PollRuntimeUpscalerTeardownReady(const char*)
	{
		++effects.idlePolls;
		return LifecycleResult::Ready;
	}
	LifecycleResult PollPendingRuntimeUpscalerTeardownFence(const char*) { return pendingFenceResponse; }
	void RecordRuntimeProviderResult(bool) {}
	bool AreRuntimeUpscalerContextsCompatible(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) const { return contextsCompatible; }
	LifecycleResult DestroyRuntimeUpscalerResources(bool)
	{
		for (auto* resources : Resources())
			for (auto& resource : *resources) resource.reset();
		return LifecycleResult::Ready;
	}
	auto Resources()
	{
		return std::array{ &runtimeColorShared, &runtimeDepthShared, &runtimeMotionShared,
			&runtimeReactiveShared, &runtimeTransparencyShared, &runtimeOutputShared };
	}
	auto Descriptors()
	{
		return std::array{ &runtimeColorSharedDesc, &runtimeDepthSharedDesc, &runtimeMotionSharedDesc,
			&runtimeReactiveSharedDesc, &runtimeTransparencySharedDesc, &runtimeOutputSharedDesc };
	}
	LifecycleResult EnsureRuntimeCommandContexts();
	LifecycleResult EnsureRuntimeUpscalerInterop();
	bool IsRuntimeUpscalerInteropReady() const;
	bool HasCompleteRuntimeUpscalerSharedResources(uint32_t) const;
	LifecycleResult EnsureRuntimeUpscalerContexts(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
	LifecycleResult EnsureRuntimeUpscalerSharedResources(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
		const D3D11_TEXTURE2D_DESC&, const D3D11_TEXTURE2D_DESC&, const D3D11_TEXTURE2D_DESC&,
		const D3D11_TEXTURE2D_DESC&, const D3D11_TEXTURE2D_DESC&, const D3D11_TEXTURE2D_DESC&);
	WrappedResource* ResolveRuntimeSharedGuide(uint32_t, FSRSharedGuidePolicy::Guide, ID3D11Resource*, const D3D11_TEXTURE2D_DESC&);
};

// Compile actual resource creation and import bodies against graphics effect counters.
#include "fsr_save_reuse_under_test.h"

namespace
{
	using Result = FidelityFX::LifecycleResult;
	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}
	Result EnsureShared(FidelityFX& a_fsr, uint32_t a_eyes = 2)
	{
		const D3D11_TEXTURE2D_DESC desc;
		return a_fsr.EnsureRuntimeUpscalerSharedResources(a_eyes, 100, 80, 200, 160, desc, desc, desc, desc, desc, desc);
	}
	void Initialize(FidelityFX& a_fsr)
	{
		globals::features::upscaling = {};
		globals::d3d::device = &graphicsDevice;
		globals::d3d::context = &graphicsContext;
		Check(EnsureShared(a_fsr) == Result::Ready, "Normal path must create a complete resource set");
		Check(effects.wrappedCreates == 12, "Normal path must create six surfaces per eye");
		globals::features::upscaling.reuseOnly = true;
		effects = {};
	}
	void TestSharedResources()
	{
		{
			FidelityFX fsr;
			effects = {};
			Initialize(fsr);
			Check(EnsureShared(fsr) == Result::Ready && effects.Empty(), "Exact shared resources must reuse without mutation");
			Check(EnsureShared(fsr, 1) == Result::Ready && effects.Empty(), "Single-eye reuse must retain the unused eye");
		}
		for (uint32_t surface = 0; surface < 6; ++surface) {
			for (uint32_t eye = 0; eye < 2; ++eye) {
				for (uint32_t missing = 0; missing < 3; ++missing) {
					FidelityFX fsr;
					effects = {};
					Initialize(fsr);
					auto& resource = (*fsr.Resources()[surface])[eye];
					if (missing == 0)
						resource.reset();
					if (missing == 1)
						resource->resource11 = nullptr;
					if (missing == 2)
						resource->resource = nullptr;
					auto* retained = fsr.runtimeColorShared[0].get();
					effects = {};
					Check(EnsureShared(fsr) == Result::Pending && effects.Empty(), "Missing shared surface must defer without mutation");
					Check(fsr.runtimeColorShared[0].get() == retained, "Later missing surface must preserve the first eye");
				}
			}
			for (uint32_t field = 0; field < 11; ++field) {
				FidelityFX fsr;
				effects = {};
				Initialize(fsr);
				auto& desc = *fsr.Descriptors()[surface];
				const std::array fields{ &desc.Width, &desc.Height, &desc.MipLevels, &desc.ArraySize, &desc.Format,
					&desc.SampleDesc.Count, &desc.SampleDesc.Quality, &desc.Usage, &desc.BindFlags, &desc.CPUAccessFlags, &desc.MiscFlags };
				++*fields[field];
				Check(EnsureShared(fsr) == Result::Pending && effects.Empty(), "Any shared descriptor mismatch must defer without mutation");
			}
		}
	}
	void TestInteropAndContexts()
	{
		for (uint32_t missing = 0; missing < 14; ++missing) {
			FidelityFX fsr;
			effects = {};
			Initialize(fsr);
			auto& swapChain = globals::features::upscaling.dx12SwapChain;
			std::array fields{ &globals::d3d::device, &globals::d3d::context,
				&swapChain.d3d11Device.value, &swapChain.d3d11Context.value,
				&swapChain.d3d12Device.value, &swapChain.commandQueue.value,
				&fsr.runtimeD3D11Fence.value, &fsr.runtimeD3D12Fence.value,
				&fsr.runtimeCommandContexts[0].commandAllocator.value, &fsr.runtimeCommandContexts[0].commandList.value,
				&fsr.runtimeCommandContexts[1].commandAllocator.value, &fsr.runtimeCommandContexts[1].commandList.value,
				&fsr.runtimeCommandContexts[2].commandAllocator.value, &fsr.runtimeCommandContexts[2].commandList.value };
			*fields[missing] = nullptr;
			Check(EnsureShared(fsr) == Result::Pending && effects.Empty(), "Incomplete interop must not lazily create devices, fences, or commands");
		}
		FidelityFX fsr;
		effects = {};
		Initialize(fsr);
		Check(fsr.EnsureRuntimeUpscalerContexts(100, 80, 200, 160, 2, 4) == Result::Ready && effects.Empty(), "Compatible contexts must reuse");
		fsr.contextsCompatible = false;
		Check(fsr.EnsureRuntimeUpscalerContexts(100, 80, 200, 160, 2, 4) == Result::Pending && effects.Empty(), "Incompatible contexts must defer");
		Check(!fsr.contextRecreationAdmissions, "Ordinary save must not admit provider recreation");
		globals::features::upscaling.reuseOnly = false;
		Check(fsr.EnsureRuntimeUpscalerContexts(100, 80, 200, 160, 2, 4) == Result::Ready && fsr.contextRecreationAdmissions == 1, "Normal path must retain provider recreation");
	}
	void TestGuideImports()
	{
		FidelityFX fsr;
		effects = {};
		Initialize(fsr);
		auto& upscaling = globals::features::upscaling;
		const std::array guides{ &upscaling.vrIntermediateLinearDepth, &upscaling.vrIntermediateMotionVectors,
			&upscaling.vrIntermediateReactiveMask, &upscaling.vrIntermediateTransparencyMask };
		D3D11_TEXTURE2D_DESC desc;
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		for (uint32_t eye = 0; eye < 2; ++eye) {
			for (uint32_t guide = 0; guide < FSRSharedGuidePolicy::kGuideCount; ++guide) {
				auto& texture = (*guides[guide])[eye];
				texture = std::make_unique<Texture2D>();
				auto resolve = [&] { return fsr.ResolveRuntimeSharedGuide(eye, static_cast<FSRSharedGuidePolicy::Guide>(guide), texture->resource.get(), desc); };
				Check(!resolve() && effects.Empty(), "Missing guide import must retain the existing copy fallback");
				Check(!fsr.runtimeSharedGuideImports[eye][guide].source, "Deferred import must not retain a rejected source");
				upscaling.reuseOnly = false;
				auto* imported = resolve();
				Check(imported && effects.wrappedCreates == 1 && effects.sharedHandles == 1, "Normal path must import a guide");
				upscaling.reuseOnly = true;
				effects = {};
				Check(resolve() == imported && effects.Empty(), "Matching imported guide must reuse");
				Texture2D replacement;
				texture->resource.value = replacement.resource.get();
				Check(!resolve() && effects.Empty(), "Changed guide identity must use the copy fallback");
				Check(fsr.runtimeSharedGuideImports[eye][guide].imported.get() == imported, "Changed guide must retain the old import");
			}
		}
	}
}

int main()
{
	try {
		TestSharedResources();
		TestInteropAndContexts();
		TestGuideImports();
		std::cout << "FSR save resource reuse passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
