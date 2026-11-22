#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using Microsoft::WRL::ComPtr;
	using Pixel = std::array<float, 4>;
	constexpr UINT outputWidth = 256;
	void Require(bool condition)
	{
		if (!condition)
			throw std::runtime_error("Paraboloid sampling regression");
	}
	ComPtr<ID3DBlob> Compile(const wchar_t* path, const char* entry, const char* profile, const char* filter, bool vr, const char* layout = nullptr)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "SHADOWFILTER", filter } };
		if (layout) {
			defines.push_back({ "PSHADER", "1" });
			defines.push_back({ "RENDER_DEPTH", "1" });
			defines.push_back({ layout, "1" });
		}
		if (vr)
			defines.push_back({ "VR", "1" });
		defines.push_back({ nullptr, nullptr });
		Util::CustomInclude includes{ "package/Shaders" };
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(path, defines.data(), &includes, entry, profile,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	struct Fixture
	{
		ID3D11DeviceContext* context;
		UINT width, height;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11Texture2D> shadow, output, readback;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> target;
		ComPtr<ID3D11SamplerState> regular, comparison;
		std::unique_ptr<ConstantBuffer> parameters;

		Fixture(ID3D11Device* device, ID3D11DeviceContext* context, UINT width, UINT height, const char* filter, bool vr) :
			context(context), width(width), height(height)
		{
			const auto vs = Compile(L"tests/paraboloid_sampling_test.hlsl", "VertexMain", "vs_5_0", filter, vr);
			const auto ps = Compile(L"tests/paraboloid_sampling_test.hlsl", "PixelMain", "ps_5_0", filter, vr);
			Check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex));
			Util::SetResourceName(vertex.Get(), "ParaboloidSamplingTest::VS");
			Check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel));
			Util::SetResourceName(pixel.Get(), "ParaboloidSamplingTest::PS");
			Check(D3DReflect(ps->GetBufferPointer(), ps->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			parameters = std::make_unique<ConstantBuffer>(device, reflection.Get(), "TestParameters");
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			Check(device->CreateTexture2D(&desc, nullptr, &shadow));
			Util::SetResourceName(shadow.Get(), "ParaboloidSamplingTest::Shadow");
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = desc.Format;
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			view.Texture2DArray.MipLevels = view.Texture2DArray.ArraySize = 1;
			Check(device->CreateShaderResourceView(shadow.Get(), &view, &srv));
			Util::SetResourceName(srv.Get(), "ParaboloidSamplingTest::Shadow SRV");
			desc.Width = outputWidth;
			desc.Height = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			Check(device->CreateTexture2D(&desc, nullptr, &output));
			Util::SetResourceName(output.Get(), "ParaboloidSamplingTest::Output");
			Check(device->CreateRenderTargetView(output.Get(), nullptr, &target));
			Util::SetResourceName(target.Get(), "ParaboloidSamplingTest::Output RTV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, &readback));
			Util::SetResourceName(readback.Get(), "ParaboloidSamplingTest::Readback");
			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
			Check(device->CreateSamplerState(&sampler, &regular));
			Util::SetResourceName(regular.Get(), "ParaboloidSamplingTest::Sampler");
			sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			Check(device->CreateSamplerState(&sampler, &comparison));
			Util::SetResourceName(comparison.Get(), "ParaboloidSamplingTest::ComparisonSampler");
		}

		void Verify(bool dual, bool invert, bool lowerPatch)
		{
			context->ClearState();
			std::vector<float> depths(width * height, 1.0f);
			for (UINT y = 0; y < height; ++y)
				for (UINT x = 0; x < width; ++x) {
					const float u = (x + .5f) / width, v = (y + .5f) / height;
					const bool lower = dual && v >= .5f;
					const float qx = 2 * u - 1, qy = dual ? 4 * v - (lower ? 3 : 1) : 2 * v - 1;
					if (qx * qx + qy * qy <= 1) {
						const bool lit = lowerPatch ? v < .6f : lower != invert;
						depths[y * width + x] = lit ? .8f : .2f;
					}
				}
			context->UpdateSubresource(shadow.Get(), 0, nullptr, depths.data(), width * sizeof(float), 0);
			std::array<Pixel, outputWidth> cases{};
			const float rim = std::clamp(1 - 1.5f * std::max(2.0f / width, (dual ? 4.0f : 2.0f) / height), 0.0f, 1.0f);
			for (UINT i = 0; i < outputWidth; ++i) {
				const bool lower = dual && (i & 1);
				const float centerY = dual ? (lower ? .75f : .25f) : .5f;
				const float angle = float(i / 8) * std::numbers::pi_v<float> / 16;
				const float radius = i % 8 < 4 ? .35f * rim : 1.08f;
				cases[i] = lowerPatch ? Pixel{ .5f, .75f, .02f, 0 } :
				                        Pixel{ .5f + std::cos(angle) * radius * .5f, centerY + std::sin(angle) * radius / (dual ? 4 : 2), (i & 2) ? .04f : 0, lower ? 1.0f : 0.0f };
			}
			parameters->SetVariable("Cases", cases);
			parameters->SetVariable("Dual", UINT(dual));
			parameters->Bind(context, D3D11ShaderTest::Stage::Pixel);
			ID3D11ShaderResourceView* views[]{ srv.Get(), srv.Get() };
			context->PSSetShaderResources(3, 2, views);
			ID3D11SamplerState* samplers[]{ regular.Get(), comparison.Get() };
			context->PSSetSamplers(3, 2, samplers);
			auto* outputView = target.Get();
			context->OMSetRenderTargets(1, &outputView, nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			const D3D11_VIEWPORT viewport{ 0, 0, float(outputWidth), 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->Draw(3, 0);
			context->ClearState();
			context->CopyResource(readback.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Pixel, outputWidth> actual;
			std::memcpy(actual.data(), mapped.pData, sizeof(actual));
			context->Unmap(readback.Get(), 0);
			for (UINT i = 0; i < outputWidth; ++i) {
				const bool lower = cases[i][3] != 0;
				const float expected = !lowerPatch && (lower != invert) ? 1.0f : 0.0f;
				for (float value : actual[i])
					Require(std::isfinite(value));
				if (std::abs(actual[i][0] - expected) > 1e-5f) {
					std::cerr << "width=" << width << " height=" << height << " dual=" << dual << " invert=" << invert
							  << " lowerPatch=" << lowerPatch << " case=" << i << " visibility=" << actual[i][0] << '\n';
					throw std::runtime_error("Filter reached an empty rim, opposite hemisphere or wrong single-map row");
				}
				Require(std::abs(actual[i][3] - rim) < 1e-6f);
				const float centerY = dual ? (lower ? .75f : .25f) : .5f;
				const float qx = (actual[i][1] - .5f) * 2, qy = (actual[i][2] - centerY) * (dual ? 4 : 2);
				Require(qx * qx + qy * qy <= rim * rim + 1e-5f);
				const float inputX = (cases[i][0] - .5f) * 2, inputY = (cases[i][1] - centerY) * (dual ? 4 : 2);
				if (inputX * inputX + inputY * inputY <= rim * rim)
					Require(actual[i][1] == cases[i][0] && actual[i][2] == cases[i][1]);
			}
		}
	};
}

int main()
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
	for (bool vr : { false, true })
		for (const char* filter : { "0", "1", "3" }) {
			for (const char* layout : { "RENDER_SHADOWMASKPB", "RENDER_SHADOWMASKDPB" })
				Compile(L"package/Shaders/Utility.hlsl", "main", "ps_5_0", filter, vr, layout);
			for (const auto dimensions : { std::array{ 8u, 8u }, std::array{ 64u, 32u }, std::array{ 32u, 64u }, std::array{ 31u, 17u } }) {
				Fixture fixture(device.Get(), context.Get(), dimensions[0], dimensions[1], filter, vr);
				for (bool dual : { false, true })
					for (bool invert : { false, true })
						fixture.Verify(dual, invert, false);
				if (dimensions[0] >= 16 && dimensions[1] >= 16)
					fixture.Verify(false, false, true);
			}
		}
	std::cout << "Paraboloid sampling: native permutations, rim footprints, hemisphere isolation and single-map lower rows passed\n";
}
