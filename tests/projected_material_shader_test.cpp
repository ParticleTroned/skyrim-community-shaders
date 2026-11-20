#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <d3dcompiler.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using Microsoft::WRL::ComPtr;
	using Pixel = std::array<float, 4>;
	constexpr UINT width = 4, height = 2;

	ComPtr<ID3DBlob> Compile(const char* entry, const char* profile)
	{
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(L"tests/projected_material_shader_test.hlsl", nullptr,
			D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> texture, output, staging;
		ComPtr<ID3D11ShaderResourceView> textureView;
		ComPtr<ID3D11RenderTargetView> outputView;
		ComPtr<ID3D11SamplerState> sampler;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11ShaderReflection> reflection;

		Fixture()
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
			std::array<std::vector<Pixel>, 4> mips;
			std::array<D3D11_SUBRESOURCE_DATA, 4> data{};
			for (UINT mip = 0; mip < mips.size(); ++mip) {
				const UINT size = 8 >> mip;
				for (UINT y = 0; y < size; ++y)
					for (UINT x = 0; x < size; ++x)
						mips[mip].push_back({ (float(x) + 0.5f) / float(size), (float(y) + 0.5f) / float(size), float(mip), 1 });
				data[mip].pSysMem = mips[mip].data();
				data[mip].SysMemPitch = size * sizeof(Pixel);
			}
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 8;
			desc.MipLevels = 4;
			desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			Check(device->CreateTexture2D(&desc, data.data(), &texture));
			Util::SetResourceName(texture.Get(), "ProjectedMaterialTest::Material");
			Check(device->CreateShaderResourceView(texture.Get(), nullptr, &textureView));
			Util::SetResourceName(textureView.Get(), "ProjectedMaterialTest::Material SRV");
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			Check(device->CreateTexture2D(&desc, nullptr, &output));
			Util::SetResourceName(output.Get(), "ProjectedMaterialTest::Output");
			Check(device->CreateRenderTargetView(output.Get(), nullptr, &outputView));
			Util::SetResourceName(outputView.Get(), "ProjectedMaterialTest::Output RTV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, &staging));
			Util::SetResourceName(staging.Get(), "ProjectedMaterialTest::Readback");
			D3D11_SAMPLER_DESC sample{};
			sample.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			sample.MaxLOD = std::numeric_limits<float>::max();
			Check(device->CreateSamplerState(&sample, &sampler));
			Util::SetResourceName(sampler.Get(), "ProjectedMaterialTest::Sampler");
			auto vs = Compile("VSMain", "vs_5_0");
			auto ps = Compile("PSMain", "ps_5_0");
			Check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex));
			Util::SetResourceName(vertex.Get(), "ProjectedMaterialTest::Vertex");
			Check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel));
			Util::SetResourceName(pixel.Get(), "ProjectedMaterialTest::Pixel");
			Check(D3DReflect(ps->GetBufferPointer(), ps->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		}

		float Draw(ConstantBuffer& constants)
		{
			constants.Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
			const D3D11_VIEWPORT viewport{ 0, 0, float(width), float(height), 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			auto* srv = textureView.Get();
			auto* samp = sampler.Get();
			auto* rtv = outputView.Get();
			context->PSSetShaderResources(0, 1, &srv);
			context->PSSetSamplers(0, 1, &samp);
			context->OMSetRenderTargets(1, &rtv, nullptr);
			const float sentinel[4]{ 100, 100, 100, 100 };
			context->ClearRenderTargetView(rtv, sentinel);
			context->Draw(3, 0);
			context->OMSetRenderTargets(0, nullptr, nullptr);
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			float maximumError = 0;
			for (UINT y = 0; y < height; ++y) {
				const auto* row = reinterpret_cast<const Pixel*>(static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch);
				for (UINT x = 0; x < width; ++x)
					for (float value : row[x])
						maximumError = std::isfinite(value) ? std::max(maximumError, std::abs(value)) : std::numeric_limits<float>::infinity();
			}
			context->Unmap(staging.Get(), 0);
			return maximumError;
		}
	};
}

int main()
{
	try {
		Fixture fixture;
		ConstantBuffer constants(fixture.device.Get(), fixture.reflection.Get(), "TestInput");
		unsigned cases = 0;
		for (UINT route = 0; route < 3; ++route)
			for (UINT plane = 0; plane < 3; ++plane)
				for (float scale : { 1.0f, -2.0f })
					for (float bias : { 0.0f, 3.0f })
						for (UINT displaced : { 0u, 1u }) {
							constants.SetVariable("Route", route);
							constants.SetVariable("Plane", plane);
							constants.SetVariable("Scale", scale);
							constants.SetVariable("Bias", bias);
							constants.SetVariable("Displaced", displaced);
							if (fixture.Draw(constants) > 1e-5f)
								throw std::runtime_error("Projected material coordinate/filtering mismatch, case " + std::to_string(cases));
							++cases;
						}
		constants.SetVariable("Route", 3u);
		constants.SetVariable("Scale", 1.0f);
		constants.SetVariable("Displaced", 1u);
		if (fixture.Draw(constants) < 1.0f)
			throw std::runtime_error("Fixture failed to detect parallax-driven mip discontinuity");
		std::cout << cases << " projected-material render cases passed; mip-discontinuity regression detected\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
