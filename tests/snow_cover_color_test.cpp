#define NOMINMAX
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Color = std::array<float, 4>;
	using Results = std::array<Color, 3>;
	constexpr Color MainTint{ 0.8f, 0.95f, 0.7f, 1.0f };
	constexpr Color AlternateTint{ 0.9f, 0.75f, 1.0f, 1.0f };
	constexpr float MainAO = 0.7f;
	constexpr float AlternateAO = 0.4f;

	ComPtr<ID3DBlob> CompilePixel(std::vector<D3D_SHADER_MACRO> defines, const char* entry)
	{
		PackageIncludes includes("package/Shaders", "features/Snow Cover/Shaders");
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> bytecode, errors;
		const auto compiled = D3DCompileFromFile(L"tests/snow_cover_color.hlsl", defines.data(), &includes,
			entry, "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(compiled);
		return bytecode;
	}

	struct Target
	{
		ComPtr<ID3D11Texture2D> texture, staging;
		ComPtr<ID3D11RenderTargetView> view;

		explicit Target(ID3D11Device* device)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 3;
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			Check(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()));
			Util::SetResourceName(texture.Get(), "SnowColorTest::Target");
			Check(device->CreateRenderTargetView(texture.Get(), nullptr, view.GetAddressOf()));
			Util::SetResourceName(view.Get(), "SnowColorTest::Target RTV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "SnowColorTest::Readback");
		}

		Results Read(ID3D11DeviceContext* context)
		{
			context->CopyResource(staging.Get(), texture.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			Results result;
			std::memcpy(result.data(), mapped.pData, sizeof(result));
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	ComPtr<ID3D11ShaderResourceView> Texture(ID3D11Device* device, const Color& color)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data{ color.data(), sizeof(color), 0 };
		ComPtr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()));
		Util::SetResourceName(texture.Get(), "SnowColorTest::Material");
		ComPtr<ID3D11ShaderResourceView> view;
		Check(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
		Util::SetResourceName(view.Get(), "SnowColorTest::Material SRV");
		return view;
	}

	size_t CheckColors(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool pbr)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (pbr)
			defines.push_back({ "TRUE_PBR", "1" });
		auto bytecode = CompilePixel(std::move(defines), "main");
		ComPtr<ID3D11PixelShader> shader;
		Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SnowColorTest::PS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer samples(device, reflection.Get(), "Samples");
		ConstantBuffer permutation(device, reflection.Get(), "Permutation::PerShader");
		permutation.SetVariable("Permutation::ExtraShaderDescriptor", 1u);
		D3D11_SHADER_TYPE_DESC mask{};
		Check(features.reflection->GetVariableByName("SharedData::snowCoverSettings")->GetType()->GetMemberTypeByName("LandscapeSnowColorMask")->GetDesc(&mask));
		if (mask.Offset != 200)
			throw std::runtime_error("Snow color mask does not occupy the existing world settings padding");
		features.SetMember("SharedData::snowCoverSettings", "MainTint", MainTint);
		features.SetMember("SharedData::snowCoverSettings", "AltTint", AlternateTint);
		features.SetMember("SharedData::snowCoverSettings", "BlendSmoothness", 1.0f);
		features.SetMember("SharedData::snowCoverSettings", "maxAngle", 1.0f);
		samples.SetVariable("MainAO", MainAO);
		samples.SetVariable("AlternateAO", AlternateAO);
		const auto normal = Texture(device, { 0.5f, 0.5f, 1.0f, 1.0f });
		const auto mainMaterial = Texture(device, { 0.8f, 0.0f, MainAO, 0.02f });
		const auto alternateMaterial = Texture(device, { 0.8f, 0.0f, AlternateAO, 0.02f });
		ComPtr<ID3D11SamplerState> sampler;
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		Check(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));
		Util::SetResourceName(sampler.Get(), "SnowColorTest::Sampler");
		Target actual(device), expected(device);
		size_t count = 0;
		for (bool linearLighting : { false, true }) {
			features.SetMember("SharedData::linearLightingSettings", "enableLinearLighting", static_cast<uint32_t>(linearLighting));
			for (const auto& controls : { std::array{ 2.2f, 1.0f }, std::array{ 1.8f, 0.8f }, std::array{ 2.4f, 1.2f } }) {
				features.SetMember("SharedData::linearLightingSettings", "colorGamma", controls[0]);
				features.SetMember("SharedData::linearLightingSettings", "vanillaDiffuseColorMult", controls[1]);
				for (uint32_t sourceMask : { 0u, 1u, 2u, 3u, 5u, 7u, 10u, 11u, 15u }) {
					features.SetMember("SharedData::snowCoverSettings", "LandscapeSnowColorMask", sourceMask);
					for (const auto& color : { Color{ 0.0f, 0.0005f, 0.02f, 1 }, Color{ 0.18f, 0.42f, 0.72f, 1 }, Color{ 0.8f, 0.95f, 1.0f, 1 } }) {
						const Color alternateColor{ color[2], color[0], color[1], 1 };
						samples.SetVariable("MainSample", std::array{ color[0], color[1], color[2] });
						samples.SetVariable("AlternateSample", std::array{ alternateColor[0], alternateColor[1], alternateColor[2] });
						const auto mainTexture = Texture(device, color);
						const auto alternateTexture = Texture(device, alternateColor);
						for (bool alternate : { false, true }) {
							context->ClearState();
							samples.SetVariable("AlternateSurface", static_cast<uint32_t>(alternate));
							features.SetMember("SharedData::snowCoverSettings", "peakMainAngle", alternate ? -10.0f : 1.0f);
							features.SetMember("SharedData::snowCoverSettings", "peakAltAngle", alternate ? 1.0f : -10.0f);
							features.Bind(context, D3D11ShaderTest::Stage::Pixel);
							samples.Bind(context, D3D11ShaderTest::Stage::Pixel);
							permutation.Bind(context, D3D11ShaderTest::Stage::Pixel);
							ID3D11ShaderResourceView* textures[]{ mainTexture.Get(), normal.Get(), mainMaterial.Get(), alternateTexture.Get(), normal.Get(), alternateMaterial.Get() };
							context->PSSetShaderResources(112, 6, textures);
							ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get() };
							context->PSSetSamplers(0, 2, samplers);
							ID3D11RenderTargetView* targets[]{ actual.view.Get(), expected.view.Get() };
							context->OMSetRenderTargets(2, targets, nullptr);
							const D3D11_VIEWPORT viewport{ 0, 0, 3, 1, 0, 1 };
							context->RSSetViewports(1, &viewport);
							context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
							context->VSSetShader(vertex, nullptr, 0);
							context->PSSetShader(shader.Get(), nullptr, 0);
							context->Draw(3, 0);
							context->ClearState();
							const auto values = actual.Read(context);
							const auto native = expected.Read(context);
							for (size_t route = 0; route < values.size(); ++route) {
								if (values[route][3] != (linearLighting ? 2.0f : 1.0f) || native[route][3] != 1.0f)
									throw std::runtime_error("Snow color route did not render with the requested Linear Lighting state");
								for (size_t channel = 0; channel < 3; ++channel) {
									if (!std::isfinite(values[route][channel]) || !std::isfinite(native[route][channel]) || std::abs(values[route][channel] - native[route][channel]) > 2e-5f)
										throw std::runtime_error("Snow color differs from native source shading: VR=" + std::to_string(vr) + " PBR=" + std::to_string(pbr) + " LL=" + std::to_string(linearLighting) + " mask=" + std::to_string(sourceMask) + " alternate=" + std::to_string(alternate) + " route=" + std::to_string(route));
								}
								++count;
							}
						}
					}
				}
			}
		}
		return count;
	}

	void CheckWeatherRange(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11VertexShader* vertex, bool vr, bool reference)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "SNOW_COVER", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (reference)
			defines.push_back({ "CSX_PERF_REFERENCE_FLAGS", "CSX_PERF_SNOW_WEATHER_RANGE" });
		auto bytecode = CompilePixel(std::move(defines), "WeatherRange");
		ComPtr<ID3D11PixelShader> shader;
		Check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SnowColorTest::WeatherRangePS");
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ConstantBuffer features(device, reflection.Get(), "SharedData::FeatureData");
		features.SetMember("SharedData::snowCoverSettings", "WeatherFadeStart", 200.0f);
		features.SetMember("SharedData::snowCoverSettings", "WeatherFadeEnd", 1000.0f);
		Target target(device);
		context->ClearState();
		context->OMSetRenderTargets(1, target.view.GetAddressOf(), nullptr);
		const D3D11_VIEWPORT viewport{ 0, 0, 3, 1, 0, 1 };
		context->RSSetViewports(1, &viewport);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertex, nullptr, 0);
		context->PSSetShader(shader.Get(), nullptr, 0);
		Results weather{};
		for (float accumulation : { 1.0f, 0.0f, -1.0f }) {
			features.SetMember("SharedData::snowCoverSettings", "TimeSnowing", accumulation);
			features.Bind(context, D3D11ShaderTest::Stage::Pixel);
			context->Draw(3, 0);
			const auto values = target.Read(context);
			if (accumulation > 0)
				weather = values;
			for (size_t i = 0; i < values.size(); ++i) {
				if (!(weather[i][0] > 0 && weather[i][0] < 1) || values[i][3] != 1)
					throw std::runtime_error("Snow weather-distance test missed the rendered fade");
				const float expected = accumulation == 0 && !reference ? 0.0f : weather[i][0];
				if (!std::isfinite(values[i][0]) || std::abs(values[i][0] - expected) > 1e-6f)
					throw std::runtime_error("Snow weather-distance optimization changed zero or negative accumulation behavior");
			}
		}
		context->ClearState();
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
			device.GetAddressOf(), nullptr, context.GetAddressOf()));
		constexpr char vertexSource[] = "float4 main(uint id : SV_VertexID) : SV_Position { return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1); }";
		ComPtr<ID3DBlob> bytecode;
		Check(D3DCompile(vertexSource, sizeof(vertexSource), nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, bytecode.GetAddressOf(), nullptr));
		ComPtr<ID3D11VertexShader> vertex;
		Check(device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, vertex.GetAddressOf()));
		Util::SetResourceName(vertex.Get(), "SnowColorTest::VS");
		size_t count = 0;
		for (bool vr : { false, true })
			for (bool pbr : { false, true })
				count += CheckColors(device.Get(), context.Get(), vertex.Get(), vr, pbr);
		for (bool vr : { false, true })
			for (bool reference : { false, true })
				CheckWeatherRange(device.Get(), context.Get(), vertex.Get(), vr, reference);
		std::cout << "Snow color: " << count << " production surface/foliage samples match native shading on WARP (SE/AE and VR, legacy/PBR sources and targets, LL controls, main/alternate fallback, retained tints/AO)\n";
		std::cout << "Snow weather distance: positive, zero and negative accumulation passed on WARP (SE/AE and VR, optimized/reference)\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
