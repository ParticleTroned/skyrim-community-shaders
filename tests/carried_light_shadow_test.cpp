#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <d3dcompiler.h>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using Microsoft::WRL::ComPtr;
	using Pixel = std::array<float, 4>;
	constexpr UINT width = 8;

	ComPtr<ID3DBlob> Compile(bool pixel, bool vr)
	{
		std::vector<D3D_SHADER_MACRO> macros{ { "RENDER_SHADOWMAP", "1" }, { "RENDER_SHADOWMAP_PB", "1" } };
		if (pixel)
			macros.push_back({ "PSHADER", "1" });
		if (vr)
			macros.push_back({ "VR", "1" });
		macros.push_back({ nullptr, nullptr });
		Util::CustomInclude includes{ "package/Shaders" };
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(L"tests/carried_light_shadow_test.hlsl", macros.data(), &includes,
			pixel ? "PixelMain" : "VertexMain", pixel ? "ps_5_0" : "vs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	void Verify(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
	{
		const auto vs = Compile(false, vr), ps = Compile(true, vr);
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11PixelShader> pixel;
		Check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex));
		Util::SetResourceName(vertex.Get(), "CarriedShadowTest::VS");
		Check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel));
		Util::SetResourceName(pixel.Get(), "CarriedShadowTest::PS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(ps->GetBufferPointer(), ps->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		ConstantBuffer parameters(device, reflection.Get(), "TestParameters"), permutation(device, reflection.Get(), "Permutation::PerShader");
		const float cutoff = 12.0f / 256.0f;
		const std::array<Pixel, width> cases{
			Pixel{ .01f, 1, .5f, cutoff }, Pixel{ cutoff, 1, .5f, cutoff }, Pixel{ .1f, 1, .5f, cutoff },
			Pixel{ .1f, 1, -.1f, cutoff }, Pixel{ .01f, 1, .5f, 0 }, Pixel{ 0, 1, .5f, cutoff },
			Pixel{ .01f, 1, .5f, std::numeric_limits<float>::quiet_NaN() }, Pixel{ .01f, 1, -.1f, cutoff }
		};
		parameters.SetVariable("Cases", cases);
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		ComPtr<ID3D11Texture2D> output, readback;
		ComPtr<ID3D11RenderTargetView> target;
		Check(device->CreateTexture2D(&desc, nullptr, &output));
		Util::SetResourceName(output.Get(), "CarriedShadowTest::Output");
		Check(device->CreateRenderTargetView(output.Get(), nullptr, &target));
		Util::SetResourceName(target.Get(), "CarriedShadowTest::Output RTV");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, &readback));
		Util::SetResourceName(readback.Get(), "CarriedShadowTest::Readback");
		for (const UINT flags : { 0u, 1u << 31, 1u << 7, 0u }) {
			context->ClearState();
			permutation.SetVariable("Permutation::ExtraShaderDescriptor", flags);
			permutation.Bind(context, D3D11ShaderTest::Stage::Pixel);
			parameters.Bind(context, D3D11ShaderTest::Stage::Pixel);
			const Pixel clear{ -1, -1, -1, -1 };
			context->ClearRenderTargetView(target.Get(), clear.data());
			auto* raw = target.Get();
			context->OMSetRenderTargets(1, &raw, nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			const D3D11_VIEWPORT viewport{ 0, 0, float(width), 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->Draw(3, 0);
			context->ClearState();
			context->CopyResource(readback.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Pixel, width> actual;
			std::memcpy(actual.data(), mapped.pData, sizeof(actual));
			context->Unmap(readback.Get(), 0);
			for (UINT i = 0; i < width; ++i) {
				const bool discarded = i == 3 || i == 7 || ((flags & (1u << 31)) && (i == 0 || i == 5));
				const float expected = discarded ? -1.0f : cases[i][0];
				if (!std::isfinite(actual[i][0]) || std::abs(actual[i][0] - expected) > 1e-6f)
					throw std::runtime_error("Carried cutoff mismatch: VR=" + std::to_string(vr) +
											 " flags=" + std::to_string(flags) + " case=" + std::to_string(i) +
											 " actual=" + std::to_string(actual[i][0]) + " expected=" + std::to_string(expected));
			}
		}
	}
}

int main()
try {
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
	for (bool vr : { false, true })
		Verify(device.Get(), context.Get(), vr);
	std::cout << "Carried shadow cutoff: disabled, enabled, boundaries, hemisphere and flat/VR passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
