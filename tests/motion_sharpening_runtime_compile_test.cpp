#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using Pixel = std::array<float, 4>;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	constexpr UINT kEyeWidth = 9, kHeight = 7;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	std::vector<Pixel> DrawFixed(ID3D11Device* device, ID3D11DeviceContext* context,
		ID3D11ComputeShader* shader, ConstantBuffer& constants, std::span<const Pixel> pixels, UINT width, UINT splitX)
	{
		context->ClearState();
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = kHeight;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		const D3D11_SUBRESOURCE_DATA data{ pixels.data(), width * static_cast<UINT>(sizeof(Pixel)), 0 };
		ComPtr<ID3D11Texture2D> input, output, staging;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		Check(device->CreateTexture2D(&desc, &data, input.GetAddressOf()));
		Util::SetResourceName(input.Get(), "SharpeningTest::Input");
		Check(device->CreateShaderResourceView(input.Get(), nullptr, srv.GetAddressOf()));
		Util::SetResourceName(srv.Get(), "SharpeningTest::Input SRV");
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		Check(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()));
		Util::SetResourceName(output.Get(), "SharpeningTest::Output");
		Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
		Util::SetResourceName(uav.Get(), "SharpeningTest::Output UAV");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "SharpeningTest::Readback");
		constants.SetVariable("outputSplitX", splitX);
		constants.Bind(context);
		auto* source = srv.Get();
		auto* target = uav.Get();
		context->CSSetShaderResources(0, 1, &source);
		context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
		context->CSSetShader(shader, nullptr, 0);
		context->Dispatch((width + 7) / 8, (kHeight + 7) / 8, 1);
		context->ClearState();
		context->CopyResource(staging.Get(), output.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::vector<Pixel> result(width * kHeight);
		for (UINT y = 0; y < kHeight; ++y)
			std::memcpy(result.data() + y * width, static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, width * sizeof(Pixel));
		context->Unmap(staging.Get(), 0);
		return result;
	}

	void CheckFixedEyeIsolation(ID3D11Device* device, ID3D11DeviceContext* context, ID3DBlob* code, bool rcas)
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SharpeningTest::Fixed CS");
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer constants(device, reflection.Get(), rcas ? "RCASConfig" : "LumaSharpenConfig");
		Require(constants.bytes.size() == 16 && constants.Offset("outputSplitX") == 8, "Fixed sharpening constant layout changed");
		constants.SetVariable("sharpness", 0.9f);
		if (!rcas)
			constants.SetVariable("limit", 0.75f);
		std::vector<Pixel> packed(2 * kEyeWidth * kHeight);
		for (UINT y = 0; y < kHeight; ++y)
			for (UINT x = 0; x < 2 * kEyeWidth; ++x) {
				const float value = (x < kEyeWidth ? .12f : .65f) + .01f * (x % kEyeWidth) + .007f * y;
				packed[y * 2 * kEyeWidth + x] = { value, value * .7f, value * .5f, .2f + .03f * x };
			}
		const auto isolated = DrawFixed(device, context, shader.Get(), constants, packed, 2 * kEyeWidth, kEyeWidth);
		const auto crossed = DrawFixed(device, context, shader.Get(), constants, packed, 2 * kEyeWidth, 0);
		bool seamDifference = false;
		for (UINT eye = 0; eye < 2; ++eye) {
			// Duplicate only the internal edge; RCAS keeps zero loads at outer texture edges.
			std::vector<Pixel> privateEye((kEyeWidth + 1) * kHeight);
			for (UINT y = 0; y < kHeight; ++y)
				for (UINT x = 0; x <= kEyeWidth; ++x) {
					const UINT sourceX = eye ? std::max(x, 1u) - 1 : std::min(x, kEyeWidth - 1);
					privateEye[y * (kEyeWidth + 1) + x] = packed[y * 2 * kEyeWidth + eye * kEyeWidth + sourceX];
				}
			const auto expected = DrawFixed(device, context, shader.Get(), constants, privateEye, kEyeWidth + 1, 0);
			for (UINT y = 0; y < kHeight; ++y)
				for (UINT x = 0; x < kEyeWidth; ++x) {
					const auto index = y * 2 * kEyeWidth + eye * kEyeWidth + x;
					for (unsigned channel = 0; channel < 4; ++channel) {
						Require(std::isfinite(isolated[index][channel]) &&
									std::abs(isolated[index][channel] - expected[y * (kEyeWidth + 1) + x + eye][channel]) < 2e-5f,
							"Fixed sharpening crossed eyes or changed outer-edge, colour or alpha behaviour");
						if (x == (eye ? 0u : kEyeWidth - 1))
							seamDifference |= std::abs(isolated[index][channel] - crossed[index][channel]) > 1e-5f;
					}
				}
		}
		Require(seamDifference, "Eye-seam fixture did not distinguish a cross-eye sample");
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		const std::filesystem::path shaderRoot = "features/Upscaling/Shaders";
		Util::CustomInclude include(shaderRoot);
		unsigned failures = 0;
		unsigned compiled = 0;
		unsigned compared = 0;
		for (const auto* shader : { "RCAS/RCAS.hlsl", "LumaSharpen/LumaSharpen.hlsl" }) {
			for (const bool adaptive : { false, true }) {
				for (const bool vr : { false, true }) {
					for (const bool hdr : { false, true }) {
						std::vector<D3D_SHADER_MACRO> defines{ { "COMPUTESHADER", "1" }, { "WINPC", "1" }, { "DX11", "1" } };
						if (adaptive)
							defines.push_back({ "MOTION_ADAPTIVE", "1" });
						if (vr)
							defines.push_back({ "VR", "1" });
						if (hdr)
							defines.push_back({ "HDR_OUTPUT", "1" });
						defines.push_back({ nullptr, nullptr });
						const auto path = shaderRoot / "Upscaling" / shader;
						ComPtr<ID3DBlob> bytecode, errors;
						const HRESULT result = D3DCompileFromFile(path.c_str(), defines.data(), &include, "main", "cs_5_0",
							D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
							0, bytecode.GetAddressOf(), errors.GetAddressOf());
						if (FAILED(result) || !bytecode) {
							++failures;
							std::cerr << shader << " adaptive=" << adaptive << " VR=" << vr << " HDR=" << hdr << '\n';
							if (errors)
								std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
						} else {
							++compiled;
							if (!adaptive) {
								CheckFixedEyeIsolation(device.Get(), context.Get(), bytecode.Get(), std::string_view(shader).starts_with("RCAS/"));
								++compared;
							}
						}
					}
				}
			}
		}
		std::cout << compiled << " runtime shader permutations compiled; " << failures << " failed; "
		          << compared << " fixed eye-seam comparisons completed.\n";
		return failures == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
