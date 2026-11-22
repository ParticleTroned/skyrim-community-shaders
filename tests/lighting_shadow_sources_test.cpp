#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <d3dcompiler.h>
#include <iostream>
#include <string_view>

namespace
{
	using D3D11ShaderTest::Check;
	using Microsoft::WRL::ComPtr;
	using Pixel = std::array<float, 4>;
	constexpr UINT kCases = 512;
	constexpr UINT kSoftLighting = 1u << 10;
	constexpr UINT kRimLighting = 1u << 11;
	constexpr UINT kBackLighting = 1u << 12;
	constexpr UINT kEngineMask = (1u << 13) | (1u << 14);

	struct Material
	{
		const char* name;
		const char* define;
		bool alwaysEligible = false;
		bool ignoresRuntimeFlags = false;
	};

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> output, staging;
		ComPtr<ID3D11UnorderedAccessView> uav;
		ComPtr<ID3D11Buffer> permutation;

		Fixture()
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
				D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = kCases;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()));
			Util::SetResourceName(output.Get(), "LightingShadowTest::Output");
			Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "LightingShadowTest::Output UAV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "LightingShadowTest::Readback");
			D3D11_BUFFER_DESC buffer{};
			buffer.ByteWidth = 16;
			buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			Check(device->CreateBuffer(&buffer, nullptr, permutation.GetAddressOf()));
			Util::SetResourceName(permutation.Get(), "LightingShadowTest::Permutation");
		}

		ComPtr<ID3D11ComputeShader> Compile(bool vr, bool deferred, bool vsm, bool sky, const Material& material, const char* entry = "main")
		{
			std::vector<D3D_SHADER_MACRO> defines{ { "LIGHTING", "1" } };
			for (const auto& [enabled, name] : std::array<std::pair<bool, const char*>, 4>{ { { vr, "VR" }, { deferred, "DEFERRED" }, { vsm, "VOLUMETRIC_SHADOWS" }, { sky, "SKYLIGHTING_SHADOW_VIS" } } })
				if (enabled)
					defines.push_back({ name, "1" });
			if (material.define)
				defines.push_back({ material.define, "1" });
			if (std::string_view(material.name) == "skin-sss") {
				defines.push_back({ "SKIN", "1" });
				defines.push_back({ "SSS", "1" });
			}
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> bytecode, errors;
			const auto compiled = D3DCompileFromFile(LIGHTING_SHADOW_TEST_SHADER, defines.data(), nullptr, entry, "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, bytecode.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			ComPtr<ID3D11ComputeShader> shader;
			Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "LightingShadowTest::CS");
			return shader;
		}

		std::array<Pixel, kCases> Draw(ID3D11ComputeShader* shader, UINT descriptor)
		{
			context->ClearState();
			const std::array<UINT, 4> constants{ 0, descriptor, 0, 0 };
			context->UpdateSubresource(permutation.Get(), 0, nullptr, constants.data(), 0, 0);
			ID3D11Buffer* buffer = permutation.Get();
			context->CSSetConstantBuffers(4, 1, &buffer);
			ID3D11UnorderedAccessView* target = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader, nullptr, 0);
			context->Dispatch(kCases / 64, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Pixel, kCases> pixels;
			std::memcpy(pixels.data(), mapped.pData, sizeof(pixels));
			context->Unmap(staging.Get(), 0);
			return pixels;
		}
	};

	void Verify(const Pixel& actual, UINT condition, UINT descriptor, bool eligible, bool vr, bool deferred, bool vsm, bool sky)
	{
		const bool usableSky = sky && eligible && (condition & 1) && (condition & 2);
		const bool vsmEnabled = (condition & 4) != 0;
		const bool reflection = (condition & 8) != 0;
		const bool world = (condition & 16) != 0;
		const bool directional = (condition & 32) != 0;
		const bool mask = (descriptor & kEngineMask) == kEngineMask;
		const float engineVisibility = condition & 64 ? 1.0f : 0.0f;
		const float skyVisibility = condition & 128 ? 0.2f : 0.8f;
		const float vsmVisibility = condition & 128 ? 0.8f : 0.2f;
		const bool samplesVSM = vsm && world && !reflection && directional && !(deferred && usableSky);
		float detailed = 1.0f;
		if (mask)
			detailed = engineVisibility;
		else if (!deferred) {
			if (vsm)
				detailed = samplesVSM && vsmEnabled ? 0.35f + (vr && (condition & 256) ? 0.1f : 0.0f) : 1.0f;
			else if (world)
				detailed = directional ? 0.45f : 1.0f;
		}
		float soft = detailed;
		if (samplesVSM && vsmEnabled)
			soft = mask ? std::max(vsmVisibility, detailed) : vsmVisibility;
		else if (samplesVSM && !mask)
			soft = 1.0f;
		if (usableSky)
			soft = deferred ? skyVisibility : std::min(soft, skyVisibility);
		const Pixel expected{ soft, detailed, samplesVSM ? 1.0f : 0.0f, eligible ? 1.0f : 0.0f };
		for (size_t channel = 0; channel < actual.size(); ++channel)
			if (!std::isfinite(actual[channel]) || std::abs(actual[channel] - expected[channel]) > 1e-6f)
				throw std::runtime_error("Shadow source mismatch: condition=" + std::to_string(condition) +
										 " descriptor=" + std::to_string(descriptor) + " channel=" + std::to_string(channel) +
										 " expected=" + std::to_string(expected[channel]) + " actual=" + std::to_string(actual[channel]));
	}
}

int main()
{
	try {
		Fixture fixture;
		unsigned permutations = 0, cases = 0;
		constexpr std::array<Material, 7> materials{ { { "dynamic", nullptr }, { "soft", "SOFT_LIGHTING", true }, { "rim", "RIM_LIGHTING", true },
			{ "back", "BACK_LIGHTING", true }, { "skin-sss", "SOFT_LIGHTING", true },
			{ "landscape", "LANDSCAPE", false, true }, { "lod", "LODOBJECTS", false, true } } };
		constexpr std::array<Pixel, 9> ranges{ { { 0, 0, 0, 0 }, { 1, 8, 3, 0 }, { 0, 0, 0, 0 },
			{ 0, 0, 0, 0 }, { 1, 0, 8, 0 }, { 1, 15, 1, 0 },
			{ 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } } };
		for (bool vr : { false, true }) {
			auto shader = fixture.Compile(vr, true, false, false, materials[0], "ContactRanges");
			const auto pixels = fixture.Draw(shader.Get(), 0);
			for (UINT condition = 0; condition < kCases; ++condition)
				if (pixels[condition] != ranges[condition % ranges.size()])
					throw std::runtime_error("Contact-shadow range did not fail closed: case=" + std::to_string(condition % ranges.size()));
		}
		for (bool vr : { false, true })
			for (bool deferred : { false, true })
				for (const auto& [vsm, sky] : std::array<std::pair<bool, bool>, 3>{ { { true, true }, { true, false }, { false, true } } })
					for (const auto& material : materials) {
						auto shader = fixture.Compile(vr, deferred, vsm, sky, material);
						++permutations;
						for (UINT flag : { 0u, 1u << 9, kSoftLighting, kRimLighting, kBackLighting })
							for (UINT mask : { 0u, kEngineMask }) {
								const bool eligible = material.alwaysEligible || (!material.ignoresRuntimeFlags && flag >= kSoftLighting);
								const auto pixels = fixture.Draw(shader.Get(), flag | mask);
								for (UINT condition = 0; condition < kCases; ++condition) {
									Verify(pixels[condition], condition, flag | mask, eligible, vr, deferred, vsm, sky);
									++cases;
								}
							}
					}
		std::cout << "Passed " << permutations << " source-extracted shader permutations and " << cases << " GPU shadow-source cases.\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
