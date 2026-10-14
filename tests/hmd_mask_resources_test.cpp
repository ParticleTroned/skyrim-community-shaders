#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace logger = spdlog;
#include "Utils/LazyShader.h"
#include "d3d_resource_naming.h"

namespace
{
	unsigned compileCalls{}, bufferCalls{}, liveShaders{}, liveBuffers{};
	unsigned deviceFailureChecks{}, deviceRemovalChecks{};
	bool failCompile{}, throwCompile{}, throwUnknownCompile{}, failBuffer{};
	bool bufferOnFailure{}, nullBufferOnSuccess{}, deviceRemoved{}, deviceLossRecorded{};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	template <class Interface>
	class TestDeviceChild : public Interface
	{
	public:
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override
		{
			if (!result)
				return E_POINTER;
			*result = nullptr;
			if (id != __uuidof(IUnknown) && id != __uuidof(ID3D11DeviceChild) && id != __uuidof(Interface) &&
				!(std::is_base_of_v<ID3D11Resource, Interface> && id == __uuidof(ID3D11Resource)))
				return E_NOINTERFACE;
			*result = static_cast<Interface*>(this);
			AddRef();
			return S_OK;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const auto remaining = --references;
			if (!remaining)
				delete this;
			return remaining;
		}
		void STDMETHODCALLTYPE GetDevice(ID3D11Device** device) override { *device = nullptr; }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT size, const void* data) override
		{
			if (size > debugName.size())
				return E_INVALIDARG;
			std::memcpy(debugName.data(), data, size);
			debugNameSize = size;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
		std::string_view Name() const { return { debugName.data(), debugNameSize }; }

	protected:
		virtual ~TestDeviceChild() = default;

	private:
		ULONG references = 1;
		std::array<char, 128> debugName{};
		UINT debugNameSize{};
	};

	class TestShader final : public TestDeviceChild<ID3D11ComputeShader>
	{
	public:
		TestShader() { ++liveShaders; }
		~TestShader() override { --liveShaders; }
	};

	class TestBuffer final : public TestDeviceChild<ID3D11Buffer>
	{
	public:
		explicit TestBuffer(const D3D11_BUFFER_DESC& description) : desc(description) { ++liveBuffers; }
		~TestBuffer() override { --liveBuffers; }
		void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* type) override { *type = D3D11_RESOURCE_DIMENSION_BUFFER; }
		void STDMETHODCALLTYPE SetEvictionPriority(UINT) override {}
		UINT STDMETHODCALLTYPE GetEvictionPriority() override { return 0; }
		void STDMETHODCALLTYPE GetDesc(D3D11_BUFFER_DESC* description) override { *description = desc; }

	private:
		D3D11_BUFFER_DESC desc;
	};

	struct TestDevice
	{
		HRESULT CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial, ID3D11Buffer** result)
		{
			++bufferCalls;
			*result = nullptr;
			Require(!initial && desc->ByteWidth == 32 && desc->Usage == D3D11_USAGE_DEFAULT &&
						desc->BindFlags == D3D11_BIND_CONSTANT_BUFFER && desc->CPUAccessFlags == 0,
				"HMD constants must retain their default-buffer upload contract");
			if (failBuffer) {
				if (bufferOnFailure)
					*result = new TestBuffer(*desc);
				return E_FAIL;
			}
			if (!nullBufferOnSuccess)
				*result = new TestBuffer(*desc);
			return S_OK;
		}
	} testDevice;

	struct UnrelatedShader
	{
		void Reset() {}
	};
	struct UnrelatedFeature
	{
		void ClearShaderCache() {}
	};
}

namespace globals
{
	namespace game
	{
		bool isVR = true;
	}
	namespace d3d
	{
		TestDevice* device = &::testDevice;
	}
}
namespace DX
{
	void ThrowIfFailed(HRESULT result)
	{
		if (FAILED(result))
			throw std::runtime_error("Injected buffer creation failure");
	}
}
namespace NeuralRendering
{
	struct Renderer
	{
		static Renderer& Instance()
		{
			static Renderer instance;
			return instance;
		}
		void ResetShaderCache() {}
	};
}

struct Upscaling
{
#include "hmd_mask_resources_members.h"

	bool reuseOrdinarySaveResources = false;
	bool ShouldReuseOrdinarySaveResources() const { return reuseOrdinarySaveResources; }
	bool EnsureHMDMaskClearResources();
	void ResetHMDMaskClearResources();
	void ClearShaderCache();
	void ResetVRMenuFinalCompositeLayer() {}
	void SetupResources()
	{
		auto* device = globals::d3d::device;
		static TestDevice* shaderDevice = nullptr;
#include "hmd_mask_device_change_under_test.h"
	}
	void ResetVRLoadPresentationHAMProbeShaderResources() {}
	bool MarkSubmitStageDeviceLostIfNeeded(const std::exception&, const char* context)
	{
		++deviceFailureChecks;
		return MarkSubmitStageDeviceLostIfDeviceRemoved(context);
	}
	bool MarkSubmitStageDeviceLostIfDeviceRemoved(const char*)
	{
		++deviceRemovalChecks;
		deviceLossRecorded |= deviceRemoved;
		return deviceRemoved;
	}

	UnrelatedShader encodeTexturesCS[5], encodeTexturesCSDepthOutput;
	UnrelatedShader depthRefractionUpscalePS, underwaterMaskUpscalePS;
	UnrelatedShader underwaterMaskUpscaleDynamicDepthNoStencilPS, underwaterMaskUpscaleRawDepthNoStencilPS;
	UnrelatedShader cameraMotionVectorsPS, upscaleVS, foveatedPeripheryCS;
	UnrelatedShader foveatedCenterBlendCS, foveatedSpatialCompositeCS, peripheryTAACS;
	UnrelatedShader submitStageStretchCS, vrDesktopMirrorBlitPS, vrMenuLayerCompositePS, copyDepthToSharedBufferPS;
	winrt::com_ptr<ID3D11RenderTargetView> vrDesktopMirrorBlitRTV;
	winrt::com_ptr<ID3D11Texture2D> vrDesktopMirrorBlitTarget;
	winrt::com_ptr<ID3D11ComputeShader> copyRawDepthRegionCS;
	winrt::com_ptr<ID3D11Buffer> copyRawDepthRegionCB;
	winrt::com_ptr<ID3D11ShaderResourceView> copyRawDepthRegionSourceSRV;
	UnrelatedFeature rcas, lumaSharpen;
};

template <class... Args>
void LogWarnOnce(bool& logged, const char*, Args&&...)
{
	logged = true;
}

#include "hmd_mask_resource_reset_under_test.h"
#include "hmd_mask_resources_constants.h"
#include "hmd_mask_resources_under_test.h"
#include "hmd_mask_shader_cache_under_test.h"

std::string Util::WStringToString(const std::wstring& value)
{
	std::string result;
	for (const auto character : value)
		result.push_back(static_cast<char>(character));
	return result;
}

ID3D11DeviceChild* Util::CompileShader(const wchar_t* path,
	const std::vector<std::pair<const char*, const char*>>& defines,
	const char* target, const char* entry, ShaderCompileTiming*)
{
	++compileCalls;
	Require(std::wstring_view(path) == L"Data/Shaders/Upscaling/ClearHMDMaskCS.hlsl" &&
				defines.empty() && std::string_view(target) == "cs_5_0" && std::string_view(entry) == "main",
		"HMD compilation must preserve its shader inputs");
	if (throwCompile)
		throw std::runtime_error("Injected compiler exception");
	if (throwUnknownCompile)
		throw 1;
	return failCompile ? nullptr : new TestShader;
}

int main()
{
	try {
		{
			Upscaling upscaling;
			globals::game::isVR = false;
			Require(!upscaling.EnsureHMDMaskClearResources() && !compileCalls && !bufferCalls,
				"SE/AE must bypass HMD resource creation");
			globals::game::isVR = true;
			globals::d3d::device = nullptr;
			Require(!upscaling.EnsureHMDMaskClearResources() && !compileCalls && !bufferCalls,
				"A missing device must bypass compilation and allocation");
			globals::d3d::device = &testDevice;
			upscaling.reuseOrdinarySaveResources = true;
			Require(!upscaling.EnsureHMDMaskClearResources() && !compileCalls && !bufferCalls,
				"Ordinary save recovery must not create missing resources");
			upscaling.reuseOrdinarySaveResources = false;
			upscaling.SetupResources();

			failCompile = true;
			for (unsigned request = 0; request < 20; ++request)
				Require(!upscaling.EnsureHMDMaskClearResources(), "Failed compilation must reject HMD clearing");
			Require(compileCalls == 1 && bufferCalls == 0 && !liveShaders && !liveBuffers,
				"Failed compilation must latch without allocating a constant buffer");
			Require(!deviceLossRecorded, "A shader failure on a healthy device must not record device loss");
			deviceRemoved = true;
			Require(!upscaling.EnsureHMDMaskClearResources() && deviceLossRecorded && compileCalls == 1 && !bufferCalls,
				"A latched shader failure must still detect device removal without recompiling or allocating");
			deviceRemoved = deviceLossRecorded = false;
			failCompile = false;
			Require(!upscaling.EnsureHMDMaskClearResources() && compileCalls == 1,
				"Compiler recovery alone must not bypass the failure latch");

			upscaling.ClearShaderCache();
			Require(upscaling.EnsureHMDMaskClearResources() && compileCalls == 2 && bufferCalls == 1,
				"Shader invalidation must permit compilation and buffer creation");
			auto* buffer = upscaling.vrClearHMDMaskCB.get();
			Require(static_cast<TestShader*>(upscaling.vrClearHMDMaskCS.get())->Name() == "Upscaling::ClearHMDMaskCS" &&
						static_cast<TestBuffer*>(buffer)->Name() == "Upscaling::HMDMaskClearConstants",
				"HMD shader and constant buffer must retain resource names");
			{
				winrt::com_ptr<IUnknown> shaderIdentity;
				winrt::com_ptr<ID3D11Resource> bufferResource;
				Require(SUCCEEDED(upscaling.vrClearHMDMaskCS.get()->QueryInterface(IID_PPV_ARGS(shaderIdentity.put()))) &&
							SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(bufferResource.put()))),
					"Mock resources must retain their inherited COM interface contracts");
			}
			upscaling.reuseOrdinarySaveResources = true;
			Require(upscaling.EnsureHMDMaskClearResources() && compileCalls == 2 && bufferCalls == 1,
				"Ordinary save recovery must reuse complete resources");
			upscaling.reuseOrdinarySaveResources = false;
			for (unsigned request = 0; request < 20; ++request)
				Require(upscaling.EnsureHMDMaskClearResources(), "Warmed resources must remain usable");
			Require(compileCalls == 2 && bufferCalls == 1, "Warmed requests must not recreate resources");
			upscaling.SetupResources();
			Require(upscaling.vrClearHMDMaskCS && upscaling.vrClearHMDMaskCB.get() == buffer && compileCalls == 2 && bufferCalls == 1,
				"Resource setup on the same device must retain the HMD shader and buffer");

			upscaling.ClearShaderCache();
			Require(!upscaling.vrClearHMDMaskCS && upscaling.vrClearHMDMaskCB.get() == buffer,
				"Shader invalidation must retain the constant buffer");
			failCompile = true;
			for (unsigned request = 0; request < 20; ++request)
				Require(!upscaling.EnsureHMDMaskClearResources(), "Recompilation failure must reject HMD clearing");
			Require(compileCalls == 3 && bufferCalls == 1 && upscaling.vrClearHMDMaskCB.get() == buffer,
				"Failed recompilation must preserve the existing buffer and latch failure");
			failCompile = false;
			upscaling.ClearShaderCache();
			Require(upscaling.EnsureHMDMaskClearResources() && compileCalls == 4 && bufferCalls == 1 &&
						upscaling.vrClearHMDMaskCB.get() == buffer,
				"Recovery after invalidation must recompile with the same constant buffer");

			TestDevice replacementDevice;
			globals::d3d::device = &replacementDevice;
			upscaling.reuseOrdinarySaveResources = true;
			upscaling.SetupResources();
			Require(!upscaling.vrClearHMDMaskCS && !upscaling.vrClearHMDMaskCB && !liveShaders && !liveBuffers,
				"A device change must release every HMD resource owned by the previous device");
			Require(!upscaling.EnsureHMDMaskClearResources() && compileCalls == 4 && bufferCalls == 1,
				"Device changes during ordinary save recovery must not allocate replacement resources");
			upscaling.reuseOrdinarySaveResources = false;
			Require(upscaling.EnsureHMDMaskClearResources() && compileCalls == 5 && bufferCalls == 2,
				"A replacement device must receive a newly compiled shader and constant buffer");
			globals::d3d::device = &testDevice;
		}
		Require(!liveShaders && !liveBuffers, "Resource destruction must release every owned reference");
		{
			Upscaling upscaling;
			failBuffer = true;
			Require(!upscaling.EnsureHMDMaskClearResources() && !upscaling.vrClearHMDMaskCS &&
						!upscaling.vrClearHMDMaskCB && deviceFailureChecks == 1,
				"Buffer creation failure must retain existing cleanup and device-loss checks");
			failBuffer = false;
			Require(upscaling.EnsureHMDMaskClearResources(), "Buffer creation failure must remain recoverable");
		}
		for (const bool unknownException : { false, true }) {
			Upscaling upscaling;
			const auto callsBefore = compileCalls;
			const auto buffersBefore = bufferCalls;
			const auto checksBefore = deviceRemovalChecks;
			const auto exceptionChecksBefore = deviceFailureChecks;
			throwCompile = !unknownException;
			throwUnknownCompile = unknownException;
			Require(!upscaling.EnsureHMDMaskClearResources() && !upscaling.vrClearHMDMaskCS &&
						!upscaling.vrClearHMDMaskCB && compileCalls == callsBefore + 1 && bufferCalls == buffersBefore &&
						deviceRemovalChecks == checksBefore + 1 && deviceFailureChecks == exceptionChecksBefore,
				"Logged compiler exceptions must take the null-shader fallback and retain device-removal checks");
			throwCompile = throwUnknownCompile = false;
			deviceRemoved = true;
			Require(!upscaling.EnsureHMDMaskClearResources() && deviceLossRecorded &&
						compileCalls == callsBefore + 1 && bufferCalls == buffersBefore && deviceRemovalChecks == checksBefore + 2,
				"Latched compiler exceptions must still detect device removal without recompiling");
			deviceRemoved = deviceLossRecorded = false;
			Require(!upscaling.EnsureHMDMaskClearResources() && compileCalls == callsBefore + 1,
				"Compiler recovery alone must not bypass an exception failure latch");
			upscaling.ClearShaderCache();
			Require(upscaling.EnsureHMDMaskClearResources() && compileCalls == callsBefore + 2 && bufferCalls == buffersBefore + 1,
				"Explicit shader-cache invalidation must recover after compiler exceptions");
		}
		{
			Upscaling upscaling;
			failBuffer = bufferOnFailure = true;
			Require(!upscaling.EnsureHMDMaskClearResources() && !upscaling.vrClearHMDMaskCS &&
						!upscaling.vrClearHMDMaskCB && !liveShaders && !liveBuffers,
				"A failed buffer creation with a returned interface must release partial resources");
			failBuffer = bufferOnFailure = false;
			Require(upscaling.EnsureHMDMaskClearResources(), "Partial buffer creation must remain recoverable");
		}
		{
			Upscaling upscaling;
			nullBufferOnSuccess = true;
			Require(!upscaling.EnsureHMDMaskClearResources() && upscaling.vrClearHMDMaskCS &&
						!upscaling.vrClearHMDMaskCB && !liveBuffers,
				"A successful buffer result without an interface must not publish complete resources");
			const auto callsBefore = compileCalls;
			nullBufferOnSuccess = false;
			Require(upscaling.EnsureHMDMaskClearResources() && compileCalls == callsBefore,
				"A missing buffer interface must recover without recompiling its valid shader");
		}
		Require(!liveShaders && !liveBuffers, "Failure recovery must not leak shaders or buffers");
		std::cout << "HMD mask resource lifecycle tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
