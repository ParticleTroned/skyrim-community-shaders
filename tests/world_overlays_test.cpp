#define NOMINMAX
#include "Features/VR/WorldOverlayPolicy.h"
#include "Features/VR/WorldOverlayShaders.h"
#include "Utils/ScopedDeviceContextState.h"
#include "d3d11_shader_test.h"
#include <array>
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <iostream>
#include <limits>
#include <winrt/base.h>

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = winrt::com_ptr<T>;
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	void ValidateInputs()
	{
		namespace P = CSX::WorldOverlays::Policy;
		namespace A = CSX::WorldOverlayAPI;
		Require(P::ValidAtlas(2048, 4096) && !P::ValidAtlas(4096, 4096) && !P::ValidAtlas(UINT_MAX, 2) && !P::ValidAtlas(0, 1), "Atlas allocation bounds");
		A::Quad001 quad;
		quad.id = 1;
		quad.width = quad.height = 10;
		Require(P::ValidQuad(quad), "Valid world quad rejected");
		quad.center[1] = std::numeric_limits<double>::quiet_NaN();
		Require(!P::ValidQuad(quad), "NaN world coordinate accepted");
		quad.center[1] = 0;
		quad.opacity = std::numeric_limits<float>::infinity();
		Require(!P::ValidQuad(quad), "Infinite opacity accepted");
		quad.opacity = 1;
		quad.uv[2] = 0;
		Require(!P::ValidQuad(quad), "Empty atlas rectangle accepted");
		Require(P::Fresh(UINT_MAX, 0) && !P::Fresh(10, 13) && !P::Fresh(10, 9), "Frame expiry/wrap contract");
		Require(!P::Contains({ UINT_MAX, 0, 2, 1 }, 32, 32) && P::Contains({ 16, 0, 16, 32 }, 32, 32), "Depth rectangle overflow");
		P::PublicationState publication;
		publication.committed = 0;
		publication.pinned = 1;
		Require(!publication.CanWrite(0) && !publication.CanWrite(1) && publication.CanWrite(2), "Frozen/committed atlas pages can be overwritten");
		publication.leased = 2;
		Require(!publication.CanWrite(2), "Concurrent atlas write leases allowed");
		publication.count = 1;
		publication.epoch = 5;
		publication.generation = 7;
		const auto frozenSerial = publication.clearSerial;
		publication.Accept(frozenSerial, 10, 5, 7, 20);
		Require(publication.ReceiptReady(5, 7, 22) && !publication.ReceiptReady(5, 7, 23) &&
					!publication.ReceiptReady(6, 7, 20) && !publication.ReceiptReady(5, 8, 20),
			"Presentation receipt freshness/identity");
		publication.Clear();
		Require(publication.leased == 2 && publication.pinned == 1 && publication.leaseCanceled &&
					publication.committed == -1 && !publication.ReceiptReady(5, 7, 20),
			"Clear released a live write/frozen lease");
		publication.count = 1;
		publication.Accept(frozenSerial, 10, 5, 7, 20);
		Require(!publication.ReceiptReady(5, 7, 20), "Old pair restored a cleared receipt after republish");
		publication.Accept(publication.clearSerial, 11, 5, 7, 21);
		Require(publication.ReceiptReady(5, 7, 21), "Fresh publication could not recover after clear");
	}
	ComPtr<ID3DBlob> Compile(const char* entry, const char* profile)
	{
		ComPtr<ID3DBlob> code, errors;
		const auto hr = D3DCompile(CSX::WorldOverlays::ShaderSource, sizeof(CSX::WorldOverlays::ShaderSource) - 1,
			"WorldOverlaysTest", nullptr, nullptr, entry, profile, D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_ENABLE_STRICTNESS, 0, code.put(), errors.put());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(hr);
		return code;
	}
	struct Texture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;
	};
	Texture MakeTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
	{
		Texture out;
		D3D11_TEXTURE2D_DESC d{};
		d.Width = width;
		d.Height = height;
		d.Format = format;
		d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
		d.BindFlags = flags;
		Check(device->CreateTexture2D(&d, nullptr, out.texture.put()));
		Util::SetResourceName(out.texture.get(), "WorldOverlayTest::Texture");
		if (flags & D3D11_BIND_SHADER_RESOURCE) {
			Check(device->CreateShaderResourceView(out.texture.get(), nullptr, out.srv.put()));
			Util::SetResourceName(out.srv.get(), "WorldOverlayTest::Texture SRV");
		}
		if (flags & D3D11_BIND_RENDER_TARGET) {
			Check(device->CreateRenderTargetView(out.texture.get(), nullptr, out.rtv.put()));
			Util::SetResourceName(out.rtv.get(), "WorldOverlayTest::Texture RTV");
		}
		return out;
	}
	void RenderChecks()
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		auto debug = device.as<ID3D11InfoQueue>();
		auto vsCode = Compile("VSMain", "vs_5_0"), psCode = Compile("PSMain", "ps_5_0");
		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11PixelShader> ps;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, vs.put()));
		Util::SetResourceName(vs.get(), "WorldOverlayTest::VS");
		Check(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, ps.put()));
		Util::SetResourceName(ps.get(), "WorldOverlayTest::PS");
		Check(D3DReflect(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), IID_ID3D11ShaderReflection, reflection.put_void()));
		D3D11ShaderTest::ConstantBuffer constants(device.get(), reflection.get(), "Constants");
		const std::array<float, 16> projection{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 100.f / 99, -100.f / 99, 0, 0, 1, 0 };
		const std::array<float, 16> inverse{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, -.99f, 1 };
		const std::array<float, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		constants.SetVariable("colorVP", projection);
		constants.SetVariable("depthVP", projection);
		constants.SetVariable("depthView", identity);
		constants.SetVariable("inverseDepthProjection", inverse);
		constants.SetVariable("right", std::array<float, 4>{ 1, 0, 0, 0 });
		constants.SetVariable("up", std::array<float, 4>{ 0, 1, 0, 0 });
		constants.SetVariable("orientation", std::array<float, 4>{ 1, 1, 0, 0 });
		std::array<float, 12 * 64> quads{};
		quads[2] = 10;
		quads[3] = 8;
		quads[6] = quads[7] = 1;
		quads[8] = 8;
		quads[9] = 1;
		quads[10] = 1;
		constants.SetVariable("quads", quads);
		auto atlas = MakeTexture(device.get(), 2, 2, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
		auto depth = MakeTexture(device.get(), 64, 32, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE);
		auto target = MakeTexture(device.get(), 32, 32, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_RENDER_TARGET);
		const float white[]{ .5f, 0, 0, .5f };
		context->ClearRenderTargetView(atlas.rtv.get(), white);
		D3D11_TEXTURE2D_DESC readDesc{};
		target.texture->GetDesc(&readDesc);
		readDesc.BindFlags = 0;
		readDesc.Usage = D3D11_USAGE_STAGING;
		readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> readback;
		Check(device->CreateTexture2D(&readDesc, nullptr, readback.put()));
		Util::SetResourceName(readback.get(), "WorldOverlayTest::Readback");
		D3D11_RASTERIZER_DESC rd{};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		rd.DepthClipEnable = true;
		ComPtr<ID3D11RasterizerState> raster;
		Check(device->CreateRasterizerState(&rd, raster.put()));
		Util::SetResourceName(raster.get(), "WorldOverlayTest::Rasterizer");
		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> sampler;
		Check(device->CreateSamplerState(&sd, sampler.put()));
		Util::SetResourceName(sampler.get(), "WorldOverlayTest::Sampler");
		D3D11_BLEND_DESC bd{};
		auto& rt = bd.RenderTarget[0];
		rt.BlendEnable = true;
		rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = 15;
		ComPtr<ID3D11BlendState> blend;
		Check(device->CreateBlendState(&bd, blend.put()));
		Util::SetResourceName(blend.get(), "WorldOverlayTest::Blend");
		D3D11_VIEWPORT viewport{ 0, 0, 32, 32, 0, 1 };
		context->VSSetShader(vs.get(), nullptr, 0);
		context->PSSetShader(ps.get(), nullptr, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->RSSetState(raster.get());
		context->RSSetViewports(1, &viewport);
		context->OMSetBlendState(blend.get(), nullptr, UINT_MAX);
		auto* samplerPtr = sampler.get();
		context->PSSetSamplers(0, 1, &samplerPtr);
		std::array<float, 64 * 32> depths{};
		for (UINT y = 0; y < 32; ++y)
			for (UINT x = 0; x < 64; ++x) depths[y * 64 + x] = (100.f / 99) - (100.f / 99) / (x < 32 ? 5.f : 20.f);
		context->UpdateSubresource(depth.texture.get(), 0, nullptr, depths.data(), 64 * sizeof(float), 0);
		const auto draw = [&](float offset, bool occlusion, float opacity = 1.f, UINT instances = 1) {
			constants.SetVariable("depthRect", std::array<float, 4>{ offset, 0, 32, 32 });
			quads[9] = opacity;
			quads[10] = occlusion ? 1.f : 0.f;
			constants.SetVariable("quads", quads);
			context->UpdateSubresource(constants.buffer.Get(), 0, nullptr, constants.bytes.data(), 0, 0);
			auto* buffer = constants.buffer.Get();
			context->VSSetConstantBuffers(0, 1, &buffer);
			context->PSSetConstantBuffers(0, 1, &buffer);
			auto* rtv = target.rtv.get();
			const float black[]{ 0, 0, 0, 0 };
			context->ClearRenderTargetView(rtv, black);
			context->OMSetRenderTargets(1, &rtv, nullptr);
			ID3D11ShaderResourceView* views[]{ atlas.srv.get(), depth.srv.get() };
			context->PSSetShaderResources(0, 2, views);
			context->DrawInstanced(6, instances, 0, 0);
			context->CopyResource(readback.get(), target.texture.get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped));
			const float red = reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData) + 16 * mapped.RowPitch)[16 * 4];
			context->Unmap(readback.get(), 0);
			return red;
		};
		Require(draw(0, true) == 0, "Near wall must occlude left eye");
		Require(std::abs(draw(32, true) - .5f) < .001f, "Right eye must use its own depth rectangle");
		Require(std::abs(draw(0, false) - .5f) < .001f, "Diagnostic depth bypass must remain visible");
		Require(std::abs(draw(32, true, .5f) - .25f) < .001f, "Premultiplied opacity must not square alpha");
		depths.fill((100.f / 99) - (100.f / 99) / 20.f);
		depths[16 * 64 + 16] = (100.f / 99) - (100.f / 99) / 5.f;
		context->UpdateSubresource(depth.texture.get(), 0, nullptr, depths.data(), 64 * sizeof(float), 0);
		auto shiftedProjection = projection;
		shiftedProjection[2] = .4f;
		constants.SetVariable("colorVP", shiftedProjection);
		Require(std::abs(draw(0, true) - .5f) < .001f, "An object outside the depth-projected surface must not erase text");
		constants.SetVariable("colorVP", projection);
		depths.fill(1);
		context->UpdateSubresource(depth.texture.get(), 0, nullptr, depths.data(), 64 * sizeof(float), 0);
		Require(std::abs(draw(0, true) - .5f) < .001f, "Clear depth must not occlude");
		std::copy_n(quads.begin(), 12, quads.begin() + 12);
		Require(std::abs(draw(0, false, 1, 2) - .75f) < .001f, "Overlapping translucent instances must retain blend order");
		constants.SetVariable("orientation", std::array<float, 4>{ -1, -1, 0, 0 });
		Require(std::abs(draw(0, true) - .5f) < .001f, "Flipped submit bounds must preserve visible geometry");
		// Swapping out the pass must restore every pipeline binding, including the render target.
		auto device1 = device.as<ID3D11Device1>();
		auto context1 = context.as<ID3D11DeviceContext1>();
		ComPtr<ID3DDeviceContextState> privateState;
		Check(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, privateState.put()));
		Util::SetResourceName(privateState.get(), "WorldOverlayTest::ContextState");
		{
			Util::ScopedDeviceContextState scope(context1.get(), privateState.get());
			context->ClearState();
		}
		ComPtr<ID3D11RenderTargetView> restored;
		context->OMGetRenderTargets(1, restored.put(), nullptr);
		Require(restored.get() == target.rtv.get(), "Graphics state was not restored");
		for (UINT64 i = 0; i < debug->GetNumStoredMessages(); ++i) {
			SIZE_T size = 0;
			Check(debug->GetMessage(i, nullptr, &size));
			std::vector<std::byte> bytes(size);
			auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
			Check(debug->GetMessage(i, message, &size));
			Require(message->Severity > D3D11_MESSAGE_SEVERITY_WARNING, message->pDescription);
		}
	}
}
int main()
{
	try {
		ValidateInputs();
		RenderChecks();
		std::cout << "World overlay input, stereo depth, premultiplied blend and state checks passed (WARP + debug layer).\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
