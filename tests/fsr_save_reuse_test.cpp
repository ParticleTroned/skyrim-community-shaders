#include "Features/Upscaling/FSRRuntimeLifecyclePolicy.h"
#include "Features/Upscaling/FSRSharedGuidePolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
constexpr uint32_t DXGI_FORMAT_R16G16B16A16_FLOAT = 10;
constexpr uint32_t DXGI_FORMAT_R11G11B10_FLOAT = 26;
constexpr uint32_t DXGI_FORMAT_R8G8B8A8_UNORM = 28;
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

struct D3D11_BOX
{
	uint32_t left, top, front, right, bottom, back;
};
struct GraphicsObject;
struct D3D12_RESOURCE_BARRIER
{
	GraphicsObject* resource;
	uint32_t before, after;
};
constexpr uint32_t D3D12_RESOURCE_STATE_COMMON = 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE = 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS = 2;
constexpr uint32_t FFX_API_RESOURCE_STATE_COMPUTE_READ = 1;
struct CD3DX12_RESOURCE_BARRIER
{
	static D3D12_RESOURCE_BARRIER Transition(GraphicsObject* a_resource, uint32_t a_before, uint32_t a_after)
	{
		return { a_resource, a_before, a_after };
	}
};
struct CopyCall
{
	GraphicsObject* destination;
	GraphicsObject* source;
	D3D11_BOX box;
};
std::vector<CopyCall> copies;
std::vector<D3D12_RESOURCE_BARRIER> barriers;
std::array<GraphicsObject*, 2> bindings{}, inputCaptures{}, outputCaptures{};
constexpr uint32_t FFX_API_RESOURCE_USAGE_READ_ONLY = 0;
struct TestApiResource
{
	GraphicsObject* resource;
	struct
	{
		uint32_t usage = 1;
	} description;
};
TestApiResource ffxApiGetResourceDX12(GraphicsObject* a_resource, uint32_t a_state)
{
	if (a_state != FFX_API_RESOURCE_STATE_COMPUTE_READ)
		throw std::runtime_error("Colour input was not bound read-only");
	return { a_resource };
}
struct TestDispatchParameters
{
	TestApiResource color{};
};
namespace Probe
{
	template <class... Args>
	void CaptureVendorStage(bool a_output, uint32_t a_eye, GraphicsObject* a_resource, Args&&...)
	{
		(a_output ? outputCaptures : inputCaptures)[a_eye] = a_resource;
	}
}
struct GraphicsObject
{
	GraphicsObject* deviceOverride = nullptr;
	void CopySubresourceRegion(GraphicsObject* a_destination, uint32_t, uint32_t, uint32_t, uint32_t,
		GraphicsObject* a_source, uint32_t, const D3D11_BOX* a_box)
	{
		copies.push_back({ a_destination, a_source, *a_box });
	}
	void ResourceBarrier(uint32_t a_count, const D3D12_RESOURCE_BARRIER* a_barriers)
	{
		barriers.insert(barriers.end(), a_barriers, a_barriers + a_count);
	}
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
	int SetName(const wchar_t*) { return 0; }
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
void GraphicsObject::GetDevice(GraphicsObject** a_result) { *a_result = deviceOverride ? deviceOverride : &graphicsDevice; }

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
	uint32_t comparisons = 0;
	bool IsSame(const GraphicsObject* a_left, const GraphicsObject* a_right)
	{
		++comparisons;
		return a_left == a_right;
	}
}
namespace Util
{
	void SetResourceName(GraphicsObject*, const char*) {}
}

bool failImport = false;
struct WrappedResource
{
	GraphicsObject storage11, storage12;
	winrt::com_ptr<GraphicsObject> resource11{ &storage11 }, resource{ &storage12 }, srv, rtv, uav;
	WrappedResource(const D3D11_TEXTURE2D_DESC&, GraphicsObject*, GraphicsObject*, const std::string&) { ++effects.wrappedCreates; }
	WrappedResource(GraphicsObject* a_source, GraphicsObject*, const std::string&, void*)
	{
		++effects.wrappedCreates;
		if (failImport)
			throw std::runtime_error("Import failed");
		resource11.value = a_source;
	}
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
	std::array<std::unique_ptr<Texture2D>, 2> vrIntermediateColorIn, vrIntermediateLinearDepth, vrIntermediateMotionVectors;
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

struct TestRegion
{
	uint32_t contextIndex;
	GraphicsObject* color;
	GraphicsObject* depth;
	GraphicsObject* motionVectors;
	GraphicsObject* reactiveMask;
	GraphicsObject* transparencyCompositionMask;
	uint32_t renderWidth = 70, renderHeight = 50, displayWidth = 200, displayHeight = 160;
};
struct TestDescriptions
{
	D3D11_TEXTURE2D_DESC color, depth, motion, reactive, transparency;
};

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
	std::array<std::array<ImportedGuide, FSRSharedGuidePolicy::kInputCount>, 2> runtimeSharedInputImports;
	uint32_t runtimeUpscalerMaxRenderWidth = 100, runtimeUpscalerMaxRenderHeight = 80;
	std::array<GraphicsObject*, 2> runtimeUpscalerContexts{ &graphicsDevice, &graphicsDevice };
	std::array<bool, 2> runtimeUpscalerContextIndeterminate{};
	uint32_t runtimeUpscalerContextCount = 0;
	uint64_t pendingRuntimeTeardownD3D11FenceValue = 0, pendingRuntimeTeardownD3D12FenceValue = 0;
	bool IsRuntimeUpscalerOwnershipDetached() const { return false; }
	bool HasRuntimeUpscalerResources() const;
	struct Module
	{
		bool CreateContext = true, DestroyContext = true;
	} ffxModule;
	bool contextsCompatible = true;
	uint32_t contextRecreationAdmissions = 0;
	LifecycleResult pendingFenceResponse = LifecycleResult::Ready;
	LifecycleResult idleResponse = LifecycleResult::Ready;
	bool sharedInputsEnabled = true;
	bool AreRuntimeSharedGuideInputsEnabled() const { return sharedInputsEnabled; }
	void InvalidateFSRRelatchDrain() { ++effects.drainInvalidations; }
	LifecycleResult ResolveRuntimeUpscalerLifecycleFailure(const char*) { return LifecycleResult::Failed; }
	LifecycleResult PollRuntimeUpscalerTeardownReady(const char*)
	{
		++effects.idlePolls;
		return idleResponse;
	}
	LifecycleResult PollPendingRuntimeUpscalerTeardownFence(const char*) { return pendingFenceResponse; }
	void RecordRuntimeProviderResult(bool) {}
	bool AreRuntimeUpscalerContextsCompatible(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) const { return contextsCompatible; }
	auto Resources()
	{
		return std::array{ &runtimeColorShared, &runtimeDepthShared, &runtimeMotionShared,
			&runtimeReactiveShared, &runtimeTransparencyShared, &runtimeOutputShared };
	}
	LifecycleResult DestroyRuntimeUpscalerResources(bool);
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
	void TestDispatchInputs(std::span<const TestRegion>, std::span<const TestDescriptions>);
	WrappedResource* ResolveRuntimeSharedInput(uint32_t, std::size_t, ID3D11Resource*, const D3D11_TEXTURE2D_DESC&);
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
			&upscaling.vrIntermediateReactiveMask, &upscaling.vrIntermediateTransparencyMask, &upscaling.vrIntermediateColorIn };
		D3D11_TEXTURE2D_DESC desc;
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		for (uint32_t eye = 0; eye < 2; ++eye) {
			for (uint32_t guide = 0; guide < FSRSharedGuidePolicy::kInputCount; ++guide) {
				auto& texture = (*guides[guide])[eye];
				texture = std::make_unique<Texture2D>();
				auto resolve = [&] { return fsr.ResolveRuntimeSharedInput(eye, guide, texture->resource.get(), desc); };
				Check(!resolve() && effects.Empty(), "Missing guide import must retain the existing copy fallback");
				Check(!fsr.runtimeSharedInputImports[eye][guide].source, "Deferred import must not retain a rejected source");
				upscaling.reuseOnly = false;
				auto* imported = resolve();
				Check(imported && effects.wrappedCreates == 1 && effects.sharedHandles == 1, "Normal path must import a guide");
				upscaling.reuseOnly = true;
				effects = {};
				Check(resolve() == imported && effects.Empty(), "Matching imported input must reuse");
				fsr.sharedInputsEnabled = false;
				CSX::NvidiaComIdentity::comparisons = 0;
				Check(!resolve() && effects.Empty() && fsr.runtimeSharedInputImports[eye][guide].imported.get() == imported,
					"Disabling direct selection must retain ownership");
				Check(CSX::NvidiaComIdentity::comparisons == 0, "Disabled sharing performed identity work");
				fsr.sharedInputsEnabled = true;
				Check(resolve() == imported && effects.Empty(), "Re-enabling must reuse the retained import");
				Texture2D replacement;
				texture->resource.value = replacement.resource.get();
				Check(!resolve() && effects.Empty(), "Changed guide identity must use the copy fallback");
				Check(fsr.runtimeSharedInputImports[eye][guide].imported.get() == imported, "Changed guide must retain the old import");
			}
		}
	}
	void TestColorEligibilityAndFailures()
	{
		using namespace FSRSharedGuidePolicy;
		for (uint32_t invalid = 0; invalid < 16; ++invalid) {
			FidelityFX fsr;
			effects = {};
			Initialize(fsr);
			auto& upscaling = globals::features::upscaling;
			upscaling.reuseOnly = false;
			auto& owner = upscaling.vrIntermediateColorIn[0];
			owner = std::make_unique<Texture2D>();
			D3D11_TEXTURE2D_DESC desc;
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
			GraphicsObject other;
			auto* source = owner->resource.get();
			uint32_t eye = 0;
			switch (invalid) {
			case 0:
				++desc.Width;
				break;
			case 1:
				++desc.Height;
				break;
			case 2:
				++desc.MipLevels;
				break;
			case 3:
				++desc.ArraySize;
				break;
			case 4:
				++desc.SampleDesc.Count;
				break;
			case 5:
				++desc.SampleDesc.Quality;
				break;
			case 6:
				++desc.Usage;
				break;
			case 7:
				++desc.CPUAccessFlags;
				break;
			case 8:
				desc.MiscFlags &= ~D3D11_RESOURCE_MISC_SHARED;
				break;
			case 9:
				desc.MiscFlags &= ~D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
				break;
			case 10:
				desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
				break;
			case 11:
				++desc.Format;
				break;
			case 12:
				source = &other;
				break;
			case 13:
				source->deviceOverride = &other;
				break;
			case 14:
				eye = 2;
				break;
			case 15:
				globals::game::isVR = false;
				break;
			}
			Check(!fsr.ResolveRuntimeSharedInput(eye, kColorInputIndex, source, desc), "Ineligible colour must copy");
			Check(!effects.wrappedCreates && !effects.sharedHandles, "Ineligible colour opened an import");
			effects = {};
			Check(!fsr.ResolveRuntimeSharedInput(eye, kColorInputIndex, source, desc) && effects.Empty(),
				"Ineligible colour repeated import work");
			globals::game::isVR = true;
		}
		for (uint32_t format : { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM }) {
			FidelityFX fsr;
			effects = {};
			Initialize(fsr);
			auto& upscaling = globals::features::upscaling;
			upscaling.reuseOnly = false;
			upscaling.vrIntermediateColorIn[0] = std::make_unique<Texture2D>();
			auto* source = upscaling.vrIntermediateColorIn[0]->resource.get();
			D3D11_TEXTURE2D_DESC desc;
			desc.Format = fsr.runtimeColorSharedDesc.Format = format;
			desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
			failImport = true;
			Check(!fsr.ResolveRuntimeSharedInput(0, kColorInputIndex, source, desc), "Failed import must copy");
			Check(effects.wrappedCreates == 1 && fsr.runtimeSharedInputImports[0][kColorInputIndex].source.get() == source,
				"Failed import must retain its identity");
			failImport = false;
			effects = {};
			Check(!fsr.ResolveRuntimeSharedInput(0, kColorInputIndex, source, desc) && effects.Empty(),
				"Failed colour import retried");
			FidelityFX fresh;
			fresh.runtimeColorSharedDesc.Format = format;
			Check(fresh.ResolveRuntimeSharedInput(0, kColorInputIndex, source, desc) != nullptr, "Eligible colour format rejected");
		}
	}

	void TestRetainedColorRetirement()
	{
		for (bool imported : { false, true }) {
			FidelityFX fsr;
			fsr.runtimeUpscalerContexts = {};
			Check(!fsr.HasRuntimeUpscalerResources(), "Empty provider needs no retirement");
			GraphicsObject source;
			auto& color = fsr.runtimeSharedInputImports[1][FSRSharedGuidePolicy::kColorInputIndex];
			color.source.value = &source;
			if (imported)
				color.imported = std::make_unique<WrappedResource>(&source, &graphicsDevice, "Colour", nullptr);
			Check(fsr.HasRuntimeUpscalerResources(), "Retained colour identity was omitted from retirement");
			fsr.sharedInputsEnabled = false;
			fsr.idleResponse = Result::Pending;
			Check(fsr.DestroyRuntimeUpscalerResources(true) == Result::Pending && color.source.get() == &source &&
					  static_cast<bool>(color.imported) == imported,
				"Pending retirement released retained colour");
			fsr.idleResponse = Result::Ready;
			fsr.runtimeUpscalerContextIndeterminate[1] = true;
			Check(fsr.DestroyRuntimeUpscalerResources(true) == Result::Failed && color.source.get() == &source &&
					  static_cast<bool>(color.imported) == imported,
				"Indeterminate provider released retained colour");
			fsr.runtimeUpscalerContextIndeterminate[1] = false;
			Check(fsr.DestroyRuntimeUpscalerResources(true) == Result::Ready && !color.source && !color.imported &&
					  !fsr.HasRuntimeUpscalerResources(),
				"Safe retirement did not release retained colour");
		}
	}

	void TestSelectedColorConsumers()
	{
		for (uint32_t directMask = 0; directMask < 4; ++directMask) {
			FidelityFX fsr;
			effects = {};
			Initialize(fsr);
			auto& upscaling = globals::features::upscaling;
			upscaling.reuseOnly = false;
			std::array<TestRegion, 2> regions{};
			std::array<TestDescriptions, 2> descriptions{};
			const std::array owners{ &upscaling.vrIntermediateLinearDepth, &upscaling.vrIntermediateMotionVectors,
				&upscaling.vrIntermediateReactiveMask, &upscaling.vrIntermediateTransparencyMask, &upscaling.vrIntermediateColorIn };
			for (uint32_t eye = 0; eye < 2; ++eye) {
				std::array<GraphicsObject*, FSRSharedGuidePolicy::kInputCount> sources{};
				for (size_t input = 0; input < owners.size(); ++input) {
					(*owners[input])[eye] = std::make_unique<Texture2D>();
					sources[input] = (*owners[input])[eye]->resource.get();
				}
				regions[eye] = { eye, sources[4], sources[0], sources[1], sources[2], sources[3] };
				auto& desc = descriptions[eye];
				for (auto* item : { &desc.depth, &desc.motion, &desc.reactive, &desc.transparency })
					item->MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
				if (directMask & (1u << eye))
					desc.color.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
			}
			for (uint32_t activeWidth : { 70u, 100u, 30u }) {
				for (uint32_t mode = 0; mode < 3; ++mode) {
					for (auto& region : regions) region.renderWidth = activeWidth;
					const uint32_t first = mode == 2 ? 1 : 0;
					const uint32_t count = mode == 0 ? 2 : 1;
					copies.clear();
					barriers.clear();
					bindings = {};
					inputCaptures = {};
					outputCaptures = {};
					fsr.TestDispatchInputs(std::span(regions).subspan(first, count), std::span(descriptions).subspan(first, count));
					uint32_t expectedCopies = 0;
					for (uint32_t eye = first; eye < first + count; ++eye) {
						const bool direct = (directMask & (1u << eye)) != 0;
						auto* selected = direct ? fsr.runtimeSharedInputImports[eye][FSRSharedGuidePolicy::kColorInputIndex].imported.get() : fsr.runtimeColorShared[eye].get();
						Check(bindings[eye] == selected->resource.get(), "Provider bound stale colour staging");
						Check(inputCaptures[eye] == selected->resource11.get(), "Input capture did not observe submitted colour");
						Check(outputCaptures[eye] == fsr.runtimeOutputShared[eye]->resource11.get(), "Output capture changed resource");
						const auto offset = (eye - first) * 6;
						const auto& begin = barriers[offset];
						const auto& end = barriers[count * 6 + offset];
						Check(begin.resource == selected->resource.get() && end.resource == begin.resource &&
								  begin.before == D3D12_RESOURCE_STATE_COMMON && begin.after == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE &&
								  end.before == begin.after && end.after == begin.before,
							"Selected colour barriers disagree");
						expectedCopies += direct ? 0 : 1;
					}
					Check(copies.size() == expectedCopies && barriers.size() == count * 12, "Direct selection changed copy or barrier counts");
					for (const auto& copy : copies)
						Check(copy.box.left == 0 && copy.box.top == 0 && copy.box.front == 0 &&
								  copy.box.right == activeWidth && copy.box.bottom == 50 && copy.box.back == 1,
							"Fallback changed the active rectangle");
				}
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
		TestColorEligibilityAndFailures();
		TestSelectedColorConsumers();
		TestRetainedColorRetirement();
		std::cout << "FSR save resource reuse passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
