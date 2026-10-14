#pragma once

// Substitute the engine registry while executing production preparation against D3D11.
namespace RE
{
	enum class RENDER_TARGETS_DEPTHSTENCIL
	{
		kVOLUMETRIC_LIGHTING_SHADOWMAPS_ESRAM
	};
}
struct ShadowTestRenderer
{
	struct DepthStencil
	{
		ID3D11ShaderResourceView* depthSRV = nullptr;
	};
	struct DepthData
	{
		std::array<DepthStencil, 1> storage;
		DepthStencil& operator[](RE::RENDER_TARGETS_DEPTHSTENCIL) { return storage[0]; }
	};
	struct Data
	{
		DepthData depthStencils;
	} data;
	Data& GetDepthStencilData() { return data; }
};
struct ShadowTestDevice
{
	ID3D11Device* device;
	unsigned failAfter = 0, allocationCalls = 0;
	HRESULT AllocationResult()
	{
		++allocationCalls;
		return failAfter != 0 && --failAfter == 0 ? E_OUTOFMEMORY : S_OK;
	}
	HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC* desc, ID3D11SamplerState** output)
	{
		const auto result = AllocationResult();
		return FAILED(result) ? result : device->CreateSamplerState(desc, output);
	}
	HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, ID3D11Texture2D** output)
	{
		const auto result = AllocationResult();
		return FAILED(result) ? result : device->CreateTexture2D(desc, data, output);
	}
	HRESULT CreateShaderResourceView(ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, ID3D11ShaderResourceView** output)
	{
		const auto result = AllocationResult();
		return FAILED(result) ? result : device->CreateShaderResourceView(resource, desc, output);
	}
	HRESULT CreateUnorderedAccessView(ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, ID3D11UnorderedAccessView** output)
	{
		const auto result = AllocationResult();
		return FAILED(result) ? result : device->CreateUnorderedAccessView(resource, desc, output);
	}
};
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F callback;
		~scope_exit() { callback(); }
	};
}
namespace logger
{
	inline unsigned allocationErrors = 0;
	inline void error(const char*, const char*) { ++allocationErrors; }
}
namespace globals
{
	struct State
	{
		bool directional = true;
		bool HasDirectionalShadows() const { return directional; }
	};
	inline State stateStorage;
	inline State* state = &stateStorage;
	namespace d3d
	{
		inline ShadowTestDevice* device = nullptr;
		inline ID3D11DeviceContext* context = nullptr;
	}
	namespace game
	{
		inline ShadowTestRenderer rendererStorage;
		inline ShadowTestRenderer* renderer = &rendererStorage;
	}
}
#include "volumetric_shadows_helpers.h"

namespace DX
{
	struct com_exception : std::runtime_error
	{
		explicit com_exception(HRESULT result) : std::runtime_error("D3D11 allocation HRESULT " + std::to_string(result)) {}
	};
	inline void ThrowIfFailed(HRESULT result)
	{
		if (FAILED(result))
			throw com_exception(result);
	}
}
namespace Util
{
	template <class T>
	struct LazyShader
	{
		T* value = nullptr;
		T* get() const { return value; }
		T* Get(const wchar_t*, std::initializer_list<std::pair<const char*, const char*>>, const char*, const char*, const char*) { return value; }
		void Reset() { value = nullptr; }
	};
}
struct VolumetricShadows
{
	struct Settings
	{
		bool Enabled = true;
	} settings;
	static constexpr uint32_t kSharedShadowMapShaderSlot = 18;
#include "volumetric_shadows_resources.h"
	ShadowTestDevice* resourceDevice = nullptr;
	~VolumetricShadows() { ReleaseResources(); }
	void SetupResources();
	void CompileComputeShaders();
	void EnsureFallbackShadowTexture();
	bool EnsureShadowCopyTextures();
	void CopyShadowLightData();
	static void SetSharedShadowMapSRV(ID3D11DeviceContext*, ID3D11ShaderResourceView*);
	void SetShaderResources(ID3D11DeviceContext*);
	void ReleaseComputeShaders();
	void ClearShaderCache();
	void PrepareShaderCache();
	void ReleaseShadowCopyTextures();
	void ReleaseTextures();
	void ReleaseSampler();
	void ReleaseResources();
};
#define ZoneScoped ((void)0)
#define CS_GPU_PASS(name) ((void)0)
#define CS_GPU_PASS_SELECT(condition, yes, no) ((void)(condition))
#include "volumetric_shadows_under_test.h"
#undef CS_GPU_PASS_SELECT
#undef CS_GPU_PASS
#undef ZoneScoped
