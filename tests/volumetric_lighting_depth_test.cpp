#include "d3d11_shader_test.h"

#include <array>
#include <cstdint>
#include <d3d11_1.h>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <winrt/base.h>

using namespace std::literals;
using D3D11ShaderTest::Check;

namespace REL
{
	template <class Function>
	using Relocation = Function*;
}
namespace SKSE::stl
{
	template <class Function>
	struct scope_exit
	{
		Function function;
		~scope_exit() { function(); }
	};
}
namespace logger
{
	inline unsigned warnings = 0;
	void warn(const char*) { ++warnings; }
}
#define CS_GPU_PASS_DYNAMIC(name) static_cast<void>(name)

namespace RE
{
	struct RENDER_TARGETS
	{
		enum RENDER_TARGET
		{
			kIMAGESPACE_VOLUMETRIC_LIGHTING,
			kIMAGESPACE_VOLUMETRIC_LIGHTING_PREVIOUS,
			kIMAGESPACE_VOLUMETRIC_LIGHTING_COPY,
			kVOLUMETRIC_LIGHTING_HALF_RES,
			kVOLUMETRIC_LIGHTING_BLUR_HALF_RES,
			kVOLUMETRIC_LIGHTING_QUARTER_RES,
			kVOLUMETRIC_LIGHTING_BLUR_QUARTER_RES,
			kTOTAL
		};
	};
	using RENDER_TARGET = RENDER_TARGETS::RENDER_TARGET;
	struct BSShader
	{
		enum class Type
		{
			ImageSpace,
			Lighting
		};
		struct
		{
			Type value = Type::ImageSpace;
			Type get() const { return value; }
		} shaderType;
		const char* fxpFilename = nullptr;
	};
	struct BSImagespaceShader : BSShader
	{};
	struct BSComputeShader
	{
		const char* name;
	};
	namespace BSGraphics
	{
		struct ComputeShader
		{};
		struct Renderer
		{
			struct Target
			{
				ID3D11RenderTargetView* RTV = nullptr;
				ID3D11UnorderedAccessView* UAV = nullptr;
			};
			struct
			{
				std::array<Target, RENDER_TARGETS::kTOTAL> renderTargets{};
			} data;
			auto& GetRuntimeData() { return data; }
		};
	}
}
struct State
{
	uint32_t frameCount = 1;
	uint64_t targetGeneration = 1;
	uint64_t completedTargetGeneration = 1;
	bool imageSpaceEnabled = true;
	bool ShaderEnabled(RE::BSShader::Type) const { return imageSpaceEnabled; }
	uint64_t GetRenderTargetResourcePublicationGeneration() const { return targetGeneration; }
	uint64_t GetCompletedRenderTargetResourcePublicationGeneration() const { return completedTargetGeneration; }
};
struct ShaderCache
{
	bool available = true;
	unsigned lookups = 0;
	RE::BSGraphics::ComputeShader replacement;
	RE::BSGraphics::ComputeShader* GetComputeShader(const RE::BSImagespaceShader&, uint32_t)
	{
		++lookups;
		return available ? &replacement : nullptr;
	}
};
struct VolumetricLighting
{
	bool loaded = true;
	bool blurValid = true;
	bool depthDispatchRejected = false;
	bool depthDispatchWarningLogged = false;
	uint32_t depthDispatchRejectedFrame = 0;
	std::optional<uint32_t> depthTargetsClearedFrame;
	uint64_t depthTargetsClearedGeneration = 0;
	std::array<ID3D11View*, 14> depthClearedViews{};
	ID3D11Buffer* blurBuffer = nullptr;
	RE::BSImagespaceShader wrapper;
	bool AdmitDepthDispatch(bool, bool);
	bool IsDepthDispatchRejected() const;
	void ClearVolumetricLightingTargets(bool reuseClearedTargets = false);
	RE::BSImagespaceShader* GetWrapper(RE::BSComputeShader* shader)
	{
		wrapper.fxpFilename = shader->name;
		return &wrapper;
	}
	RE::BSImagespaceShader* GetOrCreateGenerateCS(RE::BSComputeShader* shader) { return GetWrapper(shader); }
	RE::BSImagespaceShader* GetOrCreateRaymarchCS(RE::BSComputeShader* shader) { return GetWrapper(shader); }
	RE::BSImagespaceShader* GetOrCreateBlurHCS(RE::BSComputeShader* shader) { return GetWrapper(shader); }
	RE::BSImagespaceShader* GetOrCreateBlurVCS(RE::BSComputeShader* shader) { return GetWrapper(shader); }
	bool HasValidBlurDimensions() const { return blurValid; }
	void SetDimensionsCB() const;
	void SetGroupCountsHCS(uint32_t& x, uint32_t& y) const
	{
		x = 7;
		y = 9;
	}
	void SetGroupCountsVCS(uint32_t& x, uint32_t& y) const
	{
		x = 11;
		y = 13;
	}
};
struct CountingContext
{
	ID3D11DeviceContext* context = nullptr;
	unsigned clears = 0;
	HRESULT QueryInterface(REFIID id, void** output) { return context->QueryInterface(id, output); }
	void CSSetConstantBuffers(UINT start, UINT count, ID3D11Buffer* const* buffers) { context->CSSetConstantBuffers(start, count, buffers); }
	void CSGetConstantBuffers(UINT start, UINT count, ID3D11Buffer** buffers) { context->CSGetConstantBuffers(start, count, buffers); }
	void ClearRenderTargetView(ID3D11RenderTargetView* view, const FLOAT color[4])
	{
		++clears;
		context->ClearRenderTargetView(view, color);
	}
	void ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* view, const FLOAT color[4])
	{
		++clears;
		context->ClearUnorderedAccessViewFloat(view, color);
	}
};
namespace globals
{
	inline State stateStorage;
	inline State* state = &stateStorage;
	inline ShaderCache cacheStorage;
	inline ShaderCache* shaderCache = &cacheStorage;
	namespace features
	{
		inline struct
		{
			bool active = true;
			bool IsActive() const { return active; }
		} reverseZ;
		inline VolumetricLighting volumetricLighting;
	}
	namespace d3d
	{
		inline CountingContext* context = nullptr;
	}
	namespace game
	{
		inline RE::BSGraphics::Renderer* renderer = nullptr;
	}
}
void VolumetricLighting::SetDimensionsCB() const
{
	globals::d3d::context->CSSetConstantBuffers(1, 1, &blurBuffer);
}

#include "depth.h"
namespace CSShadersSupport
{
	RE::BSImagespaceShader* CurrentlyDispatchedShader = nullptr;
	RE::BSComputeShader* CurrentlyDispatchedComputeShader = nullptr;
	uint32_t CurrentComputeShaderTechniqueId = 0;
#include "volumetric_depth_hook.h"
}
bool ApplyAllowed(RE::BSShader* shader)
{
#include "volumetric_depth_apply.h"
	return true;
}

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	unsigned dispatches = 0;
	RE::BSGraphics::ComputeShader* selectedShader = nullptr;
	std::array<uint32_t, 3> dispatchSize{};
	ID3D11Buffer* expectedBlurBuffer = nullptr;
	void Dispatch(RE::BSGraphics::Renderer*, RE::BSGraphics::ComputeShader* shader, uint32_t x, uint32_t y, uint32_t z)
	{
		++dispatches;
		selectedShader = shader;
		dispatchSize = { x, y, z };
		if (expectedBlurBuffer) {
			winrt::com_ptr<ID3D11Buffer> bound;
			globals::d3d::context->CSGetConstantBuffers(1, 1, bound.put());
			Require(bound.get() == expectedBlurBuffer, "Replacement blur did not bind its constants");
		}
	}
	struct Fixture
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		CountingContext counted;
		RE::BSGraphics::Renderer renderer;
		std::array<winrt::com_ptr<ID3D11Texture2D>, RE::RENDER_TARGETS::kTOTAL> textures;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, RE::RENDER_TARGETS::kTOTAL> rtvs;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, RE::RENDER_TARGETS::kTOTAL> uavs;
		winrt::com_ptr<ID3D11Texture2D> staging;
		winrt::com_ptr<ID3D11Buffer> originalBuffer, replacementBuffer;
		RE::BSGraphics::ComputeShader native;
		RE::BSComputeShader compute{ "ISVolumetricLightingGenerateCS" };
		RE::BSImagespaceShader imagespace;
		Fixture()
		{
			D3D_FEATURE_LEVEL level;
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.put(), &level, context.put()));
			counted.context = context.get();
			globals::d3d::context = &counted;
			globals::game::renderer = &renderer;
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 2;
			desc.MipLevels = desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
			for (size_t i = 0; i < textures.size(); ++i) {
				Check(device->CreateTexture2D(&desc, nullptr, textures[i].put()));
				Check(device->CreateRenderTargetView(textures[i].get(), nullptr, rtvs[i].put()));
				Check(device->CreateUnorderedAccessView(textures[i].get(), nullptr, uavs[i].put()));
				Util::SetResourceName(textures[i].get(), "VolumetricDepthTest::History%zu", i);
				Util::SetResourceName(rtvs[i].get(), "VolumetricDepthTest::History%zu RTV", i);
				Util::SetResourceName(uavs[i].get(), "VolumetricDepthTest::History%zu UAV", i);
				renderer.data.renderTargets[i] = { rtvs[i].get(), uavs[i].get() };
			}
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.put()));
			Util::SetResourceName(staging.get(), "VolumetricDepthTest::Readback");
			D3D11_BUFFER_DESC buffer{};
			buffer.ByteWidth = 16;
			buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			Check(device->CreateBuffer(&buffer, nullptr, originalBuffer.put()));
			Check(device->CreateBuffer(&buffer, nullptr, replacementBuffer.put()));
			Util::SetResourceName(originalBuffer.get(), "VolumetricDepthTest::OriginalConstants");
			Util::SetResourceName(replacementBuffer.get(), "VolumetricDepthTest::BlurConstants");
			CSShadersSupport::Renderer_DispatchCSShader::func = Dispatch;
			Reset();
		}
		void Reset()
		{
			globals::stateStorage = {};
			globals::state = &globals::stateStorage;
			globals::cacheStorage = {};
			globals::shaderCache = &globals::cacheStorage;
			globals::d3d::context = &counted;
			globals::features::volumetricLighting = {};
			globals::features::volumetricLighting.blurBuffer = replacementBuffer.get();
			globals::features::reverseZ.active = true;
			dispatches = 0;
			selectedShader = nullptr;
			expectedBlurBuffer = nullptr;
			counted.clears = 0;
			const float stale[4]{ 42, 42, 42, 42 };
			for (const auto& view : rtvs)
				context->ClearRenderTargetView(view.get(), stale);
			auto* previous = originalBuffer.get();
			context->CSSetConstantBuffers(1, 1, &previous);
		}
		void Run(const char* name, bool wrapped = false, bool raw = true)
		{
			compute.name = name;
			imagespace.fxpFilename = name;
			CSShadersSupport::CurrentlyDispatchedShader = wrapped ? &imagespace : nullptr;
			CSShadersSupport::CurrentlyDispatchedComputeShader = raw ? &compute : nullptr;
			CSShadersSupport::Renderer_DispatchCSShader::thunk(&renderer, &native, 2, 3, 4);
		}
		void CheckHistory(float expected)
		{
			for (const auto& texture : textures) {
				context->CopyResource(staging.get(), texture.get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				Check(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
				bool matches = true;
				for (unsigned y = 0; y < 2; ++y) {
					auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);
					matches &= row[0] == expected && row[1] == expected;
				}
				context->Unmap(staging.get(), 0);
				Require(matches, "Volumetric output/history retained stale values or was cleared unexpectedly");
			}
		}
		void ReplaceHistory(size_t index)
		{
			D3D11_TEXTURE2D_DESC desc{};
			textures[index]->GetDesc(&desc);
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11RenderTargetView> rtv;
			winrt::com_ptr<ID3D11UnorderedAccessView> uav;
			Check(device->CreateTexture2D(&desc, nullptr, texture.put()));
			Check(device->CreateRenderTargetView(texture.get(), nullptr, rtv.put()));
			Check(device->CreateUnorderedAccessView(texture.get(), nullptr, uav.put()));
			Util::SetResourceName(texture.get(), "VolumetricDepthTest::RecreatedHistory");
			Util::SetResourceName(rtv.get(), "VolumetricDepthTest::RecreatedHistory RTV");
			Util::SetResourceName(uav.get(), "VolumetricDepthTest::RecreatedHistory UAV");
			const float stale[4]{ 42, 42, 42, 42 };
			context->ClearRenderTargetView(rtv.get(), stale);
			textures[index] = std::move(texture);
			rtvs[index] = std::move(rtv);
			uavs[index] = std::move(uav);
			renderer.data.renderTargets[index] = { rtvs[index].get(), uavs[index].get() };
		}
	};
}

int main()
{
	try {
		Fixture f;
		auto& vl = globals::features::volumetricLighting;
		constexpr std::array names{ "ISVolumetricLightingGenerateCS", "ISVolumetricLightingRaymarchCS", "ISVolumetricLightingBlurHCS", "ISVolumetricLightingBlurVCS" };
		for (const auto* name : names) {
			for (bool loaded : { false, true }) {
				f.Reset();
				vl.loaded = loaded;
				globals::cacheStorage.available = false;
				f.Run(name, true);
				Require(dispatches == 0 && vl.IsDepthDispatchRejected(), "Reverse Z dispatched an incompatible native pass");
				Require(loaded || globals::cacheStorage.lookups == 0, "Unloaded feature used missing replacement resources");
				f.CheckHistory(0);
				RE::BSShader apply;
				apply.fxpFilename = "ISApplyVolumetricLighting";
				Require(!ApplyAllowed(&apply), "Rejected density reached the apply pass");
				apply.fxpFilename = "ISCompositeLensFlareVolumetricLighting";
				Require(ApplyAllowed(&apply), "Suppression also discarded independent lens flares");
			}
		}
		f.Reset();
		globals::cacheStorage.available = false;
		f.Run(names[2], true, false);
		Require(dispatches == 0 && vl.IsDepthDispatchRejected(), "Imagespace-only VL dispatch escaped the depth gate");
		f.Reset();
		vl.blurValid = false;
		f.Run(names[2], true);
		Require(dispatches == 0, "Invalid blur bounds used native Reverse Z fallback");
		f.CheckHistory(0);
		for (bool missingState : { false, true }) {
			f.Reset();
			if (missingState)
				globals::state = nullptr;
			else
				globals::shaderCache = nullptr;
			f.Run(names[0]);
			Require(dispatches == 0 && vl.IsDepthDispatchRejected(), "Unavailable replacement infrastructure escaped the depth gate");
			f.CheckHistory(0);
		}
		f.Reset();
		globals::stateStorage.imageSpaceEnabled = false;
		f.Run(names[0]);
		Require(dispatches == 0 && vl.IsDepthDispatchRejected(), "Disabled image-space replacement escaped the depth gate");
		for (bool reversed : { false, true }) {
			f.Reset();
			globals::features::reverseZ.active = reversed;
			globals::d3d::context = nullptr;
			f.Run(names[2], true, false);
			Require(dispatches == (reversed ? 0u : 1u), "Unavailable blur context did not preserve the depth-safe fallback");
		}

		f.Reset();
		vl.loaded = false;
		for (const auto* name : names)
			f.Run(name, true);
		Require(dispatches == 0 && f.counted.clears == 14, "Sticky rejection repeated unchanged history clears in one frame");
		f.CheckHistory(0);
		f.ReplaceHistory(0);
		f.Run(names[1]);
		Require(f.counted.clears == 16, "Recreated history was not cleared independently in the rejected frame");
		f.CheckHistory(0);
		const float stale[4]{ 42, 42, 42, 42 };
		for (const auto& view : f.rtvs)
			f.context->ClearRenderTargetView(view.get(), stale);
		f.Run(names[0]);
		Require(f.counted.clears == 30, "A fresh rejected generation reused clears preceding new history writes");
		f.CheckHistory(0);
		++globals::stateStorage.frameCount;
		f.Run(names[1]);
		Require(f.counted.clears == 44, "A rejected later frame reused the previous frame's clear state");
		for (const auto& view : f.rtvs)
			f.context->ClearRenderTargetView(view.get(), stale);
		++globals::stateStorage.targetGeneration;
		globals::stateStorage.completedTargetGeneration = globals::stateStorage.targetGeneration;
		f.Run(names[1]);
		Require(f.counted.clears == 58, "A new resource publication reused cached view addresses");
		f.CheckHistory(0);

		f.Reset();
		globals::cacheStorage.available = false;
		f.Run(names[0]);
		globals::cacheStorage.available = true;
		f.Run(names[0]);
		Require(dispatches == 0, "Rejected frame recovered halfway through its generation");
		++globals::stateStorage.frameCount;
		f.Run(names[1]);
		Require(dispatches == 0, "New frame consumed an old volume without generation");
		f.ReplaceHistory(0);
		f.Run(names[0]);
		Require(dispatches == 1 && !vl.IsDepthDispatchRejected() && selectedShader == &globals::cacheStorage.replacement, "Fresh compatible generation failed to recover after a rejected dependent pass");
		f.CheckHistory(0);
		f.Run(names[1]);
		Require(dispatches == 2, "Recovered raymarch remained suppressed");
		f.Reset();
		globals::stateStorage.frameCount = (std::numeric_limits<uint32_t>::max)();
		globals::cacheStorage.available = false;
		f.Run(names[0]);
		globals::stateStorage.frameCount = 0;
		globals::cacheStorage.available = true;
		f.Run(names[1]);
		f.Run(names[0]);
		Require(dispatches == 1 && !vl.IsDepthDispatchRejected(), "Frame counter wrap prevented fresh generation recovery");

		for (bool loaded : { false, true }) {
			f.Reset();
			globals::features::reverseZ.active = false;
			vl.loaded = loaded;
			globals::cacheStorage.available = false;
			for (const auto* name : names)
				f.Run(name, true);
			Require(dispatches == names.size() && selectedShader == &f.native && dispatchSize == std::array<uint32_t, 3>{ 2, 3, 4 }, "Standard-depth fallback or dispatch dimensions changed");
			f.CheckHistory(42);
		}
		f.Reset();
		globals::cacheStorage.available = false;
		f.Run("ISUnrelatedCompute");
		Require(dispatches == 1 && !vl.IsDepthDispatchRejected(), "Unrelated compute pass was suppressed");
		f.CheckHistory(42);

		for (bool wrapped : { false, true })
			for (bool raw : { false, true }) {
				if (!wrapped && !raw)
					continue;
				for (unsigned blur : { 2u, 3u }) {
					f.Reset();
					expectedBlurBuffer = f.replacementBuffer.get();
					f.Run(names[blur], wrapped, raw);
					const std::array<uint32_t, 3> expectedSize = blur == 2 ? std::array<uint32_t, 3>{ 7, 9, 4 } : std::array<uint32_t, 3>{ 11, 13, 4 };
					Require(dispatches == 1 && dispatchSize == expectedSize, "Valid Reverse Z blur did not use replacement dispatch bounds");
					winrt::com_ptr<ID3D11Buffer> restored;
					f.context->CSGetConstantBuffers(1, 1, restored.put());
					Require(restored.get() == f.originalBuffer.get(), "Blur constants were not restored");
					f.CheckHistory(42);
					f.Reset();
					vl.blurValid = false;
					f.Run(names[blur], wrapped, raw);
					Require(dispatches == 0, "Invalid replacement blur bounds were admitted through an alternate entry path");
				}
			}
		std::cout << "Reverse Z VL native-fallback rejection, WARP history clearing, apply suppression, recovery and standard-depth preservation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
