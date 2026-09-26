#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	using Pixels = std::array<Pixel, 8>;
	constexpr Pixels samples{ Pixel{ 0, 0, 0, 1 }, Pixel{ 0.01f, 0.01f, 0.01f, 1 },
		Pixel{ 0.18f, 0.18f, 0.18f, 1 }, Pixel{ 0.6f, 0.6f, 0.6f, 1 },
		Pixel{ 0.4f, 0.2f, 0.1f, 1 }, Pixel{ 2, 4, 8, 1 },
		Pixel{ -0.1f, 0.2f, 0.3f, 1 }, Pixel{ 1e-8f, 1e-8f, 1e-8f, 1 } };

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	bool Close(float a, float b) { return std::abs(a - b) <= 2e-5f * std::max(1.0f, std::abs(b)); }

	ComPtr<ID3DBlob> Compile(const wchar_t* path, std::vector<D3D_SHADER_MACRO> defines, const char* target)
	{
		std::cerr << "Compiling " << std::filesystem::path(path).string() << "\n";
		defines.push_back({ nullptr, nullptr });
		Util::CustomInclude includes{ "package/Shaders" };
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(path, defines.data(), &includes, "main", target,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, code.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	struct Fixture
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11Texture2D> output, staging;
		ComPtr<ID3D11UnorderedAccessView> uav;
		std::unique_ptr<ConstantBuffer> feature, inputs;

		Fixture(ID3D11Device* device, bool vr)
		{
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" } };
			if (vr)
				defines.push_back({ "VR", "1" });
			auto code = Compile(L"tests/adaptive_balance_color.hlsl", defines, "cs_5_0");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "AdaptiveColorTest::CS");
			feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
			inputs = std::make_unique<ConstantBuffer>(device, reflection.Get(), "Samples");
			inputs->SetVariable("colors", samples);
			auto* balance = feature->reflection->GetVariableByName("SharedData::adaptiveBalanceSettings");
			D3D11_SHADER_VARIABLE_DESC desc{};
			Check(balance->GetDesc(&desc));
			Require(desc.Size == 80, "Adaptive Balance buffer size changed");
			for (auto [name, offset] : { std::pair{ "contrast", 40u }, std::pair{ "saturation", 44u },
					 std::pair{ "cloudBrightness", 48u }, std::pair{ "cloudSaturation", 52u },
					 std::pair{ "fogIntensity", 56u }, std::pair{ "sunGlareIntensity", 60u },
					 std::pair{ "skyStaticBrightness", 64u }, std::pair{ "skyStaticTransparency", 68u } }) {
				D3D11_SHADER_TYPE_DESC member{};
				Check(balance->GetType()->GetMemberTypeByName(name)->GetDesc(&member));
				Require(member.Offset == offset, "Adaptive Balance buffer layout differs from C++");
			}
			auto* linear = feature->reflection->GetVariableByName("SharedData::linearLightingSettings");
			Check(linear->GetDesc(&desc));
			Require(desc.Size == 112, "Linear Lighting buffer size changed");
			D3D11_SHADER_TYPE_DESC gamma{};
			Check(linear->GetType()->GetMemberTypeByName("cloudGamma")->GetDesc(&gamma));
			Require(gamma.Offset == 104, "Cloud gamma buffer layout differs from C++");
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = 8;
			texture.Height = texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
			texture.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			texture.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&texture, nullptr, output.GetAddressOf()));
			Util::SetResourceName(output.Get(), "AdaptiveColorTest::Output");
			Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "AdaptiveColorTest::Output UAV");
			texture.BindFlags = 0;
			texture.Usage = D3D11_USAGE_STAGING;
			texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&texture, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "AdaptiveColorTest::Readback");
		}

		Pixels Draw(ID3D11DeviceContext* context, bool linear, float contrast, float saturation)
		{
			context->ClearState();
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint32_t(linear));
			feature->SetMember("SharedData::adaptiveBalanceSettings", "contrast", contrast);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "saturation", saturation);
			feature->Bind(context);
			inputs->Bind(context);
			ID3D11UnorderedAccessView* target = uav.Get();
			context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Pixels result;
			std::memcpy(result.data(), mapped.pData, sizeof(result));
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	void Verify(const Pixels& result, float contrast, float saturation)
	{
		for (size_t i = 0; i < result.size(); ++i) {
			Require(result[i][3] == 1, "Neutral grading changed the original input");
			for (size_t c = 0; c < 3; ++c) {
				Require(std::isfinite(result[i][c]), "Non-finite graded color");
				if (contrast == 1 && saturation == 1)
					Require(Close(result[i][c], samples[i][c]), "Neutral output changed");
				else
					Require(result[i][c] >= 0, "Negative graded color");
				if (saturation == 0)
					Require(Close(result[i][c], result[i][0]), "Zero saturation is not monochrome");
			}
		}
		Require(result[0][0] == 0, "Contrast lifted black");
		Require(Close(result[2][0], 0.18f), "Contrast moved middle gray");
		Require(contrast >= 1 ? result[1][0] <= 0.010001f : result[1][0] > 0.01f, "Incorrect shadow contrast");
		Require(contrast <= 1 ? result[3][0] <= 0.60001f : result[3][0] > 0.6f, "Incorrect highlight contrast");
		if (saturation == 1) {
			Require(Close(result[4][0] / result[4][1], 2), "Contrast shifted hue");
			Require(result[5][2] > 1, "HDR highlights clipped to SDR");
		}
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		for (bool vr : { false, true }) {
			std::cerr << "Creating " << (vr ? "VR" : "SE/AE") << " fixture\n";
			Fixture fixture(device.Get(), vr);
			std::cerr << "Running color samples\n";
			for (float contrast : { 0.5f, 1.0f, 2.0f }) {
				for (float saturation : { 0.0f, 1.0f, 2.0f }) {
					const auto linear = fixture.Draw(context.Get(), true, contrast, saturation);
					const auto gamma = fixture.Draw(context.Get(), false, contrast, saturation);
					Verify(linear, contrast, saturation);
					Verify(gamma, contrast, saturation);
					for (size_t i = 0; i < samples.size(); ++i)
						for (size_t c = 0; c < 3; ++c)
							Require(Close(linear[i][c], gamma[i][c]), "Linear Lighting paths disagree");
				}
			}
			for (bool adaptive : { false, true }) {
				for (bool fade : { false, true }) {
					std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "BLEND", "1" } };
					if (vr)
						defines.push_back({ "VR", "1" });
					if (adaptive)
						defines.push_back({ "ADAPTIVE_BALANCE", "1" });
					if (fade)
						defines.push_back({ "FADE", "1" });
					Compile(L"package/Shaders/ISHDR.hlsl", defines, "ps_5_0");
				}
			}
		}
		std::cout << "288 WARP color samples and 8 ISHDR permutations passed (SE/AE and VR).\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
