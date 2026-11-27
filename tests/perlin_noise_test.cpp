#define NOMINMAX
#include "ShaderPackageIncludes.h"
#include "Utils/Finite.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace
{
	using D3D11ShaderTest::Check;
	using Microsoft::WRL::ComPtr;

	void Run(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, UINT flags)
	{
		auto includes = PackageIncludes::AllFeatures(".");
		std::vector<D3D_SHADER_MACRO> defines{ { "CSHADER", "1" }, { "SKIN", "1" }, { "CS_SKIN", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(L"tests/perlin_noise.hlsl", defines.data(), &includes, "main", "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | flags, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
		Util::SetResourceName(shader.Get(), "PerlinNoiseTest::CS");
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 64;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		ComPtr<ID3D11Texture2D> output, staging;
		Check(device->CreateTexture2D(&desc, nullptr, &output));
		Util::SetResourceName(output.Get(), "PerlinNoiseTest::Output");
		ComPtr<ID3D11UnorderedAccessView> view;
		Check(device->CreateUnorderedAccessView(output.Get(), nullptr, &view));
		Util::SetResourceName(view.Get(), "PerlinNoiseTest::Output UAV");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, &staging));
		Util::SetResourceName(staging.Get(), "PerlinNoiseTest::Readback");
		auto* target = view.Get();
		context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(8, 8, 1);
		context->ClearState();
		context->CopyResource(staging.Get(), output.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<float, 4> maximum{};
		bool valid = true;
		for (unsigned y = 0; y < 64; ++y) {
			const auto* pixels = reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch);
			for (unsigned x = 0; x < 64; ++x)
				for (unsigned channel = 0; channel < 4; ++channel) {
					const float error = pixels[x * 4 + channel];
					// Nested puddle coordinates amplify compiler rounding in the reference path.
					const float tolerance = channel == 3 ? 5e-6f : 2e-6f;
					valid = valid && Util::IsFinite(error) && error >= 0 && error <= tolerance;
					maximum[channel] = std::max(maximum[channel], error);
				}
		}
		context->Unmap(staging.Get(), 0);
		std::cout << "VR=" << vr << " flags=" << flags << " maximum gradient/noise/FBM/endpoint-puddle errors:";
		for (float error : maximum)
			std::cout << ' ' << error;
		std::cout << '\n';
		if (!valid)
			throw std::runtime_error("Scalar Perlin changed the reference gradient, noise or skin FBM");
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
		for (bool vr : { false, true })
			for (UINT flags : { 0u, UINT(D3DCOMPILE_PARTIAL_PRECISION), UINT(D3DCOMPILE_AVOID_FLOW_CONTROL), UINT(D3DCOMPILE_PARTIAL_PRECISION | D3DCOMPILE_AVOID_FLOW_CONTROL) })
				Run(device.Get(), context.Get(), vr, flags);
		std::cout << "Perlin WARP parity passed: all 16 gradients, negative coordinates, lattice boundaries, seeds and five-octave skin noise\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
