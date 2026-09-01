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
					 std::pair{ "useAmbientEffectLighting", 64u }, std::pair{ "skyStaticTransparency", 68u },
					 std::pair{ "effectBrightness", 72u }, std::pair{ "skyStaticBrightness", 76u } }) {
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
					Require(samples[i][c] < 0 ? result[i][c] <= 0 : result[i][c] >= 0, "Grading changed an authored channel sign");
				if (saturation == 0)
					Require(Close(std::abs(result[i][c]), std::abs(result[i][0])), "Zero saturation is not monochrome after display encoding");
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

	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;

		Texture(ID3D11Device* device, const char* name, UINT flags)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 2;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = flags;
			if (!flags) {
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			}
			Check(device->CreateTexture2D(&desc, nullptr, resource.GetAddressOf()));
			Util::SetResourceName(resource.Get(), "AdaptiveColorTest::%s", name);
			if (flags & D3D11_BIND_SHADER_RESOURCE) {
				Check(device->CreateShaderResourceView(resource.Get(), nullptr, srv.GetAddressOf()));
				Util::SetResourceName(srv.Get(), "AdaptiveColorTest::%s SRV", name);
			}
			if (flags & D3D11_BIND_RENDER_TARGET) {
				Check(device->CreateRenderTargetView(resource.Get(), nullptr, rtv.GetAddressOf()));
				Util::SetResourceName(rtv.Get(), "AdaptiveColorTest::%s RTV", name);
			}
		}

		void Set(ID3D11DeviceContext* context, Pixel color)
		{
			const std::array pixels{ color, color };
			context->UpdateSubresource(resource.Get(), 0, nullptr, pixels.data(), UINT(sizeof(pixels)), 0);
		}
	};

	struct BlendFixture
	{
		ID3D11DeviceContext* context;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11SamplerState> sampler;
		std::unique_ptr<ConstantBuffer> geometry, frame, feature;
		Texture output, staging, scene, bloom, average;

		BlendFixture(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool adaptive, bool fade) :
			context(context), output(device, "BlendOutput", D3D11_BIND_RENDER_TARGET),
			staging(device, "BlendReadback", 0), scene(device, "Scene", D3D11_BIND_SHADER_RESOURCE),
			bloom(device, "Bloom", D3D11_BIND_SHADER_RESOURCE), average(device, "Average", D3D11_BIND_SHADER_RESOURCE)
		{
			std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "BLEND", "1" } };
			if (vr)
				defines.push_back({ "VR", "1" });
			if (adaptive)
				defines.push_back({ "ADAPTIVE_BALANCE", "1" });
			if (fade)
				defines.push_back({ "FADE", "1" });
			auto code = Compile(L"package/Shaders/ISHDR.hlsl", defines, "ps_5_0");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, pixel.GetAddressOf()));
			Util::SetResourceName(pixel.Get(), "AdaptiveColorTest::BlendPS");
			constexpr char vs[] = "struct V { float4 p:SV_POSITION; float2 uv:TEXCOORD0; }; V main(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }";
			ComPtr<ID3DBlob> vertexCode;
			Check(D3DCompile(vs, sizeof(vs) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertexCode.GetAddressOf(), nullptr));
			Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
			Util::SetResourceName(vertex.Get(), "AdaptiveColorTest::FullscreenVS");
			D3D11_SAMPLER_DESC desc{};
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&desc, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "AdaptiveColorTest::Sampler");
			feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
			geometry = std::make_unique<ConstantBuffer>(device, reflection.Get(), "PerGeometry");
			frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			frame->SetVariable("FrameBuffer::FrameParams", Pixel{ 1, 0, 0, 0 });
			frame->SetVariable("FrameBuffer::DynamicResolutionParams1", Pixel{ 1, 1, 1, 1 });
			frame->SetVariable("FrameBuffer::DynamicResolutionParams2", Pixel{ 1, 1, 1, 1 });
		}

		Pixel Draw(bool linear, float contrast, float saturation, float authoredContrast = 1, Pixel fade = {}, bool dark = false, bool alternateTonemap = false)
		{
			context->ClearState();
			scene.Set(context, dark ? Pixel{ 0.05f, 0.04f, 0.03f, 1 } : Pixel{ 0.4f, 0.2f, 0.1f, 1 });
			bloom.Set(context, dark ? Pixel{} : Pixel{ 0.1f, 0.2f, 0.3f, 1 });
			average.Set(context, { 0.8f, 0.8f, 0, 0 });
			feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", uint32_t(linear));
			feature->SetMember("SharedData::adaptiveBalanceSettings", "contrast", contrast);
			feature->SetMember("SharedData::adaptiveBalanceSettings", "saturation", saturation);
			feature->Bind(context, D3D11ShaderTest::Stage::Pixel);
			geometry->SetVariable("Param", Pixel{ 1, 1, float(alternateTonemap), 0 });
			geometry->SetVariable("Cinematic", Pixel{ 1, 0, authoredContrast, 1 });
			geometry->SetVariable("Fade", fade);
			geometry->Bind(context, D3D11ShaderTest::Stage::Pixel);
			frame->Bind(context, D3D11ShaderTest::Stage::Pixel);
			ID3D11ShaderResourceView* sources[]{ bloom.srv.Get(), scene.srv.Get(), average.srv.Get() };
			context->PSSetShaderResources(0, 3, sources);
			ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get(), sampler.Get() };
			context->PSSetSamplers(0, 3, samplers);
			ID3D11RenderTargetView* target = output.rtv.Get();
			context->OMSetRenderTargets(1, &target, nullptr);
			D3D11_VIEWPORT viewport{ 0, 0, 2, 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			context->Draw(3, 0);
			context->ClearState();
			context->CopyResource(staging.resource.Get(), output.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.resource.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Pixel, 2> result;
			std::memcpy(result.data(), mapped.pData, sizeof(result));
			context->Unmap(staging.resource.Get(), 0);
			for (size_t c = 0; c < 4; ++c) {
				Require(std::isfinite(result[0][c]), "Non-finite HDR blend output");
				Require(Close(result[0][c], result[1][c]), "HDR blend eyes disagree");
			}
			return result[0];
		}
	};

	void VerifyBlend(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, bool fade)
	{
		BlendFixture baseline(device, context, vr, false, fade);
		BlendFixture graded(device, context, vr, true, fade);
		for (bool linear : { false, true }) {
			for (bool dark : { false, true }) {
				for (bool alternateTonemap : { false, true }) {
					for (const Pixel fading : { Pixel{}, Pixel{ 0.1f, 0.3f, 0.2f, 0.4f }, Pixel{ 0.1f, 0.3f, 0.2f, 1 } }) {
						const auto original = baseline.Draw(linear, 1, 1, 2, fading, dark, alternateTonemap);
						const auto neutral = graded.Draw(linear, 1, 1, 2, fading, dark, alternateTonemap);
						const auto nearNeutral = graded.Draw(linear, 1.000001f, 1, 2, fading, dark, alternateTonemap);
						const auto gray = graded.Draw(linear, 1, 0, 2, fading, dark, alternateTonemap);
						for (size_t c = 0; c < 3; ++c) {
							Require(Close(neutral[c], original[c]), "Neutral settings changed the HDR blend");
							Require(Close(nearNeutral[c], original[c]), "Tiny contrast adjustment clipped authored shadows");
							if (fade && fading[3] == 1)
								Require(Close(gray[c], original[c]), "Scene saturation changed a full fade");
							if (!fade || fading[3] == 0)
								Require(Close(gray[c], gray[0]), "Composed scene and bloom did not become monochrome");
						}
					}
				}
			}
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
			for (bool fade : { false, true })
				VerifyBlend(device.Get(), context.Get(), vr, fade);
		}
		std::cout << "288 color samples and 384 HDR blend draws passed on WARP (SE/AE and VR).\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
