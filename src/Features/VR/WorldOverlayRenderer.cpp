#include "WorldOverlayRenderer.h"
#include "GpuPass.h"
#include "Utils/ResourceName.h"
#include "Utils/ScopedDeviceContextState.h"
#include "WorldOverlayInternal.h"
#include "WorldOverlayShaders.h"
#include <cstring>
#include <d3dcompiler.h>

#ifdef _MSC_VER
#	pragma float_control(precise, on, push)
#endif

namespace CSX::WorldOverlays
{
	namespace
	{
		struct alignas(16) GpuQuad
		{
			float centerWidth[4], uv[4], heightOpacityDepth[4];
		};
		struct alignas(16) Constants
		{
			Matrix colorVP, depthVP, depthView, inverseDepthProjection;
			float right[4]{}, up[4]{}, depthRect[4]{}, orientation[4]{};
			std::array<GpuQuad, API::MaxQuads> quads{};
		};
		static_assert(sizeof(Constants) % 16 == 0);
		static_assert(API::MaxQuads == 64 && sizeof(GpuQuad) == 48, "Match the embedded shader quad layout");
		struct Resources
		{
			ID3D11Device* device = nullptr;
			bool attempted = false, ready = false;
			winrt::com_ptr<ID3D11DeviceContext1> context;
			winrt::com_ptr<ID3DDeviceContextState> state;
			winrt::com_ptr<ID3D11VertexShader> vs;
			winrt::com_ptr<ID3D11PixelShader> ps;
			winrt::com_ptr<ID3D11Buffer> constants;
			winrt::com_ptr<ID3D11BlendState> blend;
			winrt::com_ptr<ID3D11RasterizerState> raster;
			winrt::com_ptr<ID3D11DepthStencilState> depth;
			winrt::com_ptr<ID3D11SamplerState> sampler;
		} resources;
		bool CreateResources()
		{
			auto* device = globals::d3d::device;
			winrt::com_ptr<ID3D11Device1> device1;
			if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), device1.put_void())) ||
				FAILED(globals::d3d::context->QueryInterface(__uuidof(ID3D11DeviceContext1), resources.context.put_void())))
				return false;
			const auto level = device->GetFeatureLevel();
			if (FAILED(device1->CreateDeviceContextState(device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED,
					&level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, resources.state.put())))
				return false;
			Util::SetResourceName(resources.state.get(), "WorldOverlays::ContextState");
			const auto compile = [](const char* entry, const char* profile, winrt::com_ptr<ID3DBlob>& output) {
				winrt::com_ptr<ID3DBlob> errors;
				const auto hr = D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "CSXWorldOverlays", nullptr, nullptr,
					entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS, 0, output.put(), errors.put());
				if (FAILED(hr))
					logger::error("World overlay shader {} failed: {}", entry,
						errors ? std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "no compiler diagnostic");
				return SUCCEEDED(hr);
			};
			winrt::com_ptr<ID3DBlob> vs, ps;
			if (!compile("VSMain", "vs_5_0", vs) || !compile("PSMain", "ps_5_0", ps))
				return false;
			if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, resources.vs.put())))
				return false;
			Util::SetResourceName(resources.vs.get(), "WorldOverlays::VS");
			if (FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, resources.ps.put())))
				return false;
			Util::SetResourceName(resources.ps.get(), "WorldOverlays::PS");
			D3D11_BUFFER_DESC buffer{};
			buffer.ByteWidth = sizeof(Constants);
			buffer.Usage = D3D11_USAGE_DEFAULT;
			buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(device->CreateBuffer(&buffer, nullptr, resources.constants.put())))
				return false;
			Util::SetResourceName(resources.constants.get(), "WorldOverlays::Constants");
			D3D11_BLEND_DESC blend{};
			auto& rt = blend.RenderTarget[0];
			rt.BlendEnable = true;
			rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			if (FAILED(device->CreateBlendState(&blend, resources.blend.put())))
				return false;
			Util::SetResourceName(resources.blend.get(), "WorldOverlays::PremultipliedBlend");
			D3D11_RASTERIZER_DESC raster{};
			raster.FillMode = D3D11_FILL_SOLID;
			raster.CullMode = D3D11_CULL_NONE;
			raster.DepthClipEnable = true;
			if (FAILED(device->CreateRasterizerState(&raster, resources.raster.put())))
				return false;
			Util::SetResourceName(resources.raster.get(), "WorldOverlays::Rasterizer");
			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
			if (FAILED(device->CreateDepthStencilState(&depth, resources.depth.put())))
				return false;
			Util::SetResourceName(resources.depth.get(), "WorldOverlays::DepthDisabled");
			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
			if (FAILED(device->CreateSamplerState(&sampler, resources.sampler.put())))
				return false;
			Util::SetResourceName(resources.sampler.get(), "WorldOverlays::AtlasSampler");
			return true;
		}
	}
	bool EnsureRenderer()
	{
		if (resources.device != globals::d3d::device)
			resources = {};
		if (!globals::d3d::device || !globals::d3d::context)
			return false;
		if (resources.attempted)
			return resources.ready;
		resources.device = globals::d3d::device;
		resources.attempted = true;
		resources.ready = CreateResources();
		if (!resources.ready)
			logger::error("World overlay renderer initialization failed; producers must retain vanilla subtitles");
		return resources.ready;
	}
	void ResetRenderer() { resources = {}; }
	bool Draw(std::uint32_t eye, ID3D11RenderTargetView* target, const D3D11_TEXTURE2D_DESC& desc,
		const vr::VRTextureBounds_t* bounds, bool reconstructed) noexcept
	{
		if (!target || !HasContent())
			return false;
		auto& s = Shared();
		std::lock_guard lock(s.mutex);
		if (!resources.ready || !CanDrawLocked(eye))
			return false;
		const auto b = bounds ? *bounds : vr::VRTextureBounds_t{ 0, 0, 1, 1 };
		const float values[]{ b.uMin, b.uMax, b.vMin, b.vMax };
		for (auto value : values)
			if (!std::isfinite(value) || value < 0 || value > 1)
				return false;
		if (b.uMin == b.uMax || b.vMin == b.vMax)
			return false;
		D3D11_VIEWPORT viewport{ std::min(b.uMin, b.uMax) * desc.Width, std::min(b.vMin, b.vMax) * desc.Height,
			std::abs(b.uMax - b.uMin) * desc.Width, std::abs(b.vMax - b.vMin) * desc.Height, 0, 1 };
		Constants cb{};
		const auto& scene = s.pairScene;
		cb.colorVP = (reconstructed || scene.temporal) ? scene.camera.CameraViewProjUnjittered[eye] : scene.camera.CameraViewProj[eye];
		cb.depthVP = scene.camera.CameraViewProj[eye];
		cb.depthView = scene.camera.CameraView[eye];
		cb.inverseDepthProjection = scene.camera.CameraProjInverse[eye];
		for (int axis = 0; axis < 3; ++axis) {
			cb.right[axis] = (scene.camera.CameraViewInverse[0].m[axis][0] + scene.camera.CameraViewInverse[1].m[axis][0]) * .5f;
			cb.up[axis] = (scene.camera.CameraViewInverse[0].m[axis][1] + scene.camera.CameraViewInverse[1].m[axis][1]) * .5f;
		}
		const auto normalize = [](float* vector) {
			const float length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
			if (!std::isfinite(length) || length < 1e-6f)
				return false;
			for (int axis = 0; axis < 3; ++axis) vector[axis] /= length;
			return true;
		};
		if (!normalize(cb.right))
			return false;
		const float alignment = cb.right[0] * cb.up[0] + cb.right[1] * cb.up[1] + cb.right[2] * cb.up[2];
		for (int axis = 0; axis < 3; ++axis) cb.up[axis] -= alignment * cb.right[axis];
		if (!normalize(cb.up))
			return false;
		const auto rect = scene.depthRects[eye];
		cb.depthRect[0] = float(rect.x);
		cb.depthRect[1] = float(rect.y);
		cb.depthRect[2] = float(rect.width);
		cb.depthRect[3] = float(rect.height);
		cb.orientation[0] = b.uMax > b.uMin ? 1.f : -1.f;
		cb.orientation[1] = b.vMax > b.vMin ? 1.f : -1.f;
		CS_GPU_PASS("VR::WorldOverlays");
		Util::ScopedDeviceContextState isolated(resources.context.get(), resources.state.get());
		auto* context = resources.context.get();
		context->ClearState();
		context->OMSetRenderTargets(1, &target, nullptr);
		context->OMSetBlendState(resources.blend.get(), nullptr, 0xffffffff);
		context->OMSetDepthStencilState(resources.depth.get(), 0);
		context->RSSetState(resources.raster.get());
		context->RSSetViewports(1, &viewport);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(resources.vs.get(), nullptr, 0);
		context->PSSetShader(resources.ps.get(), nullptr, 0);
		auto* buffer = resources.constants.get();
		context->VSSetConstantBuffers(0, 1, &buffer);
		context->PSSetConstantBuffers(0, 1, &buffer);
		auto* sampler = resources.sampler.get();
		context->PSSetSamplers(0, 1, &sampler);
		bool drawn = false;
		const auto& origin = scene.camera.CameraPosAdjust[eye];
		const double originXYZ[]{ origin.x, origin.y, origin.z };
		for (const auto& batch : s.pair) {
			if (!batch.count)
				continue;
			for (std::uint32_t i = 0; i < batch.count; ++i) {
				const auto& q = batch.quads[i];
				auto& gpu = cb.quads[i];
				for (int axis = 0; axis < 3; ++axis) gpu.centerWidth[axis] = float(q.center[axis] - originXYZ[axis]);
				gpu.centerWidth[3] = q.width;
				std::copy_n(q.uv, 4, gpu.uv);
				gpu.heightOpacityDepth[0] = q.height;
				gpu.heightOpacityDepth[1] = q.opacity;
				gpu.heightOpacityDepth[2] = q.occlusion == API::Occlusion::SceneDepth ? 1.f : 0.f;
			}
			context->UpdateSubresource(buffer, 0, nullptr, &cb, 0, 0);
			ID3D11ShaderResourceView* views[]{ batch.atlas.get(), scene.depth.get() };
			context->PSSetShaderResources(0, 2, views);
			context->DrawInstanced(6, batch.count, 0, 0);
			++s.draws;
			drawn = true;
		}
		s.pairEyes |= 1u << eye;
		return drawn;
	}
}

#ifdef _MSC_VER
#	pragma float_control(pop)
#endif
