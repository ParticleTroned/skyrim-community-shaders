#define NOMINMAX
#include "Features/Upscaling/NeuralRendering/CharacterSettings.h"
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	constexpr UINT width = 16, height = 8;
	struct Includes : ID3DInclude
	{
		Util::CustomInclude package{ "package/Shaders" }, feature{ "features/Upscaling/Shaders" };
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			return std::string_view(name).starts_with("Upscaling/") ? feature.Open(type, name, parent, data, size) : package.Open(type, name, parent, data, size);
		}
		HRESULT Close(LPCVOID data) override { return package.Close(data); }
	};

	struct Texture
	{
		ComPtr<ID3D11Texture2D> texture, staging;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		Texture(ID3D11Device* device, DXGI_FORMAT format, const void* data, UINT stride)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = format;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			const D3D11_SUBRESOURCE_DATA initial{ data, width * stride, 0 };
			Check(device->CreateTexture2D(&desc, data ? &initial : nullptr, &texture));
			Util::SetResourceName(texture.Get(), "ActorBlendTest::Texture");
			Check(device->CreateShaderResourceView(texture.Get(), nullptr, &srv));
			Util::SetResourceName(srv.Get(), "ActorBlendTest::Texture SRV");
			Check(device->CreateUnorderedAccessView(texture.Get(), nullptr, &uav));
			Util::SetResourceName(uav.Get(), "ActorBlendTest::Texture UAV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, &staging));
			Util::SetResourceName(staging.Get(), "ActorBlendTest::Readback");
		}
	};

	struct Shader
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		Shader(ID3D11Device* device, const wchar_t* path, bool vr)
		{
			Includes includes;
			const D3D_SHADER_MACRO defines[]{ { "VR", "1" }, { nullptr, nullptr } };
			ComPtr<ID3DBlob> code, errors;
			const auto result = D3DCompileFromFile(path, vr ? defines : defines + 1, &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &code, &errors);
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "ActorBlendTest::CS");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		}
	};

	void Run(ID3D11Device* device, ID3D11DeviceContext* context, bool vr,
		const NeuralRendering::CharacterSettings& settings)
	{
		std::array<std::uint8_t, width * height> weights{};
		weights.fill(255);
		const bool scene = NeuralRendering::UsesSceneCharacterStrengths(settings);
		auto strengths = NeuralRendering::GetCharacterCategoryStrengths(settings);
		if (scene)
			for (unsigned index = 0; index < strengths.size(); ++index)
				if (!NeuralRendering::IsCharacterCategoryEnabled(NeuralRendering::CharacterPolicy::kCategories[index], settings))
					strengths[index] = 1.0f;
		for (UINT y = 3; y < 6; ++y)
			for (UINT x = 5; x < 8; ++x)
				weights[y * width + x] = x == 5 ? 0 : x == 6 ? static_cast<std::uint8_t>(std::lround(255.0f * strengths[y == 3 ? 0 : y == 4 ? 3 :
																																			  4])) :
				                                               255;
		Texture mask(device, DXGI_FORMAT_R8_UNORM, weights.data(), 1);
		Texture protection(device, DXGI_FORMAT_R8_UNORM, nullptr, 1);
		Shader alpha(device, L"package/Shaders/DLSS5ActorProtectionCS.hlsl", vr);
		ConstantBuffer alphaCB(device, alpha.reflection.Get(), "ActorProtection");
		alphaCB.SetVariable("EvaluationRect", scene ? std::array<UINT, 4>{ 0, 0, width, height } : std::array<UINT, 4>{ 3, 2, 9, 5 });
		alphaCB.SetVariable("SelectionRect", scene ? std::array<UINT, 4>{ 0, 0, width, height } : std::array<UINT, 4>{ 5, 3, 3, 3 });
		alphaCB.Bind(context);
		const float clear[]{ 64.0f / 255.0f, 0, 0, 0 };
		context->ClearUnorderedAccessViewFloat(protection.uav.Get(), clear);
		auto* input = mask.srv.Get();
		auto* output = protection.uav.Get();
		context->CSSetShader(alpha.shader.Get(), nullptr, 0);
		context->CSSetShaderResources(0, 1, &input);
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context->Dispatch(2, 1, 1);
		context->ClearState();
		context->CopyResource(protection.staging.Get(), protection.texture.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(protection.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		bool valid = true;
		for (UINT y = 0; y < height; ++y) {
			for (UINT x = 0; x < width; ++x) {
				const bool evaluated = scene || (x >= 3 && x < 12 && y >= 2 && y < 7);
				const bool selected = scene || (x >= 5 && x < 8 && y >= 3 && y < 6);
				const auto expected = evaluated ? (selected ? 255 - weights[y * width + x] : 255) : 64;
				valid &= static_cast<const std::uint8_t*>(mapped.pData)[y * mapped.RowPitch + x] == expected;
			}
		}
		context->Unmap(protection.staging.Get(), 0);
		if (!valid)
			throw std::runtime_error("Protection inversion, context ownership or ROI dispatch failed");

		std::array<Pixel, width * height> original{}, neural{};
		for (UINT i = 0; i < original.size(); ++i) {
			original[i] = { 0.2f, 0.2f, 0.2f, 1.0f };
			neural[i] = { 0.8f, 0.8f, 0.8f, 1.0f };
		}
		Texture baseline(device, DXGI_FORMAT_R32G32B32A32_FLOAT, original.data(), sizeof(Pixel));
		Texture model(device, DXGI_FORMAT_R32G32B32A32_FLOAT, neural.data(), sizeof(Pixel));
		Texture composite(device, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, sizeof(Pixel));
		Shader blend(device, L"features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl", vr);
		ConstantBuffer blendCB(device, blend.reflection.Get(), "FoveatedCenterBlendCB");
		blendCB.SetVariable("InvOutputDim", std::array{ 1.0f / width, 1.0f / height });
		blendCB.SetVariable("InvSourceDim", std::array{ 1.0f / width, 1.0f / height });
		blendCB.SetVariable("DispatchDim", std::array{ float(width), float(height) });
		blendCB.SetVariable("FullImage", 1u);
		blendCB.SetVariable("CharacterMaskBounds", scene ? Pixel{ 0, 0, 1, 1 } : Pixel{ 5.0f / width, 3.0f / height, 8.0f / width, 6.0f / height });
		for (UINT mode : { 1u, 2u }) {
			// The provider fixture models UIAlpha blending before CSX commits it.
			for (UINT i = 0; i < neural.size(); ++i) {
				const float weight = mode == 2 ? weights[i] / 255.0f : 1.0f;
				neural[i] = { 0.2f + 0.6f * weight, 0.2f + 0.6f * weight, 0.2f + 0.6f * weight, 1.0f };
			}
			context->UpdateSubresource(model.texture.Get(), 0, nullptr, neural.data(), width * sizeof(Pixel), 0);
			blendCB.SetVariable("CharacterSelectionMode", mode);
			blendCB.Bind(context);
			const std::array<ID3D11ShaderResourceView*, 3> inputs{ model.srv.Get(), baseline.srv.Get(), mask.srv.Get() };
			output = composite.uav.Get();
			context->CSSetShader(blend.shader.Get(), nullptr, 0);
			context->CSSetShaderResources(0, 3, inputs.data());
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->Dispatch(2, 1, 1);
			context->ClearState();
			context->CopyResource(composite.staging.Get(), composite.texture.Get());
			Check(context->Map(composite.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			valid = true;
			for (UINT y = 0; y < height; ++y) {
				const auto* row = reinterpret_cast<const Pixel*>(static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch);
				for (UINT x = 0; x < width; ++x) {
					const float weight = scene || (x >= 5 && x < 8 && y >= 3 && y < 6) ? weights[y * width + x] / 255.0f : 0.0f;
					const float expected = 0.2f + 0.6f * weight;
					valid &= std::abs(row[x][0] - expected) < 1e-6f;
				}
			}
			context->Unmap(composite.staging.Get(), 0);
			if (!valid)
				throw std::runtime_error("Actor composite changed ownership or applied provider strength twice");
		}
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
		for (bool vr : { false, true }) {
			for (unsigned selection = 0; selection < 4; ++selection) {
				for (float strength : { 0.0f, 0.375f, 1.0f }) {
					NeuralRendering::CharacterSettings settings;
					settings.armor = (selection & 1u) != 0;
					settings.weapons = (selection & 2u) != 0;
					settings.armorStrength = strength;
					settings.weaponsStrength = 1.0f - strength;
					Run(device.Get(), context.Get(), vr, settings);
					settings.sceneStrengthsEnabled = true;
					Run(device.Get(), context.Get(), vr, settings);
				}
			}
		}
		std::cout << "PASS: scene and actor strengths, UIAlpha protection, crop bounds and both composite paths (48 flat/VR cases; provider output simulated)\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
